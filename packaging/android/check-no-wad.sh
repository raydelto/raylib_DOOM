#!/bin/sh
# Fails if an APK holds game data: any entry whose name ends in .wad
# (any case) or another lump or game-data container extension, or
# whose first four bytes are a WAD header ("IWAD" or "PWAD"), so a
# renamed WAD is caught too.
#
#   packaging/android/check-no-wad.sh APK...
#
# The release APK must never redistribute game data, commercial or
# free; the player supplies their own WADs (see "Android" in README.md).

set -eu

[ $# -gt 0 ] || { echo "usage: $0 APK..." >&2; exit 2; }

status=0
for apk in "$@"; do
    [ -f "$apk" ] || { echo "$apk: no such file" >&2; exit 2; }
    # zipinfo -1: one entry name per line, nothing else.
    names=$(zipinfo -1 "$apk") || { echo "$apk: not a zip file" >&2; exit 2; }

    bad=$(printf '%s\n' "$names" |
          grep -Ei '\.(wad|iwad|pwad|pk3|pk4|pk7|pke|ipk3|ipk7|lmp|gwa|wad\.(gz|xz|zip|bz2))$' || true)

    # Every file entry's first four bytes.
    magic=$(printf '%s\n' "$names" | while IFS= read -r name; do
        case $name in */) continue ;; esac
        head=$(unzip -p "$apk" "$name" 2>/dev/null | head -c 4 | tr -d '\0' || true)
        case $head in IWAD|PWAD) printf '%s (%s header)\n' "$name" "$head" ;; esac
    done)

    if [ -n "$bad$magic" ]; then
        echo "$apk: game data in the APK:" >&2
        [ -z "$bad" ] || printf '%s\n' "$bad" | sed 's/^/  /' >&2
        [ -z "$magic" ] || printf '%s\n' "$magic" | sed 's/^/  /' >&2
        status=1
    else
        echo "$apk: no WAD ($(printf '%s\n' "$names" | wc -l) entries checked)"
    fi
done
exit $status
