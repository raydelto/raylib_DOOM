#!/bin/sh
# Builds the raylib4PlayStation2 libraries into $PS2SDK/ports, as
# raylib4PlayStation2's PlayStation2Build.sh does, then its
# logo_raylib_anim sample to show the toolchain makes a working ELF:
#
#   packaging/ps2/build-deps.sh [WORK_DIR]
#
# - raylib4Consoles/ps2gl: libps2gl.a and its headers.
# - raylib4Consoles/raylib, branch raylib4Consoles_6.0, built with
#   PLATFORM=PLATFORM_PLAYSTATION2: libraylib.a, raylib.h, rlgl.h ...
#
# Every repository is pinned to the commit this was tested with, so
# a build is the same build next month. PlayStation2Build.sh itself
# does not build with the current ps2dev toolchain (GCC 15), because
# the raylib4Consoles_6.0 Makefile:
#  - leaves out $PS2SDK/ports/include, so <GL/gl.h> is not found;
#  - has no PS2 install PREFIX, so "make install" writes to /lib;
#  - compiles the C sources as C++ (needed: the PS2 backend includes
#    ps2stuff's C++ headers), which GCC 15 rejects in three places.
# EXTRA_INCLUDE_PATHS, PREFIX and raylib4Consoles_6.0-ps2.patch
# (next to this script) take care of those.
#
# Needs $PS2DEV and $PS2SDK set and the ps2dev tools on $PATH, as in
# the ps2dev/ps2dev Docker image (see Dockerfile), plus git, make and
# GNU cp (raylib's install uses cp --update).

set -eu

: "${PS2SDK:?set PS2SDK (and PS2DEV) to the ps2dev toolchain first}"
: "${PS2DEV:?set PS2DEV (and PS2SDK) to the ps2dev toolchain first}"

here=$(cd "$(dirname "$0")" && pwd)
work=${1:-$here/../../build-ps2/deps}
mkdir -p "$work"
work=$(cd "$work" && pwd)
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

RAYLIB4PS2_REPO=https://github.com/raylib4Consoles/raylib4PlayStation2
RAYLIB4PS2_SHA=2d0cf82dbae7d0a2b670f57c3a8f6373542225c7	# 2026-04-22
PS2GL_REPO=https://github.com/raylib4Consoles/ps2gl
PS2GL_SHA=3edd2e158c9fb4c3960657c4f313280acb73c9d5
RAYLIB_REPO=https://github.com/raylib4Consoles/raylib
RAYLIB_SHA=8d41c3edecbbd8baa8df7d2fb02d59006b453af6	# raylib4Consoles_6.0

# fetch DIR REPO SHA: a clean checkout of exactly that commit.
fetch ()
{
    rm -rf "$1"
    git init -q "$1"
    git -C "$1" fetch -q --depth 1 "$2" "$3"
    git -C "$1" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

echo "== raylib4PlayStation2 $RAYLIB4PS2_SHA"
fetch "$work/raylib4PlayStation2" "$RAYLIB4PS2_REPO" "$RAYLIB4PS2_SHA"

echo "== ps2gl $PS2GL_SHA"
fetch "$work/ps2gl" "$PS2GL_REPO" "$PS2GL_SHA"
make -C "$work/ps2gl" -j"$jobs"
make -C "$work/ps2gl" install

echo "== raylib $RAYLIB_SHA (PLATFORM_PLAYSTATION2)"
fetch "$work/raylib" "$RAYLIB_REPO" "$RAYLIB_SHA"
git -C "$work/raylib" apply "$here/raylib4Consoles_6.0-ps2.patch"
# -Umips: GCC predefines "mips" in GNU C++ mode, and rltexgpu.h has
# a parameter of that name.
make -C "$work/raylib/src" -j"$jobs" PLATFORM=PLATFORM_PLAYSTATION2 \
    EXTRA_INCLUDE_PATHS="-I$PS2SDK/ports/include -I$PS2SDK/ee/include \
-I$PS2SDK/common/include -D_EE -G0 -DNO_VU0_VECTORS -DNO_ASM -Umips \
-fpermissive -std=gnu++20"
make -C "$work/raylib/src" PLATFORM=PLATFORM_PLAYSTATION2 \
    PREFIX="$PS2SDK/ports" install

echo "== sample: shapes/logo_raylib_anim"
sample=$work/raylib4PlayStation2/samples/shapes/logo_raylib_anim
make -C "$sample" clean
make -C "$sample"
ls -l "$sample/raylib.elf"

echo "raylib4PlayStation2 installed in $PS2SDK/ports"
