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
//	OpenXR headset output for the raylib_doom_xr build.
//	The 320x200 frame is shown on a virtual screen floating in
//	front of the player; controller buttons become DOOM keys.
//
//	Like i_raylib.h, this header uses plain C types only, so the
//	DOOM side (i_video.c) and the raylib side (i_raylib.c) can
//	both include it.
//
//-----------------------------------------------------------------------------

#ifndef __I_XR__
#define __I_XR__

// Controller buttons, as a bit mask.
enum
{
    XR_FORWARD		= 1 << 0,
    XR_BACK		= 1 << 1,
    XR_TURNLEFT		= 1 << 2,
    XR_TURNRIGHT	= 1 << 3,
    XR_STRAFELEFT	= 1 << 4,
    XR_STRAFERIGHT	= 1 << 5,
    XR_FIRE		= 1 << 6,
    XR_USE		= 1 << 7,
    XR_RUN		= 1 << 8,
    XR_MENU		= 1 << 9,
    XR_MAP		= 1 << 10,
    XR_NUMBUTTONS	= 11
};

// Before the window opens: puts OpenGL on the GPU the headset
// most likely uses (NVIDIA PRIME offload, when that driver is
// loaded), unless DOOM_XR_PRIME=0.
void XR_PrepareGL (void);

// Connects to the OpenXR runtime and creates a session on the
// current OpenGL (GLX) context, so the window must be open.
// distance and width place the virtual screen, in meters; a width
// of 0 fits it to the field of view. lift raises black, as a
// fraction of white, for see-through glasses; below 0 lifts only
// on an additive display.
// Returns 0, having said why, if there is no runtime or headset.
int XR_Init (float distance, float width, float lift);
void XR_Shutdown (void);

// True between a successful XR_Init and XR_Shutdown.
int XR_Active (void);

// How far to raise black in the headset's image (0 to 1).
float XR_BlackLift (void);

// Logs frame pacing every few seconds (-xrstats).
void XR_Stats (int on);

// Handles runtime events (session state changes) and reads the
// controllers. Returns 0 once the runtime wants the game to quit.
// If the runtime went away, says so and shuts OpenXR down, so the
// game goes on in the window.
int XR_Update (void);

// Buttons held, as of the last XR_Update.
unsigned XR_Buttons (void);

// Runs one OpenXR frame. When the headset wants a new image, calls
// draw with a framebuffer object holding the virtual screen's
// width x height swapchain image to draw the game into.
typedef void (*xr_draw_t) (unsigned int fbo, int width, int height);
void XR_Present (xr_draw_t draw);

#endif
