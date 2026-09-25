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
//	System interface for sound.
//	The original software mixer, playing through a raylib
//	audio stream instead of /dev/dsp.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_unix.c,v 1.5 1997/02/03 22:45:10 b1 Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomdef.h"

#include "i_raylib.h"


// The number of internal mixing channels,
//  the samples calculated for each mixing step,
//  and the samplerate of the mix.
#define SAMPLECOUNT		256
#define NUM_CHANNELS		8

#define SAMPLERATE		11025	// Hz

// How far ahead of the audio device to mix.
// Enough to ride out a slow frame, short enough to not lag.
#define MIXAHEAD		(SAMPLERATE/12)


// Set when an audio device was opened.
static boolean	sound_ready;

// The actual lengths of all sound effects.
int 		lengths[NUMSFX];

// Sample rate of each sound effect, from the lump header.
int		rates[NUMSFX];

// The global mixing buffer, 16bit stereo, interleaved.
signed short	mixbuffer[SAMPLECOUNT*2];


// The channel step amount...
unsigned int	channelstep[NUM_CHANNELS];
// ... and a 0.16 bit remainder of last step.
unsigned int	channelstepremainder[NUM_CHANNELS];


// The channel data pointers, start and end.
unsigned char*	channels[NUM_CHANNELS];
unsigned char*	channelsend[NUM_CHANNELS];


// Time/gametic that the channel started playing,
//  used to determine oldest, which automatically
//  has lowest priority.
// In case number of active sounds exceeds
//  available channels.
int		channelstart[NUM_CHANNELS];

// The sound in channel handles,
//  determined on registration,
//  used to stop/modify/query.
int 		channelhandles[NUM_CHANNELS];

// SFX id of the playing sound effect.
// Used to catch duplicates (like chainsaw).
int		channelids[NUM_CHANNELS];

// Pitch to stepping lookup.
int		steptable[256];

// Volume lookups.
int		vol_lookup[128*256];

// Hardware left and right channel volume lookup.
int*		channelleftvol_lookup[NUM_CHANNELS];
int*		channelrightvol_lookup[NUM_CHANNELS];



//
// This function loads the sound data from the WAD lump,
//  for single sound.
//
void*
getsfx
( char*         sfxname,
  int*          len,
  int*		rate )
{
    unsigned char*      sfx;
    unsigned char*      data;
    int                 size;
    char                name[20];
    int                 sfxlump;


    // Get the sound data from the WAD, allocate lump
    //  in zone memory.
    sprintf(name, "ds%s", sfxname);

    // Now, there is a severe problem with the
    //  sound handling, in it is not (yet/anymore)
    //  gamemode aware. That means, sounds from
    //  DOOM II will be requested even with DOOM
    //  shareware.
    // The sound list is wired into sounds.c,
    //  which sets the external variable.
    // I do not do runtime patches to that
    //  variable. Instead, we will use a
    //  default sound for replacement.
    if ( W_CheckNumForName(name) == -1 )
      sfxlump = W_GetNumForName("dspistol");
    else
      sfxlump = W_GetNumForName(name);

    size = W_LumpLength( sfxlump );
    sfx = (unsigned char*)W_CacheLumpNum( sfxlump, PU_CACHE );

    // Header: format (3), sample rate, sample count.
    if (size < 8)
    {
	*len = 0;
	*rate = SAMPLERATE;
	return NULL;
    }

    *rate = sfx[2] | (sfx[3]<<8);
    if (*rate == 0)
	*rate = SAMPLERATE;

    *len = sfx[4] | (sfx[5]<<8) | (sfx[6]<<16) | (sfx[7]<<24);
    if (*len <= 0 || *len > size-8)
	*len = size-8;

    // Keep a private copy; the lump is purgable.
    data = (unsigned char*)Z_Malloc( *len, PU_STATIC, 0 );
    memcpy( data, sfx+8, *len );

    return (void *) data;
}





