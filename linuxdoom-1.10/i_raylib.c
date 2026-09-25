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

#include <stdatomic.h>
#include <string.h>

#include "raylib.h"

#include "i_raylib.h"


// Filled in at the bottom of the file, after the DOOM key macros
// are included (they would otherwise shadow raylib's KEY_* enums).
static int TranslateSpecialKey (int index);

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


void RL_InitVideo (int width, int height, int scale, int fullscreen)
{
    screenwidth = width;
    screenheight = height;

    QuietRaylib ();
    SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);

    // DOOM's 320x200 was shown on 4:3 monitors with tall pixels.
    InitWindow (width*scale, (width*3/4)*scale, "DOOM");
    SetWindowMinSize (width, width*3/4);

    // ESC is a game key, not a quit key.
    SetExitKey (KEY_NULL);

    if (fullscreen)
	ToggleBorderlessWindowed ();

    Image blank = GenImageColor (width, height, BLACK);
    screentex = LoadTextureFromImage (blank);
    UnloadImage (blank);
    SetTextureFilter (screentex, TEXTURE_FILTER_POINT);
}


void RL_ShutdownVideo (void)
{
    if (!IsWindowReady ())
	return;

    UnloadTexture (screentex);
    CloseWindow ();
}


void RL_Present (const unsigned char* rgba)
{
    float	winw;
    float	winh;
    float	w;
    float	h;
    Rectangle	src;
    Rectangle	dst;

    UpdateTexture (screentex, rgba);

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

    src = (Rectangle) { 0, 0, (float)screenwidth, (float)screenheight };
    dst = (Rectangle) { (winw - w) / 2, (winh - h) / 2, w, h };

    BeginDrawing ();
    ClearBackground (BLACK);
    DrawTexturePro (screentex, src, dst, (Vector2) { 0, 0 }, 0.0f, WHITE);
    EndDrawing ();
}


int RL_QuitRequested (void)
{
    return WindowShouldClose ();
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

static int		mousegrabbed;
static int		mousebuttons;
static Vector2		lastmouse;

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
	    ToggleBorderlessWindowed ();
	    keystate[key] = 1;
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
    dx = dy = 0;
    if (mousegrabbed)
    {
	dx = (int)(pos.x - lastmouse.x);
	dy = (int)(pos.y - lastmouse.y);
    }
    lastmouse = pos;

    if (!mousegrabbed)
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
    if (grab && !IsWindowFocused ())
	grab = 0;

    if (grab == mousegrabbed)
	return;

    mousegrabbed = grab;
    if (grab)
	DisableCursor ();
    else
	EnableCursor ();

    // Moving the cursor is not player movement.
    lastmouse = GetMousePosition ();
}



//
// AUDIO
//
// The game mixes on the main thread and pushes into this
// single-producer, single-consumer ring. raylib's audio thread
// pulls from it and plays silence on underrun.
//

#define RINGFRAMES	8192	// power of two

static AudioStream	stream;
static int		audioready;
static short		ring[RINGFRAMES*2];
static atomic_uint	ringread;
static atomic_uint	ringwrite;


static void StreamCallback (void* buffer, unsigned int frames)
{
    short*	out = (short*)buffer;
    unsigned	rd = atomic_load_explicit (&ringread, memory_order_relaxed);
    unsigned	wr = atomic_load_explicit (&ringwrite, memory_order_acquire);
    unsigned	avail = wr - rd;
    unsigned	i;

    for (i = 0; i < frames && i < avail; i++)
    {
	unsigned idx = (rd + i) & (RINGFRAMES-1);
	out[i*2] = ring[idx*2];
	out[i*2+1] = ring[idx*2+1];
    }

    if (i < frames)
	memset (out + i*2, 0, (frames - i) * 2 * sizeof(short));

    atomic_store_explicit (&ringread, rd + i, memory_order_release);
}


int RL_InitAudio (int samplerate)
{
    QuietRaylib ();
    InitAudioDevice ();
    if (!IsAudioDeviceReady ())
	return 0;

    stream = LoadAudioStream (samplerate, 16, 2);
    SetAudioStreamCallback (stream, StreamCallback);
    PlayAudioStream (stream);
    audioready = 1;
    return 1;
}


void RL_ShutdownAudio (void)
{
    if (!audioready)
	return;

    audioready = 0;
    UnloadAudioStream (stream);
    CloseAudioDevice ();
}


int RL_AudioQueued (void)
{
    return atomic_load_explicit (&ringwrite, memory_order_relaxed)
	- atomic_load_explicit (&ringread, memory_order_acquire);
}


void RL_QueueAudio (const short* samples, int frames)
{
    unsigned	wr = atomic_load_explicit (&ringwrite, memory_order_relaxed);
    int		i;

    if (!audioready || frames > RINGFRAMES - RL_AudioQueued ())
	return;

    for (i = 0; i < frames; i++)
    {
	unsigned idx = (wr + i) & (RINGFRAMES-1);
	ring[idx*2] = samples[i*2];
	ring[idx*2+1] = samples[i*2+1];
    }

    atomic_store_explicit (&ringwrite, wr + frames, memory_order_release);
}



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
