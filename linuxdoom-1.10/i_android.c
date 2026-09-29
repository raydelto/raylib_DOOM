// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
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
// DESCRIPTION:
//	Android start-up: logcat, the bundled IWAD and the data folder.
//
//	raylib's NativeActivity glue (rcore_android.c) calls main with
//	no arguments, no $HOME, "/" as the current directory and stdout
//	going nowhere. The APK's assets are not files, and DOOM reads
//	WADs with open/read, so Freedoom is copied out of the APK into
//	the app's internal storage the first time (and again when the
//	APK brings a different one).
//
//-----------------------------------------------------------------------------

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include "m_argv.h"

#include "i_android.h"


#define LOGTAG		"raylibdoom"
#define MAXARGS		64

// raylib's rcore_android.c
extern struct android_app* GetAndroidApp (void);

// The IWAD and its license, as android/app/build.gradle puts
// them in the APK.
static const char* assets[] = { "freedoom1.wad", "freedoom-COPYING.txt" };

static int	logpipe[2];


//
// Logcat
// stdout and stderr go into a pipe; this thread writes each line
// of it to logcat, where "adb logcat -s raylibdoom" shows it.
//
static void* LogThread (void* unused)
{
    char	buf[1024];
    int		len = 0;
    int		start;
    int		i;
    ssize_t	n;

    for (;;)
    {
	n = read (logpipe[0], buf + len, sizeof(buf) - 1 - len);
	if (n <= 0)
	{
	    if (n < 0 && errno == EINTR)
		continue;
	    break;
	}
	len += (int)n;

	start = 0;
	for (i = 0; i < len; i++)
	{
	    if (buf[i] != '\n')
		continue;
	    buf[i] = '\0';
	    __android_log_write (ANDROID_LOG_INFO, LOGTAG, buf + start);
	    start = i + 1;
	}
	if (start == 0 && len == (int)sizeof(buf) - 1)
	{
	    // A line longer than the buffer goes out in pieces.
	    buf[len] = '\0';
	    __android_log_write (ANDROID_LOG_INFO, LOGTAG, buf);
	    len = 0;
	    continue;
	}
	memmove (buf, buf + start, len - start);
	len -= start;
    }
    return NULL;
}


static void RedirectOutput (void)
{
    pthread_t	thread;

    if (pipe (logpipe))
	return;
    setvbuf (stdout, NULL, _IOLBF, 0);
    setvbuf (stderr, NULL, _IONBF, 0);
    dup2 (logpipe[1], STDOUT_FILENO);
    dup2 (logpipe[1], STDERR_FILENO);
    if (!pthread_create (&thread, NULL, LogThread, NULL))
	pthread_detach (thread);
}


//
// CopyAsset
// Copies an asset into dir, unless a file of the same size is
// already there. Written under a temporary name and renamed, so a
// copy cut short is never taken for the IWAD.
//
static void CopyAsset (AAssetManager* mgr, const char* dir, const char* name)
{
    char	path[1024];
    char	temp[1040];
    char	buf[65536];
    AAsset*	asset;
    FILE*	f;
    off_t	size;
    struct stat	st;
    int		n;
    int		ok;

    asset = AAssetManager_open (mgr, name, AASSET_MODE_STREAMING);
    if (!asset)
    {
	printf ("Android: %s is not in the APK\n", name);
	return;
    }
    size = AAsset_getLength (asset);
    snprintf (path, sizeof(path), "%s/%s", dir, name);
    if (!stat (path, &st) && st.st_size == size)
    {
	AAsset_close (asset);
	return;
    }

    snprintf (temp, sizeof(temp), "%s.part", path);
    f = fopen (temp, "wb");
    if (!f)
    {
	printf ("Android: can not write %s: %s\n", temp, strerror (errno));
	AAsset_close (asset);
	return;
    }
    ok = 1;
    while ((n = AAsset_read (asset, buf, sizeof(buf))) > 0)
	if (fwrite (buf, 1, n, f) != (size_t)n)
	    ok = 0;
    if (n < 0)
	ok = 0;
    if (fclose (f))
	ok = 0;
    AAsset_close (asset);

    if (!ok || rename (temp, path))
    {
	printf ("Android: could not copy %s to %s\n", name, path);
	unlink (temp);
	return;
    }
    printf ("Android: copied %s to %s (%ld bytes)\n", name, dir, (long)size);
}


//
// ReadArgs
// There is no command line on Android; args.txt in the data
// folder stands in for one ("-warp 1 1 -skill 4"). It can be put
// there with "adb shell run-as".
//
static void ReadArgs (const char* dir)
{
    static char*	args[MAXARGS + 2];
    char		path[1024];
    char		word[256];
    FILE*		f;
    int			n;

    snprintf (path, sizeof(path), "%s/args.txt", dir);
    f = fopen (path, "r");
    if (!f)
	return;

    args[0] = myargv[0];
    n = 1;
    while (n <= MAXARGS && fscanf (f, "%255s", word) == 1)
	args[n++] = strdup (word);
    fclose (f);
    args[n] = NULL;

    myargc = n;
    myargv = args;
    printf ("Android: %d arguments from %s\n", n - 1, path);
}


int I_AndroidHasTouchscreen (void)
{
    struct android_app*	app = GetAndroidApp ();

    return !app->config
	|| AConfiguration_getTouchscreen (app->config)
	   != ACONFIGURATION_TOUCHSCREEN_NOTOUCH;
}


void I_AndroidSetup (void)
{
    struct android_app*	app = GetAndroidApp ();
    const char*		dir;
    int			i;

    RedirectOutput ();

    dir = app->activity->internalDataPath;
    if (!dir)
    {
	printf ("Android: no internal storage; playing without a data folder\n");
	return;
    }
    mkdir (dir, 0700);

    for (i = 0; i < (int)(sizeof(assets)/sizeof(assets[0])); i++)
	CopyAsset (app->activity->assetManager, dir, assets[i]);

    setenv ("HOME", dir, 1);
    setenv ("DOOMWADDIR", dir, 1);
    if (chdir (dir))
	printf ("Android: can not enter %s: %s\n", dir, strerror (errno));

    ReadArgs (dir);
}
