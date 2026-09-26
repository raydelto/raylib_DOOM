raylib DOOM
===========

The 1997 Linux DOOM source release, ported from X11 and OSS to raylib.
https://github.com/raydelto/raylib_DOOM

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

  1. the directory named by the DOOMWADDIR environment variable
  2. the current directory
  3. /usr/local/share/games/doom
  4. /usr/share/games/doom

On Debian and Ubuntu, "sudo apt install freedoom" installs Freedoom into
/usr/share/games/doom, where the game finds it. To use an IWAD under any
other name or path, name it on the command line:

  raylibdoom -iwad ~/wads/freedoom2.wad

If no IWAD is found the game prints where it looked and exits.

Running
-------

  raylibdoom               (the .deb installs it as /usr/games/raylibdoom)
  ./raylibdoom             (from the .tar.gz)

  -1 ... -4, -scale N      window size as a multiple of 320x240 (default 3)
  -fullscreen              start fullscreen; Alt+Enter toggles it
  -nosound, -nomusic       no audio / no music
  -iwad FILE               use FILE as the IWAD
  -warp E M, -skill N      as in the original

The mouse is captured while you play and released in menus and when
paused. Settings are saved to ~/.doomrc.

The binary needs glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and
later), an OpenGL driver, X11 (or XWayland) and ALSA (or PipeWire /
PulseAudio's ALSA plugin).

Licenses
--------

DOOM is under the GNU GPL version 2 (LICENSE.TXT). The music emulator,
Nuked OPL3, is under the GNU LGPL 2.1 or later (opl3-LICENSE.txt).
raylib and GLFW are under the zlib license (raylib-LICENSE.txt,
glfw-LICENSE.md). The other libraries built into the binary are listed
in THIRD-PARTY-NOTICES.txt.
