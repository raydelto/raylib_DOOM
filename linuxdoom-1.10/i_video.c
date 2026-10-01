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

#include <stdio.h>
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
#if defined(DOOM_XR) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#include "i_xr.h"
#endif

#ifdef __GNUG__
#pragma implementation "i_video.h"
#endif
#include "i_video.h"


extern int	usemouse;


#if defined(DOOM_XR) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
//
// Headset controllers, and on Android the gamepad and the touch
// controls, which report the same buttons (RL_PadButtons), as do
// the touch controls of the web page.
// The buttons become the keys they stand for: the player's key
// bindings in the game, the fixed menu keys while it is up.
//
extern int	key_right;
extern int	key_left;
extern int	key_up;
extern int	key_down;
extern int	key_strafeleft;
extern int	key_straferight;
extern int	key_fire;
extern int	key_use;
extern int	key_speed;

// m_menu.c: a message is up, and it waits for y or n.
extern int	messageToPrint;
extern boolean	messageNeedsInput;

static unsigned	xrheld;

// Key sent for each held button, so its release matches even if
// the menu opened or closed in between.
static int	xrkeys[XR_NUMBUTTONS];

#ifdef __EMSCRIPTEN__
extern int	key_strafe;

// m_menu.c: typing a savegame name into slot saveSlot.
#define SAVESTRINGSIZE	24
extern int	saveStringEnter;
extern int	saveSlot;
extern char	savegamestrings[10][SAVESTRINGSIZE];

#define WEBBUTTONS	(XR_STRAFE|XR_WEAPPREV|XR_WEAPNEXT|XR_ENTER|XR_YES)

// The key, 1 to 7, that selects each weapon.
static const char weaponslot[NUMWEAPONS] =
{
    1, 2, 3, 4, 5, 6, 7, 1, 3	// ... chainsaw, super shotgun
};

//
// WeaponKey
// The key of the next (dir 1) or previous (-1) weapon slot that
// holds a weapon the player has. As on the keyboard, the game
// picks between the fist and the chainsaw, and the two shotguns.
//
static int WeaponKey (int dir)
{
    player_t*		player = &players[consoleplayer];
    weapontype_t	current;
    int			slot;
    int			i;
    int			w;

    current = player->pendingweapon != wp_nochange
	? player->pendingweapon : player->readyweapon;
    if (current < 0 || current >= NUMWEAPONS)
	return 0;

    slot = weaponslot[current];
    for (i = 0; i < 7; i++)
    {
	slot = (slot - 1 + 7 + dir) % 7 + 1;
	for (w = 0; w < NUMWEAPONS; w++)
	    if (weaponslot[w] == slot && player->weaponowned[w])
		return '0' + slot;
    }
    return 0;
}

//
// WebKey
// Buttons only the web page's touch controls have, for keys a
// phone has no other way to press.
//
static int WebKey (unsigned button)
{
    if (messageToPrint && messageNeedsInput)
	return button == XR_YES || button == XR_ENTER ? 'y' : 0;

    if (menuactive)
    {
	switch (button)
	{
	  case XR_WEAPPREV:	return KEY_LEFTARROW;
	  case XR_WEAPNEXT:	return KEY_RIGHTARROW;
	  case XR_ENTER:	return KEY_ENTER;
	  case XR_YES:		return 'y';
	}
	return 0;
    }

    switch (button)
    {
      case XR_STRAFE:		return key_strafe;
      case XR_WEAPPREV:		return WeaponKey (-1);
      case XR_WEAPNEXT:		return WeaponKey (1);
      case XR_ENTER:		return KEY_ENTER;
      case XR_YES:		return 'y';
    }
    return 0;
}

//
// NameSave
// The menu saves a game only once it has a name, which a phone
// can not type. Enter on an empty name names it after the slot.
//
static void NameSave (void)
{
    event_t	event;
    char	name[16];
    char*	c;

    if (!saveStringEnter || savegamestrings[saveSlot][0])
	return;

    sprintf (name, "SLOT %d", saveSlot + 1);
    event.data2 = event.data3 = 0;
    for (c = name; *c; c++)
    {
	event.type = ev_keydown;
	event.data1 = *c;
	D_PostEvent (&event);
	event.type = ev_keyup;
	D_PostEvent (&event);
    }
}
#endif