//
// This function adds a sound to the
//  list of currently active sounds,
//  which is maintained as a given number
//  (eight, usually) of internal channels.
// Returns a handle.
//
int
addsfx
( int		sfxid,
  int		volume,
  int		step,
  int		seperation )
{
    static unsigned short	handlenums = 0;

    int		i;
    int		rc = -1;

    int		oldest = gametic;
    int		oldestnum = 0;
    int		slot;

    // Chainsaw troubles.
    // Play these sound effects only one at a time.
    if ( sfxid == sfx_sawup
	 || sfxid == sfx_sawidl
	 || sfxid == sfx_sawful
	 || sfxid == sfx_sawhit
	 || sfxid == sfx_stnmov
	 || sfxid == sfx_pistol	 )
    {
	// Loop all channels, check.
	for (i=0 ; i<NUM_CHANNELS ; i++)
	{
	    // Active, and using the same SFX?
	    if ( (channels[i])
		 && (channelids[i] == sfxid) )
	    {
		// Reset.
		channels[i] = 0;
		// We are sure that iff,
		//  there will only be one.
		break;
	    }
	}
    }

    // Loop all channels to find oldest SFX.
    for (i=0; (i<NUM_CHANNELS) && (channels[i]); i++)
    {
	if (channelstart[i] < oldest)
	{
	    oldestnum = i;
	    oldest = channelstart[i];
	}
    }

    // Tales from the cryptic.
    // If we found a channel, fine.
    // If not, we simply overwrite the first one, 0.
    // Probably only happens at startup.
    if (i == NUM_CHANNELS)
	slot = oldestnum;
    else
	slot = i;

    // Okay, in the less recent channel,
    //  we will handle the new SFX.
    // Set pointer to raw data.
    channels[slot] = (unsigned char *) S_sfx[sfxid].data;
    // Set pointer to end of raw data.
    channelsend[slot] = channels[slot] + lengths[sfxid];

    // Reset current handle number, limited to 0..100.
    if (!handlenums)
	handlenums = 100;

    // Assign current handle number.
    // Preserved so sounds could be stopped.
    channelhandles[slot] = rc = handlenums++;

    // Pitch, scaled by the sound's own sample rate.
    channelstep[slot] =
	(unsigned int)(((long long)step * rates[sfxid]) / SAMPLERATE);
    channelstepremainder[slot] = 0;
    // Should be gametic, I presume.
    channelstart[slot] = gametic;

    // Preserve sound SFX id,
    //  e.g. for avoiding duplicates of chainsaw.
    channelids[slot] = sfxid;

    I_UpdateSoundParams (rc, volume, seperation, 0);

    return rc;
}



//
// Find the channel playing a handle, -1 if gone.
//
static int getchannel (int handle)
{
    int		i;

    for (i=0 ; i<NUM_CHANNELS ; i++)
	if (channels[i] && channelhandles[i] == handle)
	    return i;
    return -1;
}



//
// SFX API
// Note: this was called by S_Init.
// However, whatever they did in the
// old DPMS based DOS version, this
// were simply dummies in the Linux
// version.
// See soundserver initdata().
//
void I_SetChannels()
{
  // Init internal lookups (raw data, mixing buffer, channels).
  // This function sets up internal lookups used during
  //  the mixing process.
  int		i;
  int		j;

  int*	steptablemid = steptable + 128;

  // This table provides step widths for pitch parameters.
  for (i=-128 ; i<128 ; i++)
    steptablemid[i] = (int)(pow(2.0, (i/64.0))*65536.0);


  // Generates volume lookup tables
  //  which also turn the unsigned samples
  //  into signed samples.
  for (i=0 ; i<128 ; i++)
    for (j=0 ; j<256 ; j++)
      vol_lookup[i*256+j] = (i*(j-128)*256)/127;
}


void I_SetSfxVolume(int volume)
{
  // Identical to DOS.
  // Basically, this should propagate
  //  the menu/config file setting
  //  to the state variable used in
  //  the mixing.
  snd_SfxVolume = volume;
}



//
// Retrieve the raw data lump index
//  for a given SFX name.
//
int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

//
// Starting a sound means adding it
//  to the current list of active sounds
//  in the internal channels.
// As the SFX info struct contains
//  e.g. a pointer to the raw data,
//  it is ignored.
// As our sound handling does not handle
//  priority, it is ignored.
//
int
I_StartSound
( int		id,
  int		vol,
  int		sep,
  int		pitch,
  int		priority )
{
    if (!sound_ready || !S_sfx[id].data)
	return -1;

    return addsfx( id, vol, steptable[pitch], sep );
}



void I_StopSound (int handle)
{
    int chan = getchannel (handle);

    if (chan >= 0)
	channels[chan] = 0;
}


int I_SoundIsPlaying(int handle)
{
    return getchannel (handle) >= 0;
}




