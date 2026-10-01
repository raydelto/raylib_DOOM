// Folders the player granted with ACTION_OPEN_DOCUMENT_TREE, such as
// Download or Documents. The grant is persisted, so no storage
// permission is needed; on every launch the folder is listed and its
// *.wad files (any case) are copied into files/folders/<id>/, where
// the game can open them by path. A copy is made again only when the
// file's size or date changes (always, when the provider does not
// give both), and removed when the file is gone.

package com.raylib.doom;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.text.TextUtils;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

final class WadFolders {
    private final Context context;
    private final SharedPreferences prefs;

    WadFolders(Context context, SharedPreferences prefs) {
        this.context = context;
        this.prefs = prefs;
    }

    List<Uri> trees() {
        List<Uri> trees = new ArrayList<>();
        String list = prefs.getString("trees", "");
        if (!list.isEmpty())
            for (String s : list.split("\n"))
                trees.add(Uri.parse(s));
        return trees;
    }

    private void saveTrees(List<Uri> trees) {
        prefs.edit().putString("trees", TextUtils.join("\n", trees)).commit();
    }

    void add(Uri tree) {
        context.getContentResolver().takePersistableUriPermission(
            tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        List<Uri> trees = trees();
        if (!trees.contains(tree)) {
            trees.add(tree);
            saveTrees(trees);
        }
    }

    void remove(Uri tree) {
        try {
            context.getContentResolver().releasePersistableUriPermission(
                tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            // Already gone.
        }
        List<Uri> trees = trees();
        trees.remove(tree);
        saveTrees(trees);
        deleteTree(mirrorDir(tree));
    }

    File root() {
        return new File(context.getFilesDir(), "folders");
    }

    File mirrorDir(Uri tree) {
        return new File(root(), WadFiles.folderId(tree.toString()));
    }

    // The folder's name as the picker shows it ("Download").
    String name(Uri tree) {
        try {
            Uri doc = DocumentsContract.buildDocumentUriUsingTree(
                tree, DocumentsContract.getTreeDocumentId(tree));
            try (Cursor c = context.getContentResolver().query(
                     doc, new String[] { Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
                if (c != null && c.moveToFirst() && !c.isNull(0))
                    return c.getString(0);
            }
        } catch (RuntimeException e) {
            // Fall back to the document ID ("primary:Download").
        }
        String id = tree.getLastPathSegment();
        return id != null ? id.substring(id.lastIndexOf(':') + 1) : tree.toString();
    }

    // Brings every folder's copies up to date; what can not be used
    // goes into problems.
    void sync(List<String> problems) {
        Set<String> keep = new HashSet<>();
        for (Uri tree : trees()) {
            File dir = mirrorDir(tree);
            keep.add(dir.getName());
            String folder = name(tree);
            try {
                syncTree(tree, dir, folder, problems);
            } catch (SecurityException e) {
                problems.add(folder + ": the app may no longer read this folder. "
                             + "Remove it and add it again.");
            } catch (RuntimeException | IOException e) {
                problems.add(folder + ": can not be read (" + e.getMessage() + ")");
            }
        }
        // Folders removed by an earlier version, or half-removed.
        File[] dirs = root().listFiles();
        if (dirs != null)
            for (File d : dirs)
                if (!keep.contains(d.getName()))
                    deleteTree(d);
    }

    private void syncTree(Uri tree, File dir, String folder, List<String> problems)
            throws IOException {
        ContentResolver cr = context.getContentResolver();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
            tree, DocumentsContract.getTreeDocumentId(tree));
        String[] cols = { Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
                          Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE,
                          Document.COLUMN_LAST_MODIFIED };
        dir.mkdirs();
        Set<String> present = new HashSet<>();
        try (Cursor c = cr.query(children, cols, null, null, null)) {
            if (c == null)
                throw new IOException("the folder could not be listed");
            while (c.moveToNext()) {
                String name = c.getString(1);
                if (name == null || Document.MIME_TYPE_DIR.equals(c.getString(2))
                        || !WadFiles.isWadName(name))
                    continue;
                String safe = WadFiles.safeName(name);
                if (!present.add(safe)) {
                    problems.add(folder + "/" + name + ": another file there has "
                                 + "the same name once made safe (" + safe + ")");
                    continue;
                }
                long size = c.isNull(3) ? -1 : c.getLong(3);
                long modified = c.isNull(4) ? 0 : c.getLong(4);
                File copy = new File(dir, safe);
                if (WadFiles.copyIsCurrent(copy, size, modified))
                    continue;
                Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0));
                String problem;
                try (InputStream in = cr.openInputStream(doc)) {
                    if (in == null) {
                        copy.delete();
                        problem = "can not be opened";
                    } else {
                        problem = WadFiles.copyWad(in, copy, modified);
                    }
                } catch (IOException | SecurityException e) {
                    copy.delete();
                    problem = "can not be read (" + e.getMessage() + ")";
                }
                if (problem != null)
                    problems.add(folder + "/" + name + ": " + problem);
            }
        }
        File[] files = dir.listFiles();
        if (files != null)
            for (File f : files)
                if (!present.contains(f.getName()))
                    f.delete();
    }

    static void deleteTree(File f) {
        File[] files = f.listFiles();
        if (files != null)
            for (File g : files)
                deleteTree(g);
        f.delete();
    }
}
