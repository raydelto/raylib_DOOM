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
//	iOS (raylib's SDL platform on UIKit and OpenGL ES) support for
//	the app built with -DRAYLIB_DOOM_IOS=ON, see ios/.
//
//	Defines DOOM_IOS on iOS only, and DOOM_TOUCH where the game
//	draws touch controls (Android and iOS), so the macOS build
//	keeps its behavior.
//
//-----------------------------------------------------------------------------

#ifndef __I_IOS__
#define __I_IOS__

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#define DOOM_IOS 1
#endif
#endif

#if defined(__ANDROID__) || defined(DOOM_IOS)
#define DOOM_TOUCH 1
#endif

#ifdef DOOM_IOS
// After the audio device is open: AVAudioSession "ambient", so the
// game obeys the silent switch and does not stop the user's music.
void I_IOSAudioSession (void);

// The window's safe area (rounded corners, Dynamic Island, home
// indicator) as insets in points.
void I_IOSSafeInsets (float* left, float* top, float* right, float* bottom);

// i_raylib.c: false while the app is in the background, where iOS
// kills a process that still draws with OpenGL ES.
int RL_AppActive (void);

// i_system.c: the game's clock does not run while the app is away.
void I_SkipTime (int ms);
#endif

#endif
