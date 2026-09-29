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

The Windows x64 release, `raylibdoom-<version>-windows-x64.zip`, is a
portable folder with a single statically linked `raylibdoom.exe`, built
by `.github/workflows/release-windows.yml` with
`packaging/windows/make-package.sh`. On Windows the game looks for
IWADs next to `raylibdoom.exe` when `DOOMWADDIR` is not set. Started
from Explorer, it hides its console window, keeps savegames next to
the executable, and shows fatal errors in a message box.

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

### Ubuntu packages

`.github/workflows/release-linux.yml` builds `raylibdoom_<version>_amd64.deb`
and `raylibdoom-<version>-linux-x86_64.tar.gz` on Ubuntu 22.04, and the
same for arm64 (`_arm64.deb`, `-linux-aarch64.tar.gz`) natively on
GitHub's `ubuntu-22.04-arm` runner, no cross-compiling or QEMU (they run
on 22.04, 24.04 and newer, including JetPack 6 on Jetson boards); the
scripts are in `packaging/linux/`. Pushing a `v*` tag uploads both
architectures to the draft release; running the workflow by hand keeps
them as an Actions artifact, and can also build an existing tag's
source (the `tag` input) as an artifact only, without touching that
tag's release. `sudo apt install ./raylibdoom_*.deb` installs
`/usr/games/raylibdoom` with a menu entry; `sudo apt install freedoom`
adds free IWADs it finds.

### Arch Linux and Omarchy

`.github/workflows/release-arch.yml` builds
`raylibdoom-<version>-1-x86_64.pkg.tar.zst` with
`packaging/arch/PKGBUILD` in an `archlinux:base-devel` container, and
checks it with `namcap`. Pushing a `v*` tag uploads it to the draft
release; running the workflow by hand keeps it as an Actions artifact,
unless it is given the tag of an existing draft release, which it then
builds and adds the package to. Install it with
`sudo pacman -U raylibdoom-*.pkg.tar.zst`: it puts `/usr/bin/raylibdoom`
in the app launcher, and the game finds IWADs copied to
`/usr/share/games/doom`. `sudo pacman -R raylibdoom` removes it. As
committed, the PKGBUILD builds the release tarball of its `pkgver` and
can go to the AUR. The Ubuntu `raylibdoom-<version>-linux-x86_64.tar.gz`
runs on Omarchy too, unpacked anywhere, and is the fallback for other
distributions.

On Hyprland the game runs through XWayland. The window tiles like any
other; to float it at its own size instead, add a window rule for the
class `DOOM` (in Omarchy's Lua config,
`o.window("DOOM", { float = true, center = true })`). Alt+Enter
toggles borderless fullscreen; `-fullscreen` starts that way. If another
window on the workspace is fullscreen, Omarchy hands fullscreen to the
new window, so the game can open fullscreen then. While you play the
mouse is captured and the pointer hidden; in menus, when paused, and
when the window loses focus, it is released.

### Web browser (WebAssembly)

