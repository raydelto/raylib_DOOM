#!/bin/sh
# Builds the signed release APK with the shareware DOOM1.WAD in it:
# raylibdoom-<version>-android-shareware.apk and its .sha256, in OUTDIR.
#
#   ANDROID_KEYSTORE_PASSWORD=... ANDROID_KEY_ALIAS=... ANDROID_KEY_PASSWORD=... \
#   packaging/android/build-shareware.sh VERSION /path/to/DOOM1.WAD KEYSTORE OUTDIR
#
# The WAD must be outside the checkout and is checked by its SHA-256
# (android/app/build.gradle, -PbundleWad); it never goes into git or
# CI. The default build, and release-android.yml, stay WAD-free. The
# About screen links to the source at the commit built.

set -eu

[ $# -eq 4 ] || { echo "usage: $0 VERSION DOOM1.WAD KEYSTORE OUTDIR" >&2; exit 2; }
version=$1
wad=$(realpath "$2")
keystore=$(realpath "$3")
out=$4
: "${ANDROID_KEYSTORE_PASSWORD:?}" "${ANDROID_KEY_ALIAS:?}" "${ANDROID_KEY_PASSWORD:?}"

top=$(cd "$(dirname "$0")/../.." && pwd)
commit=$(git -C "$top" rev-parse HEAD)
build_tools=$(ls -d "${ANDROID_HOME:?}"/build-tools/* | sort -V | tail -1)

cd "$top/android"
./gradlew --no-daemon clean assembleFlatRelease -PappVersion="$version" \
    -PsourceRef="$commit" -PbundleWad="$wad" -PreleaseKeystore="$keystore"

apk=app/build/outputs/apk/flat/release/app-flat-release.apk
"$build_tools/apksigner" verify --print-certs "$apk"
if "$build_tools/apksigner" verify --print-certs "$apk" | grep -q 'CN=Android Debug'; then
    echo "$apk: signed with the debug key" >&2
    exit 1
fi
# The WAD in the APK is the one given, unmodified.
[ "$(unzip -p "$apk" assets/doom1.wad | sha256sum | cut -d' ' -f1)" = \
  "$(sha256sum "$wad" | cut -d' ' -f1)" ]

mkdir -p "$out"
name=raylibdoom-$version-android-shareware.apk
cp "$apk" "$out/$name"
(cd "$out" && sha256sum "$name" > "$name.sha256" && cat "$name.sha256")
