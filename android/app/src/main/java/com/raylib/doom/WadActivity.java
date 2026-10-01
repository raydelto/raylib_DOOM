// The launcher: finds the player's WADs, lets them add more, and
// starts the game (NativeActivity, in its own process) with them.
//
// The APK has no game data. WADs come from
//   - the system file picker (ACTION_OPEN_DOCUMENT), copied into
//     files/wads in the app's internal storage, and
//   - the app's folder in shared storage,
//     Android/data/<package>/files, where a player can copy them over
//     USB or with adb push,
// neither of which needs a storage permission. The IWAD and PWADs
// chosen are remembered, and handed to the game as a command line
// in files/launch.txt, one argument per line (see i_android.c).

package com.raylib.doom;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class WadActivity extends Activity {
    private static final int PICK_WADS = 1;
    private static final String FREEDOOM_URL = "https://freedoom.github.io/download.html";

    private final List<Wad> iwads = new ArrayList<>();
    private final List<Wad> pwads = new ArrayList<>();
    // Files in the folders that are not usable WADs: name and reason.
    private final List<String> rejected = new ArrayList<>();

    private SharedPreferences prefs;
    private String iwad;
    // In the order they were chosen, which is the -file order.
    private final Set<String> chosen = new LinkedHashSet<>();
    private boolean busy;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        prefs = getSharedPreferences("wads", MODE_PRIVATE);
        iwad = prefs.getString("iwad", null);
        String list = prefs.getString("pwads", "");
        if (!list.isEmpty())
            chosen.addAll(Arrays.asList(list.split("\n")));
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!busy)
            refresh();
        showGameError();
    }

    private File importDir() {
        File dir = new File(getFilesDir(), "wads");
        dir.mkdirs();
        return dir;
    }

    // The folders searched, in order. getFilesDir() itself is where
    // earlier versions put Freedoom and where the README's run-as
    // instructions put WADs.
    private List<File> wadDirs() {
        List<File> dirs = new ArrayList<>();
        dirs.add(importDir());
        File ext = getExternalFilesDir(null);
        if (ext != null)
            dirs.add(ext);
        dirs.add(getFilesDir());
        return dirs;
    }

    private void scan() {
        iwads.clear();
        pwads.clear();
        rejected.clear();
        for (File dir : wadDirs()) {
            File[] files = dir.listFiles();
            if (files == null)
                continue;
            Arrays.sort(files);
            for (File f : files) {
                String name = f.getName();
                if (!f.isFile() || name.endsWith(".part")
                        || !name.toLowerCase().endsWith(".wad"))
                    continue;
                try {
                    Wad w = Wad.read(f);
                    (w.kind == Wad.Kind.IWAD ? iwads : pwads).add(w);
                } catch (Wad.BadWadException e) {
                    rejected.add(name + ": " + e.getMessage());
                } catch (IOException e) {
                    rejected.add(name + ": can not be read (" + e.getMessage() + ")");
                }
            }
        }

        // Forget what is gone; with one IWAD, that is the one.
        boolean found = false;
        for (Wad w : iwads)
            found |= w.file.getPath().equals(iwad);
        if (!found)
            iwad = iwads.isEmpty() ? null : iwads.get(0).file.getPath();
        Set<String> present = new LinkedHashSet<>();
        for (Wad w : pwads)
            present.add(w.file.getPath());
        chosen.retainAll(present);
        save();
    }

    private void save() {
        prefs.edit()
            .putString("iwad", iwad)
            .putString("pwads", TextUtils.join("\n", chosen))
            .commit();
    }

    // ---- Screen ----

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v,
                                               getResources().getDisplayMetrics());
    }

    private TextView text(LinearLayout parent, CharSequence s, float sp, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        if (bold)
            t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setPadding(0, dp(6), 0, dp(6));
        parent.addView(t);
        return t;
    }

    private Button button(LinearLayout parent, CharSequence s, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        b.setOnClickListener(l);
        parent.addView(b, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return b;
    }

    private String sideloadPath() {
        File ext = getExternalFilesDir(null);
        return ext != null ? "Android/data/" + getPackageName() + "/files"
                           : "(shared storage is not available)";
    }

    private void refresh() {
        scan();

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(20), dp(16), dp(20), dp(24));

        text(col, getString(R.string.app_name), 26, true);
        text(col, getString(R.string.tagline), 15, false);

        if (iwads.isEmpty()) {
            text(col, "No game data found", 20, true);
            text(col,
                 "To play, you need an IWAD: the main data file of a game "
                 + "made for this engine. This app does not include one and "
                 + "will not download one for you.\n\n"
                 + "• Freedoom (free): download freedoom1.wad or "
                 + "freedoom2.wad from the Freedoom website in your browser, "
                 + "then add it here.\n"
                 + "• The original game: the IWAD is in your own copy of "
                 + "DOOM or DOOM II, for example from Steam or GOG "
                 + "(DOOM.WAD, DOOM2.WAD, ...). Copy it to this device.",
                 15, false);
            button(col, "Open the Freedoom download page", v -> openUrl(FREEDOOM_URL));
            if (!pwads.isEmpty()) {
                List<String> names = new ArrayList<>();
                for (Wad w : pwads)
                    names.add(w.file.getName());
                text(col, "Add-ons (PWADs) waiting for game data: "
                     + TextUtils.join(", ", names), 13, false);
            }
        } else {
            text(col, "Game data (IWAD)", 18, true);
            RadioGroup group = new RadioGroup(this);
            for (Wad w : iwads) {
                RadioButton r = new RadioButton(this);
                r.setId(View.generateViewId());
                r.setText(label(w));
                r.setChecked(w.file.getPath().equals(iwad));
                r.setOnClickListener(v -> { iwad = w.file.getPath(); save(); });
                longPressRemove(r, w);
                group.addView(r);
            }
            col.addView(group);

            if (!pwads.isEmpty()) {
                text(col, "Add-ons (PWADs), loaded in the order ticked", 18, true);
                for (Wad w : pwads) {
                    CheckBox c = new CheckBox(this);
                    c.setText(label(w));
                    c.setChecked(chosen.contains(w.file.getPath()));
                    c.setOnCheckedChangeListener((v, on) -> {
                        if (on)
                            chosen.add(w.file.getPath());
                        else
                            chosen.remove(w.file.getPath());
                        save();
                    });
                    longPressRemove(c, w);
                    col.addView(c);
                }
            }

            Button play = button(col, "Play", v -> play());
            play.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        }

        button(col, "Add WAD files…", v -> pick());
        text(col,
             "You can also copy WAD files to " + sideloadPath()
             + " over USB or with adb push, then tap Rescan. Long-press a "
             + "WAD to remove it from the app.",
             13, false);
        button(col, "Rescan", v -> refresh());

        if (!rejected.isEmpty())
            text(col, "Not used:\n" + TextUtils.join("\n", rejected), 13, false);

        button(col, "About / Licenses", v -> about());

        ScrollView scroll = new ScrollView(this);
        scroll.addView(col);
        setContentView(scroll);
    }

    private String label(Wad w) {
        String where = w.file.getParentFile().equals(getExternalFilesDir(null))
                       ? " [shared folder]" : "";
        return w.file.getName() + "\n" + w.describe() + where;
    }

    private void longPressRemove(View v, Wad w) {
        v.setOnLongClickListener(x -> {
            new AlertDialog.Builder(this)
                .setTitle("Remove " + w.file.getName() + "?")
                .setMessage("The file is deleted from " + w.file.getParent() + ".")
                .setPositiveButton("Remove", (d, i) -> {
                    if (!w.file.delete())
                        message("Could not remove " + w.file.getName());
                    refresh();
                })
                .setNegativeButton("Cancel", null)
                .show();
            return true;
        });
    }

    private void message(String s) {
        new AlertDialog.Builder(this)
            .setMessage(s)
            .setPositiveButton(android.R.string.ok, null)
            .show();
    }

    private void openUrl(String url) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (ActivityNotFoundException e) {
            message("No browser is installed. The address is\n" + url);
        }
    }

    // ---- Playing ----

    private void play() {
        if (iwad == null || !new File(iwad).canRead()) {
            message("Choose the game data (an IWAD) first.");
            return;
        }
        List<String> args = new ArrayList<>();
        args.add("-iwad");
        args.add(iwad);
        if (!chosen.isEmpty()) {
            args.add("-file");
            args.addAll(chosen);
        }
        try {
            writeFile(new File(getFilesDir(), "launch.txt"),
                      TextUtils.join("\n", args) + "\n");
        } catch (IOException e) {
            message("Could not save the game's settings: " + e.getMessage());
            return;
        }
        new File(getFilesDir(), "error.txt").delete();
        startActivity(new Intent(this, NativeActivity.class));
    }

    private static void writeFile(File f, String s) throws IOException {
        File part = new File(f.getPath() + ".part");
        try (OutputStream out = new FileOutputStream(part)) {
            out.write(s.getBytes(StandardCharsets.UTF_8));
        }
        if (!part.renameTo(f))
            throw new IOException("can not rename " + part);
    }

    // The game's process writes its last error here before it
    // exits (I_Error, through i_android.c).
    private void showGameError() {
        File f = new File(getFilesDir(), "error.txt");
        if (!f.exists())
            return;
        String s;
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) Math.min(f.length(), 4096)];
            int n = in.read(b);
            s = new String(b, 0, Math.max(n, 0), StandardCharsets.UTF_8).trim();
        } catch (IOException e) {
            s = e.getMessage();
        }
        f.delete();
        new AlertDialog.Builder(this)
            .setTitle("The game stopped")
            .setMessage(s)
            .setPositiveButton(android.R.string.ok, null)
            .show();
    }

    // ---- Adding WADs ----

    private void pick() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        // WADs have no registered MIME type: providers call them
        // application/octet-stream or worse.
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        try {
            startActivityForResult(i, PICK_WADS);
        } catch (ActivityNotFoundException e) {
            message("This device has no file picker. Copy WAD files to "
                    + sideloadPath() + " instead.");
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request != PICK_WADS || result != RESULT_OK || data == null)
            return;
        List<Uri> uris = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); i++)
                uris.add(clip.getItemAt(i).getUri());
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        if (uris.isEmpty())
            return;

        busy = true;
        AlertDialog wait = new AlertDialog.Builder(this)
            .setMessage("Copying…")
            .setCancelable(false)
            .show();
        new Thread(() -> {
            List<String> report = new ArrayList<>();
            List<Wad> added = new ArrayList<>();
            for (Uri uri : uris)
                importWad(uri, report, added);
            runOnUiThread(() -> {
                busy = false;
                wait.dismiss();
                imported(report, added);
            });
        }).start();
    }

    private void importWad(Uri uri, List<String> report, List<Wad> added) {
        String name = displayName(uri);
        // Imported files keep their name, as the game would see it on
        // the desktop, made safe for a path.
        String safe = name.replaceAll("[^A-Za-z0-9._-]", "_");
        if (!safe.toLowerCase().endsWith(".wad"))
            safe += ".wad";
        File dest = new File(importDir(), safe);
        File part = new File(dest.getPath() + ".part");
        try {
            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(part)) {
                if (in == null)
                    throw new IOException("the file could not be opened");
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0)
                    out.write(buf, 0, n);
            }
            Wad w = Wad.read(part);
            if (!part.renameTo(dest))
                throw new IOException("could not be saved");
            added.add(Wad.read(dest));
            report.add(name + ": added, " + w.describe());
        } catch (Wad.BadWadException e) {
            report.add(name + ": not added, it is " + e.getMessage() + ".");
        } catch (IOException | SecurityException e) {
            report.add(name + ": not added, " + e.getMessage() + ".");
        } finally {
            part.delete();
        }
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(
                 uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0))
                return c.getString(0);
        } catch (RuntimeException e) {
            // Fall back to the URI's last segment.
        }
        String s = uri.getLastPathSegment();
        return s != null ? s.substring(s.lastIndexOf('/') + 1) : "file.wad";
    }

    private void imported(List<String> report, List<Wad> added) {
        boolean anyIwad = false;
        for (Wad w : added) {
            if (w.kind == Wad.Kind.IWAD) {
                if (!anyIwad)
                    iwad = w.file.getPath();
                anyIwad = true;
            } else {
                chosen.add(w.file.getPath());
            }
        }
        save();
        refresh();
        if (iwads.isEmpty() && !added.isEmpty())
            report.add("\nAdd-ons (PWADs) need game data (an IWAD) such as "
                       + "freedoom1.wad, freedoom2.wad, DOOM.WAD or DOOM2.WAD. "
                       + "Add one to play.");
        new AlertDialog.Builder(this)
            .setTitle(added.isEmpty() ? "Nothing added" : "WAD files")
            .setMessage(TextUtils.join("\n", report))
            .setPositiveButton(android.R.string.ok, null)
            .show();
    }

    // ---- About ----

    private void about() {
        String[] files;
        try {
            files = getAssets().list("licenses");
        } catch (IOException e) {
            files = new String[0];
        }
        List<String> names = new ArrayList<>(Arrays.asList(files));
        Collections.sort(names);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(20), dp(8), dp(20), dp(8));
        text(col,
             getString(R.string.app_name) + " " + BuildConfig.VERSION_NAME + "\n\n"
             + "The id Software DOOM engine (linuxdoom 1.10) ported to raylib. "
             + "It is free software under the GNU General Public License, "
             + "version 2; it comes with no warranty. "
             + getString(R.string.tagline) + "\n\n"
             + "DOOM is a trademark of id Software; this app is not made or "
             + "endorsed by id Software, Bethesda or ZeniMax.\n\n"
             + "Source code of this version:\n" + BuildConfig.SOURCE_URL,
             14, false);
        button(col, "Open the source code page", v -> openUrl(BuildConfig.SOURCE_URL));
        for (String n : names)
            button(col, n, v -> showAsset("licenses/" + n, n));
        ScrollView scroll = new ScrollView(this);
        scroll.addView(col);
        new AlertDialog.Builder(this)
            .setTitle("About / Licenses")
            .setView(scroll)
            .setPositiveButton(android.R.string.ok, null)
            .show();
    }

    private void showAsset(String path, String title) {
        String s;
        try (InputStream in = getAssets().open(path)) {
            byte[] buf = new byte[1 << 14];
            int n;
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            while ((n = in.read(buf)) > 0)
                bytes.write(buf, 0, n);
            s = new String(bytes.toByteArray(), StandardCharsets.UTF_8);
        } catch (IOException e) {
            s = e.getMessage();
        }
        TextView t = new TextView(this);
        t.setText(s);
        t.setTypeface(Typeface.MONOSPACE);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        t.setTextIsSelectable(true);
        t.setPadding(dp(12), dp(8), dp(12), dp(8));
        ScrollView scroll = new ScrollView(this);
        scroll.addView(t);
        new AlertDialog.Builder(this)
            .setTitle(title)
            .setView(scroll)
            .setPositiveButton(android.R.string.ok, null)
            .show();
    }
}
