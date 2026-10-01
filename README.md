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
loader and headers (`sudo apt install libopenxr-dev`), found through
their CMake config or, failing that, pkg-config (`openxr.pc`); without them
CMake downloads and builds the loader from the OpenXR SDK
([OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) 1.0.34 by
the Khronos Group, mainly under the Apache License 2.0, with bundled
parts such as jsoncpp under the licenses in its `LICENSES/`; see
`COPYING.adoc` in `build-xr/_deps/openxr-src`). A `raylib_doom_xr`
shipped with that loader has to carry those notices.

```sh
cmake -B build-xr -DRAYLIB_DOOM_XR=ON
cmake --build build-xr -j
ctest --test-dir build-xr
```

The `xr` test runs `i_xr.c` against a fake runtime under
AddressSanitizer: overlong extension and swapchain image lists,
swapchain image waits and releases that fail, a runtime that is lost
or quits, and the window's vsync.

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
usually picks the discrete GPU, so when the NVIDIA driver is loaded
`raylib_doom_xr` sets `__NV_PRIME_RENDER_OFFLOAD=1` and
`__GLX_VENDOR_LIBRARY_NAME=nvidia` before it opens its window.
`DOOM_XR_PRIME=0` turns that off, and a `__GLX_VENDOR_LIBRARY_NAME`
you set yourself is left alone.

Without a runtime or headset, or if the session can not be created, the
game says why on the `XR:` lines and plays in the window. If the runtime
goes away during the game (`monado-service` stopped, headset unplugged),
it says so once and goes on in the window; quitting from the runtime
still quits the game. While the session runs, the headset paces the
frames and the window's vsync is off; it is back on otherwise.

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
| Hands (`XR_EXT_hand_interaction`): right pinch | Fire | Enter    |
| Left pinch                      | Use               | Enter         |
| Right grasp (closed hand)       | Automap           | Backspace     |
| Left grasp                      | Menu (Esc)        | Esc           |

In a yes/no prompt, such as "are you sure you want to quit?", fire or
use answers yes, run or automap answers no, and menu cancels. So to
quit, press menu, pick Quit Game with fire, then press fire again.

| Option          | Effect                                               |
| --------------- | ---------------------------------------------------- |
| `-xrflat`       | Accepted for older scripts; playing in the window without a runtime or headset is now the default |
| `-noxr`         | Play in the window without trying OpenXR             |
| `-xrdist M`     | Screen distance in meters (default 2.5)              |
| `-xrwidth M`    | Screen width in meters (default: fitted to the field of view, at most 3.2) |
| `-xrlift N`     | Raise black to N% of white, 0 to 50 (default 12 on see-through glasses, 0 on other headsets) |
| `-xrstats`      | Log frame pacing every 10 seconds                    |

