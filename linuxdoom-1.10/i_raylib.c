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
//	raylib window, framebuffer, input and audio stream.
//	See i_raylib.h for why this lives in its own file.
//
//-----------------------------------------------------------------------------

#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "i_raylib.h"
#if defined(DOOM_XR) || defined(__ANDROID__) || defined(PLATFORM_PLAYSTATION2)
#include "i_xr.h"
#endif

#ifdef PLATFORM_PLAYSTATION2
#include <malloc.h>
#include <kernel.h>
#include <GL/gl.h>
#include "i_ps2.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __ANDROID__
#include "i_android.h"
#endif


#define WINDOWTITLE	"DOOM"

// Filled in at the bottom of the file, after the DOOM key macros
// are included (they would otherwise shadow raylib's KEY_* enums).
static int TranslateSpecialKey (int index);

static void WSL_Init (void);
static void WSL_Shutdown (void);
static void ToggleFullscreenWindow (void);
static void WM_SettleWindowed (void);
#ifdef __ANDROID__
static void DrawTouchControls (void);
#endif

// raylib's INFO chatter would drown DOOM's startup log.
static void QuietRaylib (void)
{
    SetTraceLogLevel (LOG_WARNING);
}


//
// VIDEO
//

static Texture2D	screentex;
static int		screenwidth;
static int		screenheight;

// The frame blown up by a whole number with nearest filtering, then
// smoothly scaled to the window. Straight nearest scaling to 4:3
// gives rows of uneven height (200 lines onto 720 is 3.6 each).
static RenderTexture2D	prescaled;
static int		prescale;

#define MAXPRESCALE	8


#ifdef PLATFORM_PLAYSTATION2
//
// ps2gl (OpenGL 1.1 on the GS) has no framebuffer objects, so no
// LoadRenderTexture, and no glTexSubImage2D, so UpdateTexture does
// nothing. It keeps a texture's image in main RAM and sends it to
// the GS when drawn, so each frame is handed to it as a new image
// with glTexImage2D instead. Two images, as the GS may still be
// reading the last frame while the game draws the next.
//
// The GS samples a texture as 2^n texels square whatever its size,
// so the frame sits in the corner of a 512x256 image.
//
// The frame fills the whole 640x448 picture: a TV shows that at
// 4:3, as a VGA monitor showed DOOM's 320x200.
//
#define PS2TEXW		512
#define PS2TEXH		256

static unsigned char*	ps2images[2];
static int		ps2image;

static void PS2_InitVideo (void)
{
    Image	blank;
    int		i;

    for (i = 0; i < 2; i++)
    {
	ps2images[i] = memalign (64, PS2TEXW*PS2TEXH*4);
	if (!ps2images[i])
	{
	    fprintf (stderr, "RL_InitVideo: no memory for the frame\n");
	    exit (1);
	}
	memset (ps2images[i], 0, PS2TEXW*PS2TEXH*4);
    }

    // Only for a texture name: ps2gl gets each frame's image in
    // PS2_UploadFrame. Sized as that image, which DrawTexturePro
    // divides by for texture coordinates.
    blank = GenImageColor (8, 8, BLACK);
    screentex = LoadTextureFromImage (blank);
    UnloadImage (blank);
    screentex.width = PS2TEXW;
    screentex.height = PS2TEXH;
}

static void PS2_UploadFrame (const unsigned char* rgba)
{
    unsigned char*	image = ps2images[ps2image];
    int			row = screenwidth*4;
    int			y;

    // One more column and row copied from the edge, so smooth
    // scaling does not blend the last pixels with the black beyond.
    for (y = 0; y < screenheight; y++)
    {
	unsigned char*	dst = image + y*PS2TEXW*4;

	memcpy (dst, rgba + y*row, row);
	memcpy (dst + row, dst + row - 4, 4);
    }
    memcpy (image + screenheight*PS2TEXW*4,
	    image + (screenheight-1)*PS2TEXW*4, row + 4);

    // The GS reads it by DMA, from memory, not the cache.
    FlushCache (0);

    glBindTexture (GL_TEXTURE_2D, screentex.id);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, PS2TEXW, PS2TEXH, 0,
		  GL_RGBA, GL_UNSIGNED_BYTE, image);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindTexture (GL_TEXTURE_2D, 0);

    ps2image ^= 1;
}

static void PS2_Present (const unsigned char* rgba)
{
    Rectangle	src;
    Rectangle	dst;

    PS2_UploadFrame (rgba);

    src = (Rectangle) { 0, 0, (float)screenwidth, (float)screenheight };
    dst = (Rectangle) { 0, 0, (float)GetScreenWidth (), (float)GetScreenHeight () };

    BeginDrawing ();
    ClearBackground (BLACK);
    // Opaque: the GS takes 0x80 as full alpha, DOOM's 0xff would
    // blend oddly.
    glDisable (GL_BLEND);
    DrawTexturePro (screentex, src, dst, (Vector2) { 0, 0 }, 0.0f, WHITE);
    EndDrawing ();
}
#endif


void RL_InitVideo (int width, int height, int scale, int fullscreen)
{
    screenwidth = width;
    screenheight = height;

    QuietRaylib ();
    SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);

    // DOOM's 320x200 was shown on 4:3 monitors with tall pixels.
#if defined(PLATFORM_PLAYSTATION2)
    // Always NTSC 640x448, whatever is asked for.
    InitWindow (640, 448, WINDOWTITLE);
    SetExitKey (KEY_NULL);
    PS2_InitVideo ();
    return;
#elif defined(__ANDROID__)
    // The whole display (0x0), so RL_Present letterboxes to 4:3
    // itself and the touch controls can use the side bars; raylib
    // would letterbox a 4:3 window without them.
    InitWindow (0, 0, WINDOWTITLE);
#else
    InitWindow (width*scale, (width*3/4)*scale, WINDOWTITLE);
#endif
    SetWindowMinSize (width, width*3/4);

    // ESC is a game key, not a quit key.
    SetExitKey (KEY_NULL);

    if (fullscreen)
	ToggleFullscreenWindow ();

    Image blank = GenImageColor (width, height, BLACK);
    screentex = LoadTextureFromImage (blank);
    UnloadImage (blank);
    SetTextureFilter (screentex, TEXTURE_FILTER_POINT);

    WSL_Init ();
}