The game also runs in a desktop browser, built with
[Emscripten](https://emscripten.org/) and raylib's web platform. The
page bundles Freedoom: Phase 1 (`freedoom1.wad`, BSD licensed; its
`COPYING.txt` ships next to it), which CMake downloads and checks
against its SHA-256 at configure time; it is not in this repository.

Install the [emsdk](https://emscripten.org/docs/getting_started/downloads.html)
outside this repository (CI uses 6.0.10), then:

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -B build-web
cmake --build build-web -j
```

`-DPLATFORM=Web` may be given, but is implied. Add
`-DFREEDOOM_ZIP=/path/to/freedoom-0.13.0.zip` to use a zip you already
have instead of downloading it. `build-web/` then holds
`raylibdoom.html`, `.js`, `.wasm`, `.data` (the WAD) and
`freedoom-COPYING.txt`; the `web` job in `.github/workflows/build.yml`
uploads the same files as the `raylibdoom-web` artifact.

Browsers do not load the `.data` file from `file://`, so serve the
folder over HTTP and open the page:

```sh
python3 -m http.server -d build-web 8000
# then open http://localhost:8000/raylibdoom.html
# or: emrun build-web/raylibdoom.html
```

Click the page to start; that click also lets the browser start the
audio. Click again in a level to capture the mouse (Esc lets go of
it); Alt+Enter toggles fullscreen. Saved games and settings are kept
in the browser's IndexedDB, so they survive a reload. "Load your own
IWAD / PWAD" stores WADs from your disk in the same place and reloads
the page with them in the URL (`?iwad=NAME&file=NAME,NAME`); "Back to
Freedoom" removes them. Other options go in `?args=`, for example
`raylibdoom.html?args=-warp%201%201%20-skill%204`.

In the browser the frame loop is driven by
`emscripten_set_main_loop` (once per display refresh) instead of
ASYNCIFY: `D_DoomLoop`'s body is `D_RunFrame`, `TryRunTics` returns
instead of spinning until the next tic is due, and the screen wipe
runs one step per frame. Sound effects and the OPL music go through
the same raylib audio streams as on the desktop, over Web Audio.
Netgames are not supported, and quitting from the menu ends the game
until the page is reloaded.

## Running

Put an IWAD (`doom1.wad`, `doom.wad`, `doom2.wad`, `plutonia.wad`,
`tnt.wad`, Freedoom's `freedoom1.wad` / `freedoom2.wad`, ...) in
`$DOOMWADDIR` or the current directory, then:

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
On Linux, the game then looks in `/usr/local/share/games/doom` and
`/usr/share/games/doom`, where distribution packages such as `freedoom`
install IWADs. If no IWAD is found, the game says where it looked and
exits.

| Option               | Effect                                    |
| -------------------- | ----------------------------------------- |
| `-1` ... `-4`        | Window size as a multiple of 320x240 (default 3) |
| `-scale N`           | Any window multiple                       |
| `-fullscreen`        | Start borderless fullscreen               |
| `-nosound`           | No audio device                           |
| `-nomusic`           | Sound effects only                        |
| `-iwad FILE`         | Use FILE as the IWAD                      |
| `-warp E M` / `-warp M`, `-skill N`, `-loadgame N` | As in the original |

PWADs that add episodes past E4, such as SIGIL (E5) and SIGIL II (E6),
work with a DOOM 1 IWAD: their `UMAPINFO` puts the episode in the New
Game menu and supplies level names, music, sky, intermission pictures,
par times, the next and secret levels, and the end text and picture:

```sh
./build/raylibdoom -iwad freedoom1.wad -file SIGIL_II_V1_0.WAD -warp 6 1
```

These WADs replace the IWAD's texture list, so Freedoom's own episodes
may not load while one is added.

Only part of UMAPINFO is supported (see the top of
`linuxdoom-1.10/u_mapinfo.c`). `NoIntermission` is ignored, `EndCast`
ends the game without the cast, `InterText` shows only on a level that
ends the game, and a `BossAction` other than `clear` keeps the default
boss behaviour.

The picture is scaled to the window in two steps, first by a whole
number with sharp pixels and then smoothly to the final size, so every
pixel row comes out the same height. Alt+Enter toggles fullscreen. The
mouse is captured while you are playing and released in menus, when
paused, and during demos. Settings are saved to `~/.doomrc`.

Under WSL (WSLg) GLFW cannot lock the pointer, so the game detects WSL at
run time and starts a small `powershell.exe` helper that keeps the Windows
pointer inside the window while you play. Set `DOOM_WSL_MOUSE=0` to turn
that off, or `DOOM_WSL_MOUSE=1` to force it.

## VR headsets (OpenXR)

On Linux an opt-in second target, `raylib_doom_xr`, shows the game on
a large virtual screen floating in front of you in an OpenXR headset,
with head tracking. It is still the flat 320x200 game, not a
stereoscopic 3D renderer. `raylibdoom` and the other platforms are
built as before. Besides raylib's dependencies it needs the OpenXR
loader and headers (`sudo apt install libopenxr-dev`); without them
CMake downloads and builds the loader from the OpenXR SDK.

```sh
cmake -B build-xr -DRAYLIB_DOOM_XR=ON
cmake --build build-xr -j
```

To try it without a headset, run the [Monado](https://monado.dev/)
runtime with its simulated headset (`sudo apt install monado-service
libopenxr1-monado`), which shows both eyes in a desktop window:

```sh
SIMULATED_ENABLE=1 XRT_COMPOSITOR_FORCE_XCB=1 monado-service
```

and in another terminal:

```sh
XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json \
  ./build-xr/raylib_doom_xr -iwad freedoom1.wad
```

`XR_RUNTIME_JSON` is only needed when no runtime is set as the active
one, or when the one in `~/.config/openxr/1/active_runtime.json` is
broken. `monado-service` exits at once if its standard input is not a
terminal or pipe (`epoll_ctl(stdin) failed`); start it from a terminal,
or as `sleep infinity | monado-service`. On a laptop with two GPUs the
game's OpenGL and Monado's Vulkan have to be on the same one, or the
game crashes in the OpenGL driver while creating the swapchain. Monado
usually picks the discrete GPU, so on NVIDIA run the game with
`__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia`.

The desktop window keeps a copy of the picture, and the keyboard and
mouse work as usual while it has focus. Controllers (the ones OpenXR
calls simple, Oculus Touch, Valve Index, HTC Vive and Windows Mixed
Reality) are mapped to your key bindings:

| Control                         | In the game       | In menus      |
| ------------------------------- | ----------------- | ------------- |
| Left stick / trackpad           | Move and strafe   | Up, down, left, right |
| Right stick / trackpad          | Turn              | Left, right   |
| Right trigger (simple: right select) | Fire         | Enter         |
| Right A or grip (simple: left select) | Use         | Enter         |
| Left trigger                    | Run               | Backspace     |
| Right B, or right menu          | Automap           | Backspace     |
| Left menu (Index: left B)       | Menu (Esc)        | Esc           |

In a yes/no prompt, such as "are you sure you want to quit?", fire or
use answers yes, run or automap answers no, and menu cancels. So to
quit, press menu, pick Quit Game with fire, then press fire again.

| Option          | Effect                                               |
| --------------- | ---------------------------------------------------- |
| `-xrflat`       | Play in the window if there is no runtime or headset, instead of exiting with an error |
| `-noxr`         | Play in the window without trying OpenXR             |
| `-xrdist M`     | Screen distance in meters (default 2.5)              |
| `-xrwidth M`    | Screen width in meters (default 3.2)                 |

Only OpenGL on X11 is supported (`XR_KHR_opengl_enable` with GLX), which
is how raylib runs on Linux. See the top of `linuxdoom-1.10/i_xr.c` for
how the frame reaches the headset.

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
  arrangement, like the DOS version. Standard MIDI music lumps (as in
  Freedoom and many PWADs) play through the same chip and voices.
- `s_sound.c`, `i_sound.h`: `I_RegisterSong` also takes the lump
  length, so MIDI files can be read safely.
- `opl3.c` / `opl3.h`: [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3)
  1.8 by Nuke.YKT, unmodified, under the LGPL 2.1 or later
  (`opl3-LICENSE.txt`).
- `i_xr.c` / `i_xr.h`: new, the OpenXR output of `raylib_doom_xr`
  (`-DRAYLIB_DOOM_XR=ON`), compiled into that target only.
- `doomkeys.h`: the key codes, split out of `doomdef.h`.
- Browser build: `web/web.cmake` and `web/shell.html` (the page, with
  the IndexedDB mount and the WAD picker); `d_main.c` splits the game
  loop into `D_RunFrame` for `emscripten_set_main_loop`, and
  `i_system.c` / `i_raylib.c` end the game, report errors and handle
  the mouse the way a page needs.
- 64-bit fixes: pointer arrays sized with `sizeof` instead of `4`,
  pointer/integer casts through `intptr_t`, the on-disk texture struct
  no longer holds a pointer, the config file's string settings, the
  savegame buffer, and the zone heap size.
- arm/aarch64 Linux: built with `-fsigned-char`, because the code
  assumes a signed `char` (movement in `ticcmd_t` among others) and
  plain `char` is unsigned there. Without it backward and left moves
  went forward and right, and demos desynced.
- `u_mapinfo.c` / `u_mapinfo.h`: new, a `UMAPINFO` parser. `g_game.c`,
  `m_menu.c`, `s_sound.c`, `wi_stuff.c`, `f_finale.c`, `hu_stuff.c` and
  `p_enemy.c` use it for PWAD episodes; `G_InitNew` no longer turns E5
  and E6 into E4 when the maps are there. Flats are gathered from every
  WAD's `F_START`/`F_END`, not only the last one's, and the rendering
  limits (visplanes, drawsegs, sprites, openings) and the unchecked
  arrays behind them are raised for limit-removing maps.
- Ultimate DOOM skies: `G_DoLoadLevel` compared `gamemode` with the
  mission values `pack_tnt` and `pack_plut`, and `retail` equals
  `pack_plut`, so E2-E4 levels got DOOM II's `SKY1`.
- Fixes for bugs modern compilers and glibc catch: undersized WAD path
  buffers, the unterminated sprite name list, undefined event queue
  increments, and a `memset` that cleared only part of the mouse and
  joystick button state.
