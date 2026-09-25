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


#define WINDOWTITLE	"DOOM"

// Filled in at the bottom of the file, after the DOOM key macros
// are included (they would otherwise shadow raylib's KEY_* enums).
static int TranslateSpecialKey (int index);

static void WSL_Init (void);
static void WSL_Shutdown (void);

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
    InitWindow (width*scale, (width*3/4)*scale, WINDOWTITLE);
    SetWindowMinSize (width, width*3/4);

    // ESC is a game key, not a quit key.
    SetExitKey (KEY_NULL);

    if (fullscreen)
	ToggleBorderlessWindowed ();

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
	if (wslmouse)
	    WSL_FilterMotion (pos, &dx, &dy);
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
    if (wslmouse)
	WSL_Grab (grab);
    else if (grab)
	DisableCursor ();
    else
	EnableCursor ();

    // Moving the cursor is not player movement.
    lastmouse = GetMousePosition ();
}



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



//
// AUDIO
//
// Each stream (sound effects, music) has its own single-producer,
// single-consumer ring. The game mixes on the main thread and
// pushes into it; raylib's audio thread pulls from it and plays
// silence on underrun.
//

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
