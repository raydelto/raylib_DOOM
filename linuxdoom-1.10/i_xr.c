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
//	OpenXR headset output, on top of raylib's OpenGL context.
//
//	The game is drawn into one OpenXR swapchain image, which is
//	submitted as a quad composition layer: a flat screen fixed in
//	the room in front of the player. The runtime's compositor
//	draws that quad into each eye's view from the latest head pose
//	at the headset's refresh rate, so looking around stays smooth
//	although DOOM only makes 35 frames a second. This is the same
//	way VR "cinema" apps show flat video; it is sharper than
//	drawing the quad into eye buffers ourselves, as the image is
//	resampled only once.
//
//	Only OpenGL on X11 (GLX, XR_KHR_opengl_enable) is supported,
//	which is what raylib's GLFW uses on Linux.
//
//	This file sees OpenXR, GLX and rlgl, but neither raylib.h
//	(Xlib's Font typedef clashes with raylib's) nor the DOOM
//	headers.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <GL/glx.h>

#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#include "rlgl.h"

#include "i_xr.h"


// The swapchain image: 320x200 blown up 5x6 is exactly 4:3,
// with every DOOM pixel the same size.
#define IMAGEWIDTH	1600
#define IMAGEHEIGHT	1200

#define MAXIMAGES	8

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8	0x8C43
#endif
#ifndef GL_RGBA8
#define GL_RGBA8	0x8058
#endif

// Stick deflection that counts as a key press.
#define STICKPRESS	0.5f

// raylib's GLFW, weak so a raylib that hides it still links.
extern void glfwSwapInterval (int interval) __attribute__((weak));

static XrInstance	instance = XR_NULL_HANDLE;
static XrSystemId	systemid = XR_NULL_SYSTEM_ID;
static XrSession	session = XR_NULL_HANDLE;
static XrSpace		space = XR_NULL_HANDLE;
static XrSwapchain	swapchain = XR_NULL_HANDLE;
static XrEnvironmentBlendMode	blendmode;

static unsigned int	fbos[MAXIMAGES];
static uint32_t		numimages;

static XrSessionState	state = XR_SESSION_STATE_UNKNOWN;
static int		running;	// between xrBeginSession and xrEndSession
static int		quit;

static float		screendistance;
static float		screenwidth;

static XrActionSet	actionset = XR_NULL_HANDLE;
static XrAction		act_move;
static XrAction		act_turn;
static XrAction		act_fire;
static XrAction		act_use;
static XrAction		act_run;
static XrAction		act_menu;
static XrAction		act_map;

static unsigned		buttons;


//
// Names for OpenXR enums, from the SDK's reflection header, so
// errors can be told apart before there is an instance to ask.
//
#define XR_ENUM_CASE(name, value)	case name: return #name;

static const char* ResultName (XrResult r)
{
    switch (r)
    {
	XR_LIST_ENUM_XrResult (XR_ENUM_CASE)
      default:
	return "unknown XrResult";
    }
}

static const char* StateName (XrSessionState s)
{
    switch (s)
    {
	XR_LIST_ENUM_XrSessionState (XR_ENUM_CASE)
      default:
	return "unknown XrSessionState";
    }
}


// The runtime went away (monado-service quit, headset unplugged).
static int Lost (XrResult r)
{
    if (r == XR_ERROR_SESSION_LOST || r == XR_ERROR_INSTANCE_LOST)
    {
	if (!quit)
	    printf ("XR: lost the runtime: %s\n", ResultName (r));
	quit = 1;
	running = 0;
	return 1;
    }
    return 0;
}


static int Check (XrResult r, const char* what)
{
    if (XR_SUCCEEDED (r))
	return 1;
    fprintf (stderr, "XR: %s failed: %s (%d)\n", what, ResultName (r), (int)r);
    return 0;
}


static XrPath Path (const char* s)
{
    XrPath	path = XR_NULL_PATH;

    xrStringToPath (instance, s, &path);
    return path;
}


