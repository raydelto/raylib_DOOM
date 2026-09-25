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
//	DOOM graphics stuff for raylib.
//	The software renderer still draws 8-bit paletted pixels into
//	screens[0]; this expands them to RGBA and hands them to raylib.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

#include "doomdef.h"

#include "i_raylib.h"

#ifdef __GNUG__
#pragma implementation "i_video.h"
#endif
#include "i_video.h"


extern int	usemouse;

// Current palette as RGBA bytes, gamma applied.
static byte	palette[256*4];

// screens[0] expanded through the palette.
static byte	rgbabuffer[SCREENWIDTH*SCREENHEIGHT*4];


void I_ShutdownGraphics(void)
{
    RL_SetMouseGrab (0);
    RL_ShutdownVideo ();
}



//
// I_StartFrame
//
void I_StartFrame (void)
{
    // er?
}


//
// UpdateMouseGrab
// Only hold on to the pointer while actually playing.
//
static void UpdateMouseGrab (void)
{
    boolean	grab;

    grab = usemouse
	&& !menuactive
	&& !paused
	&& !demoplayback
	&& gamestate == GS_LEVEL;

    RL_SetMouseGrab (grab);
}


//
// I_StartTic
//
void I_StartTic (void)
{
    rl_event_t	rlev;
    event_t	event;

    RL_PumpEvents ();

    if (RL_QuitRequested ())
	I_Quit ();

    while (RL_GetEvent (&rlev))
    {
	switch (rlev.type)
	{
	  case rl_keydown:
	    event.type = ev_keydown;
	    break;
	  case rl_keyup:
	    event.type = ev_keyup;
	    break;
	  case rl_mouse:
	    if (!usemouse)
		continue;
	    event.type = ev_mouse;
	    break;
	  default:
	    continue;
	}

	event.data1 = rlev.data1;
	event.data2 = rlev.data2;
	event.data3 = rlev.data3;
	D_PostEvent (&event);
    }

    UpdateMouseGrab ();
}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{

    static int	lasttic;
    int		tics;
    int		i;
    byte*	src;
    byte*	dst;

    // draws little dots on the bottom of the screen
    if (devparm)
    {

	i = I_GetTime();
	tics = i - lasttic;
	lasttic = i;
	if (tics > 20) tics = 20;

	for (i=0 ; i<tics*2 ; i+=2)
	    screens[0][ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0xff;
	for ( ; i<20*2 ; i+=2)
	    screens[0][ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0x0;

    }

    src = screens[0];
    dst = rgbabuffer;
    for (i=0 ; i<SCREENWIDTH*SCREENHEIGHT ; i++, dst+=4)
	memcpy (dst, &palette[*src++ * 4], 4);

    RL_Present (rgbabuffer);
}


//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, screens[0], SCREENWIDTH*SCREENHEIGHT);
}


//
// I_SetPalette
//
void I_SetPalette (byte* pal)
{
    int		i;

    for (i=0 ; i<256 ; i++)
    {
	palette[i*4+0] = gammatable[usegamma][*pal++];
	palette[i*4+1] = gammatable[usegamma][*pal++];
	palette[i*4+2] = gammatable[usegamma][*pal++];
	palette[i*4+3] = 0xff;
    }
}


static void I_SignalQuit (int sig)
{
    I_Quit ();
}


void I_InitGraphics(void)
{
    static int	firsttime=1;
    int		scale;
    int		p;

    if (!firsttime)
	return;
    firsttime = 0;

    signal(SIGINT, I_SignalQuit);

    // Window size, as a multiple of 320x240.
    scale = 3;
    if (M_CheckParm("-1"))
	scale = 1;
    if (M_CheckParm("-2"))
	scale = 2;
    if (M_CheckParm("-3"))
	scale = 3;
    if (M_CheckParm("-4"))
	scale = 4;
    p = M_CheckParm("-scale");
    if (p && p < myargc-1)
	scale = atoi(myargv[p+1]);
    if (scale < 1)
	scale = 1;

    RL_InitVideo (SCREENWIDTH, SCREENHEIGHT, scale,
		  M_CheckParm("-fullscreen") != 0);

    screens[0] = (unsigned char *) malloc (SCREENWIDTH * SCREENHEIGHT);
}
