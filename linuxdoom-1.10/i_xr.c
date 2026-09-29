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
//	Two graphics bindings, one for each way raylib makes its
//	context:
//	- Linux desktop: OpenGL on X11 (GLX, XR_KHR_opengl_enable),
//	  which is what raylib's GLFW uses there.
//	- Android (Android XR): OpenGL ES on EGL
//	  (XR_KHR_opengl_es_enable), from raylib's NativeActivity. The
//	  loader is started with XR_KHR_loader_init_android first.
//
//	This file sees OpenXR, GLX or EGL and rlgl, but neither raylib.h
//	(Xlib's Font typedef clashes with raylib's) nor the DOOM
//	headers.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android_native_app_glue.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#else
#include <X11/Xlib.h>
#include <GL/glx.h>

#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#include "rlgl.h"

#include "i_xr.h"


// The swapchain image: 320x200 blown up 5x6 is exactly 4:3,
// with every DOOM pixel the same size.
#define IMAGEWIDTH	1600
#define IMAGEHEIGHT	1200

// Tries of an enumeration whose list grows between the call that
// sizes it and the call that fills it.
#define ENUMTRIES	4

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8	0x8C43
#endif
#ifndef GL_RGBA8
#define GL_RGBA8	0x8058
#endif

// Stick deflection that counts as a key press.
#define STICKPRESS	0.5f

// Without -xrwidth the screen is made no wider than this, and
// small enough to fill no more than FITFOV of the narrowest half
// field of view, so it fits in see-through glasses too.
#define MAXWIDTH	3.2f
#define FITFOV		0.9f

// Black lift on see-through (additive) displays, where black is
// transparent and dark scenes disappear against the room.
#define ADDITIVELIFT	0.12f

// XR_EXT_hand_interaction is newer than some SDK headers.
#define HANDINTERACTION	"XR_EXT_hand_interaction"

#ifdef __ANDROID__
#define GRAPHICS_EXTENSION	XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME
typedef XrSwapchainImageOpenGLESKHR	swapchainimage_t;
#define SWAPCHAIN_IMAGE_TYPE	XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR

// raylib's rcore_android.c
extern struct android_app* GetAndroidApp (void);

// raylib 5.5's rlgl calls glDrawBuffersEXT when built for OpenGL
// ES 3, but Android's libGLESv3 only has the ES 3 name.
void GL_APIENTRY glDrawBuffersEXT (GLsizei n, const GLenum* bufs)
{
    glDrawBuffers (n, bufs);
}
#else
#define GRAPHICS_EXTENSION	XR_KHR_OPENGL_ENABLE_EXTENSION_NAME
typedef XrSwapchainImageOpenGLKHR	swapchainimage_t;
#define SWAPCHAIN_IMAGE_TYPE	XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR

// raylib's GLFW, weak so a raylib that hides it still links.
extern void glfwSwapInterval (int interval) __attribute__((weak));
#endif

static XrInstance	instance = XR_NULL_HANDLE;
static XrSystemId	systemid = XR_NULL_SYSTEM_ID;
static XrSession	session = XR_NULL_HANDLE;
static XrSpace		space = XR_NULL_HANDLE;
static XrSwapchain	swapchain = XR_NULL_HANDLE;
static XrEnvironmentBlendMode	blendmode;

static unsigned int*	fbos;		// one per swapchain image
static uint32_t		numimages;
static uint32_t		numfbos;	// those made so far

// The swapchain image being drawn. One whose wait failed stays
// acquired, as OpenXR allows neither releasing it nor acquiring
// another, and is waited for again next frame.
enum { IMAGE_FREE, IMAGE_ACQUIRED, IMAGE_WAITED };
static int		imagestate;
static uint32_t		imageindex;

static XrSessionState	state = XR_SESSION_STATE_UNKNOWN;
static int		running;	// between xrBeginSession and xrEndSession
static int		quit;
static const char*	lost;		// why the runtime went away
static int		novsync;	// the window's vsync is off

