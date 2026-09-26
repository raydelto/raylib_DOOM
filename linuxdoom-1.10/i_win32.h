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
//	Windows-only startup and error reporting (i_win32.c).
//
//-----------------------------------------------------------------------------

#ifndef __I_WIN32__
#define __I_WIN32__

#ifdef _WIN32

// Before main(), i_win32.c defaults DOOMWADDIR to the executable's
// folder and, when started from Explorer, hides the console window
// and changes to that folder.

// Show a fatal error in a message box if there is no console.
void I_Win32ErrorBox (const char* message);

#endif

#endif