void RL_ShutdownVideo (void)
{
    if (!IsWindowReady ())
	return;

    WSL_Shutdown ();
#ifdef DOOM_XR
    // While raylib's GL context, which the session uses, is alive.
    XR_Shutdown ();
#endif
    if (prescale)
	UnloadRenderTexture (prescaled);
    UnloadTexture (screentex);
    CloseWindow ();
}


#ifdef DOOM_XR
//
// DrawXR
// Fills the headset's virtual screen. 320x200 onto its 1600x1200
// is a whole 5x6, so nearest filtering keeps every pixel even.
// Drawn the way raylib draws render textures, which is the right
// way up for OpenXR's bottom-left GL image origin.
//
static void DrawXR (unsigned int fbo, int width, int height)
{
    RenderTexture2D	target = { 0 };
    Rectangle		src;
    Rectangle		dst;
    float		lift = XR_BlackLift ();
    unsigned char	scale = (unsigned char)(255.0f * (1.0f - lift) + 0.5f);
    unsigned char	bias = (unsigned char)(255.0f * lift + 0.5f);

    target.id = fbo;
    target.texture.width = width;
    target.texture.height = height;

    src = (Rectangle) { 0, 0, (float)screenwidth, (float)screenheight };
    dst = (Rectangle) { 0, 0, (float)width, (float)height };

    BeginTextureMode (target);
    ClearBackground (BLACK);
    // lift + (1 - lift) * color: black becomes a dim gray that
    // see-through glasses still show, white stays white.
    DrawTexturePro (screentex, src, dst, (Vector2) { 0, 0 }, 0.0f,
		    (Color) { scale, scale, scale, 255 });
    if (bias)
    {
	BeginBlendMode (BLEND_ADDITIVE);
	DrawRectangle (0, 0, width, height, (Color) { bias, bias, bias, 255 });
	EndBlendMode ();
    }
    EndTextureMode ();
}
#endif


void RL_Present (const unsigned char* rgba)
{
    float	winw;
    float	winh;
    float	w;
    float	h;
    int		n;
    Rectangle	src;
    Rectangle	big;
    Rectangle	dst;

#ifdef PLATFORM_PLAYSTATION2
    PS2_Present (rgba);
    return;
#endif

    UpdateTexture (screentex, rgba);

#ifdef DOOM_XR
    // The headset first; the window keeps a mirror of the game,
    // and the keyboard and mouse.
    if (XR_Active ())
	XR_Present (DrawXR);
#endif

    // Letterbox to 4:3.
    winw = (float)GetScreenWidth ();
    winh = (float)GetScreenHeight ();
    w = winw;
    h = w * 3.0f / 4.0f;
    if (h > winh)
    {
	h = winh;
	w = h * 4.0f / 3.0f;
    }

    // Smallest whole factor at least as big as the output, so the
    // smooth pass only ever shrinks, which keeps edges sharp.
    n = (int)ceilf (h / screenheight);
    if (n < (int)ceilf (w / screenwidth))
	n = (int)ceilf (w / screenwidth);
    if (n < 1)
	n = 1;
    if (n > MAXPRESCALE)
	n = MAXPRESCALE;

    if (n != prescale)
    {
	if (prescale)
	    UnloadRenderTexture (prescaled);
	prescaled = LoadRenderTexture (screenwidth*n, screenheight*n);
	SetTextureFilter (prescaled.texture, TEXTURE_FILTER_BILINEAR);
	prescale = n;
    }

    src = (Rectangle) { 0, 0, (float)screenwidth, (float)screenheight };
    big = (Rectangle) { 0, 0, (float)(screenwidth*n), (float)(screenheight*n) };

    BeginTextureMode (prescaled);
    DrawTexturePro (screentex, src, big, (Vector2) { 0, 0 }, 0.0f, WHITE);
    EndTextureMode ();

    // Render textures are stored upside down.
    big.height = -big.height;
    dst = (Rectangle) { (winw - w) / 2, (winh - h) / 2, w, h };

    BeginDrawing ();
    ClearBackground (BLACK);
    DrawTexturePro (prescaled.texture, big, dst, (Vector2) { 0, 0 }, 0.0f, WHITE);
#ifdef __ANDROID__
    DrawTouchControls ();
#endif
    EndDrawing ();
}


#ifdef __APPLE__
// raylib only reports a close request until the next poll, and
// EndDrawing polls before RL_PumpEvents polls again, so a close
// request (Cmd+Q, the close button) that arrives during EndDrawing
// is lost. RL_PumpEvents latches it here first.
static int		quitrequested;
#endif

int RL_QuitRequested (void)
{
#ifdef __EMSCRIPTEN__
    // A page has no close request, and raylib's WindowShouldClose
    // sleeps there, which only works in an ASYNCIFY build.
    return 0;
#endif
    // WindowShouldClose is true when there is no window, and
    // netgames poll input while arbitrating, before one opens.
#ifdef __APPLE__
    if (quitrequested)
	return 1;
#endif
    return IsWindowReady () && WindowShouldClose ();
}



//
// INPUT
//

#define MAXEVENTS	64

static rl_event_t	events[MAXEVENTS];
static int		eventhead;
static int		eventtail;

// Last key state we reported to DOOM, indexed by raylib key code.
static unsigned char	keystate[512];

// Keys whose press we kept from DOOM (Alt+Enter), so their
// release is kept from it too.
static unsigned char	swallowed[512];

static int		mousegrabbed;
static int		mouselocked;	// grabbed, and the pointer really is
static int		mousebuttons;
static Vector2		lastmouse;

static int		wslmouse;

static void WSL_Grab (int grab);
static void WSL_FilterMotion (Vector2 pos, int* dx, int* dy);