//
// This function loops all active (internal) sound
//  channels, retrieves a given number of samples
//  from the raw sound data, modifies it according
//  to the current (internal) channel parameters,
//  mixes the per channel samples into the global
//  mixbuffer, clamping it to the allowed range.
//
static void I_MixSound( void )
{
  // Mix current sound data.
  // Data, from raw sound, for right and left.
  register unsigned int	sample;
  register int		dl;
  register int		dr;

  // Pointers in global mixbuffer, left, right, end.
  signed short*		leftout;
  signed short*		rightout;
  signed short*		leftend;
  // Step in mixbuffer, left and right, thus two.
  int				step;

  // Mixing channel index.
  int				chan;

    // Left and right channel
    //  are in global mixbuffer, alternating.
    leftout = mixbuffer;
    rightout = mixbuffer+1;
    step = 2;

    // Determine end, for left channel only
    //  (right channel is implicit).
    leftend = mixbuffer + SAMPLECOUNT*step;

    // Mix sounds into the mixing buffer.
    while (leftout != leftend)
    {
	// Reset left/right value.
	dl = 0;
	dr = 0;

	// Love thy L2 chache - made this a loop.
	// Now more channels could be set at compile time
	//  as well. Thus loop those  channels.
	for ( chan = 0; chan < NUM_CHANNELS; chan++ )
	{
	    // Check channel, if active.
	    if (channels[ chan ])
	    {
		// Get the raw data from the channel.
		sample = *channels[ chan ];
		// Add left and right part
		//  for this channel (sound)
		//  to the current data.
		// Adjust volume accordingly.
		dl += channelleftvol_lookup[ chan ][sample];
		dr += channelrightvol_lookup[ chan ][sample];
		// Step through the sample, 16.16 fixed point.
		channelstepremainder[ chan ] += channelstep[ chan ];
		channels[ chan ] += channelstepremainder[ chan ] >> 16;
		channelstepremainder[ chan ] &= 65536-1;

		// Check whether we are done.
		if (channels[ chan ] >= channelsend[ chan ])
		    channels[ chan ] = 0;
	    }
	}

	// Clamp to range. Left hardware channel.
	if (dl > 0x7fff)
	    *leftout = 0x7fff;
	else if (dl < -0x8000)
	    *leftout = -0x8000;
	else
	    *leftout = dl;

	// Same for right hardware channel.
	if (dr > 0x7fff)
	    *rightout = 0x7fff;
	else if (dr < -0x8000)
	    *rightout = -0x8000;
	else
	    *rightout = dr;

	// Increment current pointers in mixbuffer.
	leftout += step;
	rightout += step;
    }
}


//
// Keep the audio stream fed. Mixing happens here, on the game
//  thread, so the channel state never races the audio thread.
//
void I_UpdateSound( void )
{
    int		needed;

    if (!sound_ready)
	return;

    // Measured once, so a device that drains faster than real
    // time can't keep us here.
    needed = MIXAHEAD - RL_AudioQueued(RL_SFX);
    while (needed > 0)
    {
	I_MixSound ();
	RL_QueueAudio (RL_SFX, mixbuffer, SAMPLECOUNT);
	needed -= SAMPLECOUNT;
    }

    I_UpdateMusic ();
}


//
// Nothing left to do here: raylib pulls the
//  mixed audio from its own thread.
//
void
I_SubmitSound(void)
{
}



void
I_UpdateSoundParams
( int	handle,
  int	vol,
  int	sep,
  int	pitch)
{
    int		chan;
    int		leftvol;
    int		rightvol;

    chan = getchannel (handle);
    if (chan < 0)
	return;

    // Separation, that is, orientation/stereo.
    //  range is: 1 - 256
    sep += 1;

    // Per left/right channel.
    //  x^2 seperation,
    //  adjust volume properly.
    leftvol = vol - ((vol*sep*sep) >> 16); ///(256*256);
    sep = sep - 257;
    rightvol = vol - ((vol*sep*sep) >> 16);

    // Sanity check, clamp volume.
    if (rightvol < 0 || rightvol > 127)
	I_Error("rightvol out of bounds");

    if (leftvol < 0 || leftvol > 127)
	I_Error("leftvol out of bounds");

    // Get the proper lookup table piece
    //  for this volume level.
    channelleftvol_lookup[chan] = &vol_lookup[leftvol*256];
    channelrightvol_lookup[chan] = &vol_lookup[rightvol*256];
}




void I_ShutdownSound(void)
{
    sound_ready = false;
    RL_ShutdownAudio ();
}






void
I_InitSound()
{
  int i;

  // Initialize external data (all sounds) at start, keep static.
  // Done even without a device so S_StartSound finds its data.
  fprintf( stderr, "I_InitSound: ");
  
  for (i=1 ; i<NUMSFX ; i++)
  { 
    // Alias? Example is the chaingun sound linked to pistol.
    if (!S_sfx[i].link)
    {
      // Load data from WAD file.
      S_sfx[i].data = getsfx( S_sfx[i].name, &lengths[i], &rates[i] );
    }	
    else
    {
      // Previously loaded already?
      S_sfx[i].data = S_sfx[i].link->data;
      lengths[i] = lengths[S_sfx[i].link - S_sfx];
      rates[i] = rates[S_sfx[i].link - S_sfx];
    }
  }

  fprintf( stderr, " pre-cached all sound data\n");
  
  // Now initialize mixbuffer with zero.
  memset (mixbuffer, 0, sizeof(mixbuffer));

  fprintf( stderr, "I_InitSound: ");

  if (M_CheckParm("-nosound") || M_CheckParm("-nosfx"))
  {
    fprintf(stderr, " sound disabled\n");
    return;
  }

  if (!RL_InitAudio () || !RL_OpenStream (RL_SFX, SAMPLERATE))
  {
    fprintf(stderr, " could not open audio device\n");
    return;
  }

  sound_ready = true;
  fprintf(stderr, " configured audio device\n");
  
  // Finished initialization.
  fprintf(stderr, "I_InitSound: sound module ready\n");

  I_InitMusic ();
}
