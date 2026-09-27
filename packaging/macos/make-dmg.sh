#!/bin/sh
# Builds raylibdoom-<version>-macos-arm64.dmg and its .sha256 from an
# already built binary.
#
#   packaging/macos/make-dmg.sh VERSION BUILD_DIR OUT_DIR
#
# BUILD_DIR is the CMake build directory: the binary is taken from
# there, and the raylib and GLFW licenses from the raylib source
# FetchContent put in it.
#
# Signing is ad-hoc (codesign -s -) unless MACOS_SIGN_IDENTITY names a
# Developer ID Application identity in the keychain. The app and dmg
# are then signed with it, with the hardened runtime and a timestamp,
# and when MACOS_NOTARY_APPLE_ID, MACOS_NOTARY_TEAM_ID and
# MACOS_NOTARY_PASSWORD (an app-specific password) are set too, the
# dmg is notarized and the ticket stapled.

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
identity=${MACOS_SIGN_IDENTITY:-}

case $version in
    [0-9]*) ;;
    *) echo "version must start with a digit: $version" >&2; exit 2 ;;
esac
# Info.plist versions are dot-separated numbers only.
short=$(printf '%s' "$version" | sed 's/[^0-9.].*//; s/\.$//')

for f in "$build/raylibdoom" "$raylib/LICENSE" \
         "$raylib/src/external/glfw/LICENSE.md"; do
    [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done

archs=$(lipo -archs "$build/raylibdoom")
[ "$archs" = arm64 ] || { echo "binary is '$archs', not arm64" >&2; exit 1; }

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
umask 022

name=raylibdoom-$version-macos-arm64
root=$stage/root
app=$root/raylibDOOM.app
contents=$app/Contents
licenses=$contents/Resources/licenses
mkdir -p "$contents/MacOS" "$licenses"

sed -e "s/@VERSION@/$version/" -e "s/@SHORT_VERSION@/$short/" \
    "$here/Info.plist.in" > "$contents/Info.plist"
plutil -lint "$contents/Info.plist"
printf 'APPL????' > "$contents/PkgInfo"

cp "$build/raylibdoom" "$contents/MacOS/raylibdoom"
strip -x "$contents/MacOS/raylibdoom"
chmod 0755 "$contents/MacOS/raylibdoom"

# The icon, at every size iconutil wants.
iconset=$stage/raylibDOOM.iconset
mkdir "$iconset"
python3 "$here/make-icon.py" "$stage/icon.png"
for s in 16 32 128 256 512; do
    sips -z $s $s "$stage/icon.png" --out "$iconset/icon_${s}x${s}.png" >/dev/null
    d=$((s * 2))
    sips -z $d $d "$stage/icon.png" --out "$iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns -o "$contents/Resources/raylibDOOM.icns" "$iconset"

for dir in "$licenses" "$root"; do
    cp "$top/LICENSE.TXT" "$dir/LICENSE.TXT"
    cp "$top/linuxdoom-1.10/opl3-LICENSE.txt" "$dir/opl3-LICENSE.txt"
    cp "$raylib/LICENSE" "$dir/raylib-LICENSE.txt"
    cp "$raylib/src/external/glfw/LICENSE.md" "$dir/glfw-LICENSE.md"
    cp "$here/THIRD-PARTY-NOTICES.txt" "$dir/THIRD-PARTY-NOTICES.txt"
done
cp "$here/README.txt" "$contents/Resources/README.txt"
cp "$here/README.txt" "$root/README.txt"
ln -s /Applications "$root/Applications"

if [ -n "$identity" ]; then
    codesign --force --options runtime --timestamp -s "$identity" "$app"
else
    codesign --force -s - "$app"
fi
codesign --verify --strict --verbose=2 "$app"
codesign -dv "$app" 2>&1 | grep -E '^(Identifier|Format|Signature|TeamIdentifier)'

dmg=$out/$name.dmg
rm -f "$dmg"
hdiutil create -volname "raylibDOOM $version" -srcfolder "$root" \
    -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$dmg"

if [ -n "$identity" ]; then
    codesign --force --timestamp -s "$identity" "$dmg"
    if [ -n "${MACOS_NOTARY_APPLE_ID:-}" ] && [ -n "${MACOS_NOTARY_TEAM_ID:-}" ] &&
       [ -n "${MACOS_NOTARY_PASSWORD:-}" ]; then
        xcrun notarytool submit "$dmg" --wait \
            --apple-id "$MACOS_NOTARY_APPLE_ID" \
            --team-id "$MACOS_NOTARY_TEAM_ID" \
            --password "$MACOS_NOTARY_PASSWORD"
        xcrun stapler staple "$dmg"
    fi
fi
hdiutil verify "$dmg"

cd "$out"
shasum -a 256 "$name.dmg" > "$name.dmg.sha256"
cat "$name.dmg.sha256"
