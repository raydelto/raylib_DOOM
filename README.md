# raylib DOOM

The 1997 Linux DOOM source release (`linuxdoom-1.10`), ported from X11 and
`/dev/dsp` to [raylib](https://www.raylib.com/) and made to run on 64-bit
systems. The original release notes are in [README.TXT](README.TXT).

The software renderer is untouched: it still draws 8-bit paletted pixels
into a 320x200 buffer. raylib opens the window, shows that buffer scaled
to 4:3, reads the keyboard and mouse, and plays the sound effects and
music.

## Building

You need a C compiler, CMake 3.16+, and raylib's system dependencies
(on Debian/Ubuntu: `libx11-dev libxrandr-dev libxinerama-dev
libxcursor-dev libxi-dev libgl1-mesa-dev libasound2-dev`).

```sh
cmake -B build
cmake --build build -j
```

CMake uses an installed raylib 5.x if it finds one, and otherwise
downloads and builds raylib 5.5.

The original Makefile also still works if raylib is installed with
pkg-config support:

```sh
cd linuxdoom-1.10
make
```

### macOS

Install the Xcode Command Line Tools (`xcode-select --install`) and
CMake (`brew install cmake`), then build as above. raylib uses Cocoa
and OpenGL, which ship with macOS; nothing else is needed. Works on
Apple Silicon and Intel.

The macOS release is a `.dmg` for Apple Silicon (macOS 11+), made by
`.github/workflows/release-macos.yml` with `packaging/macos/make-dmg.sh`.
Inside `raylibDOOM.app` the game looks for its IWAD in `$DOOMWADDIR`,
`~/Library/Application Support/raylibDOOM` and the folder holding the
app, saves games to that Application Support folder, and logs to
`~/Library/Logs/raylibDOOM.log`; see `packaging/macos/README.txt`.

### Windows

Build with MinGW-w64 GCC, for example from
[w64devkit](https://github.com/skeeto/w64devkit) or MSYS2:

```sh
cmake -B build -G "MinGW Makefiles"
cmake --build build -j
```

or cross-compile from Linux with a MinGW-w64 toolchain file
(`CMAKE_SYSTEM_NAME Windows`, `CMAKE_C_COMPILER x86_64-w64-mingw32-gcc`).
MSVC is not supported: the code uses C11 atomics and POSIX headers such
as `<unistd.h>` and `<dirent.h>`, which MinGW provides and MSVC does not.
Settings go to `%USERPROFILE%\.doomrc` when `HOME` is not set.

## Running

Put an IWAD (`doom1.wad`, `doom.wad`, `doom2.wad`, `plutonia.wad`,
`tnt.wad`, ...) in the current directory or in `$DOOMWADDIR`, then:

```sh
./build/raylibdoom
```

Upper- or mixed-case names such as `DOOM1.WAD` are found too. To use an
IWAD under any other name or path, name it with `-iwad`; the game is
identified from the maps it contains:

```sh
./build/raylibdoom -iwad ~/wads/freedoom2.wad
```

An IWAD passed with `-file` (`-file DOOM1.WAD`) is also used as the IWAD.

| Option               | Effect                                    |
| -------------------- | ----------------------------------------- |
| `-1` ... `-4`        | Window size as a multiple of 320x240 (default 3) |
| `-scale N`           | Any window multiple                       |
| `-fullscreen`        | Start borderless fullscreen               |
| `-nosound`           | No audio device                           |
| `-nomusic`           | Sound effects only                        |
| `-iwad FILE`         | Use FILE as the IWAD                      |
| `-warp E M` / `-warp M`, `-skill N`, `-loadgame N` | As in the original |

The picture is scaled to the window in two steps, first by a whole
number with sharp pixels and then smoothly to the final size, so every
pixel row comes out the same height. Alt+Enter toggles fullscreen. The
mouse is captured while you are playing and released in menus, when
paused, and during demos. Settings are saved to `~/.doomrc`.

Under WSL (WSLg) GLFW cannot lock the pointer, so the game detects WSL at
run time and starts a small `powershell.exe` helper that keeps the Windows
pointer inside the window while you play. Set `DOOM_WSL_MOUSE=0` to turn
that off, or `DOOM_WSL_MOUSE=1` to force it.

## What changed

- `i_raylib.c` / `i_raylib.h`: new, the only code that talks to raylib.
  `raylib.h` and DOOM's headers clash (`KEY_*`, `<stdbool.h>` vs. the
  `boolean` enum), so the rest of the game sees a small plain-C API.
- `i_video.c`: X11/MIT-SHM code replaced with palette expansion and
  calls into `i_raylib.c`.
- `i_sound.c`: the original software mixer, now feeding a raylib audio
  stream instead of `/dev/dsp`. Sounds stop, update and report
  "playing" by handle, and play at their own sample rate.
- `i_music.c`: new. Plays the music the way DOS DOOM did on an AdLib or
  Sound Blaster: the MUS score drives an emulated OPL2 FM chip, with the
  instruments from the IWAD's `GENMIDI` lump and DMX's voice allocation,
  pitch table and volume curve. The title uses `D_INTROA`, the OPL
  arrangement, like the DOS version.
- `opl3.c` / `opl3.h`: [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3)
  1.8 by Nuke.YKT, unmodified, under the LGPL 2.1 or later
  (`opl3-LICENSE.txt`).
- `doomkeys.h`: the key codes, split out of `doomdef.h`.
- 64-bit fixes: pointer arrays sized with `sizeof` instead of `4`,
  pointer/integer casts through `intptr_t`, the on-disk texture struct
  no longer holds a pointer, the config file's string settings, the
  savegame buffer, and the zone heap size.
- Fixes for bugs modern compilers and glibc catch: undersized WAD path
  buffers, the unterminated sprite name list, undefined event queue
  increments, and a `memset` that cleared only part of the mouse and
  joystick button state.
