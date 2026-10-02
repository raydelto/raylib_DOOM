// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	Main program, simply calls D_DoomMain high level loop.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_main.c,v 1.4 1997/02/03 22:45:10 b1 Exp $";



#include "doomdef.h"

#include "m_argv.h"
#include "d_main.h"

#ifdef __EMSCRIPTEN__
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#ifdef __ANDROID__
#include "i_android.h"
#endif

#include "i_ios.h"

#ifdef __APPLE__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <mach-o/dyld.h>
#include <CoreFoundation/CoreFoundation.h>
#ifdef DOOM_IOS
#include <SDL_main.h>	// renames main to SDL_main; SDL's own main starts UIKit
#endif

// Started from Finder, the game has "/" as its current directory,
// no DOOMWADDIR and no terminal, so inside raylibDOOM.app it looks
// for the IWAD itself, saves games to Application Support, logs to
// ~/Library/Logs and says in a dialog when there is no IWAD.
// Started as a plain binary it behaves as on Linux.

// Application Support/raylibDOOM, set only inside raylibDOOM.app.
static char		bundlesupport[PATH_MAX];

extern char*		defaultfile;	// m_misc.c

// Searched in this order within each directory, as in IdentifyVersion.
static const char* bundleiwads[] =
{
    "doom2f.wad", "doom2.wad", "plutonia.wad", "tnt.wad",
    "doomu.wad", "doom.wad", "doom1.wad",
    "freedoom2.wad", "freedoom1.wad", "freedm.wad",
    NULL
};

static int BundleFindIWAD (const char* dir, char* out)
{
    int			i;
    DIR*		d;
    struct dirent*	ent;

    if (!dir || !*dir)
	return 0;
    d = opendir (dir);
    if (!d)
	return 0;
    for (i = 0; bundleiwads[i]; i++)
    {
	rewinddir (d);
	while ((ent = readdir (d)) != NULL)
	{
	    if (strcasecmp (ent->d_name, bundleiwads[i]))
		continue;
	    snprintf (out, PATH_MAX, "%s/%s", dir, ent->d_name);
	    if (!access (out, R_OK))
	    {
		closedir (d);
		return 1;
	    }
	}
    }
    closedir (d);
    return 0;
}

// True if path is a WAD file with an IWAD header.
static int BundleIsIWAD (const char* path)
{
    char	id[4];
    FILE*	f;
    int		ok;

    f = fopen (path, "rb");
    if (!f)
	return 0;
    ok = fread (id, 1, 4, f) == 4 && !memcmp (id, "IWAD", 4);
    fclose (f);
    return ok;
}

#ifndef DOOM_IOS
static void BundleAlert (const char* text)
{
    CFStringRef	title;
    CFStringRef	msg;

    title = CFSTR ("raylibDOOM: no game data found");
    msg = CFStringCreateWithCString (NULL, text, kCFStringEncodingUTF8);
    CFUserNotificationDisplayAlert (0, kCFUserNotificationStopAlertLevel,
				    NULL, NULL, NULL, title, msg,
				    CFSTR ("Quit"), NULL, NULL, NULL);
    if (msg)
	CFRelease (msg);
}

#endif	// !DOOM_IOS

