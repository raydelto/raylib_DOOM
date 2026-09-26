#!/bin/sh
# Builds raylibdoom-<version>-windows-x64.zip and its .sha256 file
# from an already built raylibdoom.exe. Run it in an MSYS2 MINGW64
# shell (it needs objdump and zip).
#
#   packaging/windows/make-package.sh VERSION BUILD_DIR OUT_DIR
#
# BUILD_DIR is the CMake build directory: the executable is taken from
# there, and the raylib and GLFW licenses from the raylib source
# FetchContent put in it.

set -eu

if [ $# -ne 3 ]; then
    echo "usage: $0 VERSION BUILD_DIR OUT_DIR" >&2
    exit 2
fi

version=$1
build=$(cd "$2" && pwd)
mkdir -p "$3"
out=$(cd "$3" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
raylib=$build/_deps/raylib-src

case $version in
    [0-9]*) ;;
    *) echo "version must start with a digit: $version" >&2; exit 2 ;;
esac

for f in "$build/raylibdoom.exe" "$raylib/LICENSE" \
         "$raylib/src/external/glfw/LICENSE.md"; do
    [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done

# The zip must run on a bare Windows install: the executable may only
# import DLLs that ship with Windows. A MinGW DLL here (libgcc_s_seh-1,
# libwinpthread-1, libraylib, ...) means the static link went wrong.
dlls=$(objdump -p "$build/raylibdoom.exe" | sed -n 's/^[[:space:]]*DLL Name: //p' |
       tr 'A-Z' 'a-z' | tr -d '\r' | sort -u)
echo "raylibdoom.exe imports:" $dlls
for dll in $dlls; do
    case $dll in
        kernel32.dll|user32.dll|gdi32.dll|shell32.dll|winmm.dll|ws2_32.dll|\
        msvcrt.dll|advapi32.dll|ole32.dll|opengl32.dll|api-ms-win-*.dll|\
        ucrtbase.dll) ;;
        *) echo "not a Windows system DLL: $dll" >&2; exit 1 ;;
    esac
done

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

# Every file gets the commit's timestamp, so the same commit gives
# the same zip.
: "${SOURCE_DATE_EPOCH:=$(git -C "$top" log -1 --format=%ct 2>/dev/null || date +%s)}"

name=raylibdoom-$version-windows-x64
tree=$stage/$name
mkdir -p "$tree"
cp "$build/raylibdoom.exe" "$tree/raylibdoom.exe"
strip --strip-unneeded "$tree/raylibdoom.exe"

# Text files get CRLF line endings so Notepad on older Windows shows
# them properly.
crlf() { sed 's/\r$//; s/$/\r/' "$1" > "$2"; }
crlf "$here/README.txt" "$tree/README.txt"
crlf "$here/THIRD-PARTY-NOTICES.txt" "$tree/THIRD-PARTY-NOTICES.txt"
crlf "$top/LICENSE.TXT" "$tree/LICENSE.TXT"
crlf "$top/linuxdoom-1.10/opl3-LICENSE.txt" "$tree/opl3-LICENSE.txt"
crlf "$raylib/LICENSE" "$tree/raylib-LICENSE.txt"
crlf "$raylib/src/external/glfw/LICENSE.md" "$tree/glfw-LICENSE.md"
crlf "$here/mingw-w64-runtime-LICENSE.txt" "$tree/mingw-w64-runtime-LICENSE.txt"
crlf "$here/gcc-RUNTIME-EXCEPTION.txt" "$tree/gcc-RUNTIME-EXCEPTION.txt"

find "$tree" -exec touch -d "@$SOURCE_DATE_EPOCH" {} +

zipfile=$name.zip
rm -f "$out/$zipfile"
(cd "$stage" && find "$name" -type f | LC_ALL=C sort |
    TZ=UTC zip -X -9 -q "$out/$zipfile" -@)

cd "$out"
sha256sum "$zipfile" | sed 's/ [*]/  /' > "$zipfile.sha256"
cat "$zipfile.sha256"
