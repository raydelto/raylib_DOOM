// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//	OpenXR session, cinema screen and controllers.
//
//	raylib has no OpenXR backend, so this talks to the OpenXR
//	loader directly, sharing raylib's OpenGL context through
//	XR_KHR_opengl_enable (GLX on X11). The picture goes to the
//	headset as a single quad layer fixed in space in front of
//	where the head was when the session started; the runtime's
//	compositor draws it for each eye with the latest head pose,
//	so head tracking runs at the headset's rate even though the
//	game only draws 35 frames a second.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>

#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "i_xr.h"

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8		0x8C43
#endif
#ifndef GL_RGBA8
#define GL_RGBA8		0x8058
#endif

// Headers newer than 1.0 default to a 1.1 instance, which a 1.0
// loader or runtime (Monado from Ubuntu 24.04, for one) refuses.
#ifdef XR_API_VERSION_1_0
#define XRAPIVERSION		XR_API_VERSION_1_0
#else
#define XRAPIVERSION		XR_CURRENT_API_VERSION
#endif

// The screen: 4:3, SCREENDIST metres ahead, its middle at eye height.
#define SCREENWIDTH_M		4.0f
#define SCREENHEIGHT_M		3.0f
#define SCREENDIST_M		3.0f

// Swapchain size: 320x200 blown up 5 times with sharp pixels.
// The compositor scales it to the screen smoothly, and shows
// DOOM's tall pixels by drawing it on a 4:3 quad.
#define IMAGEWIDTH		1600
#define IMAGEHEIGHT		1000

#define MAXIMAGES		8

// Thumbstick deflection that counts as a press, and the smaller
// one that ends it, so a stick held near the edge does not chatter.
#define STICKPRESS		0.5f
#define STICKRELEASE		0.35f


static XrInstance		instance = XR_NULL_HANDLE;
static XrSystemId		systemid = XR_NULL_SYSTEM_ID;
static XrSession		session = XR_NULL_HANDLE;
static XrSpace			localspace = XR_NULL_HANDLE;
static XrSwapchain		swapchain = XR_NULL_HANDLE;
static XrEnvironmentBlendMode	blendmode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

static XrSwapchainImageOpenGLKHR images[MAXIMAGES];
static uint32_t			numimages;

static XrSessionState		sessionstate = XR_SESSION_STATE_UNKNOWN;
static int			running;	// between xrBeginSession and xrEndSession
static int			exitrequested;

static int			framebegun;
static int			imageacquired;
static XrTime			displaytime;

static XrActionSet		actionset = XR_NULL_HANDLE;
static XrAction			moveaction = XR_NULL_HANDLE;
static XrAction			turnaction = XR_NULL_HANDLE;
static XrAction			fireaction = XR_NULL_HANDLE;
static XrAction			useaction = XR_NULL_HANDLE;
static XrAction			menuaction = XR_NULL_HANDLE;
static int			buttons;

// Set when a call reports the runtime or the session gone.
static int			lost;


static int Failed (XrResult result, const char* what)
{
    char	name[XR_MAX_RESULT_STRING_SIZE];

    if (XR_SUCCEEDED (result))
	return 0;

    if (result == XR_ERROR_INSTANCE_LOST || result == XR_ERROR_SESSION_LOST)
	lost = 1;

    if (instance == XR_NULL_HANDLE
	|| XR_FAILED (xrResultToString (instance, result, name)))
	snprintf (name, sizeof(name), "%d", (int)result);
    fprintf (stderr, "XR: %s failed: %s\n", what, name);
    return 1;
}


static XrPath Path (const char* string)
{
    XrPath	path = XR_NULL_PATH;

    xrStringToPath (instance, string, &path);
    return path;
}


