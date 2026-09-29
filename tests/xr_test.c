// OpenXR regression tests: i_xr.c against a fake runtime.
//
// i_xr.c is included so its static state and helpers can be
// reached. The OpenXR calls it makes on the paths tested here are
// defined below; the executable's definitions take precedence over
// the shared loader's, which only resolves the rest.

#include "i_xr.c"

#include <stdarg.h>

static int failures;

static void Expect (int ok, const char* fmt, ...)
{
    va_list	ap;

    if (ok)
	return;
    va_start (ap, fmt);
    fprintf (stderr, "FAIL: ");
    vfprintf (stderr, fmt, ap);
    fprintf (stderr, "\n");
    va_end (ap);
    failures++;
}


//
// The fake runtime.
//

// Extensions: how many the runtime has, how much the list grows
// on each filling call, and whether it always claims to need more.
static uint32_t	fake_extensions;
static uint32_t	fake_extgrowth;
static int	fake_extalwaysshort;

static uint32_t	fake_images;
static int	fake_incompletefbo = -1;	// this FBO is incomplete

static XrResult	fake_waitresult;
static XrResult	fake_releaseresult;
static uint32_t	fake_acquireindex;

// Session states xrPollEvent hands out, one per call, and what
// it returns once they are gone.
static XrSessionState	fake_states[4];
static int		fake_numstates;
static int		fake_nextstate;
static XrResult		fake_pollresult = XR_EVENT_UNAVAILABLE;

static int	acquires, waits, releases, draws, endframes;
static uint32_t	lastlayers;
static int	loadedfbos, unloadedfbos;
static int	swapinterval = -1;	// last glfwSwapInterval

static XrEnvironmentBlendMode	fake_blendmode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
static uint32_t	fake_blendmodes = 1;
static XrFovf	fake_fov;	// both eyes'
static int	locates;


XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties (
    const char* layer, uint32_t capacity, uint32_t* count,
    XrExtensionProperties* props)
{
    uint32_t	i;

    (void)layer;
    *count = fake_extensions;
    if (!capacity)
	return XR_SUCCESS;
    if (fake_extalwaysshort || capacity < fake_extensions)
    {
	for (i = 0; i < capacity; i++)
	    strcpy (props[i].extensionName, "XR_test_placeholder");
	*count = fake_extensions + 1;
	return XR_ERROR_SIZE_INSUFFICIENT;
    }
    if (fake_extgrowth)
    {
	// Another extension appeared since the count was asked for.
	fake_extensions += fake_extgrowth;
	fake_extgrowth = 0;
	*count = fake_extensions;
	return XR_ERROR_SIZE_INSUFFICIENT;
    }
    for (i = 0; i < fake_extensions; i++)
	strcpy (props[i].extensionName, "XR_test_placeholder");
    // Last, so a list read short misses it.
    strcpy (props[fake_extensions - 1].extensionName,
	    XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance (
    const XrInstanceCreateInfo* info, XrInstance* out)
{
    (void)info;
    *out = (XrInstance)(uintptr_t)1;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProperties (
    XrInstance inst, XrInstanceProperties* props)
{
    (void)inst;
    strcpy (props->runtimeName, "fake");
    props->runtimeVersion = XR_MAKE_VERSION (1, 0, 0);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainFormats (
    XrSession s, uint32_t capacity, uint32_t* count, int64_t* formats)
{
    (void)s;
    *count = 1;
    if (!capacity)
	return XR_SUCCESS;
    formats[0] = GL_SRGB8_ALPHA8;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain (
    XrSession s, const XrSwapchainCreateInfo* info, XrSwapchain* out)
{
    (void)s;
    (void)info;
    *out = (XrSwapchain)(uintptr_t)1;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages (
    XrSwapchain sc, uint32_t capacity, uint32_t* count,
    XrSwapchainImageBaseHeader* images)
{
    uint32_t	i;

    (void)sc;
    *count = fake_images;
    if (!capacity)
	return XR_SUCCESS;
    if (capacity < fake_images)
	return XR_ERROR_SIZE_INSUFFICIENT;
    for (i = 0; i < fake_images; i++)
	((XrSwapchainImageOpenGLKHR*)images)[i].image = 100 + i;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes (
    XrInstance inst, XrSystemId sys, XrViewConfigurationType type,
    uint32_t capacity, uint32_t* count, XrEnvironmentBlendMode* modes)
{
    (void)inst;
    (void)sys;
    (void)type;
    // One mode, or opaque and then that one.
    *count = fake_blendmodes;
    if (capacity >= fake_blendmodes)
    {
	modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	modes[fake_blendmodes - 1] = fake_blendmode;
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews (
    XrSession s, const XrViewLocateInfo* info, XrViewState* state,
    uint32_t capacity, uint32_t* count, XrView* views)
{
    uint32_t	i;

    (void)s;
    (void)info;
    (void)state;
    locates++;
    *count = 2;
    for (i = 0; i < capacity && i < 2; i++)
	views[i].fov = fake_fov;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain (XrSwapchain h)
{ (void)h; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace (XrSpace h)
{ (void)h; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet (XrActionSet h)
{ (void)h; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession (XrSession h)
{ (void)h; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance (XrInstance h)
{ (void)h; return XR_SUCCESS; }

XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent (
    XrInstance inst, XrEventDataBuffer* ev)
{
    XrEventDataSessionStateChanged*	sc;

    (void)inst;
    if (fake_nextstate >= fake_numstates)
	return fake_pollresult;
    sc = (XrEventDataSessionStateChanged*)ev;
    memset (sc, 0, sizeof(*sc));
    sc->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    sc->state = fake_states[fake_nextstate++];
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession (
    XrSession s, const XrSessionBeginInfo* info)
{
    (void)s;
    (void)info;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndSession (XrSession s)
{
    (void)s;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame (
    XrSession s, const XrFrameWaitInfo* info, XrFrameState* frame)
{
    (void)s;
    (void)info;
    frame->shouldRender = XR_TRUE;
    frame->predictedDisplayTime = 1;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame (
    XrSession s, const XrFrameBeginInfo* info)
{
    (void)s;
    (void)info;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame (
    XrSession s, const XrFrameEndInfo* info)
{
    (void)s;
    endframes++;
    lastlayers = info->layerCount;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage (
    XrSwapchain sc, const XrSwapchainImageAcquireInfo* info,
    uint32_t* index)
{
    (void)sc;
    (void)info;
    acquires++;
    *index = fake_acquireindex;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage (
    XrSwapchain sc, const XrSwapchainImageWaitInfo* info)
{
    (void)sc;
    (void)info;
    waits++;
    return fake_waitresult;
}

XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage (
    XrSwapchain sc, const XrSwapchainImageReleaseInfo* info)
{
    (void)sc;
    (void)info;
    releases++;
    return fake_releaseresult;
}


//
// rlgl, without a GL context.
//
unsigned int rlLoadFramebuffer (void)
{
    return ++loadedfbos;
}

void rlFramebufferAttach (unsigned int fbo, unsigned int tex,
			  int attach, int textype, int mip)
{
    (void)fbo;
    (void)tex;
    (void)attach;
    (void)textype;
    (void)mip;
}

bool rlFramebufferComplete (unsigned int fbo)
{
    return (int)fbo - 1 != fake_incompletefbo;
}


// raylib's GLFW, which i_xr.c only has a weak reference to.
void glfwSwapInterval (int interval)
{
    swapinterval = interval;
}

void rlUnloadFramebuffer (unsigned int fbo)
{
    (void)fbo;
    unloadedfbos++;
}


static void Draw (unsigned int fbo, int width, int height)
{
    (void)fbo;
    (void)width;
    (void)height;
    draws++;
}


static void Reset (void)
{
    XR_Shutdown ();
    fake_extensions = 0;
    fake_extgrowth = 0;
    fake_extalwaysshort = 0;
    fake_images = 3;
    fake_incompletefbo = -1;
    fake_waitresult = XR_SUCCESS;
    fake_releaseresult = XR_SUCCESS;
    fake_acquireindex = 0;
    acquires = waits = releases = draws = endframes = 0;
    lastlayers = 0;
    loadedfbos = unloadedfbos = 0;
    fake_numstates = fake_nextstate = 0;
    fake_pollresult = XR_EVENT_UNAVAILABLE;
    quit = 0;
    swapinterval = -1;
    fake_blendmode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    fake_blendmodes = 1;
    locates = 0;
    fitscreen = 0;
    blacklift = -1;
    screendistance = 2.5f;
    screenwidth = MAXWIDTH;
}


// More extensions than the old fixed 128-entry list held, with
// the one needed last.
static void TestManyExtensions (void)
{
    Reset ();
    fake_extensions = 129;
    Expect (CreateInstance (), "129 extensions: OpenGL not found");
}

// The list grows between the sizing and the filling call.
static void TestGrowingExtensions (void)
{
    Reset ();
    fake_extensions = 3;
    fake_extgrowth = 2;
    Expect (CreateInstance (), "growing extension list: OpenGL not found");
}

// A runtime that never has enough room is given up on.
static void TestShortExtensions (void)
{
    Reset ();
    fake_extensions = 129;
    fake_extalwaysshort = 1;
    Expect (!CreateInstance (), "always-short extension list accepted");
    instance = XR_NULL_HANDLE;
}

// More swapchain images than the old fixed 8-entry FBO list.
static void TestManyImages (void)
{
    Reset ();
    fake_images = 12;
    Expect (CreateSwapchain (), "12 images: swapchain not made");
    Expect (numfbos == 12, "12 images: %u FBOs", numfbos);
    XR_Shutdown ();
    Expect (unloadedfbos == 12, "12 images: %d FBOs unloaded", unloadedfbos);
}

// A failure part way through frees only the FBOs that were made.
static void TestIncompleteFbo (void)
{
    Reset ();
    fake_images = 12;
    fake_incompletefbo = 3;
    Expect (!CreateSwapchain (), "incomplete FBO accepted");
    XR_Shutdown ();
    Expect (loadedfbos == 4 && unloadedfbos == 4,
	    "incomplete FBO: %d made, %d unloaded", loadedfbos, unloadedfbos);
}

static void StartSession (void)
{
    fake_images = 3;
    Expect (CreateSwapchain (), "swapchain not made");
    session = (XrSession)(uintptr_t)1;
    running = 1;
}

// A failed wait is neither released nor shown; the image stays
// acquired and is waited for again next frame.
static void TestWaitFails (void)
{
    Reset ();
    StartSession ();
    fake_waitresult = XR_ERROR_RUNTIME_FAILURE;
    XR_Present (Draw);
    Expect (acquires == 1 && waits == 1, "wait fails: %d acquires, %d waits",
	    acquires, waits);
    Expect (releases == 0, "wait fails: image released");
    Expect (draws == 0, "wait fails: image drawn");
    Expect (endframes == 1 && lastlayers == 0,
	    "wait fails: %d frames ended, %u layers", endframes, lastlayers);

    fake_waitresult = XR_SUCCESS;
    XR_Present (Draw);
    Expect (acquires == 1, "after failed wait: acquired again");
    Expect (waits == 2 && releases == 1 && draws == 1,
	    "after failed wait: %d waits, %d releases, %d draws",
	    waits, releases, draws);
    Expect (lastlayers == 1, "after failed wait: %u layers", lastlayers);
}

// XR_TIMEOUT_EXPIRED is a success code, yet the image is not ready.
static void TestWaitTimesOut (void)
{
    Reset ();
    StartSession ();
    fake_waitresult = XR_TIMEOUT_EXPIRED;
    XR_Present (Draw);
    Expect (releases == 0 && draws == 0 && lastlayers == 0,
	    "wait times out: %d releases, %d draws, %u layers",
	    releases, draws, lastlayers);
}

// A failed release is not shown, and is retried next frame.
static void TestReleaseFails (void)
{
    Reset ();
    StartSession ();
    fake_releaseresult = XR_ERROR_RUNTIME_FAILURE;
    XR_Present (Draw);
    Expect (releases == 1 && endframes == 1 && lastlayers == 0,
	    "release fails: %d releases, %d frames, %u layers",
	    releases, endframes, lastlayers);

    fake_releaseresult = XR_SUCCESS;
    XR_Present (Draw);
    Expect (acquires == 1 && waits == 1,
	    "after failed release: %d acquires, %d waits", acquires, waits);
    Expect (releases == 2 && lastlayers == 1,
	    "after failed release: %d releases, %u layers",
	    releases, lastlayers);

    XR_Present (Draw);
    Expect (acquires == 2 && lastlayers == 1,
	    "next frame: %d acquires, %u layers", acquires, lastlayers);
}

// A lost session stops using OpenXR without releasing the image,
// and the next update drops it; the game goes on in the window.
static void TestWaitLost (void)
{
    Reset ();
    StartSession ();
    instance = (XrInstance)(uintptr_t)1;
    fake_waitresult = XR_ERROR_SESSION_LOST;
    XR_Present (Draw);
    Expect (lost && !running && !quit, "session lost: not noticed");
    Expect (releases == 0 && lastlayers == 0 && endframes == 1,
	    "session lost: %d releases, %u layers, %d frames",
	    releases, lastlayers, endframes);

    XR_Present (Draw);
    Expect (endframes == 1, "session lost: %d frames", endframes);
    Expect (XR_Update (), "session lost: game quits");
    Expect (!XR_Active () && instance == XR_NULL_HANDLE && !lost,
	    "session lost: OpenXR kept");
    Expect (XR_Update (), "after session lost: game quits");
}

// A lost instance, reported by xrPollEvent, does the same.
static void TestPollLost (void)
{
    Reset ();
    StartSession ();
    instance = (XrInstance)(uintptr_t)1;
    fake_pollresult = XR_ERROR_INSTANCE_LOST;
    Expect (XR_Update (), "instance lost: game quits");
    Expect (!XR_Active (), "instance lost: OpenXR kept");
}

// LOSS_PENDING goes on in the window; EXITING quits.
static void TestLossPending (void)
{
    Reset ();
    StartSession ();
    instance = (XrInstance)(uintptr_t)1;
    fake_states[0] = XR_SESSION_STATE_LOSS_PENDING;
    fake_numstates = 1;
    Expect (XR_Update (), "loss pending: game quits");
    Expect (!XR_Active (), "loss pending: OpenXR kept");
}

static void TestExiting (void)
{
    Reset ();
    StartSession ();
    instance = (XrInstance)(uintptr_t)1;
    fake_states[0] = XR_SESSION_STATE_EXITING;
    fake_numstates = 1;
    Expect (!XR_Update (), "exiting: game goes on");
}

// The window's vsync is off only while the session runs.
static void TestVsync (void)
{
    Reset ();
    StartSession ();
    instance = (XrInstance)(uintptr_t)1;
    running = 0;
    fake_states[0] = XR_SESSION_STATE_READY;
    fake_numstates = 1;
    XR_Update ();
    Expect (running && swapinterval == 0,
	    "session running: swap interval %d", swapinterval);
    fake_states[1] = XR_SESSION_STATE_STOPPING;
    fake_numstates = 2;
    XR_Update ();
    Expect (!running && swapinterval == 1,
	    "session stopped: swap interval %d", swapinterval);

    fake_states[2] = XR_SESSION_STATE_READY;
    fake_numstates = 3;
    XR_Update ();
    swapinterval = -1;
    fake_waitresult = XR_ERROR_SESSION_LOST;
    XR_Present (Draw);
    XR_Update ();
    Expect (!XR_Active () && swapinterval == 1,
	    "session lost: swap interval %d", swapinterval);
}

// PRIME offload leaves alone a vendor the user chose, and is off
// with DOOM_XR_PRIME=0.
static void TestPrime (void)
{
    setenv ("__GLX_VENDOR_LIBRARY_NAME", "mesa", 1);
    unsetenv ("__NV_PRIME_RENDER_OFFLOAD");
    XR_PrepareGL ();
    Expect (!strcmp (getenv ("__GLX_VENDOR_LIBRARY_NAME"), "mesa")
	    && !getenv ("__NV_PRIME_RENDER_OFFLOAD"),
	    "PRIME: user's GLX vendor replaced");

    unsetenv ("__GLX_VENDOR_LIBRARY_NAME");
    setenv ("DOOM_XR_PRIME", "0", 1);
    XR_PrepareGL ();
    Expect (!getenv ("__GLX_VENDOR_LIBRARY_NAME")
	    && !getenv ("__NV_PRIME_RENDER_OFFLOAD"),
	    "PRIME: on with DOOM_XR_PRIME=0");
    unsetenv ("DOOM_XR_PRIME");
}

// An index past the swapchain is released unshown, not drawn.
static void TestBadIndex (void)
{
    Reset ();
    StartSession ();
    fake_acquireindex = 7;
    XR_Present (Draw);
    Expect (draws == 0 && releases == 1 && lastlayers == 0,
	    "bad index: %d draws, %d releases, %u layers",
	    draws, releases, lastlayers);
    Expect (imagestate == IMAGE_FREE, "bad index: image kept");
}


// Black is lifted by default only on see-through glasses.
static void TestBlackLift (void)
{
    Reset ();
    StartSession ();
    Expect (blacklift == 0, "opaque: black lifted to %f", blacklift);

    Reset ();
    fake_blendmode = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
    StartSession ();
    Expect (blacklift == ADDITIVELIFT, "additive: black lifted to %f",
	    blacklift);

    // Listed after opaque, as the Aura does.
    Reset ();
    fake_blendmodes = 2;
    fake_blendmode = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
    StartSession ();
    Expect (blendmode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
	    "opaque, additive: not using opaque");
    Expect (blacklift == ADDITIVELIFT, "opaque, additive: black lifted"
	    " to %f", blacklift);

    Reset ();
    fake_blendmode = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
    blacklift = 0;
    StartSession ();
    Expect (blacklift == 0, "-xrlift 0: black lifted to %f", blacklift);
}

// Without -xrwidth the screen shrinks to narrow glasses once, and
// a wide headset keeps the old 3.2m.
static void TestFitScreen (void)
{
    float	want;

    Reset ();
    StartSession ();
    fitscreen = 1;
    fake_fov.angleLeft = -0.5f;	// 57 x 33 degrees, like the Aura
    fake_fov.angleRight = 0.5f;
    fake_fov.angleUp = 0.29f;
    fake_fov.angleDown = -0.29f;
    XR_Present (Draw);
    XR_Present (Draw);
    want = 2 * 2.5f * tanf (0.29f * FITFOV) * 4.0f / 3.0f;
    Expect (locates == 1, "fit: located views %d times", locates);
    Expect (fabsf (screenwidth - want) < 0.01f,
	    "fit: %.2fm wide, not %.2fm", screenwidth, want);

    Reset ();
    StartSession ();
    fitscreen = 1;
    fake_fov.angleLeft = -0.9f;
    fake_fov.angleRight = 0.9f;
    fake_fov.angleUp = 0.9f;
    fake_fov.angleDown = -0.9f;
    XR_Present (Draw);
    Expect (screenwidth == MAXWIDTH, "wide headset: %.2fm wide",
	    screenwidth);
}


int main (void)
{
    TestManyExtensions ();
    TestGrowingExtensions ();
    TestShortExtensions ();
    TestManyImages ();
    TestIncompleteFbo ();
    TestWaitFails ();
    TestWaitTimesOut ();
    TestReleaseFails ();
    TestWaitLost ();
    TestPollLost ();
    TestLossPending ();
    TestExiting ();
    TestVsync ();
    TestPrime ();
    TestBadIndex ();
    TestBlackLift ();
    TestFitScreen ();
    Reset ();

    if (failures)
    {
	fprintf (stderr, "%d failure(s)\n", failures);
	return 1;
    }
    printf ("xr_test: all passed\n");
    return 0;
}