//
// CreateInstance
//
static int CreateInstance (void)
{
    XrInstanceCreateInfo	info;
    XrExtensionProperties	props[128];
    uint32_t			count;
    uint32_t			i;
    int				havegl;
    XrResult			r;
    const char*			extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };

    count = 0;
    r = xrEnumerateInstanceExtensionProperties (NULL, 0, &count, NULL);
    if (XR_FAILED (r))
    {
	fprintf (stderr,
		 "XR: no OpenXR runtime found (%s).\n"
		 "XR: Start one (for Monado: monado-service), or point\n"
		 "XR: XR_RUNTIME_JSON at its manifest, e.g.\n"
		 "XR:   XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json\n",
		 ResultName (r));
	return 0;
    }
    if (count > 128)
	count = 128;
    for (i = 0; i < count; i++)
    {
	props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
	props[i].next = NULL;
    }
    xrEnumerateInstanceExtensionProperties (NULL, count, &count, props);

    havegl = 0;
    for (i = 0; i < count; i++)
	if (!strcmp (props[i].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
	    havegl = 1;
    if (!havegl)
    {
	fprintf (stderr, "XR: the OpenXR runtime has no OpenGL support"
		 " (" XR_KHR_OPENGL_ENABLE_EXTENSION_NAME ").\n");
	return 0;
    }

    memset (&info, 0, sizeof(info));
    info.type = XR_TYPE_INSTANCE_CREATE_INFO;
    strcpy (info.applicationInfo.applicationName, "raylib DOOM");
    info.applicationInfo.applicationVersion = 1;
    strcpy (info.applicationInfo.engineName, "raylib");
    info.applicationInfo.engineVersion = 5;
    // A newer SDK's XR_CURRENT_API_VERSION may be 1.1, which
    // 1.0 runtimes such as Ubuntu's Monado refuse.
#ifdef XR_API_VERSION_1_0
    info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
#else
    info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
#endif
    info.enabledExtensionCount = 1;
    info.enabledExtensionNames = extensions;

    r = xrCreateInstance (&info, &instance);
    if (XR_FAILED (r))
    {
	fprintf (stderr,
		 "XR: xrCreateInstance failed: %s (%d).\n"
		 "XR: Is the OpenXR runtime running (monado-service)?\n",
		 ResultName (r), (int)r);
	instance = XR_NULL_HANDLE;
	return 0;
    }

    {
	XrInstanceProperties	ip = { XR_TYPE_INSTANCE_PROPERTIES };

	if (XR_SUCCEEDED (xrGetInstanceProperties (instance, &ip)))
	    printf ("XR: runtime %s %d.%d.%d\n", ip.runtimeName,
		    (int)XR_VERSION_MAJOR (ip.runtimeVersion),
		    (int)XR_VERSION_MINOR (ip.runtimeVersion),
		    (int)XR_VERSION_PATCH (ip.runtimeVersion));
    }
    return 1;
}


//
// GetSystem
//
static int GetSystem (void)
{
    XrSystemGetInfo	info = { XR_TYPE_SYSTEM_GET_INFO };
    XrSystemProperties	props = { XR_TYPE_SYSTEM_PROPERTIES };
    XrResult		r;

    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem (instance, &info, &systemid);
    if (XR_FAILED (r))
    {
	fprintf (stderr, "XR: no headset found: %s (%d).\n"
		 "XR: Connect one, or for a test without one run Monado"
		 " with a simulated headset\n"
		 "XR: (SIMULATED_ENABLE=1 monado-service).\n",
		 ResultName (r), (int)r);
	return 0;
    }

    if (XR_SUCCEEDED (xrGetSystemProperties (instance, systemid, &props)))
	printf ("XR: headset %s\n", props.systemName);
    return 1;
}


//
// CreateSession
// On the OpenGL context raylib made current, found through GLX.
//
static int CreateSession (void)
{
    PFN_xrGetOpenGLGraphicsRequirementsKHR	getreqs;
    XrGraphicsRequirementsOpenGLKHR		reqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    XrGraphicsBindingOpenGLXlibKHR		binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR };
    XrSessionCreateInfo				info = { XR_TYPE_SESSION_CREATE_INFO };
    Display*		dpy;
    GLXContext		ctx;
    GLXFBConfig*	configs;
    XVisualInfo*	visual;
    int			attribs[3];
    int			fbconfigid;
    int			screen;
    int			n;
    XrResult		r;

    dpy = glXGetCurrentDisplay ();
    ctx = glXGetCurrentContext ();
    if (!dpy || !ctx)
    {
	fprintf (stderr, "XR: no current GLX context; OpenXR needs raylib"
		 " on X11 with OpenGL (not EGL or Wayland).\n");
	return 0;
    }

    // The runtime refuses a session unless this was asked first.
    if (!Check (xrGetInstanceProcAddr (instance,
				       "xrGetOpenGLGraphicsRequirementsKHR",
				       (PFN_xrVoidFunction*)&getreqs),
		"xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)")
	|| !Check (getreqs (instance, systemid, &reqs),
		   "xrGetOpenGLGraphicsRequirementsKHR"))
	return 0;

    fbconfigid = 0;
    screen = DefaultScreen (dpy);
    glXQueryContext (dpy, ctx, GLX_FBCONFIG_ID, &fbconfigid);
    glXQueryContext (dpy, ctx, GLX_SCREEN, &screen);
    attribs[0] = GLX_FBCONFIG_ID;
    attribs[1] = fbconfigid;
    attribs[2] = None;
    configs = glXChooseFBConfig (dpy, screen, attribs, &n);
    if (!configs || n < 1)
    {
	fprintf (stderr, "XR: can not find the GLX framebuffer config"
		 " of raylib's context.\n");
	return 0;
    }

    binding.xDisplay = dpy;
    binding.glxFBConfig = configs[0];
    binding.glxDrawable = glXGetCurrentDrawable ();
    binding.glxContext = ctx;
    visual = glXGetVisualFromFBConfig (dpy, configs[0]);
    if (visual)
    {
	binding.visualid = (uint32_t)visual->visualid;
	XFree (visual);
    }
    XFree (configs);

    info.next = &binding;
    info.systemId = systemid;
    r = xrCreateSession (instance, &info, &session);
    if (XR_FAILED (r))
    {
	fprintf (stderr, "XR: xrCreateSession failed: %s (%d).\n",
		 ResultName (r), (int)r);
	session = XR_NULL_HANDLE;
	return 0;
    }
    return 1;
}


