raylib DOOM for macOS (Apple Silicon)
=====================================

The 1997 Linux DOOM source release, ported to raylib.
https://github.com/raydelto/raylib_DOOM

Needs macOS 11 (Big Sur) or later on an M-series Mac.

Installing
----------

Drag raylibDOOM.app onto the Applications folder in this window.

The app is not notarized by Apple (the project has no paid Developer
ID yet), so the first time you open it macOS says it "cannot be
opened" or "cannot verify" it. To open it anyway, either:

  - Open raylibDOOM once (it will be refused), then go to
    System Settings > Privacy & Security, scroll down to the message
    about raylibDOOM and click "Open Anyway". Confirm with your
    password or Touch ID and click "Open". From then on it opens
    normally.

  - Or, in Terminal, remove the download quarantine flag:

      xattr -dr com.apple.quarantine /Applications/raylibDOOM.app

The app is ad-hoc signed. It needs no special permissions.

Game data
---------

No game data is included. DOOM needs an IWAD, which is one of:

  doom1.wad      the shareware episode, free to share
  doom.wad       registered DOOM, doomu.wad for The Ultimate DOOM
  doom2.wad      DOOM II (also plutonia.wad, tnt.wad)
  freedoom1.wad  Freedoom, free content that plays like DOOM
  freedoom2.wad  Freedoom, free content that plays like DOOM II
                 (https://freedoom.github.io)

Put one in any of these places. They are searched in this order, and
upper- or mixed-case names such as DOOM1.WAD are found too:

  1. the folder named by the DOOMWADDIR environment variable
  2. ~/Library/Application Support/raylibDOOM
     (in Finder: Go > Go to Folder..., paste that path; the app
     creates the folder the first time it runs)
  3. the folder raylibDOOM.app is in (for example /Applications)

If there are several, the first place that has one wins, and within a
place the id IWADs win over Freedoom. If no IWAD is found the app says
so in a dialog and quits.

Running from Terminal
---------------------

The game binary takes the usual options:

  /Applications/raylibDOOM.app/Contents/MacOS/raylibdoom -iwad ~/wads/freedoom2.wad

  -1 ... -4, -scale N      window size as a multiple of 320x240 (default 3)
  -fullscreen              start fullscreen; Alt+Enter toggles it
  -nosound, -nomusic       no audio / no music
  -iwad FILE               use FILE as the IWAD
  -warp E M, -skill N      as in the original

Files
-----

  ~/.doomrc                                    settings
  ~/Library/Application Support/raylibDOOM/    saved games
  ~/Library/Logs/raylibDOOM.log                output of the last run

The mouse is captured while you play and released in menus and when
paused. Cmd+Q or the window's close button quits.

Licenses
--------

DOOM is under the GNU GPL version 2 (LICENSE.TXT). The music emulator,
Nuked OPL3, is under the GNU LGPL 2.1 or later (opl3-LICENSE.txt).
raylib and GLFW are under the zlib license (raylib-LICENSE.txt,
glfw-LICENSE.md). The other libraries built into the program are listed
in THIRD-PARTY-NOTICES.txt. The same files are inside the app, in
raylibDOOM.app/Contents/Resources/licenses.
