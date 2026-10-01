// What the launcher does with WAD files that needs no Android: the
// names imported files get, where they go, and how many the game can
// take. Unit tests: src/test/java/com/raylib/doom/WadFilesTest.java.

package com.raylib.doom;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;

final class WadFiles {
    // wadfiles[MAXWADFILES] in linuxdoom-1.10/d_main.h, which ends
    // with a NULL: the IWAD and at most 18 add-ons.
    static final int MAX_WAD_FILES = 20;
    static final int MAX_PWADS = MAX_WAD_FILES - 2;

    private WadFiles() {
    }

    // Null if the game can load this many add-ons, else why not.
    static String checkPwadCount(int n) {
        if (n <= MAX_PWADS)
            return null;
        return "At most " + MAX_PWADS + " add-ons (PWADs) can be loaded at once; "
               + n + " are ticked. Untick some to play.";
    }

    // A file the launcher looks at: *.wad, in any case.
    static boolean isWadName(String name) {
        return name.toLowerCase(Locale.ROOT).endsWith(".wad") && name.length() > 4;
    }

    // The directory a granted folder's copies go in, from its URI:
    // the same folder always gets the same one.
    static String folderId(String uri) {
        try {
            byte[] d = MessageDigest.getInstance("SHA-1")
                .digest(uri.getBytes(StandardCharsets.UTF_8));
            StringBuilder s = new StringBuilder();
            for (int i = 0; i < 8; i++)
                s.append(String.format(Locale.ROOT, "%02x", d[i] & 0xff));
            return s.toString();
        } catch (NoSuchAlgorithmException e) {
            throw new AssertionError(e);
        }
    }

    // The file name an imported WAD gets: its own name, made safe for
    // a path, ending in .wad.
    static String safeName(String name) {
        String s = name.replaceAll("[^A-Za-z0-9._-]", "_");
        if (!s.toLowerCase(Locale.ROOT).endsWith(".wad"))
            s += ".wad";
        String base = s.substring(0, s.length() - 4);
        if (base.isEmpty() || base.matches("\\.+"))
            base = "file";
        // Room for "-NN" within the usual 255-byte limit.
        if (base.length() > 120)
            base = base.substring(0, 120);
        return base + s.substring(s.length() - 4);
    }

    static final class Placed {
        final File file;
        // An identical file was already there; nothing was written.
        final boolean existing;

        Placed(File file, boolean existing) {
            this.file = file;
            this.existing = existing;
        }
    }

    // Moves part, a WAD just copied, into dir as name. A file already
    // there is never replaced: if it is the same WAD, it is reused and
    // part deleted; if not, part gets the first free name-2.wad,
    // name-3.wad, ...
    static Placed place(File part, File dir, String name) throws IOException {
        String base = name.substring(0, name.length() - 4);
        String ext = name.substring(name.length() - 4);
        for (int i = 1; i < 1000; i++) {
            File dest = new File(dir, i == 1 ? name : base + "-" + i + ext);
            if (dest.exists()) {
                if (sameContents(part, dest)) {
                    part.delete();
                    return new Placed(dest, true);
                }
                continue;
            }
            if (!part.renameTo(dest))
                throw new IOException("could not be saved");
            return new Placed(dest, false);
        }
        throw new IOException("too many files are named " + name);
    }

    static boolean sameContents(File a, File b) throws IOException {
        if (!b.isFile() || a.length() != b.length())
            return false;
        try (InputStream x = new FileInputStream(a);
             InputStream y = new FileInputStream(b)) {
            byte[] p = new byte[1 << 16], q = new byte[1 << 16];
            for (;;) {
                int n = readFully(x, p), m = readFully(y, q);
                if (n != m)
                    return false;
                for (int i = 0; i < n; i++)
                    if (p[i] != q[i])
                        return false;
                if (n < p.length)
                    return true;
            }
        }
    }

    private static int readFully(InputStream in, byte[] b) throws IOException {
        int n = 0, r;
        while (n < b.length && (r = in.read(b, n, b.length - n)) > 0)
            n += r;
        return n;
    }
}