On the desktop only OpenGL on X11 is supported (`XR_KHR_opengl_enable`
with GLX), which is how raylib runs on Linux; the Android APK uses
OpenGL ES on EGL (see [Android and Android XR](#android-and-android-xr)). See the top of `linuxdoom-1.10/i_xr.c` for
how the frame reaches the headset.

## Android and Android XR

`android/` is a Gradle project that builds the game as an APK:
`CMakeLists.txt`, configured by the NDK with
`-DRAYLIB_DOOM_ANDROID=ON`, makes `libraylibdoom.so`, which
`NativeActivity` loads (raylib's `PLATFORM=Android`), started by a
small Java launcher that finds the player's WADs. The desktop,
Windows, macOS, web and OpenXR builds are not affected; the option is
off unless Gradle sets it. There are two flavors, each for
`arm64-v8a` (devices) and `x86_64` (the emulator):

| Flavor | Package              | What it is |
| ------ | -------------------- | ---------- |
| `flat` | `com.raylib.doom`    | A normal Android app (OpenGL ES 2) for phones, tablets and TVs. On Android XR it opens as a 4:3 panel in the Home Space. |
| `xr`   | `com.raylib.doom.xr` | The same with OpenXR (OpenGL ES 3, `XR_KHR_opengl_es_enable`, the Khronos loader AAR): on Android XR it starts in Full Space and shows the game on the virtual screen of [VR headsets](#vr-headsets-openxr). Without a runtime or headset it logs why and plays as the flat app. |

You need the Android SDK with NDK 28.1.13356709 and CMake 3.22.1
(Android Studio's SDK Manager, or `sdkmanager "ndk;28.1.13356709"
"cmake;3.22.1"`), and a JDK 17 or newer (with `javac`, not only a
JRE; set `JAVA_HOME` to it). The Gradle wrapper fetches
Gradle and the Android Gradle plugin. With `ANDROID_HOME` set, or
`sdk.dir` in `android/local.properties`:

```sh
cd android
./gradlew assembleFlatDebug assembleXrDebug
# app/build/outputs/apk/flat/debug/app-flat-debug.apk
# app/build/outputs/apk/xr/debug/app-xr-debug.apk
```

The `android` job in `.github/workflows/build.yml` uploads both debug
APKs as the `raylibdoom-android` artifact.

### No game data is included

The APK holds no WAD at all, not even a free one: the player supplies
their own, and the app never downloads any. It is an engine
compatible with DOOM WAD files. A launcher screen (`WadActivity`, the
only Java in the app) finds the WADs and starts the game:

- **Add WAD files…** opens the system file picker
  (`ACTION_OPEN_DOCUMENT`). The chosen files are copied into the app's
  internal storage (`files/wads`), so they stay usable whatever
  happens to the original.
- **Add a folder with WAD files…** asks for a folder
  (`ACTION_OPEN_DOCUMENT_TREE`) such as `Documents` or a folder inside
  `Download` (Android 11 and newer do not let apps have `Download`
  itself). The grant is kept, and on every start the folder's `*.wad`
  files (any case) are copied into `files/folders/`, again only when
  one changes; **Stop using the folder** forgets it.
- WADs copied over USB or with `adb push` to
  `Android/data/com.raylib.doom/files/` (`com.raylib.doom.xr` for the
  `xr` flavor) are found too; tap **Rescan**.

None needs a storage permission. IWADs are told apart as on the
desktop, by the maps in their lump directory (after checking that the
directory and the lumps fit in the file); with several, the player
picks one. PWADs are ticked as add-ons and loaded in that order, as
with `-file`. The choice is remembered. A file that is not a WAD, or a
damaged one, is refused with a message, and a PWAD alone is told it
needs an IWAD. With no IWAD the screen says what is needed and links
to [Freedoom's download page](https://freedoom.github.io/download.html)
(opened in the browser), and explains that the original game's IWAD
(`DOOM.WAD`, `DOOM2.WAD`, ...) is in the player's own copy, from Steam
or GOG for example. **About / Licenses** shows the GPL-2.0, the Nuked
OPL3 LGPL, the third-party notices for raylib, miniaudio and the rest
(`packaging/android/THIRD-PARTY-NOTICES.txt`), and a link to the
source of the exact tag. The app's name, "raylib 1.10 Engine", and
its icon use no id Software or Bethesda trademark or artwork.

The game runs in a process of its own (`:game`), so when it quits,
or stops on an error, the launcher comes back; an error (`I_Error`)
is shown there. `packaging/android/check-no-wad.sh APK...` fails if an
APK has an entry ending in `.wad` (any case) or another game-data
extension (`.pk3`, `.lmp`, ...), or any file starting with a WAD
header; CI runs it on every APK it builds.

Install and start one with `adb`:

```sh
adb install -r app/build/outputs/apk/flat/debug/app-flat-debug.apk
adb push freedoom1.wad /sdcard/Android/data/com.raylib.doom/files/
adb shell monkey -p com.raylib.doom -c android.intent.category.LAUNCHER 1
adb logcat -s raylibdoom        # DOOM's console output
```

The settings (`.doomrc`) and saved games go to the app's internal
storage (`/data/user/0/<package>/files`). The launcher writes the
command line it starts the game with to `launch.txt` there, one
argument per line. To pass more options, put them in `args.txt` (a
debug build allows `run-as`):

```sh
echo '-warp 1 -skill 4' > args.txt
adb push args.txt /data/local/tmp/
adb shell run-as com.raylib.doom cp /data/local/tmp/args.txt files/
```

### Release APK

On a `v*` tag, `.github/workflows/release-android.yml` builds
`raylibdoom-<version>-android-flat.apk` (the `flat` flavor,
`arm64-v8a` and `x86_64` in one APK) with its `.sha256`, checks it has
no WAD, and uploads both to the tag's draft release, adding the
Android section of the release notes
(`packaging/android/RELEASE-NOTES.md`). `versionName` is the tag
without the `v` and `versionCode` is `major*1000000 + minor*1000 +
patch` (`-PappVersion=1.2.3`). Pull requests that touch the Android
build, and a manual run, build the same release APK unsigned as an
artifact. The `xr` flavor is not released: it is for headsets and
glasses, and its OpenXR loader adds another component to ship; it
stays a CI artifact.

The release APK is signed with a key kept in four repository secrets.
Without them the tag's job fails rather than upload an unsigned or
debug-signed APK. To make the key (once; keep the keystore and its
passwords safe and out of the repository, as every later update must
be signed with the same key):

```sh
keytool -genkeypair -v -keystore raylibdoom-release.jks \
    -alias raylibdoom -keyalg RSA -keysize 4096 -validity 10000 \
    -dname "CN=raylib DOOM, O=raylib DOOM"
base64 -w0 raylibdoom-release.jks > raylibdoom-release.jks.b64
gh secret set ANDROID_KEYSTORE_BASE64 < raylibdoom-release.jks.b64
gh secret set ANDROID_KEYSTORE_PASSWORD     # the keystore password
gh secret set ANDROID_KEY_ALIAS --body raylibdoom
gh secret set ANDROID_KEY_PASSWORD          # the key password
rm raylibdoom-release.jks.b64
```

To sign locally, pass the keystore and set the same variables:
`ANDROID_KEYSTORE_PASSWORD=... ANDROID_KEY_ALIAS=... ANDROID_KEY_PASSWORD=...
./gradlew assembleFlatRelease -PappVersion=1.2.3 -PreleaseKeystore=/path/to/raylibdoom-release.jks`.

Players install it by opening the downloaded APK on the device and
allowing their browser or file manager to install unknown apps.

### Shareware APK (DOOM1.WAD included)

One APK, put on a release by hand, includes the shareware `DOOM1.WAD`
(episode 1), unmodified, as `assets/doom1.wad`: `-PbundleWad=FILE`
adds it, after checking its SHA-256 and that the file is outside the
checkout, so it can never be committed. Builds without the property,
CI and `release-android.yml` among them, stay WAD-free. The launcher
unpacks it and chooses it when nothing else is chosen; a fresh install
goes straight into the game. Other IWADs and PWADs are added as above,
and the launcher shows "DOOM shareware © id Software. Not affiliated
with id Software / ZeniMax / Microsoft." To build, sign and name it
(`raylibdoom-<version>-android-shareware.apk` and its `.sha256`):

```sh
ANDROID_KEYSTORE_PASSWORD=... ANDROID_KEY_ALIAS=... ANDROID_KEY_PASSWORD=... \
packaging/android/build-shareware.sh 0.1.1 /path/to/DOOM1.WAD \
    /path/to/raylibdoom-release.jks out/
```

### Controls and Android XR

A Bluetooth or USB gamepad plays with the controller mapping of the
[VR headsets](#vr-headsets-openxr) table: left stick (or d-pad up and
down) moves and strafes, right stick (or d-pad left and right) turns,
right trigger or X fires, A uses, left trigger runs, B or Y is the
automap and Start is the menu. On a touchscreen, buttons for turning,
moving, fire, use, run, the menu and the automap are drawn over the
picture; they hide once a gamepad is used and come back when the
screen is touched. The system Back button is Esc.

In the `xr` flavor the first run on a device logs the runtime's
OpenXR extensions (`XR: runtime extension ...`), then the session's
state changes. Android XR recommends Vulkan, but its runtime offers
`XR_KHR_opengl_es_enable`, which is what raylib's EGL context needs.
Both flavors have been run on XREAL's Project Aura glasses (Android
XR, Adreno, OpenGL ES 3.2) and on the `x86_64` Android XR emulator;
the `flat` one also on a plain `x86_64` phone emulator image. The
shared `i_xr.c` code is also tested on Monado on the desktop.

On see-through glasses such as the Aura (70° diagonal, about 58° x
34° per eye) two things differ from a VR headset:

- Their optics add the picture to the room, so black is transparent
  and DOOM's dark areas vanish. The runtime lists the `additive`
  blend mode for them (the Aura lists `opaque` first, but offers
  `additive`), and then the game raises black to 12% of white,
  scaling the rest so white stays white: dark rooms show as a dim
  image. `-xrlift N` sets another level (`-xrlift 0` turns it off),
  and DOOM's own gamma (F11, or the options menu) brightens the mid
  tones on top.
- The screen is sized to the field of view the runtime reports: at
  2.5 m it is 1.85 m wide on the Aura, so the whole 4:3 picture,
  status bar included, is in view. `-xrwidth` still sets it.

Hand tracking works without a controller, through
`XR_EXT_hand_interaction` (see the controls above); the Aura's
runtime picks that profile for both hands. A Bluetooth gamepad pairs
with the compute puck and plays as on a phone.

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
  (`-DRAYLIB_DOOM_XR=ON`), compiled into that target only;
  `tests/xr_test.c` tests it.
- Android: the launcher in `android/app/src/main/java` (the player's
  WADs: file picker, shared folder, IWAD detection, About / Licenses),
  `i_android.c` / `i_android.h` (logcat, the data folder,
  `launch.txt` and `args.txt`, errors for the launcher), the gamepad and touch controls in
  `i_raylib.c`, and the EGL / OpenGL ES graphics binding and
  `xrInitializeLoaderKHR` in `i_xr.c` for the `xr` flavor of the APK
  in `android/`.
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
