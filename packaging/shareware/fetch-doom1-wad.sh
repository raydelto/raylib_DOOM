#!/bin/sh
# Puts the shareware DOOM1.WAD (DOOM 1.9, episode 1) at FILE, for the
# release packages, and fails unless it is exactly that file: an IWAD
# of 4,196,020 bytes with the SHA-256 below.
#
#   packaging/shareware/fetch-doom1-wad.sh FILE
#
# With DOOM1_WAD set to an existing copy, that copy is checked and
# used; otherwise it is downloaded from DOOM1_WAD_URL (default: the
# project's copy on Google Drive). FILE must be outside the checkout:
# the WAD never goes into git. Only the release workflows run this,
# and only for a tag (or a workflow_dispatch asking for the shareware
# build); everything else stays WAD-free.

set -eu

[ $# -eq 1 ] || { echo "usage: $0 FILE" >&2; exit 2; }
out=$1

sha256=1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771
size=4196020
: "${DOOM1_WAD_URL:=https://drive.usercontent.google.com/download?id=1aJU3Pq9tVWeL5OAv04wamhk8DhWXYeZF&export=download&confirm=t}"

top=$(cd "$(dirname "$0")/../.." && pwd -P)
mkdir -p "$(dirname "$out")"
dir=$(cd "$(dirname "$out")" && pwd -P)
case $dir/ in
    "$top"/*) echo "$out: keep the WAD outside the checkout ($top)" >&2; exit 2 ;;
esac

tmp=$out.part
rm -f "$tmp"
if [ -n "${DOOM1_WAD:-}" ]; then
    cp "$DOOM1_WAD" "$tmp"
else
    curl -fsSL --retry 3 -o "$tmp" "$DOOM1_WAD_URL"
fi

got_size=$(wc -c < "$tmp" | tr -d ' ')
got_sum=$( (sha256sum "$tmp" 2>/dev/null || shasum -a 256 "$tmp") | cut -d' ' -f1)
if [ "$got_size" != "$size" ] || [ "$got_sum" != "$sha256" ] ||
   [ "$(head -c 4 "$tmp")" != IWAD ]; then
    echo "not the shareware DOOM1.WAD: $got_size bytes, SHA-256 $got_sum" \
         "(want $size bytes, $sha256)" >&2
    rm -f "$tmp"
    exit 1
fi
mv "$tmp" "$out"
echo "DOOM1.WAD: $out ($got_size bytes, SHA-256 $got_sum)"