//
// CreateSpace
// LOCAL: a seated origin where the head was when the session
// started, so the screen comes up straight ahead.
//
static int CreateSpace (void)
{
    XrReferenceSpaceCreateInfo	info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };

    info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    info.poseInReferenceSpace.orientation.w = 1.0f;
    return Check (xrCreateReferenceSpace (session, &info, &space),
		  "xrCreateReferenceSpace");
}


//
// CreateSwapchain
//
static int CreateSwapchain (void)
{
    XrSwapchainCreateInfo	info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    XrSwapchainImageOpenGLKHR	images[MAXIMAGES];
    int64_t			formats[64];
    uint32_t			count;
    uint32_t			i;
    XrEnvironmentBlendMode	modes[8];

    count = 0;
    if (!Check (xrEnumerateSwapchainFormats (session, 64, &count, formats),
		"xrEnumerateSwapchainFormats"))
	return 0;

    // DOOM's palette is sRGB. An sRGB image with GL_FRAMEBUFFER_SRGB
    // left off (raylib never enables it) stores the colors as they
    // are and tells the compositor what they are.
    info.format = 0;
    for (i = 0; i < count && !info.format; i++)
	if (formats[i] == GL_SRGB8_ALPHA8)
	    info.format = GL_SRGB8_ALPHA8;
    for (i = 0; i < count && !info.format; i++)
	if (formats[i] == GL_RGBA8)
	    info.format = GL_RGBA8;
    if (!info.format)
    {
	if (!count)
	{
	    fprintf (stderr, "XR: the runtime offers no swapchain formats.\n");
	    return 0;
	}
	info.format = formats[0];
    }

    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT
		    | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.sampleCount = 1;
    info.width = IMAGEWIDTH;
    info.height = IMAGEHEIGHT;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!Check (xrCreateSwapchain (session, &info, &swapchain),
		"xrCreateSwapchain"))
	return 0;

    for (i = 0; i < MAXIMAGES; i++)
    {
	images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
	images[i].next = NULL;
    }
    if (!Check (xrEnumerateSwapchainImages (swapchain, MAXIMAGES, &numimages,
					    (XrSwapchainImageBaseHeader*)images),
		"xrEnumerateSwapchainImages"))
	return 0;

    for (i = 0; i < numimages; i++)
    {
	fbos[i] = rlLoadFramebuffer ();
	rlFramebufferAttach (fbos[i], images[i].image,
			     RL_ATTACHMENT_COLOR_CHANNEL0,
			     RL_ATTACHMENT_TEXTURE2D, 0);
	if (!rlFramebufferComplete (fbos[i]))
	{
	    fprintf (stderr, "XR: can not draw into the swapchain images.\n");
	    return 0;
	}
    }

    // Opaque on a VR headset; take what the runtime lists first.
    count = 0;
    blendmode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (XR_SUCCEEDED (xrEnumerateEnvironmentBlendModes (instance, systemid,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8, &count, modes))
	&& count)
	blendmode = modes[0];

    printf ("XR: %dx%d swapchain, %u images, format 0x%llx\n",
	    IMAGEWIDTH, IMAGEHEIGHT, numimages,
	    (unsigned long long)info.format);
    return 1;
}


