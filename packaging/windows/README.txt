raylib DOOM for Windows (x64)
=============================

The 1997 Linux DOOM source release, ported from X11 and OSS to raylib.
https://github.com/raydelto/raylib_DOOM

raylibdoom.exe is a single portable program: no installer, no DLLs to
copy, nothing written to the registry. It runs on 64-bit Windows 10
and 11.

Game data
---------

No game data is included. DOOM needs an IWAD, which is one of:

  doom1.wad      the shareware episode, free to share
  doom.wad       registered DOOM, doomu.wad for The Ultimate DOOM
  doom2.wad      DOOM II (also plutonia.wad, tnt.wad)
  freedoom1.wad  Freedoom, free content that plays like DOOM
  freedoom2.wad  Freedoom, free content that plays like DOOM II
                 (https://freedoom.github.io)

Put the IWAD in the same folder as raylibdoom.exe and double-click
raylibdoom.exe. Upper- or mixed-case names such as DOOM1.WAD work too.

To keep your WADs somewhere else, set the DOOMWADDIR environment
variable to their folder, or name the IWAD on the command line:

  raylibdoom.exe -iwad C:\wads\freedoom2.wad

If no IWAD is found the game says where it looked and exits.

First start: Windows SmartScreen
--------------------------------

raylibdoom.exe is not code-signed yet, so the first time you open it
Windows may show "Windows protected your PC". Click "More info", then
"Run anyway". Windows remembers the choice for this copy of the file.

If Windows says the file came from another computer and blocks it,
right-click the zip before extracting it, choose Properties, tick
"Unblock" and click OK.

Controls
--------

  Arrow keys          move and turn
  Mouse               turn (captured while you play)
  Ctrl / left button  fire
  Space               use (open doors, press switches)
  Shift               run
  Alt / right button  strafe while held
  , and .             strafe left / right
  1 ... 7             choose weapon
  Tab                 automap
  Esc                 menu
  Pause               pause
  F2 / F3             save / load game
  F6 / F9             quicksave / quickload
  F10                 quit
  Alt+Enter           toggle fullscreen

The mouse is released in menus, when paused and during demos. Keys can
be changed in %USERPROFILE%\.doomrc, where settings are saved.
Savegames (doomsav0.dsg ... doomsav5.dsg) are kept next to
raylibdoom.exe when you start it from Explorer or a shortcut, and in
the current folder when you start it from a terminal. Extract the zip
somewhere you can write to, such as your Desktop or Documents.

Command line
------------

Started from a terminal (PowerShell or cmd) the game prints its startup
messages there. Started from Explorer it shows only its window, and an
error message box if it cannot start.

  -1 ... -4, -scale N      window size as a multiple of 320x240 (default 3)
  -fullscreen              start fullscreen; Alt+Enter toggles it
  -nosound, -nomusic       no audio / no music
  -iwad FILE               use FILE as the IWAD
  -warp E M, -skill N      as in the original

Licenses
--------

DOOM is under the GNU GPL version 2 (LICENSE.TXT); its source is at
https://github.com/raydelto/raylib_DOOM. The music emulator, Nuked
OPL3, is under the GNU LGPL 2.1 or later (opl3-LICENSE.txt). raylib and
GLFW are under the zlib license (raylib-LICENSE.txt, glfw-LICENSE.md).
The MinGW-w64 runtime and libgcc are linked in statically
(mingw-w64-runtime-LICENSE.txt, gcc-RUNTIME-EXCEPTION.txt). All other
libraries built into the program are listed in THIRD-PARTY-NOTICES.txt.