// raylib keys without a plain ASCII equivalent,
// in the same order as TranslateSpecialKey.
static const int specialkeys[] =
{
    KEY_RIGHT, KEY_LEFT, KEY_UP, KEY_DOWN,
    KEY_ESCAPE, KEY_ENTER, KEY_KP_ENTER, KEY_TAB,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_BACKSPACE, KEY_DELETE, KEY_PAUSE,
    KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT,
    KEY_LEFT_CONTROL, KEY_RIGHT_CONTROL,
    KEY_LEFT_ALT, KEY_RIGHT_ALT,
    KEY_KP_ADD, KEY_KP_SUBTRACT, KEY_KP_EQUAL,
};

#define NUMSPECIALKEYS	(int)(sizeof(specialkeys)/sizeof(specialkeys[0]))


static void PostEvent (rl_evtype_t type, int data1, int data2, int data3)
{
    int next = (eventhead + 1) % MAXEVENTS;

    if (next == eventtail)
	return;		// full, drop it

    events[eventhead].type = type;
    events[eventhead].data1 = data1;
    events[eventhead].data2 = data2;
    events[eventhead].data3 = data3;
    eventhead = next;
}


//
// TranslateKey
// Maps a raylib key code to a DOOM key code, 0 if unused.
//
static int TranslateKey (int key)
{
    int		i;

#ifdef __ANDROID__
    // The system Back button (and gesture).
    if (key == KEY_BACK)
	key = KEY_ESCAPE;
#endif

    // Letters, digits, space and punctuation are
    // ASCII in raylib. DOOM wants them lowercase.
    if (key >= KEY_SPACE && key <= KEY_GRAVE)
    {
	if (key >= KEY_A && key <= KEY_Z)
	    return key - KEY_A + 'a';
	return key;
    }

    for (i = 0; i < NUMSPECIALKEYS; i++)
	if (specialkeys[i] == key)
	    return TranslateSpecialKey (i);

    return 0;
}


static void SetKeyState (int key, int down)
{
    int		doomkey;

    if (keystate[key] == down)
	return;

    keystate[key] = down;

    if (!down && swallowed[key])
    {
	swallowed[key] = 0;
	return;
    }

    doomkey = TranslateKey (key);
    if (doomkey)
	PostEvent (down ? rl_keydown : rl_keyup, doomkey, 0, 0);
}


//
// DrainPressedKeys
// A key pressed and released between two polls never shows up
// in IsKeyDown, but it is in raylib's pressed queue. Report
// those as a down immediately followed by an up.
//
static void DrainPressedKeys (void)
{
    int		key;

    while ((key = GetKeyPressed ()) != 0)
    {
	if (key <= 0 || key >= (int)sizeof(keystate))
	    continue;

	if ((key == KEY_ENTER || key == KEY_KP_ENTER)
	    && (IsKeyDown (KEY_LEFT_ALT) || IsKeyDown (KEY_RIGHT_ALT)))
	{
	    // Alt+Enter toggles fullscreen, and is not passed on.
	    ToggleFullscreenWindow ();
	    keystate[key] = 1;
	    swallowed[key] = 1;
	    continue;
	}

	SetKeyState (key, 1);
	if (!IsKeyDown (key))
	    SetKeyState (key, 0);
    }
}


void RL_PumpEvents (void)
{
    int		key;
    int		buttons;
    int		dx;
    int		dy;
    Vector2	pos;

    if (!IsWindowReady ())
	return;

#ifdef __APPLE__
    if (WindowShouldClose ())
	quitrequested = 1;
#endif

    WM_SettleWindowed ();

    // EndDrawing polls too and resets the pressed queue,
    // so grab what it collected before polling again.
    DrainPressedKeys ();
    PollInputEvents ();
    DrainPressedKeys ();

    for (key = 1; key < (int)sizeof(keystate); key++)
	SetKeyState (key, IsKeyDown (key));

    // Mouse. Track position ourselves: GetMouseDelta only covers
    // the last poll, and EndDrawing polls behind our back.
    buttons = 0;
    if (IsMouseButtonDown (MOUSE_BUTTON_LEFT))
	buttons |= 1;
    if (IsMouseButtonDown (MOUSE_BUTTON_RIGHT))
	buttons |= 2;
    if (IsMouseButtonDown (MOUSE_BUTTON_MIDDLE))
	buttons |= 4;

    pos = GetMousePosition ();

#ifdef __EMSCRIPTEN__
    // The browser lets go of the pointer on Esc, and locks it again
    // only on a click in the page; until then the mouse is not ours.
    if (mousegrabbed && IsCursorHidden ())
    {
	if (!mouselocked)
	    lastmouse = pos;
	mouselocked = 1;
    }
    else
	mouselocked = 0;
#else
    mouselocked = mousegrabbed;
#endif

    dx = dy = 0;
    if (mouselocked)
    {
	dx = (int)(pos.x - lastmouse.x);
	dy = (int)(pos.y - lastmouse.y);
	if (wslmouse)
	    WSL_FilterMotion (pos, &dx, &dy);
    }
    lastmouse = pos;

    if (!mouselocked)
	buttons = 0;

    if (dx || dy || buttons != mousebuttons)
    {
	mousebuttons = buttons;
	// Same scaling as the original X11 code.
	PostEvent (rl_mouse, buttons, dx << 2, -dy << 2);
    }
}


int RL_GetEvent (rl_event_t* ev)
{
    if (eventtail == eventhead)
	return 0;

    *ev = events[eventtail];
    eventtail = (eventtail + 1) % MAXEVENTS;
    return 1;
}


void RL_SetMouseGrab (int grab)
{
#ifdef PLATFORM_PLAYSTATION2
    // No mouse.
    return;
#endif
    if (grab && !IsWindowFocused ())
	grab = 0;
#ifdef __ANDROID__
    // raylib reports the first finger as the mouse; the touch
    // controls have it instead.
    grab = 0;
#endif

    if (grab == mousegrabbed)
	return;

    mousegrabbed = grab;
#ifdef __EMSCRIPTEN__
    // The page asks for pointer lock on the next click when the
    // game wants it (raylib's own request needs a user gesture).
    EM_ASM ({ Module.doomWantsPointer = $0; }, grab);
#endif
    if (wslmouse)
	WSL_Grab (grab);
    else if (grab)
	DisableCursor ();
    else
	EnableCursor ();

    // Moving the cursor is not player movement.
    lastmouse = GetMousePosition ();
}