//
// CreateActions
// The controller buttons DOOM listens to, and where they are on
// the controllers runtimes commonly offer. A runtime that lacks
// a profile rejects its bindings; that is not an error.
//
typedef struct
{
    XrAction*	action;
    const char*	path;
} xrbind_t;

static int CreateAction (XrAction* action, const char* name,
			 const char* localized, XrActionType type)
{
    XrActionCreateInfo	info = { XR_TYPE_ACTION_CREATE_INFO };

    strcpy (info.actionName, name);
    strcpy (info.localizedActionName, localized);
    info.actionType = type;
    return Check (xrCreateAction (actionset, &info, action), "xrCreateAction");
}

static void Suggest (const char* profile, const xrbind_t* binds, int count)
{
    XrActionSuggestedBinding			suggested[16];
    XrInteractionProfileSuggestedBinding	info =
	{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    XrResult	r;
    int		i;

    for (i = 0; i < count; i++)
    {
	suggested[i].action = *binds[i].action;
	suggested[i].binding = Path (binds[i].path);
    }
    info.interactionProfile = Path (profile);
    info.suggestedBindings = suggested;
    info.countSuggestedBindings = count;
    r = xrSuggestInteractionProfileBindings (instance, &info);
    if (XR_FAILED (r))
	printf ("XR: no bindings for %s: %s\n", profile, ResultName (r));
}

#define L	"/user/hand/left/input/"
#define R	"/user/hand/right/input/"
#define NUMBINDS(b)	(int)(sizeof(b)/sizeof(b[0]))

static const xrbind_t simplebinds[] =
{
    { &act_fire,	R "select/click" },
    { &act_use,		L "select/click" },
    { &act_menu,	L "menu/click" },
    { &act_map,		R "menu/click" },
};

static const xrbind_t touchbinds[] =
{
    { &act_move,	L "thumbstick" },
    { &act_turn,	R "thumbstick" },
    { &act_fire,	R "trigger/value" },
    { &act_use,		R "a/click" },
    { &act_use,		R "squeeze/value" },
    { &act_map,		R "b/click" },
    { &act_run,		L "trigger/value" },
    { &act_menu,	L "menu/click" },
};

static const xrbind_t indexbinds[] =
{
    { &act_move,	L "thumbstick" },
    { &act_turn,	R "thumbstick" },
    { &act_fire,	R "trigger/click" },
    { &act_use,		R "a/click" },
    { &act_use,		R "squeeze/value" },
    { &act_map,		R "b/click" },
    { &act_run,		L "trigger/click" },
    { &act_menu,	L "b/click" },
};

static const xrbind_t vivebinds[] =
{
    { &act_move,	L "trackpad" },
    { &act_turn,	R "trackpad" },
    { &act_fire,	R "trigger/click" },
    { &act_use,		R "squeeze/click" },
    { &act_map,		R "menu/click" },
    { &act_run,		L "trigger/click" },
    { &act_menu,	L "menu/click" },
};

static const xrbind_t wmrbinds[] =
{
    { &act_move,	L "thumbstick" },
    { &act_turn,	R "thumbstick" },
    { &act_fire,	R "trigger/value" },
    { &act_use,		R "squeeze/click" },
    { &act_map,		R "menu/click" },
    { &act_run,		L "trigger/value" },
    { &act_menu,	L "menu/click" },
};

static int CreateActions (void)
{
    XrActionSetCreateInfo		setinfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
    XrSessionActionSetsAttachInfo	attach = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };

    strcpy (setinfo.actionSetName, "gameplay");
    strcpy (setinfo.localizedActionSetName, "Gameplay");
    if (!Check (xrCreateActionSet (instance, &setinfo, &actionset),
		"xrCreateActionSet"))
	return 0;

    if (!CreateAction (&act_move, "move", "Move and strafe", XR_ACTION_TYPE_VECTOR2F_INPUT)
	|| !CreateAction (&act_turn, "turn", "Turn", XR_ACTION_TYPE_VECTOR2F_INPUT)
	|| !CreateAction (&act_fire, "fire", "Fire", XR_ACTION_TYPE_BOOLEAN_INPUT)
	|| !CreateAction (&act_use, "use", "Use / menu select", XR_ACTION_TYPE_BOOLEAN_INPUT)
	|| !CreateAction (&act_run, "run", "Run", XR_ACTION_TYPE_BOOLEAN_INPUT)
	|| !CreateAction (&act_menu, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT)
	|| !CreateAction (&act_map, "map", "Automap", XR_ACTION_TYPE_BOOLEAN_INPUT))
	return 0;

    Suggest ("/interaction_profiles/khr/simple_controller",
	     simplebinds, NUMBINDS (simplebinds));
    Suggest ("/interaction_profiles/oculus/touch_controller",
	     touchbinds, NUMBINDS (touchbinds));
    Suggest ("/interaction_profiles/valve/index_controller",
	     indexbinds, NUMBINDS (indexbinds));
    Suggest ("/interaction_profiles/htc/vive_controller",
	     vivebinds, NUMBINDS (vivebinds));
    Suggest ("/interaction_profiles/microsoft/motion_controller",
	     wmrbinds, NUMBINDS (wmrbinds));

    attach.countActionSets = 1;
    attach.actionSets = &actionset;
    return Check (xrAttachSessionActionSets (session, &attach),
		  "xrAttachSessionActionSets");
}


