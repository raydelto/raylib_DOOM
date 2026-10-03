## Android

`raylibdoom-@VERSION@-android-shareware.apk` runs on Android 8.0 or
newer (arm64 phones and tablets, and the x86_64 emulator). It
**includes the shareware DOOM1.WAD** (episode 1, unmodified), so it
plays right after install. DOOM shareware © id Software. This app is
not affiliated with id Software / ZeniMax / Microsoft. The engine is
GPL-2.0; *About / Licenses* links to this release's source code.

**Install:** download the APK on the device, open it and allow your
browser or file manager to install unknown apps when Android asks
(Settings > Apps > Special app access > Install unknown apps). Check
the download with `sha256sum -c raylibdoom-@VERSION@-android-shareware.apk.sha256`.

**Your own WADs:** tap *Add WAD files…* to pick an IWAD you have, such
as `freedoom1.wad` or `freedoom2.wad` from
<https://freedoom.github.io/download.html>, or `DOOM.WAD` / `DOOM2.WAD`
from your own copy of the game (Steam, GOG, ...), or *Add a folder with
WAD files…*, or copy WADs over USB or with `adb push` to
`Android/data/com.raylib.doom/files/` and tap *Rescan*. Pick the IWAD
and tick any add-ons (PWADs); the app remembers the choice. No storage
permission is needed.
