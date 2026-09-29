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
// copies the bundled IWAD out of the APK into the app's internal
// storage, and makes that directory $HOME, $DOOMWADDIR and the
// current directory, so the IWAD, the settings (.doomrc) and the
// saved games are all kept there. Words in args.txt in that
// directory are added to the command line.
void I_AndroidSetup (void);

// False on devices without a touchscreen (Android XR glasses and
// headsets, TVs), which start with the touch controls hidden.
int I_AndroidHasTouchscreen (void);

#endif
