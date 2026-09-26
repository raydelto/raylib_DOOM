#!/bin/sh
# Builds raylibdoom_<version>_amd64.deb and
# raylibdoom-<version>-linux-x86_64.tar.gz, each with a .sha256 file,
# from an already built binary.
#
#   packaging/linux/make-packages.sh VERSION BUILD_DIR OUT_DIR
#
# BUILD_DIR is the CMake build directory: the binary is taken from
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

for f in "$build/raylibdoom" "$raylib/LICENSE" \
         "$raylib/src/external/glfw/LICENSE.md"; do
    [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
umask 022

# Everything in the packages has a fixed timestamp, so the same
# commit gives the same archives.
: "${SOURCE_DATE_EPOCH:=$(git -C "$top" log -1 --format=%ct 2>/dev/null || date +%s)}"
export SOURCE_DATE_EPOCH

# The binary links only glibc; GLFW and miniaudio dlopen libX11,
# libGL and libasound at run time. The libc6 version comes from the
# newest glibc symbol version the binary uses. libasound2t64 goes
# first: on 24.04 "libasound2" is only a virtual package, and apt
# satisfies it with liboss4-salsa-asound2, which has no libasound.so.2.
bin=$stage/raylibdoom
cp "$build/raylibdoom" "$bin"
strip --strip-unneeded --remove-section=.comment --remove-section=.note "$bin"
chmod 0755 "$bin"
glibc=$(objdump -T "$bin" | sed -n 's/.*GLIBC_\([0-9][0-9.]*\).*/\1/p' |
        sort -t. -k1,1n -k2,2n -k3,3n | tail -1)
[ -n "$glibc" ] || { echo "no glibc symbol versions found" >&2; exit 1; }
echo "binary needs glibc >= $glibc"


# --- .deb -------------------------------------------------------------

deb=$stage/deb
doc=$deb/usr/share/doc/raylibdoom
install -d "$deb/DEBIAN" "$deb/usr/games" "$doc" \
    "$deb/usr/share/applications" \
    "$deb/usr/share/icons/hicolor/scalable/apps" \
    "$deb/usr/share/man/man6"
install -m 0755 "$bin" "$deb/usr/games/raylibdoom"
install -m 0644 "$here/raylibdoom.desktop" "$deb/usr/share/applications/"
install -m 0644 "$here/raylibdoom.svg" \
    "$deb/usr/share/icons/hicolor/scalable/apps/"
gzip -9n < "$here/raylibdoom.6" > "$deb/usr/share/man/man6/raylibdoom.6.gz"
install -m 0644 "$here/copyright" "$doc/copyright"
gzip -9n < "$here/README.txt" > "$doc/README.gz"

date=$(date -u -R -d "@$SOURCE_DATE_EPOCH")
maintainer="Raydelto Hernandez <raydelto@gmail.com>"
# The version has no Debian revision, so this is a native package,
# whose changelog is changelog.gz.
gzip -9n > "$doc/changelog.gz" <<EOF
raylibdoom ($version) unstable; urgency=medium

  * Release $version.
    https://github.com/raydelto/raylib_DOOM/releases

 -- $maintainer  $date
EOF
chmod 0644 "$doc"/*.gz "$deb/usr/share/man/man6/raylibdoom.6.gz"

size=$(du -k -s --apparent-size "$deb/usr" | cut -f1)
cat > "$deb/DEBIAN/control" <<EOF
Package: raylibdoom
Version: $version
Architecture: amd64
Maintainer: $maintainer
Installed-Size: $size
Depends: libc6 (>= $glibc), libgl1, libx11-6, libasound2t64 | libasound2
Recommends: libxcursor1, libxi6, libxinerama1, libxrandr2
Suggests: freedoom
Section: games
Priority: optional
Homepage: https://github.com/raydelto/raylib_DOOM
Description: DOOM (1997 Linux source release) running on raylib
 The 1997 Linux DOOM source release, ported from X11 and OSS to raylib
 and made to run on 64-bit systems. The software renderer is unchanged;
 raylib opens the window, scales the 320x200 picture, reads the keyboard
 and mouse, and plays the sound effects and OPL music.
 .
 No game data is included. The game looks for an IWAD in \$DOOMWADDIR,
 the current directory and /usr/share/games/doom, where the freedoom
 package installs Freedoom. The shareware doom1.wad works too.
EOF
(cd "$deb" && find usr -type f | LC_ALL=C sort | xargs md5sum) \
    > "$deb/DEBIAN/md5sums"
chmod 0644 "$deb/DEBIAN/control" "$deb/DEBIAN/md5sums"
find "$deb" -type d -exec chmod 0755 {} +
find "$deb" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +

debfile=raylibdoom_${version}_amd64.deb
dpkg-deb --root-owner-group -Zxz --build "$deb" "$out/$debfile"


# --- .tar.gz ------------------------------------------------------------

name=raylibdoom-$version-linux-x86_64
tree=$stage/$name
install -d "$tree"
install -m 0755 "$bin" "$tree/raylibdoom"
install -m 0644 "$here/README.txt" "$tree/README.txt"
install -m 0644 "$top/LICENSE.TXT" "$tree/LICENSE.TXT"
install -m 0644 "$top/linuxdoom-1.10/opl3-LICENSE.txt" "$tree/opl3-LICENSE.txt"
install -m 0644 "$raylib/LICENSE" "$tree/raylib-LICENSE.txt"
install -m 0644 "$raylib/src/external/glfw/LICENSE.md" "$tree/glfw-LICENSE.md"
install -m 0644 "$here/copyright" "$tree/THIRD-PARTY-NOTICES.txt"
install -m 0644 "$here/raylibdoom.desktop" "$tree/raylibdoom.desktop"
install -m 0644 "$here/raylibdoom.svg" "$tree/raylibdoom.svg"
install -m 0644 "$here/raylibdoom.6" "$tree/raylibdoom.6"

tarfile=$name.tar.gz
tar -C "$stage" --sort=name --owner=0 --group=0 --numeric-owner \
    --mtime="@$SOURCE_DATE_EPOCH" -cf - "$name" | gzip -9n > "$out/$tarfile"


cd "$out"
for f in "$debfile" "$tarfile"; do
    sha256sum "$f" > "$f.sha256"
    cat "$f.sha256"
done
