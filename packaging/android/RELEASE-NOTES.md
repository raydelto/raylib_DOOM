## Android

`raylibdoom-@VERSION@-android-flat.apk` runs on Android 8.0 or newer
(arm64 phones and tablets, and the x86_64 emulator). It is an engine
compatible with DOOM WAD files: **no game data is included**, and the
app never downloads any.

**Install:** download the APK on the device, open it and allow your
browser or file manager to install unknown apps when Android asks
(Settings > Apps > Special app access > Install unknown apps). Check
the download with `sha256sum -c raylibdoom-@VERSION@-android-flat.apk.sha256`.

**Supply a WAD:** start the app and tap *Add WAD files…* to pick an
IWAD you have, such as `freedoom1.wad` or `freedoom2.wad` from
<https://freedoom.github.io/download.html>, or `DOOM.WAD` / `DOOM2.WAD`
from your own copy of the game (Steam, GOG, ...). The app copies it
into its private storage. Or copy WADs over USB or with `adb push` to
`Android/data/com.raylib.doom/files/` and tap *Rescan*. Add-ons
(PWADs) are added the same way and ticked under *Add-ons*. No storage
permission is needed. *About / Licenses* lists the licenses (GPL-2.0
and others) and links to this release's source code.
