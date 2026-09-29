#!/bin/sh
# Builds build-ps2/DOOM.ELF, DOOM for the PlayStation 2:
#
#   packaging/ps2/build.sh [--deps]
#
# Builds raylib4PlayStation2 first (build-deps.sh) if it is not in
# $PS2SDK/ports yet, or with --deps. Run it in the image made from
# the Dockerfile next to it, which has everything; see README.txt.

set -eu

: "${PS2SDK:?set PS2SDK (and PS2DEV) to the ps2dev toolchain first}"

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

if [ "${1:-}" = --deps ] || [ ! -f "$PS2SDK/ports/lib/libraylib.a" ]; then
    "$here/build-deps.sh" "$top/build-ps2/deps"
fi

make -C "$top/linuxdoom-1.10" -f Makefile.ps2 clean
make -C "$top/linuxdoom-1.10" -f Makefile.ps2 -j"$jobs"

mkdir -p "$top/build-ps2"
cp "$top/linuxdoom-1.10/ps2/DOOM.ELF" "$top/build-ps2/DOOM.ELF"
ls -l "$top/build-ps2/DOOM.ELF"
