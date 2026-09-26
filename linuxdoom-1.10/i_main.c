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

// Started from Finder, the game has "/" as its current directory,
// no DOOMWADDIR and no terminal, so inside raylibDOOM.app it looks
// for the IWAD itself, saves games to Application Support, logs to
// ~/Library/Logs and says in a dialog when there is no IWAD.
// Started as a plain binary it behaves as on Linux.

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

static void BundleSetup (int argc, char** argv)
{
    static char		exe[PATH_MAX];
    static char		appdir[PATH_MAX];
    static char		support[PATH_MAX];
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

    snprintf (support, sizeof(support), "%s/Library", home);
    mkdir (support, 0700);
    snprintf (support, sizeof(support), "%s/Library/Application Support", home);
    mkdir (support, 0755);
    strcat (support, "/raylibDOOM");
    mkdir (support, 0755);
    if (chdir (support))
	chdir (home);

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

    // An IWAD named on the command line wins, as usual.
    for (i = 1; i < argc; i++)
	if (!strcasecmp (argv[i], "-iwad") || !strcasecmp (argv[i], "-file"))
	    return;

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
 
    D_DoomMain (); 

    return 0;
} 