int XR_Init (float distance, float width)
{
    screendistance = distance;
    screenwidth = width;

    if (!CreateInstance ()
	|| !GetSystem ()
	|| !CreateSession ()
	|| !CreateSpace ()
	|| !CreateSwapchain ()
	|| !CreateActions ())
    {
	XR_Shutdown ();
	return 0;
    }

    // The headset paces the frames (xrWaitFrame); waiting for the
    // desktop window's vsync as well would halve the frame rate.
    if (glfwSwapInterval)
	glfwSwapInterval (0);

    printf ("XR: session created, screen %.1fm wide at %.1fm\n",
	    screenwidth, screendistance);
    return 1;
}


void XR_Shutdown (void)
{
    uint32_t	i;

    for (i = 0; i < numimages; i++)
	if (fbos[i])
	    rlUnloadFramebuffer (fbos[i]);
    numimages = 0;

    if (swapchain != XR_NULL_HANDLE)
	xrDestroySwapchain (swapchain);
    if (space != XR_NULL_HANDLE)
	xrDestroySpace (space);
    if (actionset != XR_NULL_HANDLE)
	xrDestroyActionSet (actionset);
    // A session may be destroyed in any state; xrEndSession is
    // only for one the runtime is stopping.
    if (session != XR_NULL_HANDLE)
	xrDestroySession (session);
    if (instance != XR_NULL_HANDLE)
	xrDestroyInstance (instance);

    swapchain = XR_NULL_HANDLE;
    space = XR_NULL_HANDLE;
    actionset = XR_NULL_HANDLE;
    session = XR_NULL_HANDLE;
    instance = XR_NULL_HANDLE;
    running = 0;
    buttons = 0;
}


