// What the launcher does with WAD files that needs no Android: the
// names imported files get, where they go, and how many the game can
// take. Unit tests: src/test/java/com/raylib/doom/WadFilesTest.java.

package com.raylib.doom;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
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

    // A granted folder's file needs no new copy only when its size and
    // date are both known and match the copy's: a provider may leave
    // either out (null), and then the file is read again.
    static boolean copyIsCurrent(File copy, long size, long modified) {
        return copy.isFile() && size >= 0 && copy.length() == size
               && modified > 0 && copy.lastModified() == modified;
    }

    // Makes dest a copy of in, a WAD from a granted folder; null if
    // done, else why not, and then no copy is left. Anything but a WAD
    // is turned down after its header, without copying the rest. A
    // copy that already has the same bytes is kept as it is.
    static String copyWad(InputStream in, File dest, long modified) {
        File part = new File(dest.getPath() + ".part");
        try {
            try (OutputStream out = new FileOutputStream(part)) {
                byte[] buf = new byte[1 << 16];
                int n = readFully(in, buf);
                String magic = new String(buf, 0, Math.min(n, 4), StandardCharsets.US_ASCII);
                if (!magic.equals("IWAD") && !magic.equals("PWAD")) {
                    dest.delete();
                    return "not a WAD file";
                }
                out.write(buf, 0, n);
                while ((n = in.read(buf)) > 0)
                    out.write(buf, 0, n);
            }
            Wad.read(part);
            if (!sameContents(part, dest)) {
                dest.delete();
                if (!part.renameTo(dest))
                    return "could not be saved";
            }
            if (modified > 0)
                dest.setLastModified(modified);
            return null;
        } catch (Wad.BadWadException e) {
            dest.delete();
            return e.getMessage();
        } catch (IOException e) {
            dest.delete();
            return "can not be read (" + e.getMessage() + ")";
        } finally {
            part.delete();
        }
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
