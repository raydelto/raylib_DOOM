#!/bin/sh
# Tests check-no-wad.sh with small zip files standing in for APKs:
# a clean one passes; one with a .WAD, one with a renamed WAD (an
# IWAD header under another name) and one with a .pk3 fail.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

mkdir -p clean/assets/licenses clean/lib
echo 'GPL' > clean/assets/licenses/COPYING-GPL-2.0.txt
printf '\177ELF....IWAD....' > clean/lib/libraylibdoom.so
(cd clean && zip -qr ../clean.apk .)

fail=0
expect() {
    want=$1; shift
    if sh "$here/check-no-wad.sh" "$@" > out.txt 2>&1; then got=pass; else got=fail; fi
    if [ "$got" != "$want" ]; then
        echo "FAIL: $* should $want, did $got:" >&2
        cat out.txt >&2
        fail=1
    else
        echo "ok: $* -> $want"
    fi
}

expect pass clean.apk

cp clean.apk upper.apk
mkdir -p a/assets && printf 'PWAD\0\0\0\0\0\0\0\0' > a/assets/DOOM1.WAD
(cd a && zip -q ../upper.apk assets/DOOM1.WAD)
expect fail upper.apk

cp clean.apk renamed.apk
mkdir -p b/assets && printf 'IWAD\1\0\0\0\14\0\0\0' > b/assets/data.bin
(cd b && zip -q ../renamed.apk assets/data.bin)
expect fail renamed.apk

cp clean.apk pk3.apk
mkdir -p c/assets && echo x > c/assets/game.pk3
(cd c && zip -q ../pk3.apk assets/game.pk3)
expect fail pk3.apk

expect fail clean.apk upper.apk

exit $fail
