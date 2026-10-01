package com.raylib.doom;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class WadFilesTest {
    @Rule
    public TemporaryFolder tmp = new TemporaryFolder();

    // ---- How many WADs the game takes ----

    @Test
    public void limitMatchesTheEngine() throws IOException {
        // Gradle runs unit tests in android/app.
        String h = new String(Files.readAllBytes(
            new File("../../linuxdoom-1.10/d_main.h").toPath()), StandardCharsets.UTF_8);
        Matcher m = Pattern.compile("#define\\s+MAXWADFILES\\s+(\\d+)").matcher(h);
        assertTrue(m.find());
        assertEquals(Integer.parseInt(m.group(1)), WadFiles.MAX_WAD_FILES);
        // The IWAD, the add-ons and the NULL at the end of wadfiles.
        assertEquals(WadFiles.MAX_WAD_FILES, 1 + WadFiles.MAX_PWADS + 1);
    }

    @Test
    public void pwadCountBoundary() {
        assertNull(WadFiles.checkPwadCount(0));
        assertNull(WadFiles.checkPwadCount(18));
        assertNotNull(WadFiles.checkPwadCount(19));
        assertNotNull(WadFiles.checkPwadCount(20));
    }

    // ---- Names ----

    @Test
    public void safeNames() {
        assertEquals("freedoom1.wad", WadFiles.safeName("freedoom1.wad"));
        assertEquals("DOOM2.WAD", WadFiles.safeName("DOOM2.WAD"));
        assertEquals("my_wad.wad", WadFiles.safeName("my wad.wad"));
        assertEquals("a_b.wad", WadFiles.safeName("a/b"));
        assertEquals("sigil.zip.wad", WadFiles.safeName("sigil.zip"));
        assertEquals("file.wad", WadFiles.safeName(".wad"));
        assertEquals("file.wad", WadFiles.safeName(""));
        assertEquals("file.wad", WadFiles.safeName(".."));
        assertEquals(124, WadFiles.safeName(new String(new char[300]).replace('\0', 'x')).length());
    }

    // ---- Placing imported files ----

    private File write(File f, String s) throws IOException {
        Files.write(f.toPath(), s.getBytes(StandardCharsets.UTF_8));
        return f;
    }

    private String read(File f) throws IOException {
        return new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8);
    }

    @Test
    public void newName() throws IOException {
        File dir = tmp.newFolder();
        File part = write(new File(dir, "import.part"), "one");
        WadFiles.Placed p = WadFiles.place(part, dir, "x.wad");
        assertEquals(new File(dir, "x.wad"), p.file);
        assertFalse(p.existing);
        assertEquals("one", read(p.file));
        assertFalse(part.exists());
    }

    @Test
    public void sameNameDifferentFileIsKeptBoth() throws IOException {
        File dir = tmp.newFolder();
        File old = write(new File(dir, "sigil.wad"), "the first one");
        File part = write(new File(dir, "import.part"), "another one");
        WadFiles.Placed p = WadFiles.place(part, dir, "sigil.wad");
        assertEquals(new File(dir, "sigil-2.wad"), p.file);
        assertFalse(p.existing);
        assertEquals("the first one", read(old));
        assertEquals("another one", read(p.file));

        // And a third.
        part = write(new File(dir, "import.part"), "a third one");
        p = WadFiles.place(part, dir, "sigil.wad");
        assertEquals(new File(dir, "sigil-3.wad"), p.file);
        assertEquals("the first one", read(old));
        assertEquals("another one", read(new File(dir, "sigil-2.wad")));
    }

    @Test
    public void sameLengthDifferentContentsIsKeptBoth() throws IOException {
        File dir = tmp.newFolder();
        File old = write(new File(dir, "a.wad"), "aaaa");
        File part = write(new File(dir, "import.part"), "aaab");
        WadFiles.Placed p = WadFiles.place(part, dir, "a.wad");
        assertEquals(new File(dir, "a-2.wad"), p.file);
        assertEquals("aaaa", read(old));
    }

    @Test
    public void sameFileAgainIsReused() throws IOException {
        File dir = tmp.newFolder();
        File old = write(new File(dir, "freedoom1.wad"), "IWAD data");
        File part = write(new File(dir, "import.part"), "IWAD data");
        WadFiles.Placed p = WadFiles.place(part, dir, "freedoom1.wad");
        assertEquals(old, p.file);
        assertTrue(p.existing);
        assertFalse(part.exists());
        assertEquals(1, dir.list().length);
    }

    @Test
    public void sameFileAgainAfterARename() throws IOException {
        File dir = tmp.newFolder();
        write(new File(dir, "x.wad"), "first");
        write(new File(dir, "x-2.wad"), "second");
        File part = write(new File(dir, "import.part"), "second");
        WadFiles.Placed p = WadFiles.place(part, dir, "x.wad");
        assertEquals(new File(dir, "x-2.wad"), p.file);
        assertTrue(p.existing);
    }

    @Test
    public void namesThatCollideOnlyAfterSanitizing() throws IOException {
        File dir = tmp.newFolder();
        // "my wad.wad" and "my_wad.wad" (or "my?wad.wad") are different
        // files on the device but both are saved as my_wad.wad.
        String a = WadFiles.safeName("my wad.wad"), b = WadFiles.safeName("my?wad.wad");
        assertEquals(a, b);
        WadFiles.Placed p = WadFiles.place(write(new File(dir, "import.part"), "A"), dir, a);
        WadFiles.Placed q = WadFiles.place(write(new File(dir, "import.part"), "B"), dir, b);
        assertEquals(new File(dir, "my_wad.wad"), p.file);
        assertEquals(new File(dir, "my_wad-2.wad"), q.file);
        assertEquals("A", read(p.file));
        assertEquals("B", read(q.file));
    }

    @Test
    public void keepsTheExtensionsCase() throws IOException {
        File dir = tmp.newFolder();
        write(new File(dir, "DOOM2.WAD"), "one");
        WadFiles.Placed p = WadFiles.place(write(new File(dir, "import.part"), "two"),
                                           dir, "DOOM2.WAD");
        assertEquals(new File(dir, "DOOM2-2.WAD"), p.file);
    }

    @Test
    public void bigFilesCompared() throws IOException {
        File dir = tmp.newFolder();
        byte[] b = new byte[200000];
        for (int i = 0; i < b.length; i++)
            b[i] = (byte) i;
        Files.write(new File(dir, "big.wad").toPath(), b);
        byte[] c = b.clone();
        c[150000] ^= 1;
        File part = new File(dir, "import.part");
        Files.write(part.toPath(), c);
        WadFiles.Placed p = WadFiles.place(part, dir, "big.wad");
        assertEquals(new File(dir, "big-2.wad"), p.file);
        assertArrayEquals(b, Files.readAllBytes(new File(dir, "big.wad").toPath()));

        part = new File(dir, "import.part");
        Files.write(part.toPath(), b);
        p = WadFiles.place(part, dir, "big.wad");
        assertEquals(new File(dir, "big.wad"), p.file);
        assertTrue(p.existing);
    }
}