#ifdef DOOM_IOS
// On iOS the app is sandboxed: the IWAD is looked for in the app
// bundle (the build puts one there), then in the app's Documents
// folder (reachable from the Files app), which also holds the saved
// games and settings. A relative -config or -file stays relative to
// the Documents folder too.
static void BundleSetup (int argc, char** argv)
{
    static char		iwad[PATH_MAX];
    char		appdir[PATH_MAX];
    char*		support = bundlesupport;
    char*		home;
    char**		args;
    int			i;
    CFBundleRef		bundle;
    CFURLRef		url;

    home = getenv ("HOME");
    if (!home)
	return;

    snprintf (support, PATH_MAX, "%s/Documents", home);
    mkdir (support, 0755);

    appdir[0] = '\0';
    bundle = CFBundleGetMainBundle ();
    url = bundle ? CFBundleCopyResourcesDirectoryURL (bundle) : NULL;
    if (url)
    {
	if (!CFURLGetFileSystemRepresentation (url, true, (UInt8*)appdir,
					       sizeof(appdir)))
	    appdir[0] = '\0';
	CFRelease (url);
    }

    for (i = 1; i < argc; i++)
	if (!strcasecmp (argv[i], "-iwad"))
	    return;

    if (!BundleFindIWAD (getenv ("DOOMWADDIR"), iwad)
	&& !BundleFindIWAD (support, iwad)
	&& !BundleFindIWAD (appdir, iwad))
    {
	fprintf (stderr, "No IWAD found in %s or the app bundle %s\n",
		 support, appdir);
	exit (1);
    }

    printf ("IWAD: %s\n", iwad);
    args = malloc ((argc + 3) * sizeof(*args));
    for (i = 0; i < argc; i++)
	args[i] = argv[i];
    args[argc] = "-iwad";
    args[argc+1] = iwad;
    args[argc+2] = NULL;
    myargc = argc + 2;
    myargv = args;
}
#else
static void BundleSetup (int argc, char** argv)
{
    static char		exe[PATH_MAX];
    static char		appdir[PATH_MAX];
    char*		support = bundlesupport;
    static char		iwad[PATH_MAX];
    static char		text[4*PATH_MAX];
    char		raw[PATH_MAX];
    uint32_t		size = sizeof(raw);
    char*		app;
    char*		home;
    char*		wadenv;
    char**		args;
    FILE*		f;
    int			i;

    if (_NSGetExecutablePath (raw, &size) || !realpath (raw, exe))
	return;
    app = strstr (exe, ".app/Contents/MacOS/");
    home = getenv ("HOME");
    if (!app || !home)
	return;

    // The directory holding raylibDOOM.app.
    app[4] = '\0';
    snprintf (appdir, sizeof(appdir), "%s", exe);
    *strrchr (appdir, '/') = '\0';

    // Created now, but only entered once the WADs are open (see
    // I_BundleEnterDataDir), so command-line paths and DOOMWADDIR
    // stay relative to the caller's directory.
    snprintf (support, PATH_MAX, "%s/Library", home);
    mkdir (support, 0700);
    snprintf (support, PATH_MAX, "%s/Library/Application Support", home);
    mkdir (support, 0755);
    strcat (support, "/raylibDOOM");
    mkdir (support, 0755);

    if (!isatty (STDERR_FILENO))
    {
	snprintf (text, sizeof(text), "%s/Library/Logs", home);
	mkdir (text, 0755);
	snprintf (text, sizeof(text), "%s/Library/Logs/raylibDOOM.log", home);
	if ((f = freopen (text, "w", stdout)) != NULL)
	{
	    setvbuf (stdout, NULL, _IOLBF, 0);
	    dup2 (fileno (stdout), STDERR_FILENO);
	}
    }

    // An IWAD named on the command line wins, as usual: with -iwad,
    // or as one of the -file WADs ("doom -file DOOM1.WAD"). PWADs
    // alone still get the IWAD found here.
    for (i = 1; i < argc; i++)
    {
	if (!strcasecmp (argv[i], "-iwad"))
	    return;
	if (!strcasecmp (argv[i], "-file"))
	    while (i+1 < argc && argv[i+1][0] != '-')
		if (BundleIsIWAD (argv[++i]))
		    return;
    }

    wadenv = getenv ("DOOMWADDIR");
    if (!BundleFindIWAD (wadenv, iwad)
	&& !BundleFindIWAD (support, iwad)
	&& !BundleFindIWAD (appdir, iwad))
    {
	snprintf (text, sizeof(text),
		  "Put an IWAD, such as the shareware DOOM1.WAD or Freedoom's "
		  "freedoom1.wad / freedoom2.wad (https://freedoom.github.io), "
		  "in one of these folders:\n\n"
		  "%s\n\n%s\n%s%s\n\n"
		  "Then open raylibDOOM again.",
		  support, appdir,
		  wadenv ? "DOOMWADDIR: " : "", wadenv ? wadenv : "");
	fprintf (stderr, "No IWAD found.\n%s\n", text);
	BundleAlert (text);
	exit (1);
    }

    printf ("IWAD: %s\n", iwad);
    args = malloc ((argc + 3) * sizeof(*args));
    for (i = 0; i < argc; i++)
	args[i] = argv[i];
    args[argc] = "-iwad";
    args[argc+1] = iwad;
    args[argc+2] = NULL;
    myargc = argc + 2;
    myargv = args;
}
#endif	// DOOM_IOS

//
// I_BundleEnterDataDir
// Called when the WADs are open and the window is about to open.
// From here on relative files (saved games, screenshots, recorded
// demos) are in Application Support; a relative -config still means
// the caller's directory.
//
void I_BundleEnterDataDir (void)
{
    static char		config[PATH_MAX];
    char		cwd[PATH_MAX];

    if (!bundlesupport[0])
	return;

    // M_SaveDefaults writes it again at quit.
    if (defaultfile && defaultfile[0] != '/' && getcwd (cwd, sizeof(cwd)))
    {
	snprintf (config, sizeof(config), "%s/%s", cwd, defaultfile);
	defaultfile = config;
    }

    if (chdir (bundlesupport))
	fprintf (stderr, "Can't use %s for saved games\n", bundlesupport);
}
#endif

int
main
( int		argc,
  char**	argv ) 
{ 
    myargc = argc; 
    myargv = argv; 

#ifdef __APPLE__
    BundleSetup (argc, argv);
#endif

#ifdef __ANDROID__
    I_AndroidSetup ();
#endif

#ifdef __EMSCRIPTEN__
    // Saved games are written to the current directory and the
    // settings to $HOME; /save is kept in the browser's IndexedDB by
    // the shell page. Freedoom is preloaded into /doom.
    setenv ("HOME", "/save", 1);
    setenv ("DOOMWADDIR", "/doom", 0);
    if (chdir ("/save"))
	fprintf (stderr, "No /save directory; games will not be kept\n");
#endif
 
    D_DoomMain (); 

    return 0;
} 