static float		screendistance;
static float		screenwidth;
static int		fitscreen;	// no -xrwidth: fit to the field of view
static float		blacklift;	// -xrlift, or <0 for the default
static int		havehands;	// XR_EXT_hand_interaction

// -xrstats: frame pacing, logged every STATSECONDS.
#define STATSECONDS	10
static int		stats;
static double		statstart;
static double		lastwait;
static int		statframes;
static int		statlate;
static double		statworst;

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
// Nothing more is sent to it; XR_Update drops OpenXR and the game
// goes on in the window.
static const char* BlendName (XrEnvironmentBlendMode m)
{
    switch (m)
    {
      case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:	return "opaque";
      case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:	return "additive";
      case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:	return "alpha blend";
      default:					return "unknown";
    }
}


static int Lost (XrResult r)
{
    if (r == XR_ERROR_SESSION_LOST || r == XR_ERROR_INSTANCE_LOST)
    {
	if (!lost)
	    lost = ResultName (r);
	running = 0;
	return 1;
    }
    return 0;
}


//
// SetVsync
// While the session runs, xrWaitFrame paces the frames; waiting
// for the desktop window's vsync as well would halve the frame
// rate. Otherwise the window's vsync keeps the game from spinning.
//
static void SetVsync (int on)
{
    if (novsync == !on)
	return;
    novsync = !on;
#ifdef __ANDROID__
    eglSwapInterval (eglGetCurrentDisplay (), on);
#else
    if (glfwSwapInterval)
	glfwSwapInterval (on);
#endif
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


#ifdef __ANDROID__
//
// InitLoader
// The Android loader has to be given the VM and the activity
// before anything else, even the extension list.
//
static int InitLoader (void)
{
    PFN_xrInitializeLoaderKHR	initloader = NULL;
    XrLoaderInitInfoAndroidKHR	info = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
    struct android_app*		app = GetAndroidApp ();
    XrResult			r;

    r = xrGetInstanceProcAddr (XR_NULL_HANDLE, "xrInitializeLoaderKHR",
			       (PFN_xrVoidFunction*)&initloader);
    if (XR_FAILED (r) || !initloader)
    {
	fprintf (stderr, "XR: the OpenXR loader has no"
		 " xrInitializeLoaderKHR: %s (%d).\n", ResultName (r), (int)r);
	return 0;
    }
    info.applicationVM = app->activity->vm;
    info.applicationContext = app->activity->clazz;
    r = initloader ((const XrLoaderInitInfoBaseHeaderKHR*)&info);
    if (XR_FAILED (r))
    {
	fprintf (stderr, "XR: xrInitializeLoaderKHR failed: %s (%d).\n",
		 ResultName (r), (int)r);
	return 0;
    }
    return 1;
}
#endif


//
// XR_PrepareGL
// The runtime's compositor renders on the GPU the headset hangs
// off, the NVIDIA one on a laptop that has it, while the window
// opens on the integrated GPU by default. OpenGL can not share the
// swapchain images across the two, and Mesa crashes in
// xrCreateSwapchain trying. So with the NVIDIA driver loaded, ask
// for PRIME render offload before the window opens.
// DOOM_XR_PRIME=0 turns this off, and a __GLX_VENDOR_LIBRARY_NAME
// the user set is left alone.
//
void XR_PrepareGL (void)
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
// CreateInstance
//
static int CreateInstance (void)
{
    XrInstanceCreateInfo	info;
    XrExtensionProperties*	props;
    uint32_t			count;
    uint32_t			capacity;
    int				tries;
    uint32_t			i;
    int				havegl;
    XrResult			r;
    const char*			extensions[3];
    uint32_t			numextensions;
#ifdef __ANDROID__
    XrInstanceCreateInfoAndroidKHR	android = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    struct android_app*		app = GetAndroidApp ();

    if (!InitLoader ())
	return 0;
#endif

    numextensions = 0;
    extensions[numextensions++] = GRAPHICS_EXTENSION;
#ifdef __ANDROID__
    extensions[numextensions++] = XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME;
#endif
    havehands = 0;

    props = NULL;
    r = XR_ERROR_SIZE_INSUFFICIENT;
    for (tries = 0; tries < ENUMTRIES && r == XR_ERROR_SIZE_INSUFFICIENT; tries++)
    {
	free (props);
	props = NULL;
	count = 0;
	r = xrEnumerateInstanceExtensionProperties (NULL, 0, &count, NULL);
	if (XR_FAILED (r))
	{
#ifdef __ANDROID__
	    fprintf (stderr, "XR: no OpenXR runtime found (%s).\n",
		     ResultName (r));
	    return 0;
#endif
	    fprintf (stderr,
		     "XR: no OpenXR runtime found (%s).\n"
		     "XR: Start one (for Monado: monado-service), or point\n"
		     "XR: XR_RUNTIME_JSON at its manifest, e.g.\n"
		     "XR:   XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json\n",
		     ResultName (r));
	    return 0;
	}
	capacity = count;
	props = calloc (capacity ? capacity : 1, sizeof(*props));
	if (!props)
	    return 0;
	for (i = 0; i < capacity; i++)
	    props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
	r = xrEnumerateInstanceExtensionProperties (NULL, capacity, &count,
						    props);
    }
    if (!Check (r, "xrEnumerateInstanceExtensionProperties"))
    {
	free (props);
	return 0;
    }
    if (count > capacity)
	count = capacity;

    havegl = 0;
    for (i = 0; i < count; i++)
    {
#ifdef __ANDROID__
	// What the device's runtime offers; Android XR recommends
	// Vulkan, so check that GLES is there.
	printf ("XR: runtime extension %s v%u\n", props[i].extensionName,
		(unsigned)props[i].extensionVersion);
#endif
	if (!strncmp (props[i].extensionName, GRAPHICS_EXTENSION,
		      XR_MAX_EXTENSION_NAME_SIZE))
	    havegl = 1;
	// Pinches, on glasses and headsets tracking bare hands.
	if (!strncmp (props[i].extensionName, HANDINTERACTION,
		      XR_MAX_EXTENSION_NAME_SIZE))
	    havehands = 1;
    }
    free (props);
    if (!havegl)
    {
#ifdef __ANDROID__
	fprintf (stderr, "XR: the OpenXR runtime has no OpenGL ES support"
		 " (" GRAPHICS_EXTENSION ").\n");
#else
	fprintf (stderr, "XR: the OpenXR runtime has no OpenGL support"
		 " (" GRAPHICS_EXTENSION ").\n");
#endif
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
    if (havehands)
	extensions[numextensions++] = HANDINTERACTION;
    info.enabledExtensionCount = numextensions;
    info.enabledExtensionNames = extensions;
#ifdef __ANDROID__
    android.applicationVM = app->activity->vm;
    android.applicationActivity = app->activity->clazz;
    info.next = &android;
#endif

    r = xrCreateInstance (&info, &instance);
#ifdef __ANDROID__
    if (XR_FAILED (r))
    {
	fprintf (stderr, "XR: xrCreateInstance failed: %s (%d).\n",
		 ResultName (r), (int)r);
	instance = XR_NULL_HANDLE;
	return 0;
    }
#endif
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
#ifdef __ANDROID__
    if (XR_FAILED (r))
    {
	fprintf (stderr, "XR: no headset or glasses found: %s (%d).\n",
		 ResultName (r), (int)r);
	return 0;
    }
#endif
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


#ifdef __ANDROID__
//
// CreateSession
// On the OpenGL ES context raylib made current, found through EGL.
//
static int CreateSession (void)
{
    PFN_xrGetOpenGLESGraphicsRequirementsKHR	getreqs;
    XrGraphicsRequirementsOpenGLESKHR		reqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
    XrGraphicsBindingOpenGLESAndroidKHR		binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    XrSessionCreateInfo				info = { XR_TYPE_SESSION_CREATE_INFO };
    EGLDisplay	dpy;
    EGLContext	ctx;
    EGLConfig	config;
    EGLint	attribs[3];
    EGLint	configid;
    EGLint	n;
    int		major;
    int		minor;
    XrResult	r;

    dpy = eglGetCurrentDisplay ();
    ctx = eglGetCurrentContext ();
    if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT)
    {
	fprintf (stderr, "XR: no current EGL context.\n");
	return 0;
    }

    // The runtime refuses a session unless this was asked first.
    if (!Check (xrGetInstanceProcAddr (instance,
				       "xrGetOpenGLESGraphicsRequirementsKHR",
				       (PFN_xrVoidFunction*)&getreqs),
		"xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR)")
	|| !Check (getreqs (instance, systemid, &reqs),
		   "xrGetOpenGLESGraphicsRequirementsKHR"))
	return 0;

    // raylib asks EGL for ES 2, which Android drivers answer with
    // the newest ES they have.
    major = rlGetVersion () == RL_OPENGL_ES_30 ? 3 : 2;
    minor = 0;
    {
	const char*	version = (const char*)glGetString (GL_VERSION);

	if (version)
	{
	    printf ("XR: %s\n", version);
	    sscanf (version, "OpenGL ES %d.%d", &major, &minor);
	}
    }
    if (XR_MAKE_VERSION (major, minor, 0) < reqs.minApiVersionSupported)
    {
	fprintf (stderr, "XR: the runtime needs OpenGL ES %d.%d, raylib's"
		 " context is %d.%d.\n",
		 (int)XR_VERSION_MAJOR (reqs.minApiVersionSupported),
		 (int)XR_VERSION_MINOR (reqs.minApiVersionSupported),
		 major, minor);
	return 0;
    }

    configid = 0;
    eglQueryContext (dpy, ctx, EGL_CONFIG_ID, &configid);
    attribs[0] = EGL_CONFIG_ID;
    attribs[1] = configid;
    attribs[2] = EGL_NONE;
    if (!eglChooseConfig (dpy, attribs, &config, 1, &n) || n < 1)
    {
	fprintf (stderr, "XR: can not find the EGL config"
		 " of raylib's context.\n");
	return 0;
    }

    binding.display = dpy;
    binding.config = config;
    binding.context = ctx;

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
#else
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
#endif


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
    swapchainimage_t*		images;
    int64_t*			formats;
    uint32_t			count;
    uint32_t			capacity;
    uint32_t			i;
    int				tries;
    XrResult			r;
    XrEnvironmentBlendMode	modes[8];
    int				seethrough;

    formats = NULL;
    count = capacity = 0;
    r = XR_ERROR_SIZE_INSUFFICIENT;
    for (tries = 0; tries < ENUMTRIES && r == XR_ERROR_SIZE_INSUFFICIENT; tries++)
    {
	free (formats);
	formats = NULL;
	count = 0;
	r = xrEnumerateSwapchainFormats (session, 0, &count, NULL);
	if (XR_FAILED (r))
	    break;
	capacity = count;
	formats = calloc (capacity ? capacity : 1, sizeof(*formats));
	if (!formats)
	    return 0;
	r = xrEnumerateSwapchainFormats (session, capacity, &count, formats);
    }
    if (!Check (r, "xrEnumerateSwapchainFormats"))
    {
	free (formats);
	return 0;
    }
    if (count > capacity)
	count = capacity;

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
	    free (formats);
	    return 0;
	}
	info.format = formats[0];
    }
    free (formats);

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

    // A swapchain's images are fixed once it is made.
    count = 0;
    if (!Check (xrEnumerateSwapchainImages (swapchain, 0, &count, NULL),
		"xrEnumerateSwapchainImages"))
	return 0;
    if (!count)
    {
	fprintf (stderr, "XR: the swapchain has no images.\n");
	return 0;
    }
    capacity = count;
    images = calloc (capacity, sizeof(*images));
    fbos = calloc (capacity, sizeof(*fbos));
    if (!images || !fbos)
    {
	free (images);
	return 0;
    }
    for (i = 0; i < capacity; i++)
	images[i].type = SWAPCHAIN_IMAGE_TYPE;
    r = xrEnumerateSwapchainImages (swapchain, capacity, &count,
				    (XrSwapchainImageBaseHeader*)images);
    if (!Check (r, "xrEnumerateSwapchainImages") || count != capacity)
    {
	free (images);
	return 0;
    }
    numimages = count;

    for (i = 0; i < numimages; i++)
    {
	fbos[i] = rlLoadFramebuffer ();
	numfbos = i + 1;
	rlFramebufferAttach (fbos[i], images[i].image,
			     RL_ATTACHMENT_COLOR_CHANNEL0,
			     RL_ATTACHMENT_TEXTURE2D, 0);
	if (!rlFramebufferComplete (fbos[i]))
	{
	    fprintf (stderr, "XR: can not draw into the swapchain images.\n");
	    free (images);
	    return 0;
	}
    }
    free (images);

    // Take what the runtime lists first, which it prefers. OpenXR
    // offers additive only on see-through displays, whatever mode
    // is used: the Xreal Aura lists opaque first, but its optics
    // still show black as the room behind it.
    count = 0;
    seethrough = 0;
    blendmode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (XR_SUCCEEDED (xrEnumerateEnvironmentBlendModes (instance, systemid,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8, &count, modes))
	&& count)
    {
	if (count > 8)
	    count = 8;
	blendmode = modes[0];
	for (i = 0; i < count; i++)
	{
	    printf ("XR: blend mode %s\n", BlendName (modes[i]));
	    if (modes[i] == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE)
		seethrough = 1;
	}
    }
    printf ("XR: using blend mode %s%s\n", BlendName (blendmode),
	    seethrough ? " on a see-through display" : "");

    // So dark scenes do not vanish into the room, black is lifted
    // there unless -xrlift says otherwise.
    if (blacklift < 0)
	blacklift = seethrough ? ADDITIVELIFT : 0;
    if (blacklift > 0)
	printf ("XR: black lifted to %d%%\n", (int)(blacklift * 100 + 0.5f));

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

// XR_EXT_hand_interaction: a right pinch fires, a left one uses,
// and a grasp (closed hand) opens the automap or the menu. The
// runtime turns the pinch strength into a press.
static const xrbind_t handbinds[] =
{
    { &act_fire,	R "pinch_ext/value" },
    { &act_use,		L "pinch_ext/value" },
    { &act_map,		R "grasp_ext/value" },
    { &act_menu,	L "grasp_ext/value" },
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

    if (havehands)
	Suggest ("/interaction_profiles/ext/hand_interaction_ext",
		 handbinds, NUMBINDS(handbinds));

    attach.countActionSets = 1;
    attach.actionSets = &actionset;
    return Check (xrAttachSessionActionSets (session, &attach),
		  "xrAttachSessionActionSets");
}


void XR_Stats (int on)
{
    stats = on;
}


int XR_Init (float distance, float width, float lift)
{
    screendistance = distance;
    fitscreen = width <= 0;
    screenwidth = fitscreen ? MAXWIDTH : width;
    blacklift = lift;

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

    printf ("XR: session created, screen %.1fm wide at %.1fm%s\n",
	    screenwidth, screendistance,
	    fitscreen ? " (until the field of view is known)" : "");
    return 1;
}


void XR_Shutdown (void)
{
    uint32_t	i;

    for (i = 0; i < numfbos; i++)
	if (fbos[i])
	    rlUnloadFramebuffer (fbos[i]);
    free (fbos);
    fbos = NULL;
    numfbos = 0;
    numimages = 0;
    imagestate = IMAGE_FREE;

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
    state = XR_SESSION_STATE_UNKNOWN;
    lost = NULL;
    lastwait = 0;
    SetVsync (1);
}


int XR_Active (void)
{
    return session != XR_NULL_HANDLE;
}


float XR_BlackLift (void)
{
    return XR_Active () && blacklift > 0 ? blacklift : 0;
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
	    SetVsync (0);
	    printf ("XR: session running\n");
	}
	break;

      case XR_SESSION_STATE_STOPPING:
	Check (xrEndSession (session), "xrEndSession");
	running = 0;
	SetVsync (1);
	break;

      // The user quit from the runtime.
      case XR_SESSION_STATE_EXITING:
	quit = 1;
	break;

      // The headset or the runtime is going away.
      case XR_SESSION_STATE_LOSS_PENDING:
	if (!lost)
	    lost = StateName (state);
	running = 0;
	break;

      default:
	break;
    }
}


