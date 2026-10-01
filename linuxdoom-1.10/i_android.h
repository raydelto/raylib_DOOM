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
//	Android (NativeActivity) start-up for the APK in android/.
//
//-----------------------------------------------------------------------------

#ifndef __I_ANDROID__
#define __I_ANDROID__

// Called first thing in main. Sends stdout and stderr to logcat,
// and makes the app's internal storage $HOME, $DOOMWADDIR and the
// current directory, so the settings (.doomrc) and the saved games
// are kept there. The launcher's launch.txt in that directory (one
// argument per line: the IWAD and PWADs the player chose), then the
// words in args.txt, become the command line.
void I_AndroidSetup (void);

// Called by I_Error: leaves the message in error.txt for the
// launcher to show once the game's process has exited.
void I_AndroidError (const char* message);

// False on devices without a touchscreen (Android XR glasses and
// headsets, TVs), which start with the touch controls hidden.
int I_AndroidHasTouchscreen (void);

#endif