static int XRKey (unsigned button)
{
#ifdef __EMSCRIPTEN__
    if (button & WEBBUTTONS)
	return WebKey (button);
#endif

    // Yes/no prompts (quit, end game, overwrite a save, nightmare)
    // ignore Enter and Backspace, so answer them directly.
    if (messageToPrint && messageNeedsInput)
    {
	switch (button)
	{
	  case XR_FIRE:
	  case XR_USE:		return 'y';
	  case XR_RUN:
	  case XR_MAP:		return 'n';
	  case XR_MENU:		return KEY_ESCAPE;
	}
	return 0;
    }

    if (menuactive)
    {
	switch (button)
	{
	  case XR_FORWARD:	return KEY_UPARROW;
	  case XR_BACK:		return KEY_DOWNARROW;
	  case XR_TURNLEFT:
	  case XR_STRAFELEFT:	return KEY_LEFTARROW;
	  case XR_TURNRIGHT:
	  case XR_STRAFERIGHT:	return KEY_RIGHTARROW;
	  case XR_FIRE:
	  case XR_USE:		return KEY_ENTER;
	  case XR_RUN:
	  case XR_MAP:		return KEY_BACKSPACE;
	  case XR_MENU:		return KEY_ESCAPE;
	}
	return 0;
    }

    switch (button)
    {
      case XR_FORWARD:		return key_up;
      case XR_BACK:		return key_down;
      case XR_TURNLEFT:		return key_left;
      case XR_TURNRIGHT:	return key_right;
      case XR_STRAFELEFT:	return key_strafeleft;
      case XR_STRAFERIGHT:	return key_straferight;
      case XR_FIRE:		return key_fire;
      case XR_USE:		return key_use;
      case XR_RUN:		return key_speed;
      case XR_MENU:		return KEY_ESCAPE;
      case XR_MAP:		return KEY_TAB;
    }
    return 0;
}

static void I_PostXRButtons (void)
{
    unsigned	now;
    unsigned	bit;
    int		i;
    event_t	event;

    now = 0;
#ifdef DOOM_XR
    now |= XR_Buttons ();
#endif
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    now |= RL_PadButtons ();
#endif
    for (i = 0; i < XR_NUMBUTTONS; i++)
    {
	bit = 1u << i;
	if (!((now ^ xrheld) & bit))
	    continue;

	if (now & bit)
	{
	    xrkeys[i] = XRKey (bit);
	    event.type = ev_keydown;
	}
	else
	    event.type = ev_keyup;

	event.data1 = xrkeys[i];
	event.data2 = event.data3 = 0;
#ifdef __EMSCRIPTEN__
	if (event.type == ev_keydown && event.data1 == KEY_ENTER)
	    NameSave ();
#endif
	if (event.data1)
	    D_PostEvent (&event);
    }
    xrheld = now;
}
#endif

#ifdef DOOM_XR

//
// I_InitXR
// Without a runtime or headset the game is played in the window,
// as with -noxr, which does not try (-xrflat, once needed for
// that, is still accepted). -xrdist and -xrwidth place
// the screen; without -xrwidth it is fitted to the field of view.
// -xrlift N raises black to N% of white (default: 12 on
// see-through glasses, 0 on other headsets). -xrstats logs frame
// pacing.
//
static void I_InitXR (void)
{
    float	distance = 2.5f;
    float	width = 0;
    float	lift = -1;
    int		p;

    if (M_CheckParm ("-noxr"))
	return;

    p = M_CheckParm ("-xrdist");
    if (p && p < myargc-1 && atof (myargv[p+1]) > 0)
	distance = atof (myargv[p+1]);
    p = M_CheckParm ("-xrwidth");
    if (p && p < myargc-1 && atof (myargv[p+1]) > 0)
	width = atof (myargv[p+1]);
    p = M_CheckParm ("-xrlift");
    if (p && p < myargc-1)
    {
	lift = atof (myargv[p+1]) / 100.0f;
	if (lift < 0)
	    lift = 0;
	if (lift > 0.5f)
	    lift = 0.5f;
    }

    XR_Stats (M_CheckParm ("-xrstats") != 0);
    if (!XR_Init (distance, width, lift))
	printf ("XR: no headset, playing in the window\n");
}
#endif

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

#ifdef DOOM_XR
    if (!XR_Update ())
	I_Quit ();
#endif
#if defined(DOOM_XR) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    I_PostXRButtons ();
#endif

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

#ifdef __APPLE__
    // The WADs are open; saved games go to Application Support.
    I_BundleEnterDataDir();
#endif

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

#ifdef DOOM_XR
    if (!M_CheckParm ("-noxr"))
	XR_PrepareGL ();
#endif
    RL_InitVideo (SCREENWIDTH, SCREENHEIGHT, scale,
		  M_CheckParm("-fullscreen") != 0);

#ifdef DOOM_XR
    I_InitXR ();
#endif
}