int XR_Active (void)
{
    return session != XR_NULL_HANDLE;
}


static void PrintProfile (const char* hand)
{
    XrInteractionProfileState	ps = { XR_TYPE_INTERACTION_PROFILE_STATE };
    char			name[XR_MAX_PATH_LENGTH];
    uint32_t			len;

    if (XR_FAILED (xrGetCurrentInteractionProfile (session, Path (hand), &ps)))
	return;
    if (ps.interactionProfile == XR_NULL_PATH)
    {
	printf ("XR: %s: no controller\n", hand);
	return;
    }
    if (XR_SUCCEEDED (xrPathToString (instance, ps.interactionProfile,
				      sizeof(name), &len, name)))
	printf ("XR: %s: %s\n", hand, name);
}


static void SessionStateChanged (XrSessionState newstate)
{
    XrSessionBeginInfo	begin = { XR_TYPE_SESSION_BEGIN_INFO };

    state = newstate;
    printf ("XR: session state %s\n", StateName (state));

    switch (state)
    {
      case XR_SESSION_STATE_READY:
	begin.primaryViewConfigurationType =
	    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	if (Check (xrBeginSession (session, &begin), "xrBeginSession"))
	{
	    running = 1;
	    printf ("XR: session running\n");
	}
	break;

      case XR_SESSION_STATE_STOPPING:
	Check (xrEndSession (session), "xrEndSession");
	running = 0;
	break;

      case XR_SESSION_STATE_EXITING:
      case XR_SESSION_STATE_LOSS_PENDING:
	quit = 1;
	break;

      default:
	break;
    }
}


static void PollEvents (void)
{
    XrEventDataBuffer	ev;
    XrResult		r;

    for (;;)
    {
	ev.type = XR_TYPE_EVENT_DATA_BUFFER;
	ev.next = NULL;
	r = xrPollEvent (instance, &ev);
	if (r != XR_SUCCESS)
	{
	    Lost (r);
	    break;
	}

	switch (ev.type)
	{
	  case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
	    SessionStateChanged (((XrEventDataSessionStateChanged*)&ev)->state);
	    break;

	  case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
	    printf ("XR: runtime going away\n");
	    quit = 1;
	    break;

	  case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
	    PrintProfile ("/user/hand/left");
	    PrintProfile ("/user/hand/right");
	    break;

	  default:
	    break;
	}
    }
}


