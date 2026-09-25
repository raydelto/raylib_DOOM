# raylib DOOM

The 1997 Linux DOOM source release (`linuxdoom-1.10`), ported from X11 and
`/dev/dsp` to [raylib](https://www.raylib.com/) and made to run on 64-bit
systems. The original release notes are in [README.TXT](README.TXT).

The software renderer is untouched: it still draws 8-bit paletted pixels
into a 320x200 buffer. raylib opens the window, shows that buffer scaled
to 4:3, reads the keyboard and mouse, and plays the sound effects.

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

## Running

Put an IWAD (`doom1.wad`, `doom.wad`, `doom2.wad`, `plutonia.wad`,
`tnt.wad`, ...) in the current directory or in `$DOOMWADDIR`, then:

```sh
./build/raylibdoom
```

[Freedoom](https://freedoom.github.io/) works too: copy or link
`freedoom2.wad` as `doom2.wad`.

| Option               | Effect                                    |
| -------------------- | ----------------------------------------- |
| `-1` ... `-4`        | Window size as a multiple of 320x240 (default 3) |
| `-scale N`           | Any window multiple                       |
| `-fullscreen`        | Start borderless fullscreen               |
| `-nosound`           | No audio device                           |
| `-warp E M` / `-warp M`, `-skill N`, `-loadgame N` | As in the original |

Alt+Enter toggles fullscreen. The mouse is captured while you are playing
and released in menus, when paused, and during demos. Settings are saved
to `~/.doomrc`.

## What changed

- `i_raylib.c` / `i_raylib.h`: new, the only code that talks to raylib.
  `raylib.h` and DOOM's headers clash (`KEY_*`, `<stdbool.h>` vs. the
  `boolean` enum), so the rest of the game sees a small plain-C API.
- `i_video.c`: X11/MIT-SHM code replaced with palette expansion and
  calls into `i_raylib.c`.
- `i_sound.c`: the original software mixer, now feeding a raylib audio
  stream instead of `/dev/dsp`. Sounds stop, update and report
  "playing" by handle, and play at their own sample rate.
- `doomkeys.h`: the key codes, split out of `doomdef.h`.
- 64-bit fixes: pointer arrays sized with `sizeof` instead of `4`,
  pointer/integer casts through `intptr_t`, the on-disk texture struct
  no longer holds a pointer, the config file's string settings, the
  savegame buffer, and the zone heap size.
- Fixes for bugs modern compilers and glibc catch: undersized WAD path
  buffers, the unterminated sprite name list, undefined event queue
  increments, and a `memset` that cleared only part of the mouse and
  joystick button state.

Music is not implemented, same as in the original Linux release.
