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
//	OpenXR headset output, only in the raylib_doom_xr build.
//
//	The game frame is shown on a virtual cinema screen: an OpenXR
//	quad layer in front of the viewer, which the runtime draws for
//	each eye with head tracking. Only i_raylib.c calls this, and
//	like it, this header uses plain C types: i_xr.c includes Xlib
//	and GLX, whose Font clashes with raylib's.
//
//-----------------------------------------------------------------------------

#ifndef __I_XR__
#define __I_XR__

// Controller buttons, as returned by XR_Buttons.
enum
{
    XRB_FORWARD		= 1 << 0,
    XRB_BACK		= 1 << 1,
    XRB_STRAFELEFT	= 1 << 2,
    XRB_STRAFERIGHT	= 1 << 3,
    XRB_TURNLEFT	= 1 << 4,
    XRB_TURNRIGHT	= 1 << 5,
    XRB_FIRE		= 1 << 6,
    XRB_USE		= 1 << 7,
    XRB_MENU		= 1 << 8,
    XRB_NUMBUTTONS	= 9
};

// Before the window opens: finds a runtime and a headset.
// Returns 0, having said why, if there is none.
int XR_Init (void);

// Once the window's OpenGL context is current: starts the session.
// Returns 0, having said why and shut OpenXR down, on failure.
int XR_StartSession (void);

void XR_Shutdown (void);

// True while a session exists (running or waiting to run).
int XR_Active (void);

// Handles runtime events and reads the controllers.
void XR_PumpEvents (void);

// True once the runtime asked the application to exit.
int XR_ExitRequested (void);

// XRB_* bits held down.
int XR_Buttons (void);

// Starts a headset frame. Returns 1 with an acquired swapchain
// image (an OpenGL texture) that the frame must be drawn into,
// 0 if nothing is to be drawn this time. XR_EndFrame must follow
// either way.
int XR_BeginFrame (unsigned int* texture, int* width, int* height);
void XR_EndFrame (void);

#endif