static int Pressed (XrAction action)
{
    XrActionStateGetInfo	info = { XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateBoolean	st = { XR_TYPE_ACTION_STATE_BOOLEAN };

    info.action = action;
    if (XR_FAILED (xrGetActionStateBoolean (session, &info, &st)))
	return 0;
    return st.isActive && st.currentState;
}


static XrVector2f Stick (XrAction action)
{
    XrActionStateGetInfo	info = { XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateVector2f	st = { XR_TYPE_ACTION_STATE_VECTOR2F };
    XrVector2f			none = { 0, 0 };

    info.action = action;
    if (XR_FAILED (xrGetActionStateVector2f (session, &info, &st))
	|| !st.isActive)
	return none;
    return st.currentState;
}


static void ReadControllers (void)
{
    XrActiveActionSet	active = { actionset, XR_NULL_PATH };
    XrActionsSyncInfo	sync = { XR_TYPE_ACTIONS_SYNC_INFO };
    XrVector2f		move;
    XrVector2f		turn;

    buttons = 0;

    // Only the focused session gets input.
    if (state != XR_SESSION_STATE_FOCUSED)
	return;

    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (Lost (xrSyncActions (session, &sync)))
	return;

    move = Stick (act_move);
    turn = Stick (act_turn);

    if (move.y > STICKPRESS)
	buttons |= XR_FORWARD;
    if (move.y < -STICKPRESS)
	buttons |= XR_BACK;
    if (move.x < -STICKPRESS)
	buttons |= XR_STRAFELEFT;
    if (move.x > STICKPRESS)
	buttons |= XR_STRAFERIGHT;
    if (turn.x < -STICKPRESS)
	buttons |= XR_TURNLEFT;
    if (turn.x > STICKPRESS)
	buttons |= XR_TURNRIGHT;

    if (Pressed (act_fire))
	buttons |= XR_FIRE;
    if (Pressed (act_use))
	buttons |= XR_USE;
    if (Pressed (act_run))
	buttons |= XR_RUN;
    if (Pressed (act_menu))
	buttons |= XR_MENU;
    if (Pressed (act_map))
	buttons |= XR_MAP;
}


int XR_Update (void)
{
    if (instance == XR_NULL_HANDLE)
	return 1;

    PollEvents ();
    ReadControllers ();
    return !quit;
}


unsigned XR_Buttons (void)
{
    return buttons;
}


void XR_Present (xr_draw_t draw)
{
    XrFrameWaitInfo		waitinfo = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState		frame = { XR_TYPE_FRAME_STATE };
    XrFrameBeginInfo		begininfo = { XR_TYPE_FRAME_BEGIN_INFO };
    XrFrameEndInfo		endinfo = { XR_TYPE_FRAME_END_INFO };
    XrSwapchainImageAcquireInfo	acquire = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo	wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    XrSwapchainImageReleaseInfo	release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XrCompositionLayerQuad	quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    const XrCompositionLayerBaseHeader*	layers[1];
    uint32_t			index;
    XrResult			r;

    if (!running)
	return;

    r = xrWaitFrame (session, &waitinfo, &frame);
    if (Lost (r) || !Check (r, "xrWaitFrame"))
	return;
    r = xrBeginFrame (session, &begininfo);
    if (Lost (r) || !Check (r, "xrBeginFrame"))
	return;

    endinfo.displayTime = frame.predictedDisplayTime;
    endinfo.environmentBlendMode = blendmode;
    endinfo.layerCount = 0;
    endinfo.layers = layers;

    if (frame.shouldRender
	&& Check (xrAcquireSwapchainImage (swapchain, &acquire, &index),
		  "xrAcquireSwapchainImage"))
    {
	wait.timeout = XR_INFINITE_DURATION;
	if (Check (xrWaitSwapchainImage (swapchain, &wait),
		   "xrWaitSwapchainImage"))
	    draw (fbos[index], IMAGEWIDTH, IMAGEHEIGHT);
	xrReleaseSwapchainImage (swapchain, &release);

	// A 4:3 screen straight ahead of where the head started.
	quad.layerFlags = 0;
	quad.space = space;
	quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	quad.subImage.swapchain = swapchain;
	quad.subImage.imageRect.offset.x = 0;
	quad.subImage.imageRect.offset.y = 0;
	quad.subImage.imageRect.extent.width = IMAGEWIDTH;
	quad.subImage.imageRect.extent.height = IMAGEHEIGHT;
	quad.subImage.imageArrayIndex = 0;
	quad.pose.orientation.w = 1.0f;
	quad.pose.position.z = -screendistance;
	quad.size.width = screenwidth;
	quad.size.height = screenwidth * 3.0f / 4.0f;

	layers[0] = (const XrCompositionLayerBaseHeader*)&quad;
	endinfo.layerCount = 1;
    }

    r = xrEndFrame (session, &endinfo);
    if (!Lost (r))
	Check (r, "xrEndFrame");
}
