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
//	PlayStation 2: the IOP modules, and where files are.
//	raylib4PlayStation2 loads the pad modules itself, in InitWindow.
//
//-----------------------------------------------------------------------------

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
// Only for fileXioInit, which moves the C library's files onto
// fileXio; they are still opened with open and fopen.
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <libcdvd.h>
#include <libpad.h>

#include "i_ps2.h"


// IOP modules built into the ELF by Makefile.ps2 (bin2c).
#define IRX(name) \
    extern unsigned char name##_irx[]; \
    extern unsigned int size_##name##_irx

IRX (iomanX);
IRX (fileXio);
IRX (usbd);
IRX (bdm);
IRX (bdmfs_fatfs);
IRX (usbmass_bd);

static char	bootdir[256];


//
// LoadIRX
// Starts a module from EE memory. True if it stays resident.
//
static int LoadIRX (const char* name, unsigned char* irx, unsigned int size)
{
    int		ret;
    int		result;

    ret = SifExecModuleBuffer (irx, size, 0, NULL, &result);
    if (ret < 0 || result == 1)	// 1: NO_RESIDENT_END, it quit
    {
	printf ("I_PS2_Init: could not load %s (%d, %d)\n", name, ret, result);
	return 0;
    }
    return 1;
}


//
// SetBootDir
// argv[0] minus the file name: "host:DOOM.ELF", "host:/games/DOOM.ELF",
// "cdrom0:\DOOM.ELF;1", "mass:/DOOM/DOOM.ELF".
//
static void SetBootDir (const char* argv0)
{
    const char*	end = NULL;
    const char*	p;
    size_t	len;

    bootdir[0] = 0;
    if (!argv0 || !strchr (argv0, ':'))
	return;

    for (p = argv0; *p; p++)
	if (*p == ':' || *p == '/' || *p == '\\')
	    end = p + 1;

    len = end - argv0;
    if (len >= sizeof(bootdir))
	return;
    memcpy (bootdir, argv0, len);
    bootdir[len] = 0;

    // "cdrom0:" alone; files on the CD are "cdrom0:\NAME;1".
    if (!strncasecmp (bootdir, "cdrom", 5) && bootdir[len-1] == ':'
	&& len + 1 < sizeof(bootdir))
	strcat (bootdir, "\\");
}


void I_PS2_Init (int argc, char** argv)
{
    int		fromhost;
    int		ok;

    SetBootDir (argc > 0 ? argv[0] : NULL);

    // ps2link and the like give host: through their own IOP
    // modules, which a reset would unload. PCSX2 serves host:
    // itself, whatever the IOP runs.
    fromhost = !strncasecmp (bootdir, "host", 4);

    SifInitRpc (0);
    if (!fromhost)
    {
	// A clean IOP, as the BIOS boots it (with CDVDMAN for
	// cdrom0:), that also takes modules from EE memory.
	while (!SifIopReset ("", 0))
	    ;
	while (!SifIopSync ())
	    ;
	SifInitRpc (0);
	sbv_patch_enable_lmb ();
	sbv_patch_disable_prefix_check ();

	ok = LoadIRX ("iomanX", iomanX_irx, size_iomanX_irx)
	    && LoadIRX ("fileXio", fileXio_irx, size_fileXio_irx);
	// Files go through fileXio from here on, which sees the
	// mass: device as well as the BIOS ones.
	if (ok && fileXioInit () >= 0)
	{
	    ok = LoadIRX ("usbd", usbd_irx, size_usbd_irx)
		&& LoadIRX ("bdm", bdm_irx, size_bdm_irx)
		&& LoadIRX ("bdmfs_fatfs", bdmfs_fatfs_irx, size_bdmfs_fatfs_irx)
		&& LoadIRX ("usbmass_bd", usbmass_bd_irx, size_usbmass_bd_irx);
	    if (!ok)
		printf ("I_PS2_Init: no USB storage (mass:)\n");
	}

	sceCdInit (SCECdINoD);
    }

    printf ("I_PS2_Init: started from %s\n",
	    bootdir[0] ? bootdir : "an unknown device");

    // Saved games and the settings go next to DOOM.ELF.
    if (bootdir[0] && strncasecmp (bootdir, "cdrom", 5))
	chdir (bootdir);
}


const char* I_PS2_BootDir (void)
{
    return bootdir;
}


int I_PS2_CanRead (const char* path)
{
    int		fd;

    fd = open (path, O_RDONLY);
    if (fd < 0)
	return 0;
    close (fd);
    return 1;
}


char* I_PS2_WadPath (const char* dir, const char* name)
{
    const char*	sep = "/";
    char*	path;
    char*	p;
    size_t	len;
    int		cd;

    if (!strcmp (dir, "."))
	dir = bootdir[0] ? bootdir : "host:";

    len = strlen (dir);
    cd = !strncasecmp (dir, "cdrom", 5);
    path = malloc (len + 1 + strlen (name) + 3);

    if (cd)
    {
	sprintf (path, "%s%s;1", dir, name);
	for (p = path + len; *p; p++)
	    *p = toupper (*p);
	return path;
    }

    if (len && (dir[len-1] == ':' || dir[len-1] == '/' || dir[len-1] == '\\'))
	sep = "";
    sprintf (path, "%s%s%s", dir, sep, name);
    if (I_PS2_CanRead (path))
	return path;

    for (p = path + len; *p; p++)
	*p = toupper (*p);
    if (I_PS2_CanRead (path))
	return path;

    // Neither; the name as given, for the error message.
    sprintf (path, "%s%s%s", dir, sep, name);
    return path;
}


static float StickAxis (unsigned char value)
{
    return (value - 128) / 128.0f;
}

void I_PS2_Sticks (float* lx, float* ly, float* rx, float* ry)
{
    struct padButtonStatus	pad;

    *lx = *ly = *rx = *ry = 0;

    // Port 0, slot 0: the one raylib opened.
    if (!padRead (0, 0, &pad))
	return;
    // High nibble 7: analog (DualShock) mode.
    if ((pad.mode >> 4) != 7)
	return;

    *lx = StickAxis (pad.ljoy_h);
    *ly = StickAxis (pad.ljoy_v);
    *rx = StickAxis (pad.rjoy_h);
    *ry = StickAxis (pad.rjoy_v);
}
