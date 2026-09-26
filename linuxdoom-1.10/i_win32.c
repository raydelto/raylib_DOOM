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
//	Windows-only startup and error reporting.
//
//	Like i_raylib.c this file must not see the DOOM headers:
//	<windows.h> defines its own "boolean". The rest of the game
//	only calls the two functions declared in i_win32.h.
//
//-----------------------------------------------------------------------------

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "i_win32.h"

// Set when we were started from Explorer and hid our console:
// stderr then goes nowhere, so I_Error shows a message box.
static int consolehidden;

void I_Win32Init (void)
{
    char	dir[MAX_PATH];
    char*	slash;
    DWORD	pids[2];
    DWORD	len;

    len = GetModuleFileNameA (NULL, dir, sizeof(dir));
    slash = len > 0 && len < sizeof(dir) ? strrchr (dir, '\\') : NULL;
    if (!slash)
	return;
    *slash = 0;

    // Look for IWADs next to raylibdoom.exe unless told otherwise,
    // so the portable zip works from any current directory.
    if (!getenv ("DOOMWADDIR"))
	_putenv_s ("DOOMWADDIR", dir);

    // A console that no other process shares was made for us by
    // Explorer. Close it: the game has its own window. When run
    // from a terminal the console stays and shows the startup text,
    // and when stderr is redirected to a file or pipe it is left
    // alone, so scripts see the messages and no dialog blocks them.
    if (GetFileType (GetStdHandle (STD_ERROR_HANDLE)) == FILE_TYPE_CHAR
	&& GetConsoleProcessList (pids, 2) == 1)
    {
	FreeConsole ();
	consolehidden = 1;

	// Savegames go to the current directory, which for a program
	// started from a shortcut, the Run box or a drag and drop may
	// be anywhere (often System32, where they can't be written).
	// Keep them next to the executable, like the IWAD.
	SetCurrentDirectoryA (dir);
    }
}

void I_Win32ErrorBox (const char* message)
{
    if (consolehidden)
	MessageBoxA (NULL, message, "raylib DOOM", MB_OK | MB_ICONERROR);
}

#endif
