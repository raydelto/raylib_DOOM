#!/bin/sh
# Installs, runs and removes a raylibdoom .deb on a clean Debian or
# Ubuntu system, as root. CI runs it in ubuntu:22.04 and ubuntu:24.04
# containers to check that the Depends resolve there.
#
#   packaging/linux/test-deb.sh raylibdoom_<version>_amd64.deb

set -eu

deb=$(realpath "$1")
export DEBIAN_FRONTEND=noninteractive

. /etc/os-release
echo "== $PRETTY_NAME"

apt-get update -qq
echo "== install"
apt-get install -y --no-install-recommends "$deb"

echo "== package files"
dpkg -L raylibdoom
test -x /usr/games/raylibdoom
test -f /usr/share/applications/raylibdoom.desktop
test -f /usr/share/icons/hicolor/scalable/apps/raylibdoom.svg

echo "== runtime libraries"
for lib in libc.so.6 libGL.so.1 libX11.so.6 libasound.so.2; do
    ldconfig -p | grep -q "$lib" || { echo "missing $lib" >&2; exit 1; }
    echo "found $lib"
done
ldd /usr/games/raylibdoom
if ldd /usr/games/raylibdoom | grep -q "not found"; then
    echo "unresolved shared libraries" >&2
    exit 1
fi

# Without an IWAD the game must stop before opening a window, with a
# message that says what to do.
echo "== run without an IWAD"
empty=$(mktemp -d)
status=0
(cd "$empty" && HOME=$empty DOOMWADDIR=$empty /usr/games/raylibdoom) \
    > "$empty/out.txt" 2>&1 || status=$?
cat "$empty/out.txt"
[ "$status" -ne 0 ] || { echo "expected a non-zero exit" >&2; exit 1; }
grep -q "No IWAD found" "$empty/out.txt"
grep -q "/usr/share/games/doom" "$empty/out.txt"
grep -q "freedoom" "$empty/out.txt"

echo "== remove"
apt-get purge -y raylibdoom
if dpkg -s raylibdoom > /dev/null 2>&1; then
    echo "raylibdoom still installed" >&2
    exit 1
fi
for f in /usr/games/raylibdoom /usr/share/applications/raylibdoom.desktop \
         /usr/share/icons/hicolor/scalable/apps/raylibdoom.svg \
         /usr/share/doc/raylibdoom; do
    [ ! -e "$f" ] || { echo "left behind: $f" >&2; exit 1; }
done
echo "== PASS on $PRETTY_NAME"