static void PollEvents (void)
{
    XrEventDataBuffer	ev;
    XrResult		r;

    while (!lost)
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
	    if (!lost)
		lost = "XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING";
	    running = 0;
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
    if (lost && !quit)
    {
	printf ("XR: lost the OpenXR runtime (%s),"
		" continuing in the window\n", lost);
	XR_Shutdown ();
	return 1;
    }
    ReadControllers ();
    return !quit;
}


unsigned XR_Buttons (void)
{
    return buttons;
}


//
// WaitImage
// Gets a swapchain image to draw into, going on from where an
// earlier frame's failure left it.
//
static int WaitImage (void)
{
    XrSwapchainImageAcquireInfo	acquire = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo	wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    XrResult			r;

    if (imagestate == IMAGE_FREE)
    {
	r = xrAcquireSwapchainImage (swapchain, &acquire, &imageindex);
	if (Lost (r) || !Check (r, "xrAcquireSwapchainImage"))
	    return 0;
	imagestate = IMAGE_ACQUIRED;
    }
    if (imagestate == IMAGE_ACQUIRED)
    {
	wait.timeout = XR_INFINITE_DURATION;
	r = xrWaitSwapchainImage (swapchain, &wait);
	if (Lost (r))
	    return 0;
	// XR_TIMEOUT_EXPIRED succeeds, but the image is not ready.
	if (r != XR_SUCCESS)
	{
	    if (r != XR_TIMEOUT_EXPIRED)
		Check (r, "xrWaitSwapchainImage");
	    return 0;
	}
	imagestate = IMAGE_WAITED;
    }
    if (imageindex >= numfbos)
    {
	XrSwapchainImageReleaseInfo	release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };

	fprintf (stderr, "XR: the runtime gave swapchain image %u of %u.\n",
		 imageindex, numfbos);
	r = xrReleaseSwapchainImage (swapchain, &release);
	if (!Lost (r) && Check (r, "xrReleaseSwapchainImage"))
	    imagestate = IMAGE_FREE;
	return 0;
    }
    return 1;
}


