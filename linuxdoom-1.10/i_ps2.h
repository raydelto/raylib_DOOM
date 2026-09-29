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
//	PlayStation 2: the IOP modules, and where files are.
//
//-----------------------------------------------------------------------------

#ifndef __I_PS2__
#define __I_PS2__

// First thing in main: resets the IOP and loads the USB storage
// modules (not when started from host:, whose driver a reset would
// unload), then makes the folder DOOM.ELF was started from the
// current directory, where games are saved.
void I_PS2_Init (int argc, char** argv);

// The folder DOOM.ELF was started from, with its trailing separator:
// "host:", "mass:/DOOM/", "cdrom0:\". Empty if not known.
const char* I_PS2_BootDir (void);

// True if path can be opened for reading. Not every device can
// stat a file, or list a folder.
int I_PS2_CanRead (const char* path);

// A malloc'ed path to name in dir, a device ("mass:") or a folder
// on one ("mass:/DOOM/"); "." is the boot folder. On the CD the
// name is the ISO 9660 one, upper case with ";1". Elsewhere the
// name as given, or upper case if only that exists (FAT is case
// blind, host: may not be).
char* I_PS2_WadPath (const char* dir, const char* name);

// DualShock analog sticks, -1.0 to 1.0 (up and left negative),
// all 0 while the pad is in digital mode.
void I_PS2_Sticks (float* lx, float* ly, float* rx, float* ry);

#endif