#ifdef PLATFORM_PLAYSTATION2
//
// DUALSHOCK 2
//
// Reported as the controller buttons of i_xr.h, like Android's
// gamepad, which i_video.c turns into keys: the player's key
// bindings in the game, the menu keys in menus (X is Enter there),
// y and n in a yes/no prompt.
//
//   left stick		move and strafe
//   right stick	turn
//   d-pad		move and turn
//   Square, R2		fire
//   X (Cross)		use; Enter in menus
//   Circle		strafe while held; back in menus
//   Triangle, L2	run while held
//   L1, R1		previous, next weapon
//   Start		menu (Esc)
//   Select		automap (Tab)
//
// raylib4PlayStation2 reports the buttons but not the sticks, which
// come from libpad (I_PS2_Sticks).
//

// Stick deflection that counts as a key press.
#define PADSTICK	0.5f

static int PadDown (int button)
{
    return IsGamepadButtonDown (0, button);
}

unsigned RL_PadButtons (void)
{
    unsigned	held = 0;
    float	lx;
    float	ly;
    float	rx;
    float	ry;

    if (!IsGamepadAvailable (0))
	return 0;

    I_PS2_Sticks (&lx, &ly, &rx, &ry);

    if (ly < -PADSTICK || PadDown (GAMEPAD_BUTTON_LEFT_FACE_UP))
	held |= XR_FORWARD;
    if (ly > PADSTICK || PadDown (GAMEPAD_BUTTON_LEFT_FACE_DOWN))
	held |= XR_BACK;
    if (lx < -PADSTICK)
	held |= XR_STRAFELEFT;
    if (lx > PADSTICK)
	held |= XR_STRAFERIGHT;
    if (rx < -PADSTICK || PadDown (GAMEPAD_BUTTON_LEFT_FACE_LEFT))
	held |= XR_TURNLEFT;
    if (rx > PADSTICK || PadDown (GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
	held |= XR_TURNRIGHT;

    if (PadDown (GAMEPAD_BUTTON_RIGHT_FACE_LEFT)		// Square
	|| PadDown (GAMEPAD_BUTTON_RIGHT_TRIGGER_2))
	held |= XR_FIRE;
    if (PadDown (GAMEPAD_BUTTON_RIGHT_FACE_DOWN))		// Cross
	held |= XR_USE;
    if (PadDown (GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))		// Circle
	held |= XR_STRAFE;
    if (PadDown (GAMEPAD_BUTTON_RIGHT_FACE_UP)			// Triangle
	|| PadDown (GAMEPAD_BUTTON_LEFT_TRIGGER_2))
	held |= XR_RUN;
    if (PadDown (GAMEPAD_BUTTON_LEFT_TRIGGER_1))
	held |= XR_PREVWEAPON;
    if (PadDown (GAMEPAD_BUTTON_RIGHT_TRIGGER_1))
	held |= XR_NEXTWEAPON;
    if (PadDown (GAMEPAD_BUTTON_MIDDLE_RIGHT))			// Start
	held |= XR_MENU;
    if (PadDown (GAMEPAD_BUTTON_MIDDLE_LEFT))			// Select
	held |= XR_MAP;

    return held;
}
#endif


#ifdef __ANDROID__
//
// GAMEPAD AND TOUCH CONTROLS
//
// A gamepad (Bluetooth or USB, through raylib) and buttons drawn
// over the picture, both reported as the headset controller
// buttons of i_xr.h, which i_video.c turns into keys: the player's
// key bindings in the game, the menu keys in menus, y and n in a
// yes/no prompt. The touch buttons are hidden once a gamepad has
// been used, and on devices without a touchscreen, and come back
// when the screen is touched.
//

// Stick deflection that counts as a key press.
#define PADSTICK	0.5f

typedef struct
{
    unsigned	button;
    const char*	label;
    // In units of a seventh of the screen height (small enough to
    // stay in the side bars of a 20:9 phone), from the left
    // (x >= 0) or the right (x < 0) edge, and from the top (y >= 0)
    // or the bottom (y < 0) edge.
    float	x;
    float	y;
    float	w;
    float	h;
} touchbutton_t;

static const touchbutton_t touchbuttons[] =
{
    { XR_FORWARD,	"^",	1.1f, -2.9f, 1.0f, 1.0f },
    { XR_BACK,		"v",	1.1f, -1.1f, 1.0f, 1.0f },
    { XR_TURNLEFT,	"<",	0.2f, -2.0f, 1.0f, 1.0f },
    { XR_TURNRIGHT,	">",	2.0f, -2.0f, 1.0f, 1.0f },
    { XR_FIRE,		"FIRE",	-1.6f, -2.2f, 1.4f, 1.4f },
    { XR_USE,		"USE",	-3.1f, -1.3f, 1.2f, 1.0f },
    { XR_RUN,		"RUN",	-3.1f, -2.5f, 1.2f, 1.0f },
    { XR_MENU,		"MENU",	0.2f, 0.2f, 1.4f, 0.7f },
    { XR_MAP,		"MAP",	-1.6f, 0.2f, 1.4f, 0.7f },
};

#define NUMTOUCHBUTTONS	(int)(sizeof(touchbuttons)/sizeof(touchbuttons[0]))

static int		touchshown = -1;	// -1: not decided yet
static unsigned		touchheld;


static Rectangle TouchRect (const touchbutton_t* b)
{
    float	u = GetScreenHeight () / 7.0f;
    float	x = b->x >= 0 ? b->x*u : GetScreenWidth () + b->x*u;
    float	y = b->y >= 0 ? b->y*u : GetScreenHeight () + b->y*u;

    return (Rectangle) { x, y, b->w*u, b->h*u };
}


static unsigned TouchButtons (void)
{
    unsigned	held = 0;
    int		count;
    int		i;
    int		j;

    if (touchshown < 0)
	touchshown = I_AndroidHasTouchscreen ();
    count = GetTouchPointCount ();
    if (count > 0)
	touchshown = 1;
    if (!touchshown)
	return 0;

    for (i = 0; i < count; i++)
    {
	Vector2	pos = GetTouchPosition (i);

	for (j = 0; j < NUMTOUCHBUTTONS; j++)
	    if (CheckCollisionPointRec (pos, TouchRect (&touchbuttons[j])))
		held |= touchbuttons[j].button;
    }
    return held;
}


static int PadDown (int button)
{
    return IsGamepadButtonDown (0, button);
}


static unsigned GamepadButtons (void)
{
    unsigned	held = 0;
    float	lx;
    float	ly;
    float	rx;

    if (!IsGamepadAvailable (0))
	return 0;

    lx = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_X);
    ly = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_Y);
    rx = GetGamepadAxisMovement (0, GAMEPAD_AXIS_RIGHT_X);

    // Left stick moves and strafes, the right one and the d-pad's
    // sides turn; in menus all of them move the cursor.
    if (ly < -PADSTICK || PadDown (GAMEPAD_BUTTON_LEFT_FACE_UP))
	held |= XR_FORWARD;
    if (ly > PADSTICK || PadDown (GAMEPAD_BUTTON_LEFT_FACE_DOWN))
	held |= XR_BACK;
    if (lx < -PADSTICK)
	held |= XR_STRAFELEFT;
    if (lx > PADSTICK)
	held |= XR_STRAFERIGHT;
    if (rx < -PADSTICK || PadDown (GAMEPAD_BUTTON_LEFT_FACE_LEFT))
	held |= XR_TURNLEFT;
    if (rx > PADSTICK || PadDown (GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
	held |= XR_TURNRIGHT;

    // Triggers come as buttons on some pads, as axes on others.
    if (PadDown (GAMEPAD_BUTTON_RIGHT_TRIGGER_2)
	|| PadDown (GAMEPAD_BUTTON_RIGHT_TRIGGER_1)
	|| PadDown (GAMEPAD_BUTTON_RIGHT_FACE_LEFT)
	|| GetGamepadAxisMovement (0, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.0f)
	held |= XR_FIRE;
    if (PadDown (GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
	held |= XR_USE;
    if (PadDown (GAMEPAD_BUTTON_LEFT_TRIGGER_2)
	|| PadDown (GAMEPAD_BUTTON_LEFT_TRIGGER_1)
	|| GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_TRIGGER) > 0.0f)
	held |= XR_RUN;
    if (PadDown (GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)
	|| PadDown (GAMEPAD_BUTTON_RIGHT_FACE_UP)
	|| PadDown (GAMEPAD_BUTTON_MIDDLE_LEFT))
	held |= XR_MAP;
    if (PadDown (GAMEPAD_BUTTON_MIDDLE_RIGHT))
	held |= XR_MENU;

    if (held)
	touchshown = 0;
    return held;
}


unsigned RL_PadButtons (void)
{
    touchheld = TouchButtons ();
    return GamepadButtons () | touchheld;
}


static void DrawTouchControls (void)
{
    float	u = GetScreenHeight () / 7.0f;
    int		size = (int)(u * 0.3f);
    int		i;

    if (touchshown <= 0)
	return;

    for (i = 0; i < NUMTOUCHBUTTONS; i++)
    {
	const touchbutton_t*	b = &touchbuttons[i];
	Rectangle		r = TouchRect (b);
	int			held = (touchheld & b->button) != 0;
	int			w = MeasureText (b->label, size);

	DrawRectangleRounded (r, 0.3f, 8, Fade (WHITE, held ? 0.35f : 0.12f));
	DrawRectangleRoundedLinesEx (r, 0.3f, 8, 2.0f, Fade (WHITE, 0.4f));
	DrawText (b->label, (int)(r.x + (r.width - w) / 2),
		  (int)(r.y + (r.height - size) / 2), size, Fade (WHITE, 0.7f));
    }
}
#endif


//
// WSL MOUSE
//
// WSLg runs X clients on Xwayland inside Weston, which reaches
// Windows over RDP and only ever gets absolute pointer positions.
// GLFW's disabled cursor breaks there in three ways:
//  - its raw motion events carry the absolute position, not a
//    delta, so every event turns the player by thousands of pixels;
//  - warping the pointer back to the centre never moves the real
//    Windows pointer, so each delta measures the distance from
//    the centre instead of the movement since the last event;
//  - an invisible cursor is not passed on, so the Windows arrow
//    stays on screen and can wander off the window.
//
// So under WSL the cursor stays in normal mode, motion comes from
// absolute positions, the cursor is a single almost transparent
// pixel (a fully transparent one is dropped), and a PowerShell
// helper on the Windows side keeps the real pointer inside the
// window and puts it back in the middle when asked. The jump back
// to the middle is not player movement and is filtered out.
//
// A native Windows build has none of this to work around.
//

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(PLATFORM_PLAYSTATION2)

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWcursor GLFWcursor;

typedef struct
{
    int			width;
    int			height;
    unsigned char*	pixels;
} glfwimage_t;

// raylib's GLFW, weak so a raylib that hides it still links.
extern GLFWcursor* glfwCreateCursor (const glfwimage_t* image,
				     int xhot, int yhot)
    __attribute__((weak));
extern void glfwSetCursor (GLFWwindow* window, GLFWcursor* cursor)
    __attribute__((weak));

// Clips the pointer to the middle of the foreground window when it
// is ours, and lets go when it no longer is, even if we hang.
// WSLg titles windows "DOOM (<distro>)".
static const char wslhelperscript[] =
    "Add-Type -TypeDefinition '"
    "using System; using System.Runtime.InteropServices;"
    " using System.Text; using System.Threading;"
    " public static class DoomMouse {"
    " [StructLayout(LayoutKind.Sequential)]"
    " public struct RECT { public int L, T, R, B; }"
    " [DllImport(\"user32.dll\")] static extern IntPtr GetForegroundWindow();"
    " [DllImport(\"user32.dll\")]"
    " static extern bool GetWindowRect(IntPtr h, out RECT r);"
    " [DllImport(\"user32.dll\", CharSet = CharSet.Unicode)]"
    " static extern int GetWindowText(IntPtr h, StringBuilder s, int n);"
    " [DllImport(\"user32.dll\", CharSet = CharSet.Unicode)]"
    " static extern int GetClassName(IntPtr h, StringBuilder s, int n);"
    " [DllImport(\"user32.dll\")] static extern bool ClipCursor(ref RECT r);"
    " [DllImport(\"user32.dll\", EntryPoint = \"ClipCursor\")]"
    " static extern bool Unclip(IntPtr r);"
    " [DllImport(\"user32.dll\")] static extern bool SetCursorPos(int x, int y);"
    " [DllImport(\"user32.dll\")] static extern bool SetProcessDPIAware();"
    " static readonly object mutex = new object();"
    " static IntPtr win; static string title; static Timer watchdog;"
    " public static void Init(string t) {"
    "  title = t; SetProcessDPIAware();"
    "  AppDomain.CurrentDomain.ProcessExit += delegate { Release(); };"
    "  watchdog = new Timer(delegate { lock (mutex) {"
    "   if (win != IntPtr.Zero && GetForegroundWindow() != win) Release();"
    "  } }, null, 100, 100); }"
    " public static void Grab() { lock (mutex) {"
    "  IntPtr h = GetForegroundWindow();"
    "  StringBuilder s = new StringBuilder(256);"
    "  StringBuilder k = new StringBuilder(256);"
    "  GetWindowText(h, s, 256); GetClassName(h, k, 256);"
    "  string n = s.ToString();"
    "  if (k.ToString() != \"RAIL_WINDOW\""
    "   || (n != title && !n.StartsWith(title + \" (\"))) return;"
    "  win = h; Center(); } }"
    " public static void Center() { lock (mutex) {"
    "  RECT r;"
    "  if (win == IntPtr.Zero || GetForegroundWindow() != win"
    "   || !GetWindowRect(win, out r)) return;"
    "  int x = (r.L + r.R) / 2; int y = (r.T + r.B) / 2;"
    "  int d = Math.Max(Math.Min(r.R - r.L, r.B - r.T) / 2 - 96, 16);"
    "  RECT c; c.L = x - d; c.T = y - d; c.R = x + d; c.B = y + d;"
    "  ClipCursor(ref c); SetCursorPos(x, y); } }"
    " public static void Release() { lock (mutex) {"
    "  if (win == IntPtr.Zero) return;"
    "  win = IntPtr.Zero; Unclip(IntPtr.Zero); } } }'\n"
    "[DoomMouse]::Init('" WINDOWTITLE "')\n";

static FILE*		wslhelper;
static GLFWcursor*	wslcursor;
static int		wslwaiting;	// polls since asking for a re-centre
static int		wslanchored;
static Vector2		wslanchor;	// where a re-centre lands
static Vector2		wslwinpos;
static int		wslwinw;
static int		wslwinh;


static int IsWSL (void)
{
    const char*	env;
    FILE*	f;
    char	buf[256];
    int		i;
    int		found = 0;

    // DOOM_WSL_MOUSE=0/1 overrides the detection.
    env = getenv ("DOOM_WSL_MOUSE");
    if (env && *env)
	return atoi (env) != 0;

    if (getenv ("WSL_DISTRO_NAME"))
	return 1;

    f = fopen ("/proc/sys/kernel/osrelease", "r");
    if (!f)
	return 0;
    if (fgets (buf, sizeof(buf), f))
    {
	for (i = 0; buf[i]; i++)
	    buf[i] = tolower ((unsigned char)buf[i]);
	found = strstr (buf, "microsoft") != NULL;
    }
    fclose (f);
    return found;
}


static void WSL_Command (const char* cmd)
{
    if (!wslhelper)
	return;

    fprintf (wslhelper, "[DoomMouse]::%s()\n", cmd);
    fflush (wslhelper);
}


static void WSL_Init (void)
{
    static unsigned char	pixel[4] = { 0, 0, 0, 1 };
    glfwimage_t			image = { 1, 1, pixel };

    wslmouse = IsWSL ();
    if (!wslmouse)
	return;

    if (glfwCreateCursor)
	wslcursor = glfwCreateCursor (&image, 0, 0);

    // A helper that is missing or dies must not take us with it.
    signal (SIGPIPE, SIG_IGN);
    wslhelper = popen ("P=$(command -v powershell.exe)"
		       " || P=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe;"
		       " exec \"$P\" -NoLogo -NoProfile -NonInteractive -Command -"
		       " >/dev/null 2>&1", "w");
    if (wslhelper)
    {
	fputs (wslhelperscript, wslhelper);
	fflush (wslhelper);
    }
}


static void WSL_Shutdown (void)
{
    if (!wslhelper)
	return;

    WSL_Command ("Release");
    pclose (wslhelper);
    wslhelper = NULL;
}


static void WSL_Grab (int grab)
{
    if (glfwSetCursor && wslcursor)
	glfwSetCursor (GetWindowHandle (), grab ? wslcursor : NULL);

    wslwaiting = 0;
    if (!grab)
    {
	WSL_Command ("Release");
	return;
    }

    // The window may have moved since the last grab.
    wslanchored = 0;
    WSL_Command ("Grab");
    wslwaiting = 1;
}


static float Distance (Vector2 a, Vector2 b)
{
    return hypotf (a.x - b.x, a.y - b.y);
}


//
// WSL_FilterMotion
// Drops the jump when the helper re-centres the pointer, and asks
// for a re-centre once the pointer has strayed from the middle.
//
static void WSL_FilterMotion (Vector2 pos, int* dx, int* dy)
{
    Vector2	winpos = GetWindowPosition ();
    int		winw = GetScreenWidth ();
    int		winh = GetScreenHeight ();
    float	radius = (winw < winh ? winw : winh) / 16.0f;
    Vector2	target;

    if (winpos.x != wslwinpos.x || winpos.y != wslwinpos.y
	|| winw != wslwinw || winh != wslwinh)
    {
	// Moved or resized: the middle is somewhere else now.
	wslwinpos = winpos;
	wslwinw = winw;
	wslwinh = winh;
	wslanchored = 0;
    }

    // Until a landing has been seen, guess the middle of the window.
    // Window decorations put the real spot a little off from that.
    target = wslanchored ? wslanchor
	: (Vector2) { winw / 2.0f, winh / 2.0f };

    if (wslwaiting)
    {
	float before = Distance (lastmouse, target);

	if (before >= radius && Distance (pos, target) < before / 2)
	{
	    // Landed. Anything past the landing spot is real movement.
	    if (wslanchored)
	    {
		*dx = (int)(pos.x - wslanchor.x);
		*dy = (int)(pos.y - wslanchor.y);
	    }
	    else
	    {
		*dx = *dy = 0;
		wslanchor = pos;
		wslanchored = 1;
	    }
	    wslwaiting = 0;
	    return;
	}

	if (++wslwaiting > 10)
	{
	    // Never saw it land, likely because the pointer was
	    // already in the middle. Take where it is as the middle.
	    if (!wslanchored)
	    {
		wslanchor = pos;
		wslanchored = 1;
	    }
	    wslwaiting = 0;
	}
	return;
    }

    if (Distance (pos, target) > radius)
    {
	WSL_Command ("Center");
	wslwaiting = 1;
    }
}

#else

static void WSL_Init (void) {}
static void WSL_Shutdown (void) {}
static void WSL_Grab (int grab) {}
static void WSL_FilterMotion (Vector2 pos, int* dx, int* dy) {}

#endif



//
// WINDOW MANAGER FULLSCREEN
//
// Some window managers do not let raylib's borderless fullscreen
// cover the whole monitor:
//
// - Hyprland (as on Omarchy) lays out tiled windows itself and
//   ignores the move and resize, so the game stayed tiled.
// - GNOME's mutter keeps an undecorated window that is not
//   fullscreen inside the work area, so the top bar and the dock
//   stayed visible.
//
// There the window manager is asked for fullscreen instead
// (_NET_WM_STATE on X11, xdg-shell on Wayland), at the monitor's
// current mode so the resolution never changes. Everywhere else,
// including WSL and a bare X server, nothing changes.
// DOOM_WM_FULLSCREEN=0/1 overrides the detection.
//

#ifdef __linux__

typedef struct GLFWmonitor GLFWmonitor;

typedef struct
{
    int			width;
    int			height;
    int			redbits;
    int			greenbits;
    int			bluebits;
    int			refreshrate;
} glfwvidmode_t;

#define GLFW_AUTO_ICONIFY	0x00020006
#define GLFW_DONT_CARE		-1

extern GLFWmonitor** glfwGetMonitors (int* count)
    __attribute__((weak));
extern const glfwvidmode_t* glfwGetVideoMode (GLFWmonitor* monitor)
    __attribute__((weak));
extern void glfwSetWindowMonitor (GLFWwindow* window, GLFWmonitor* monitor,
				  int xpos, int ypos, int width, int height,
				  int refreshrate)
    __attribute__((weak));
extern void glfwSetWindowAttrib (GLFWwindow* window, int attrib, int value)
    __attribute__((weak));

static int		wmfullscreen;
static Rectangle	wmwindowed;
static int		wmsettle;	// polls left to check the restore
static int		wmfullwidth;
static int		wmfullheight;

#define WMSETTLEPOLLS	70	// about two seconds of tics


static int UseWMFullscreen (void)
{
    const char*	env;

    env = getenv ("DOOM_WM_FULLSCREEN");
    if (env && *env)
	return atoi (env) != 0;

    if (getenv ("HYPRLAND_INSTANCE_SIGNATURE"))
	return 1;

    // XDG_CURRENT_DESKTOP is a colon-separated list, "ubuntu:GNOME"
    // on Ubuntu. WSLg never runs GNOME, but keep WSL out regardless.
    env = getenv ("XDG_CURRENT_DESKTOP");
    if (env && strstr (env, "GNOME") && !IsWSL ())
	return 1;

    return 0;
}

static int WM_ToggleFullscreen (void)
{
    GLFWwindow*			win;
    GLFWmonitor**		monitors;
    const glfwvidmode_t*	mode;
    Vector2			pos;
    int				count;
    int				i;

    if (!UseWMFullscreen ()
	|| !glfwGetMonitors || !glfwGetVideoMode
	|| !glfwSetWindowMonitor || !glfwSetWindowAttrib)
	return 0;

    win = GetWindowHandle ();

    if (wmfullscreen)
    {
	glfwSetWindowMonitor (win, NULL,
			      (int)wmwindowed.x, (int)wmwindowed.y,
			      (int)wmwindowed.width, (int)wmwindowed.height,
			      GLFW_DONT_CARE);
	wmfullscreen = 0;
	// Hyprland places the window itself; leave it alone there.
	if (!getenv ("HYPRLAND_INSTANCE_SIGNATURE"))
	    wmsettle = WMSETTLEPOLLS;
	return 1;
    }

    monitors = glfwGetMonitors (&count);
    i = GetCurrentMonitor ();
    if (!monitors || i < 0 || i >= count)
	return 0;
    mode = glfwGetVideoMode (monitors[i]);
    if (!mode)
	return 0;

    pos = GetWindowPosition ();
    wmwindowed = (Rectangle) { pos.x, pos.y,
			       GetScreenWidth (), GetScreenHeight () };

    // Stay fullscreen when focus moves to another window or workspace.
    glfwSetWindowAttrib (win, GLFW_AUTO_ICONIFY, 0);
    glfwSetWindowMonitor (win, monitors[i], 0, 0,
			  mode->width, mode->height, mode->refreshrate);
    wmfullscreen = 1;
    wmsettle = 0;
    wmfullwidth = mode->width;
    wmfullheight = mode->height;
    return 1;
}


//
// Leaving fullscreen, mutter fits the title bar inside the size
// asked for, so the window came back a title bar shorter and lower
// than before. Once the window manager has let go of the monitor
// size, ask again for the old size and position; by then it is
// laying out a normal decorated window and takes them as asked.
//
static void WM_SettleWindowed (void)
{
    Vector2	pos;

    if (!wmsettle)
	return;

    wmsettle--;
    if (wmsettle && GetScreenWidth () == wmfullwidth
	&& GetScreenHeight () == wmfullheight)
	return;		// not restored yet

    wmsettle = 0;
    pos = GetWindowPosition ();
    if (GetScreenWidth () == (int)wmwindowed.width
	&& GetScreenHeight () == (int)wmwindowed.height
	&& (int)pos.x == (int)wmwindowed.x && (int)pos.y == (int)wmwindowed.y)
	return;

    SetWindowSize ((int)wmwindowed.width, (int)wmwindowed.height);
    SetWindowPosition ((int)wmwindowed.x, (int)wmwindowed.y);
}

#else

static int WM_ToggleFullscreen (void) { return 0; }
static void WM_SettleWindowed (void) {}

#endif


static void ToggleFullscreenWindow (void)
{
#ifdef __EMSCRIPTEN__
    // Browsers allow fullscreen only from inside an input event,
    // which the frame loop never is; the page does Alt+Enter itself.
    return;
#endif
    if (!WM_ToggleFullscreen ())
	ToggleBorderlessWindowed ();
}



//
// AUDIO
//
// Each stream (sound effects, music) has its own single-producer,
// single-consumer ring. The game mixes on the main thread and
// pushes into it; raylib's audio thread pulls from it and plays
// silence on underrun.
//

#ifdef PLATFORM_PLAYSTATION2

//
// raylib4PlayStation2 has no raudio (miniaudio has no PS2 backend),
// so there is no audio device yet: the game runs silent. Sound
// effects and music will go through audsrv.
//
int RL_InitAudio (void)
{
    return 0;
}

int RL_OpenStream (int id, int samplerate)
{
    return 0;
}

void RL_ShutdownAudio (void)
{
}

int RL_AudioQueued (int id)
{
    return 0;
}

void RL_QueueAudio (int id, const short* samples, int frames)
{
}

#else

#define RINGFRAMES	8192	// power of two

typedef struct
{
    AudioStream		stream;
    int			open;
    short		ring[RINGFRAMES*2];
    atomic_uint		read;
    atomic_uint		write;
} rlstream_t;

static int		audioready;
static rlstream_t	streams[RL_NUMSTREAMS];


static void DrainStream (rlstream_t* s, void* buffer, unsigned int frames)
{
    short*	out = (short*)buffer;
    unsigned	rd = atomic_load_explicit (&s->read, memory_order_relaxed);
    unsigned	wr = atomic_load_explicit (&s->write, memory_order_acquire);
    unsigned	avail = wr - rd;
    unsigned	i;

    for (i = 0; i < frames && i < avail; i++)
    {
	unsigned idx = (rd + i) & (RINGFRAMES-1);
	out[i*2] = s->ring[idx*2];
	out[i*2+1] = s->ring[idx*2+1];
    }

    if (i < frames)
	memset (out + i*2, 0, (frames - i) * 2 * sizeof(short));

    atomic_store_explicit (&s->read, rd + i, memory_order_release);
}

// raylib callbacks carry no user pointer, so one per stream.
static void SfxCallback (void* buffer, unsigned int frames)
{
    DrainStream (&streams[RL_SFX], buffer, frames);
}

static void MusicCallback (void* buffer, unsigned int frames)
{
    DrainStream (&streams[RL_MUSIC], buffer, frames);
}


int RL_InitAudio (void)
{
    if (audioready)
	return 1;

    QuietRaylib ();
    InitAudioDevice ();
    if (!IsAudioDeviceReady ())
	return 0;

    audioready = 1;
    return 1;
}


int RL_OpenStream (int id, int samplerate)
{
    rlstream_t*	s = &streams[id];

    if (!audioready || s->open)
	return s->open;

    s->stream = LoadAudioStream (samplerate, 16, 2);
    if (s->stream.buffer == NULL)
	return 0;

    SetAudioStreamCallback (s->stream,
			    id == RL_MUSIC ? MusicCallback : SfxCallback);
    PlayAudioStream (s->stream);
    s->open = 1;
    return 1;
}


void RL_ShutdownAudio (void)
{
    int		i;

    if (!audioready)
	return;

    for (i = 0; i < RL_NUMSTREAMS; i++)
    {
	if (streams[i].open)
	    UnloadAudioStream (streams[i].stream);
	streams[i].open = 0;
    }

    audioready = 0;
    CloseAudioDevice ();
}


int RL_AudioQueued (int id)
{
    rlstream_t*	s = &streams[id];

    return atomic_load_explicit (&s->write, memory_order_relaxed)
	- atomic_load_explicit (&s->read, memory_order_acquire);
}


void RL_QueueAudio (int id, const short* samples, int frames)
{
    rlstream_t*	s = &streams[id];
    unsigned	wr = atomic_load_explicit (&s->write, memory_order_relaxed);
    int		i;

    if (!s->open || frames > RINGFRAMES - RL_AudioQueued (id))
	return;

    for (i = 0; i < frames; i++)
    {
	unsigned idx = (wr + i) & (RINGFRAMES-1);
	s->ring[idx*2] = samples[i*2];
	s->ring[idx*2+1] = samples[i*2+1];
    }

    atomic_store_explicit (&s->write, wr + frames, memory_order_release);
}

#endif	// PLATFORM_PLAYSTATION2



//
// DOOM key codes. Nothing from raylib may be used below this point.
//
#include "doomkeys.h"

static int TranslateSpecialKey (int index)
{
    static const int doomkeys[] =
    {
	KEY_RIGHTARROW, KEY_LEFTARROW, KEY_UPARROW, KEY_DOWNARROW,
	KEY_ESCAPE, KEY_ENTER, KEY_ENTER, KEY_TAB,
	KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
	KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
	KEY_BACKSPACE, KEY_BACKSPACE, KEY_PAUSE,
	KEY_RSHIFT, KEY_RSHIFT,
	KEY_RCTRL, KEY_RCTRL,
	KEY_LALT, KEY_RALT,
	KEY_EQUALS, KEY_MINUS, KEY_EQUALS,
    };

    _Static_assert (sizeof(doomkeys)/sizeof(doomkeys[0]) == NUMSPECIALKEYS,
		    "specialkeys and doomkeys must line up");

    return doomkeys[index];
}
