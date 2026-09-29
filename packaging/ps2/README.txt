raylib DOOM for the PlayStation 2
=================================

The 1997 Linux DOOM source release on raylib, built for the PS2 with
raylib4PlayStation2 (https://github.com/raylib4Consoles/raylib4PlayStation2):
raylib's PLATFORM_PLAYSTATION2 backend on ps2gl, OpenGL 1.1 on the GS.
The result is one file, DOOM.ELF.

Status: builds; not yet run on an emulator or a console. Silent for
now (see Sound).


Building
--------

With Docker, from the top of the repository:

  docker build -t raylibdoom-ps2 packaging/ps2
  docker run --rm -v "$PWD:/src" -w /src raylibdoom-ps2 packaging/ps2/build.sh
  # build-ps2/DOOM.ELF

The image is ps2dev/ps2dev (pinned in the Dockerfile) with
raylib4PlayStation2's libraries built into $PS2SDK/ports by
build-deps.sh. That script does what raylib4PlayStation2's
PlayStation2Build.sh does, at pinned commits, plus the fixes that
script needs with the current toolchain (GCC 15): include and install
paths, and raylib4Consoles_6.0-ps2.patch. It also builds the
shapes/logo_raylib_anim sample, to show the toolchain works.

Without Docker, with the ps2dev toolchain installed (for instance
ps2dev-ubuntu-latest.tar.gz from https://github.com/ps2dev/ps2dev/releases,
unpacked anywhere):

  export PS2DEV=/path/to/ps2dev PS2SDK=$PS2DEV/ps2sdk
  export PATH=$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH
  packaging/ps2/build.sh        # builds the libraries first if needed

Either way, "make -f Makefile.ps2" in linuxdoom-1.10 rebuilds just
the game (into linuxdoom-1.10/ps2/). It links -lraylib -lps2gl
-lps2stuff -lpad -ldma, as raylib4PlayStation2 apps do, and the IOP
modules for USB storage are built into the ELF.

The build.yml workflow's ps2 job does the Docker build and uploads
DOOM.ELF as the raylibdoom-ps2 artifact.


Game data
---------

No game data is included, and none may be committed. Use Freedoom
(https://freedoom.github.io) or a WAD you own. DOOM.ELF looks for the
IWADs other builds do (freedoom1.wad, freedoom2.wad, doom2.wad ...),
in this order:

  1. the folder DOOM.ELF was started from
  2. host:           PCSX2's host file system, or ps2link's host
  3. mass:/DOOM/     a USB stick (FAT)
  4. mass:
  5. cdrom0:\        the CD

"-iwad path" names one directly, "-file path" adds PWADs; paths are
PS2 paths such as mass:/DOOM/freedoom2.wad or cdrom0:\DOOM2.WAD;1.

Names are tried as written and in upper case. On the CD they are the
ISO 9660 ones, upper case with ";1", and must be 8.3: DOOM2.WAD and
PLUTONIA.WAD fit, but Freedoom's do not, so on a CD name them
FDOOM1.WAD (Phase 1) or FDOOM2.WAD (Phase 2), which are looked for
too.

Settings (default.cfg) and saved games go in the folder DOOM.ELF was
started from; started from the CD, they are not kept (no memory card
support yet).


Running
-------

PCSX2 2.x: turn on its "Enable Host Filesystem" setting, put the IWAD
next to DOOM.ELF, and start DOOM.ELF with System > Start File. A PS2 BIOS is needed; dump it from your own
console. DOOM's console output is in PCSX2's log window (EE console).

A console: start DOOM.ELF from a USB stick with a launcher such as
wLaunchELF, with the IWAD in mass:/DOOM/ or next to it; or with
ps2client from a PC running ps2link (host:).

Started from anywhere but host:, the game resets the IOP and loads
the USB storage modules. From host: it leaves the IOP alone, as a
reset would unload ps2link's host: driver; there is no mass: then.


Controls (DualShock 2)
----------------------

  left stick        move and strafe
  right stick       turn
  d-pad             move and turn
  Square, R2        fire
  X (Cross)         use; Enter in menus
  Circle            strafe while held; back in menus
  Triangle, L2      run while held
  L1, R1            previous, next weapon
  Start             menu (Esc)
  Select            automap (Tab)

In a yes/no prompt, X or Square is yes, Circle or Triangle no. The
pad must be in port 1, and plugged in: raylib4PlayStation2 waits for
it.


Video
-----

ps2gl has no framebuffer objects (so no LoadRenderTexture) and no
glTexSubImage2D (so raylib's UpdateTexture does nothing there). Each
320x200 frame goes into a 512x256 image in main RAM, the GS needing
power-of-two textures, which is handed to ps2gl again with
glTexImage2D and drawn over the whole 640x448 NTSC picture, smoothly
scaled. A TV shows 640x448 at 4:3, as a VGA monitor showed 320x200.


Sound
-----

None yet. raylib4PlayStation2 has no raudio (miniaudio has no PS2
backend), so the audio calls in i_raylib.c are stubs and the game runs
silent; sound effects and music through audsrv come later.


Memory
------

The EE has 32 MB. DOOM.ELF takes about 3.6 MB loaded, and the zone
heap (levels, graphics, sounds) 16 MB, twice what DOS DOOM had,
pointers being 32-bit as then. "-mb N" sets another size; if there
is not that much, the game takes what there is, down to 6 MB.
