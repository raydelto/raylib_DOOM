#!/bin/sh
# Builds a signed-ready source package for the Launchpad PPA
# (ppa:raydelto/raylibdoom), one Ubuntu series at a time:
#
#   packaging/ppa/make-source.sh SERIES ORIG_TARBALL OUT_DIR
#
# SERIES is an Ubuntu series such as jammy, noble or resolute.
# ORIG_TARBALL is the Debian orig tarball,
# raylibdoom_VERSION.orig.tar.xz (uscan --repack, or the one on
# mentors.debian.net). The result in OUT_DIR is what
# "debuild -S -sa" would make, unsigned: sign and upload it with
#
#   debsign OUT_DIR/raylibdoom_*_source.changes
#   dput ppa:raydelto/raylibdoom OUT_DIR/raylibdoom_*_source.changes
#
# The PPA uses debian/ unchanged except that Ubuntu releases before
# 26.10 have no libraylib-dev: raylib 5.5, the version the release
# packages use, goes in as a second orig tarball (component
# "raylib", downloaded here, never on Launchpad's builders, which
# have no network) and is linked in statically. The Recommends are
# the same as in Debian, so "apt install raylibdoom" brings Freedoom
# and "apt install raylibdoom doom-wad-shareware" the shareware DOOM.
#
# PPA_REVISION (default 1) numbers uploads of the same version. The
# version is the Debian one with ~ppaN~SERIES1 after it, so the
# Ubuntu archive's own package, once there, replaces it.

set -eu

if [ $# -ne 3 ]; then
    echo "usage: $0 SERIES ORIG_TARBALL OUT_DIR" >&2
    exit 2
fi

series=$1
orig=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
mkdir -p "$3"
out=$(cd "$3" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
raylib_tag=5.5
raylib_sha256=aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e

case $series in
    jammy) series_version=22.04 ;;
    noble) series_version=24.04 ;;
    resolute) series_version=26.04 ;;
    *) echo "unknown series: $series" >&2; exit 2 ;;
esac

case $(basename "$orig") in
    raylibdoom_*.orig.tar.xz) ;;
    *) echo "not an orig tarball: $orig" >&2; exit 2 ;;
esac
[ -f "$orig" ] || { echo "missing $orig" >&2; exit 1; }
upstream=$(basename "$orig" .orig.tar.xz)
upstream=${upstream#raylibdoom_}

debian_version=$(dpkg-parsechangelog -l "$top/debian/changelog" -S Version)
case $debian_version in
    "$upstream"-*) ;;
    *) echo "debian/changelog is $debian_version, not $upstream-N" >&2; exit 1 ;;
esac
version=$debian_version~ppa${PPA_REVISION:-1}~${series}1

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
src=$stage/raylibdoom-$upstream

# raylib's source, cut down to what builds the library: no examples
# or projects, whose assets carry licenses of their own.
curl -fsSL -o "$stage/raylib.tar.gz" \
    "https://github.com/raysan5/raylib/archive/refs/tags/$raylib_tag.tar.gz"
echo "$raylib_sha256  $stage/raylib.tar.gz" | sha256sum -c --quiet
tar -xzf "$stage/raylib.tar.gz" -C "$stage"
mkdir "$stage/raylib"
for f in CMakeLists.txt CMakeOptions.txt LICENSE README.md raylib.pc.in \
         cmake src; do
    mv "$stage/raylib-$raylib_tag/$f" "$stage/raylib/"
done
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 \
    -C "$stage" -cJf "$stage/raylibdoom_$upstream.orig-raylib.tar.xz" raylib
cp "$orig" "$stage/raylibdoom_$upstream.orig.tar.xz"

mkdir "$src"
tar -xJf "$orig" -C "$src" --strip-components=1
mv "$stage/raylib" "$src/raylib"
cp -a "$top/debian" "$src/debian"
cd "$src"

# No libraylib-dev: build raylib's own dependencies instead, and
# depend on what it opens at run time.
python3 - "$series" <<'EOF'
import re, sys
path = "debian/control"
s = open(path).read()
s = s.replace(" libraylib-dev (>= 5.5),\n",
              " libasound2-dev,\n libgl-dev,\n libx11-dev,\n"
              " libxcursor-dev,\n libxi-dev,\n libxinerama-dev,\n"
              " libxrandr-dev,\n")
s = s.replace(" ${shlibs:Depends},\n",
              " ${shlibs:Depends},\n libasound2t64 | libasound2,\n"
              " libgl1,\n libx11-6,\n libxcursor1,\n libxi6,\n"
              " libxinerama1,\n libxrandr2,\n")
s = re.sub(r"^Architecture: .*$", "Architecture: amd64 arm64", s,
           count=1, flags=re.M)
s = s.replace("Maintainer: Debian Games Team "
              "<pkg-games-devel@lists.alioth.debian.org>\n"
              "Uploaders: Raydelto Hernandez <raydelto@gmail.com>\n",
              "Maintainer: Raydelto Hernandez <raydelto@gmail.com>\n")
open(path, "w").write(s)
EOF
grep -q "libx11-dev" debian/control ||
    { echo "could not edit debian/control" >&2; exit 1; }

cat >> debian/rules <<'EOF'

# PPA: the raylib 5.5 component, linked in statically. Only the
# game's files are installed, not raylib's library and headers.
override_dh_auto_configure:
	dh_auto_configure -- \
	   -DFETCHCONTENT_SOURCE_DIR_RAYLIB=$(CURDIR)/raylib \
	   -DRAYLIB_DOOM_TEST_SANITIZERS=OFF \
	   -DRAYLIB_DOOM_INSTALL_BINDIR=games

include /usr/share/dpkg/architecture.mk
override_dh_auto_install:
	DESTDIR=$(CURDIR)/debian/raylibdoom \
	   cmake --install obj-$(DEB_HOST_GNU_TYPE) --component raylibdoom
EOF
# The new override_dh_auto_configure replaces the Debian one.
python3 - <<'EOF'
path = "debian/rules"
s = open(path).read()
first = s.index("override_dh_auto_configure:")
end = s.index("\n\n", first)
start = s.rindex("\n\n", 0, first)
s = s[:start] + s[end:]
open(path, "w").write(s)
EOF
grep -c "^override_dh_auto_configure:" debian/rules | grep -qx 1 ||
    { echo "could not edit debian/rules" >&2; exit 1; }

sed '/^#/d' "$here/copyright-raylib" > "$stage/copyright-raylib"
python3 - "$stage/copyright-raylib" <<'EOF'
import sys
path = "debian/copyright"
s = open(path).read()
extra = open(sys.argv[1]).read().strip("\n")
s = s.rstrip("\n") + "\n\n" + extra + "\n"
open(path, "w").write(s)
EOF

date=$(date -R)
{
    cat <<EOF
raylibdoom ($version) $series; urgency=medium

  * PPA build for Ubuntu $series_version, with raylib $raylib_tag built in.

 -- Raydelto Hernandez <raydelto@gmail.com>  $date

EOF
    cat debian/changelog
} > "$stage/changelog"
mv "$stage/changelog" debian/changelog

dpkg-buildpackage -S -sa -d -us -uc
cd "$stage"
mv raylibdoom_"$version"* raylibdoom_"$upstream".orig*.tar.xz "$out/"
ls -l "$out"