//
// CountFrame
// With -xrstats, how often xrWaitFrame let a frame through. DOOM
// makes a frame each 35th of a second, so on a faster display most
// frames span a few periods (the compositor fills in); one taking
// over 1.5 tics, or 1.5 periods on a slower display, was late.
//
static double Now (void)
{
    struct timespec	ts;

    clock_gettime (CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void CountFrame (XrDuration period)
{
    double	now = Now ();
    double	gap;
    double	p = period / 1e9;
    double	tic = p > 1.0 / 35 ? p : 1.0 / 35;

    if (lastwait > 0)
    {
	gap = now - lastwait;
	statframes++;
	if (gap > tic * 1.5)
	    statlate++;
	if (gap > statworst)
	    statworst = gap;
    }
    else
	statstart = now;
    lastwait = now;

    if (now - statstart >= STATSECONDS)
    {
	printf ("XR: %d frames in %.1fs (%.1f/s), display period %.1fms,"
		" %d late, worst %.1fms\n", statframes, now - statstart,
		statframes / (now - statstart), p * 1000, statlate,
		statworst * 1000);
	statstart = now;
	statframes = statlate = 0;
	statworst = 0;
    }
}


//
// FitScreen
// Without -xrwidth, sizes the screen once to the eyes' field of
// view: glasses such as the Xreal Aura see much less than a VR
// headset. The narrowest half angle of either eye, as the screen
// is centered, and never wider than MAXWIDTH.
//
static void FitScreen (XrTime time)
{
    XrViewLocateInfo	info = { XR_TYPE_VIEW_LOCATE_INFO };
    XrViewState		vs = { XR_TYPE_VIEW_STATE };
    XrView		views[2];
    uint32_t		count;
    uint32_t		i;
    float		h;
    float		v;
    float		w;
    XrResult		r;

    for (i = 0; i < 2; i++)
    {
	memset (&views[i], 0, sizeof(views[i]));
	views[i].type = XR_TYPE_VIEW;
    }
    info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    info.displayTime = time;
    info.space = space;
    count = 0;
    r = xrLocateViews (session, &info, &vs, 2, &count, views);
    if (XR_FAILED (r) || !count)
	return;
    fitscreen = 0;

    h = v = 1.5f;
    for (i = 0; i < count && i < 2; i++)
    {
	if (-views[i].fov.angleLeft < h)	h = -views[i].fov.angleLeft;
	if (views[i].fov.angleRight < h)	h = views[i].fov.angleRight;
	if (-views[i].fov.angleDown < v)	v = -views[i].fov.angleDown;
	if (views[i].fov.angleUp < v)		v = views[i].fov.angleUp;
    }
    printf ("XR: field of view %.0f x %.0f degrees (narrowest half"
	    " angles doubled)\n", h * 2 * 57.29578f, v * 2 * 57.29578f);
    if (h <= 0 || v <= 0)
	return;

    w = 2 * screendistance * tanf (h * FITFOV);
    if (w > 2 * screendistance * tanf (v * FITFOV) * 4.0f / 3.0f)
	w = 2 * screendistance * tanf (v * FITFOV) * 4.0f / 3.0f;
    if (w > MAXWIDTH)
	w = MAXWIDTH;
    screenwidth = w;
    printf ("XR: screen %.2fm wide at %.1fm\n", screenwidth, screendistance);
}


void XR_Present (xr_draw_t draw)
{
    XrFrameWaitInfo		waitinfo = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState		frame = { XR_TYPE_FRAME_STATE };
    XrFrameBeginInfo		begininfo = { XR_TYPE_FRAME_BEGIN_INFO };
    XrFrameEndInfo		endinfo = { XR_TYPE_FRAME_END_INFO };
    XrSwapchainImageReleaseInfo	release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XrCompositionLayerQuad	quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    const XrCompositionLayerBaseHeader*	layers[1];
    XrResult			r;

    if (!running || lost)
	return;

    r = xrWaitFrame (session, &waitinfo, &frame);
    if (Lost (r) || !Check (r, "xrWaitFrame"))
	return;
    if (stats)
	CountFrame (frame.predictedDisplayPeriod);
    r = xrBeginFrame (session, &begininfo);
    if (Lost (r) || !Check (r, "xrBeginFrame"))
	return;

    if (fitscreen)
	FitScreen (frame.predictedDisplayTime);

    endinfo.displayTime = frame.predictedDisplayTime;
    endinfo.environmentBlendMode = blendmode;
    endinfo.layerCount = 0;
    endinfo.layers = layers;

    // Only an image that was waited for is drawn and released, and
    // only one that was released is shown; the frame is ended
    // either way, empty if need be.
    if (frame.shouldRender && WaitImage ())
    {
	draw (fbos[imageindex], IMAGEWIDTH, IMAGEHEIGHT);
	r = xrReleaseSwapchainImage (swapchain, &release);
	if (Lost (r) || !Check (r, "xrReleaseSwapchainImage"))
	    goto end;
	imagestate = IMAGE_FREE;

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

  end:
    r = xrEndFrame (session, &endinfo);
    if (!Lost (r))
	Check (r, "xrEndFrame");
}