//
// UseNvidiaGL
// The runtime's compositor renders on the GPU the headset hangs
// off, which on a laptop is the NVIDIA one, while a window opens
// on the integrated GPU by default. OpenGL can not share textures
// across the two, and Mesa crashes in xrCreateSwapchain trying.
// So with the NVIDIA driver loaded, ask for PRIME render offload
// before the window opens. DOOM_XR_PRIME=0 turns this off, and
// an explicit __GLX_VENDOR_LIBRARY_NAME is left alone.
//
static void UseNvidiaGL (void)
{
    const char*	env = getenv ("DOOM_XR_PRIME");

    if ((env && *env && atoi (env) == 0)
	|| getenv ("__GLX_VENDOR_LIBRARY_NAME")
	|| access ("/proc/driver/nvidia/version", F_OK) != 0)
	return;

    setenv ("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
    setenv ("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
    printf ("XR: rendering on the NVIDIA GPU (DOOM_XR_PRIME=0 to not)\n");
}


//
// XR_Init
//
int XR_Init (void)
{
    XrInstanceCreateInfo	ici;
    XrSystemGetInfo		sgi;
    XrSystemProperties		props;
    XrExtensionProperties*	exts;
    const char*			enabled[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
    uint32_t			count = 0;
    uint32_t			i;
    int				found = 0;

    // Is there a runtime at all, and can it share OpenGL textures?
    if (Failed (xrEnumerateInstanceExtensionProperties (NULL, 0, &count, NULL),
		"finding an OpenXR runtime"))
    {
	fprintf (stderr, "XR: no OpenXR runtime; is one installed and"
		 " active (XR_RUNTIME_JSON)?\n");
	return 0;
    }

    exts = calloc (count, sizeof(*exts));
    for (i = 0; i < count; i++)
	exts[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    if (!Failed (xrEnumerateInstanceExtensionProperties (NULL, count, &count, exts),
		 "listing OpenXR extensions"))
    {
	for (i = 0; i < count; i++)
	    if (!strcmp (exts[i].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
		found = 1;
    }
    free (exts);

    if (!found)
    {
	fprintf (stderr, "XR: the OpenXR runtime does not support %s\n",
		 XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
	return 0;
    }

    memset (&ici, 0, sizeof(ici));
    ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
    strcpy (ici.applicationInfo.applicationName, "raylib DOOM");
    ici.applicationInfo.applicationVersion = 1;
    strcpy (ici.applicationInfo.engineName, "raylib");
    ici.applicationInfo.engineVersion = 5;
    ici.applicationInfo.apiVersion = XRAPIVERSION;
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = enabled;

    if (Failed (xrCreateInstance (&ici, &instance), "xrCreateInstance"))
    {
	fprintf (stderr, "XR: is the OpenXR service running"
		 " (monado-service for Monado)?\n");
	instance = XR_NULL_HANDLE;
	return 0;
    }

    memset (&sgi, 0, sizeof(sgi));
    sgi.type = XR_TYPE_SYSTEM_GET_INFO;
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (Failed (xrGetSystem (instance, &sgi, &systemid), "xrGetSystem"))
    {
	fprintf (stderr, "XR: no headset found\n");
	XR_Shutdown ();
	return 0;
    }

    memset (&props, 0, sizeof(props));
    props.type = XR_TYPE_SYSTEM_PROPERTIES;
    if (XR_SUCCEEDED (xrGetSystemProperties (instance, systemid, &props)))
	printf ("XR: headset: %s\n", props.systemName);

    UseNvidiaGL ();
    return 1;
}


//
// CreateActions
// Move, turn, fire, use and menu, with bindings for the common
// controllers. A runtime that does not know a profile rejects
// its bindings; the others still apply.
//
typedef struct
{
    XrAction*	action;
    const char*	path;
} binding_t;

static XrAction CreateAction (const char* name, const char* localized,
			      XrActionType type)
{
    XrActionCreateInfo	aci;
    XrAction		action = XR_NULL_HANDLE;

    memset (&aci, 0, sizeof(aci));
    aci.type = XR_TYPE_ACTION_CREATE_INFO;
    aci.actionType = type;
    strcpy (aci.actionName, name);
    strcpy (aci.localizedActionName, localized);
    if (Failed (xrCreateAction (actionset, &aci, &action), name))
	return XR_NULL_HANDLE;
    return action;
}

static void SuggestBindings (const char* profile, const binding_t* b, int n)
{
    XrActionSuggestedBinding		sb[16];
    XrInteractionProfileSuggestedBinding ipsb;
    int					i;
    int					count = 0;

    for (i = 0; i < n && count < 16; i++)
    {
	if (*b[i].action == XR_NULL_HANDLE)
	    continue;
	sb[count].action = *b[i].action;
	sb[count].binding = Path (b[i].path);
	count++;
    }

    memset (&ipsb, 0, sizeof(ipsb));
    ipsb.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    ipsb.interactionProfile = Path (profile);
    ipsb.countSuggestedBindings = count;
    ipsb.suggestedBindings = sb;
    if (XR_FAILED (xrSuggestInteractionProfileBindings (instance, &ipsb)))
	printf ("XR: no bindings for %s\n", profile);
}

#define NUMBINDINGS(b)	(int)(sizeof(b)/sizeof(b[0]))

static void CreateActions (void)
{
    XrActionSetCreateInfo		asci;
    XrSessionActionSetsAttachInfo	attach;

    static const binding_t simple[] =
    {
	{ &fireaction, "/user/hand/right/input/select/click" },
	{ &useaction, "/user/hand/left/input/select/click" },
	{ &menuaction, "/user/hand/left/input/menu/click" },
	{ &menuaction, "/user/hand/right/input/menu/click" },
    };
    static const binding_t touch[] =
    {
	{ &moveaction, "/user/hand/left/input/thumbstick" },
	{ &turnaction, "/user/hand/right/input/thumbstick" },
	{ &fireaction, "/user/hand/right/input/trigger/value" },
	{ &useaction, "/user/hand/right/input/a/click" },
	{ &useaction, "/user/hand/left/input/x/click" },
	{ &menuaction, "/user/hand/left/input/menu/click" },
    };
    static const binding_t index[] =
    {
	{ &moveaction, "/user/hand/left/input/thumbstick" },
	{ &turnaction, "/user/hand/right/input/thumbstick" },
	{ &fireaction, "/user/hand/right/input/trigger/click" },
	{ &useaction, "/user/hand/right/input/a/click" },
	{ &useaction, "/user/hand/left/input/a/click" },
	{ &menuaction, "/user/hand/left/input/b/click" },
    };
    static const binding_t vive[] =
    {
	{ &moveaction, "/user/hand/left/input/trackpad" },
	{ &turnaction, "/user/hand/right/input/trackpad" },
	{ &fireaction, "/user/hand/right/input/trigger/click" },
	{ &useaction, "/user/hand/left/input/trigger/click" },
	{ &menuaction, "/user/hand/left/input/menu/click" },
	{ &menuaction, "/user/hand/right/input/menu/click" },
    };
    static const binding_t wmr[] =
    {
	{ &moveaction, "/user/hand/left/input/thumbstick" },
	{ &turnaction, "/user/hand/right/input/thumbstick" },
	{ &fireaction, "/user/hand/right/input/trigger/value" },
	{ &useaction, "/user/hand/left/input/trigger/value" },
	{ &menuaction, "/user/hand/left/input/menu/click" },
	{ &menuaction, "/user/hand/right/input/menu/click" },
    };

    memset (&asci, 0, sizeof(asci));
    asci.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    strcpy (asci.actionSetName, "gameplay");
    strcpy (asci.localizedActionSetName, "Gameplay");
    if (Failed (xrCreateActionSet (instance, &asci, &actionset),
		"xrCreateActionSet"))
	return;

    moveaction = CreateAction ("move", "Move and strafe",
			       XR_ACTION_TYPE_VECTOR2F_INPUT);
    turnaction = CreateAction ("turn", "Turn", XR_ACTION_TYPE_VECTOR2F_INPUT);
    fireaction = CreateAction ("fire", "Fire", XR_ACTION_TYPE_BOOLEAN_INPUT);
    useaction = CreateAction ("use", "Use", XR_ACTION_TYPE_BOOLEAN_INPUT);
    menuaction = CreateAction ("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT);

    SuggestBindings ("/interaction_profiles/khr/simple_controller",
		     simple, NUMBINDINGS(simple));
    SuggestBindings ("/interaction_profiles/oculus/touch_controller",
		     touch, NUMBINDINGS(touch));
    SuggestBindings ("/interaction_profiles/valve/index_controller",
		     index, NUMBINDINGS(index));
    SuggestBindings ("/interaction_profiles/htc/vive_controller",
		     vive, NUMBINDINGS(vive));
    SuggestBindings ("/interaction_profiles/microsoft/motion_controller",
		     wmr, NUMBINDINGS(wmr));

    memset (&attach, 0, sizeof(attach));
    attach.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    attach.countActionSets = 1;
    attach.actionSets = &actionset;
    if (Failed (xrAttachSessionActionSets (session, &attach),
		"xrAttachSessionActionSets"))
    {
	xrDestroyActionSet (actionset);
	actionset = XR_NULL_HANDLE;
    }
}


//
// CreateSwapchain
// Prefers an sRGB format, so the runtime shows the colours as the
// game wrote them instead of taking them for linear light.
//
static int CreateSwapchain (void)
{
    XrSwapchainCreateInfo	sci;
    int64_t			formats[64];
    int64_t			format = 0;
    uint32_t			count = 0;
    uint32_t			i;

    if (Failed (xrEnumerateSwapchainFormats (session, 64, &count, formats),
		"xrEnumerateSwapchainFormats"))
	return 0;

    for (i = 0; i < count && !format; i++)
	if (formats[i] == GL_SRGB8_ALPHA8)
	    format = formats[i];
    for (i = 0; i < count && !format; i++)
	if (formats[i] == GL_RGBA8)
	    format = formats[i];
    if (!format)
    {
	fprintf (stderr, "XR: the runtime offers no RGBA8 swapchain\n");
	return 0;
    }

    memset (&sci, 0, sizeof(sci));
    sci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT
	| XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    sci.format = format;
    sci.sampleCount = 1;
    sci.width = IMAGEWIDTH;
    sci.height = IMAGEHEIGHT;
    sci.faceCount = 1;
    sci.arraySize = 1;
    sci.mipCount = 1;
    if (Failed (xrCreateSwapchain (session, &sci, &swapchain),
		"xrCreateSwapchain"))
	return 0;

    for (i = 0; i < MAXIMAGES; i++)
    {
	images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
	images[i].next = NULL;
    }
    if (Failed (xrEnumerateSwapchainImages (swapchain, MAXIMAGES, &numimages,
					    (XrSwapchainImageBaseHeader*)images),
		"xrEnumerateSwapchainImages"))
	return 0;

    return 1;
}


//
// XR_StartSession
//
int XR_StartSession (void)
{
    PFN_xrGetOpenGLGraphicsRequirementsKHR	getreqs = NULL;
    XrGraphicsRequirementsOpenGLKHR		reqs;
    XrGraphicsBindingOpenGLXlibKHR		binding;
    XrSessionCreateInfo				sci;
    XrReferenceSpaceCreateInfo			rsci;
    XrEnvironmentBlendMode			modes[8];
    uint32_t					count = 0;
    Display*					dpy;
    GLXContext					ctx;
    GLXFBConfig*				configs;
    XVisualInfo*				visual;
    int						attrs[] = { GLX_FBCONFIG_ID, 0, None };
    int						n = 0;

    if (instance == XR_NULL_HANDLE)
	return 0;

    // The runtime must be asked before a session is created.
    if (Failed (xrGetInstanceProcAddr (instance, "xrGetOpenGLGraphicsRequirementsKHR",
				       (PFN_xrVoidFunction*)&getreqs),
		"xrGetOpenGLGraphicsRequirementsKHR"))
	goto fail;
    memset (&reqs, 0, sizeof(reqs));
    reqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
    if (Failed (getreqs (instance, systemid, &reqs), "getting OpenGL requirements"))
	goto fail;

    // raylib's context, through GLFW's GLX.
    dpy = glXGetCurrentDisplay ();
    ctx = glXGetCurrentContext ();
    if (!dpy || !ctx)
    {
	fprintf (stderr, "XR: no current GLX context; the XR build needs"
		 " raylib on X11 (XWayland is fine)\n");
	goto fail;
    }

    memset (&binding, 0, sizeof(binding));
    binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR;
    binding.xDisplay = dpy;
    binding.glxContext = ctx;
    binding.glxDrawable = glXGetCurrentDrawable ();
    glXQueryContext (dpy, ctx, GLX_FBCONFIG_ID, &attrs[1]);
    configs = glXChooseFBConfig (dpy, DefaultScreen (dpy), attrs, &n);
    if (configs && n > 0)
    {
	binding.glxFBConfig = configs[0];
	visual = glXGetVisualFromFBConfig (dpy, configs[0]);
	if (visual)
	{
	    binding.visualid = (uint32_t)visual->visualid;
	    XFree (visual);
	}
    }
    if (configs)
	XFree (configs);

    memset (&sci, 0, sizeof(sci));
    sci.type = XR_TYPE_SESSION_CREATE_INFO;
    sci.next = &binding;
    sci.systemId = systemid;
    if (Failed (xrCreateSession (instance, &sci, &session), "xrCreateSession"))
    {
	session = XR_NULL_HANDLE;
	goto fail;
    }
    printf ("XR: session created\n");

    memset (&rsci, 0, sizeof(rsci));
    rsci.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    if (Failed (xrCreateReferenceSpace (session, &rsci, &localspace),
		"xrCreateReferenceSpace"))
	goto fail;

    if (XR_SUCCEEDED (xrEnumerateEnvironmentBlendModes (instance, systemid,
			    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			    8, &count, modes)) && count > 0)
	blendmode = modes[0];

    if (!CreateSwapchain ())
	goto fail;

    CreateActions ();
    return 1;

  fail:
    fprintf (stderr, "XR: falling back to the flat window\n");
    XR_Shutdown ();
    return 0;
}


void XR_Shutdown (void)
{
    if (framebegun)
	XR_EndFrame ();

    if (actionset != XR_NULL_HANDLE)
	xrDestroyActionSet (actionset);
    if (swapchain != XR_NULL_HANDLE)
	xrDestroySwapchain (swapchain);
    if (localspace != XR_NULL_HANDLE)
	xrDestroySpace (localspace);
    // xrEndSession is only for a session the runtime is stopping;
    // one that is running can simply be destroyed.
    if (session != XR_NULL_HANDLE)
	xrDestroySession (session);
    if (instance != XR_NULL_HANDLE)
	xrDestroyInstance (instance);

    actionset = XR_NULL_HANDLE;
    moveaction = turnaction = fireaction = useaction = menuaction
	= XR_NULL_HANDLE;
    swapchain = XR_NULL_HANDLE;
    localspace = XR_NULL_HANDLE;
    session = XR_NULL_HANDLE;
    instance = XR_NULL_HANDLE;
    numimages = 0;
    running = 0;
    buttons = 0;
    sessionstate = XR_SESSION_STATE_UNKNOWN;
}


//
// CheckLost
// Once the runtime is gone (the service stopped, the headset was
// unplugged) every call fails; drop OpenXR and carry on flat.
//
static int CheckLost (void)
{
    if (!lost)
	return 0;

    fprintf (stderr, "XR: lost the OpenXR runtime,"
	     " continuing in the window\n");
    framebegun = 0;
    XR_Shutdown ();
    lost = 0;
    return 1;
}


int XR_Active (void)
{
    return session != XR_NULL_HANDLE;
}


int XR_ExitRequested (void)
{
    return exitrequested;
}


int XR_Buttons (void)
{
    return buttons;
}


static void SessionStateChanged (XrSessionState state)
{
    XrSessionBeginInfo	sbi;

    sessionstate = state;

    switch (state)
    {
      case XR_SESSION_STATE_READY:
	memset (&sbi, 0, sizeof(sbi));
	sbi.type = XR_TYPE_SESSION_BEGIN_INFO;
	sbi.primaryViewConfigurationType =
	    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	if (!Failed (xrBeginSession (session, &sbi), "xrBeginSession"))
	{
	    running = 1;
	    printf ("XR: session running\n");
	}
	break;

      case XR_SESSION_STATE_STOPPING:
	xrEndSession (session);
	running = 0;
	break;

      case XR_SESSION_STATE_EXITING:
	// The user quit from the runtime.
	exitrequested = 1;
	break;

      case XR_SESSION_STATE_LOSS_PENDING:
	// The headset or the service is going away; carry on flat.
	lost = 1;
	break;

      default:
	break;
    }
}


static int StickButtons (XrAction action, int neg, int pos, int negy, int posy)
{
    XrActionStateGetInfo	gi;
    XrActionStateVector2f	st;
    int				held = 0;
    float			x;
    float			y;

    if (action == XR_NULL_HANDLE)
	return 0;

    memset (&gi, 0, sizeof(gi));
    gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
    gi.action = action;
    memset (&st, 0, sizeof(st));
    st.type = XR_TYPE_ACTION_STATE_VECTOR2F;
    if (XR_FAILED (xrGetActionStateVector2f (session, &gi, &st))
	|| !st.isActive)
	return 0;

    x = st.currentState.x;
    y = st.currentState.y;

#define STICK(v, limit, bit) \
    if (bit && ((buttons & bit) ? (v) > STICKRELEASE*(limit) \
				 : (v) > STICKPRESS*(limit))) \
	held |= bit;

    STICK (-x, 1.0f, neg);
    STICK (x, 1.0f, pos);
    STICK (-y, 1.0f, negy);
    STICK (y, 1.0f, posy);

#undef STICK

    return held;
}


static int Pressed (XrAction action)
{
    XrActionStateGetInfo	gi;
    XrActionStateBoolean	st;

    if (action == XR_NULL_HANDLE)
	return 0;

    memset (&gi, 0, sizeof(gi));
    gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
    gi.action = action;
    memset (&st, 0, sizeof(st));
    st.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    return XR_SUCCEEDED (xrGetActionStateBoolean (session, &gi, &st))
	&& st.isActive && st.currentState;
}


//
// XR_PumpEvents
//
void XR_PumpEvents (void)
{
    XrEventDataBuffer	ev;
    XrActiveActionSet	active;
    XrActionsSyncInfo	si;
    XrResult		result;
    int			held;

    while (instance != XR_NULL_HANDLE && !lost)
    {
	memset (&ev, 0, sizeof(ev));
	ev.type = XR_TYPE_EVENT_DATA_BUFFER;
	result = xrPollEvent (instance, &ev);
	if (result != XR_SUCCESS)
	{
	    Failed (result, "xrPollEvent");
	    break;
	}

	switch (ev.type)
	{
	  case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
	    SessionStateChanged (((XrEventDataSessionStateChanged*)&ev)->state);
	    break;

	  case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
	    lost = 1;
	    break;

	  default:
	    break;
	}
    }

    CheckLost ();

    // Controllers only report while the game has input focus.
    if (session == XR_NULL_HANDLE || actionset == XR_NULL_HANDLE
	|| sessionstate != XR_SESSION_STATE_FOCUSED)
    {
	buttons = 0;
	return;
    }

    active.actionSet = actionset;
    active.subactionPath = XR_NULL_PATH;
    memset (&si, 0, sizeof(si));
    si.type = XR_TYPE_ACTIONS_SYNC_INFO;
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    if (Failed (xrSyncActions (session, &si), "xrSyncActions"))
    {
	buttons = 0;
	CheckLost ();
	return;
    }

    held = StickButtons (moveaction, XRB_STRAFELEFT, XRB_STRAFERIGHT,
			 XRB_BACK, XRB_FORWARD);
    held |= StickButtons (turnaction, XRB_TURNLEFT, XRB_TURNRIGHT, 0, 0);
    if (Pressed (fireaction))
	held |= XRB_FIRE;
    if (Pressed (useaction))
	held |= XRB_USE;
    if (Pressed (menuaction))
	held |= XRB_MENU;

    buttons = held;
}


//
// XR_BeginFrame
//
int XR_BeginFrame (unsigned int* texture, int* width, int* height)
{
    XrFrameState		fs;
    XrSwapchainImageWaitInfo	wi;
    uint32_t			index;

    imageacquired = 0;
    if (CheckLost () || session == XR_NULL_HANDLE || !running)
	return 0;

    memset (&fs, 0, sizeof(fs));
    fs.type = XR_TYPE_FRAME_STATE;
    if (Failed (xrWaitFrame (session, NULL, &fs), "xrWaitFrame")
	|| Failed (xrBeginFrame (session, NULL), "xrBeginFrame"))
    {
	CheckLost ();
	return 0;
    }

    framebegun = 1;
    displaytime = fs.predictedDisplayTime;
    if (!fs.shouldRender)
	return 0;

    if (Failed (xrAcquireSwapchainImage (swapchain, NULL, &index),
		"xrAcquireSwapchainImage"))
	return 0;

    memset (&wi, 0, sizeof(wi));
    wi.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    wi.timeout = XR_INFINITE_DURATION;
    if (Failed (xrWaitSwapchainImage (swapchain, &wi), "xrWaitSwapchainImage")
	|| index >= numimages)
    {
	xrReleaseSwapchainImage (swapchain, NULL);
	return 0;
    }

    imageacquired = 1;
    *texture = images[index].image;
    *width = IMAGEWIDTH;
    *height = IMAGEHEIGHT;
    return 1;
}


//
// XR_EndFrame
//
void XR_EndFrame (void)
{
    XrCompositionLayerQuad		quad;
    const XrCompositionLayerBaseHeader*	layers[1];
    XrFrameEndInfo			fei;

    if (!framebegun)
	return;
    framebegun = 0;

    memset (&fei, 0, sizeof(fei));
    fei.type = XR_TYPE_FRAME_END_INFO;
    fei.displayTime = displaytime;
    fei.environmentBlendMode = blendmode;

    if (imageacquired)
    {
	imageacquired = 0;
	xrReleaseSwapchainImage (swapchain, NULL);

	memset (&quad, 0, sizeof(quad));
	quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
	quad.space = localspace;
	quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	quad.subImage.swapchain = swapchain;
	quad.subImage.imageRect.extent.width = IMAGEWIDTH;
	quad.subImage.imageRect.extent.height = IMAGEHEIGHT;
	quad.pose.orientation.w = 1.0f;
	quad.pose.position.z = -SCREENDIST_M;
	quad.size.width = SCREENWIDTH_M;
	quad.size.height = SCREENHEIGHT_M;

	layers[0] = (const XrCompositionLayerBaseHeader*)&quad;
	fei.layerCount = 1;
	fei.layers = layers;
    }

    Failed (xrEndFrame (session, &fei), "xrEndFrame");
    CheckLost ();
}
