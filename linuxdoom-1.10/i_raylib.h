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
//	Thin wrapper around raylib for window, input and audio.
//
//	raylib.h and the DOOM headers can not be included in the
//	same translation unit: raylib's KEY_* enums collide with the
//	DOOM key macros, and <stdbool.h> (pulled in by raylib.h)
//	breaks the "boolean" enum in doomtype.h. So i_raylib.c is the
//	only file that sees raylib, and this header uses plain C types.
//
//-----------------------------------------------------------------------------

#ifndef __I_RAYLIB__
#define __I_RAYLIB__

typedef enum
{
    rl_keydown,
    rl_keyup,
    rl_mouse
} rl_evtype_t;

typedef struct
{
    rl_evtype_t	type;
    int		data1;	// DOOM key code, or mouse button mask
    int		data2;	// mouse x delta
    int		data3;	// mouse y delta
} rl_event_t;


//
// Video
//

// Opens a window showing a width x height framebuffer,
// stretched to 4:3 like a CRT would.
void RL_InitVideo (int width, int height, int scale, int fullscreen);
void RL_ShutdownVideo (void);

// Uploads a width x height RGBA8 frame and presents it.
void RL_Present (const unsigned char* rgba);

// True once the user asked to close the window.
int RL_QuitRequested (void);


//
// Input
//

// Polls the OS for new input and queues DOOM-ready events.
void RL_PumpEvents (void);

// Returns 1 and fills ev while queued events remain.
int RL_GetEvent (rl_event_t* ev);

// Captures (hides and locks) the mouse pointer.
void RL_SetMouseGrab (int grab);


//
// Audio
//

// Starts a stereo 16-bit output stream. Returns 0 if no audio device.
int RL_InitAudio (int samplerate);
void RL_ShutdownAudio (void);

// Frames queued but not yet played.
int RL_AudioQueued (void);

// Appends interleaved stereo frames to the output queue.
void RL_QueueAudio (const short* samples, int frames);

#endif
