#!/bin/sh
# Builds raylibdoom-<version>-1-x86_64.pkg.tar.zst and its .sha256
# from the checked-out commit, with packaging/arch/PKGBUILD. Run as a
# normal user (makepkg refuses root) with base-devel and cmake
# installed. Extra makepkg options can be given in MAKEPKG_FLAGS.
#
#   packaging/arch/make-package.sh VERSION OUT_DIR
#
# The PKGBUILD downloads the v$pkgver release tarball. Here a copy of
# it gets pkgver=VERSION and the checksum of a git archive of HEAD,
# which is put where makepkg looks before downloading. raylib is still
# downloaded and checked against the PKGBUILD's checksum.

set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 VERSION OUT_DIR" >&2
    exit 2
fi

version=$1
mkdir -p "$2"
out=$(cd "$2" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)

# pkgver: no hyphen, colon, slash or whitespace.
if ! printf '%s\n' "$version" | grep -Eq '^[0-9][A-Za-z0-9.+_]*$'; then
    echo "not a valid package version: $version" >&2
    exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

: "${SOURCE_DATE_EPOCH:=$(git -C "$top" log -1 --format=%ct)}"
export SOURCE_DATE_EPOCH

tarball=raylib_DOOM-$version.tar.gz
git -C "$top" archive --format=tar.gz --prefix="raylib_DOOM-$version/" \
    -o "$work/$tarball" HEAD
sum=$(sha256sum "$work/$tarball" | cut -d' ' -f1)

# The first sha256sums entry is the raylib_DOOM tarball.
sed -e "s/^pkgver=.*/pkgver=$version/" \
    -e "s/^sha256sums=('[0-9a-f]*'/sha256sums=('$sum'/" \
    "$here/PKGBUILD" > "$work/PKGBUILD"
grep -q "^sha256sums=('$sum'" "$work/PKGBUILD" ||
    { echo "could not set the source checksum" >&2; exit 1; }

(cd "$work" && PKGDEST="$out" SRCDEST="$work" makepkg --noconfirm --cleanbuild ${MAKEPKG_FLAGS:-})

pkg=raylibdoom-$version-1-x86_64.pkg.tar.zst
[ -f "$out/$pkg" ] || { echo "makepkg did not make $pkg" >&2; exit 1; }
cd "$out"
sha256sum "$pkg" > "$pkg.sha256"
cat "$pkg.sha256"
