#!/bin/sh
# Fails unless each PACKAGE holds the shareware DOOM1.WAD, unmodified,
# where that package's game looks for it, and DOOM1-NOTICE.txt:
#
#   .deb           /usr/share/raylibdoom/doom1.wad
#   .pkg.tar.zst   /usr/share/raylibdoom/doom1.wad
#   .tar.gz        doom1.wad next to raylibdoom
#   .zip           DOOM1.WAD next to raylibdoom.exe
#   .apk           assets/doom1.wad
#
#   packaging/shareware/check-wad-in-package.sh PACKAGE...
#
# The release workflows run it on a tag before uploading, so no
# platform's package goes out without the WAD. (The .dmg is checked by
# release-macos.yml on the Mac that builds it.)

set -eu

[ $# -gt 0 ] || { echo "usage: $0 PACKAGE..." >&2; exit 2; }

want=1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771

sum() { (sha256sum 2>/dev/null || shasum -a 256) | cut -d' ' -f1; }

status=0
for pkg in "$@"; do
    [ -f "$pkg" ] || { echo "$pkg: no such file" >&2; exit 2; }
    case $pkg in
        *.deb)
            list=$(dpkg-deb --fsys-tarfile "$pkg" | tar -tf -)
            wad=./usr/share/raylibdoom/doom1.wad
            notice=./usr/share/doc/raylibdoom/DOOM1-NOTICE.txt
            got=$(dpkg-deb --fsys-tarfile "$pkg" | tar -xOf - "$wad" | sum) ;;
        *.pkg.tar.zst)
            list=$(bsdtar -tf "$pkg")
            wad=usr/share/raylibdoom/doom1.wad
            notice=usr/share/licenses/raylibdoom/DOOM1-NOTICE.txt
            got=$(bsdtar -xOf "$pkg" "$wad" | sum) ;;
        *.tar.gz)
            list=$(tar -tzf "$pkg")
            top=$(printf '%s\n' "$list" | head -1 | cut -d/ -f1)
            wad=$top/doom1.wad
            notice=$top/DOOM1-NOTICE.txt
            got=$(tar -xzOf "$pkg" "$wad" | sum) ;;
        *.zip)
            list=$(zipinfo -1 "$pkg")
            top=$(printf '%s\n' "$list" | head -1 | cut -d/ -f1)
            wad=$top/DOOM1.WAD
            notice=$top/DOOM1-NOTICE.txt
            got=$(unzip -p "$pkg" "$wad" | sum) ;;
        *.apk)
            list=$(zipinfo -1 "$pkg")
            wad=assets/doom1.wad
            notice=assets/licenses/THIRD-PARTY-NOTICES.txt
            got=$(unzip -p "$pkg" "$wad" | sum) ;;
        *) echo "$pkg: unknown package type" >&2; exit 2 ;;
    esac
    if [ "$got" != "$want" ]; then
        echo "$pkg: $wad is missing or not the shareware DOOM1.WAD ($got)" >&2
        status=1
    elif ! printf '%s\n' "$list" | grep -qxF "$notice"; then
        echo "$pkg: no $notice" >&2
        status=1
    else
        echo "$pkg: $wad OK ($got)"
    fi
done
exit $status
