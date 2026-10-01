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
//	Android start-up: logcat, the command line and the data folder.
//
//	raylib's NativeActivity glue (rcore_android.c) calls main with
//	no arguments, no $HOME, "/" as the current directory and stdout
//	going nowhere. The APK has no game data: the launcher
//	(android/app/src/main/java/com/raylib/doom/WadActivity.java)
//	finds the player's WADs and writes the IWAD and PWADs to use as
//	a command line in launch.txt in the app's internal storage.
//
//-----------------------------------------------------------------------------

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include "m_argv.h"

#include "i_android.h"


#define LOGTAG		"raylibdoom"
#define MAXARGS		64

// raylib's rcore_android.c
extern struct android_app* GetAndroidApp (void);

static int	logpipe[2];
static const char*	datadir;


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
// ReadArgs
// There is no command line on Android. launch.txt in the data
// folder, written by the launcher, has one argument per line
// ("-iwad", the IWAD's path, "-file" and the PWADs' paths), so
// paths may hold spaces. args.txt, which can be put there with
// "adb shell run-as", adds words of its own ("-warp 1 1 -skill 4").
//
static int ReadFile (const char* dir, const char* name, int lines,
		     char** args, int n)
{
    char	path[1024];
    char	word[1024];
    FILE*	f;
    int		first = n;
    size_t	len;

    snprintf (path, sizeof(path), "%s/%s", dir, name);
    f = fopen (path, "r");
    if (!f)
	return n;

    while (n <= MAXARGS)
    {
	if (lines)
	{
	    if (!fgets (word, sizeof(word), f))
		break;
	    len = strcspn (word, "\r\n");
	    word[len] = '\0';
	    if (!len)
		continue;
	}
	else if (fscanf (f, "%1023s", word) != 1)
	    break;
	args[n++] = strdup (word);
    }
    fclose (f);
    printf ("Android: %d arguments from %s\n", n - first, path);
    return n;
}


static void ReadArgs (const char* dir)
{
    static char*	args[MAXARGS + 2];
    int			n;

    args[0] = myargv[0];
    n = ReadFile (dir, "launch.txt", 1, args, 1);
    n = ReadFile (dir, "args.txt", 0, args, n);
    args[n] = NULL;

    myargc = n;
    myargv = args;
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

    RedirectOutput ();

    dir = app->activity->internalDataPath;
    if (!dir)
    {
	printf ("Android: no internal storage; playing without a data folder\n");
	return;
    }
    mkdir (dir, 0700);
    datadir = dir;

    setenv ("HOME", dir, 1);
    setenv ("DOOMWADDIR", dir, 1);
    if (chdir (dir))
	printf ("Android: can not enter %s: %s\n", dir, strerror (errno));

    ReadArgs (dir);
}


void I_AndroidError (const char* message)
{
    char	path[1024];
    FILE*	f;

    if (!datadir)
	return;
    snprintf (path, sizeof(path), "%s/error.txt", datadir);
    f = fopen (path, "w");
    if (!f)
	return;
    fprintf (f, "%s\n", message);
    fclose (f);
}
