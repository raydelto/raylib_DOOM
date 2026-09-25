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
//	Music, played the way DOS DOOM did on an AdLib or Sound Blaster:
//	the MUS score drives an emulated OPL2 FM chip (Nuked OPL3,
//	in OPL2 mode), with instruments from the IWAD's GENMIDI lump.
//
//	Standard MIDI files (as in Freedoom and many PWADs) are
//	sequenced here too, and drive the same voices as MUS: MIDI
//	events are translated to the MUS events they correspond to.
//
//	How DMX, DOOM's sound library, drove the chip (its frequency
//	table, volume curve and voice allocation) follows Chocolate
//	Doom's i_oplmusic.c, Copyright (C) 2005-2014 Simon Howard,
//	GPL 2 or later.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "doomdef.h"
#include "doomstat.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_swap.h"
#include "w_wad.h"
#include "z_zone.h"

#include "opl3.h"
#include "i_raylib.h"


#define OPL_RATE		49716	// the chip's native rate, Hz
#define MUS_TICRATE		140	// MUS score ticks per second

#define MIDI_MAXTRACKS		64
#define MIDI_PERCUSSION		9	// MIDI channel 10
#define MIDI_DEFAULT_TEMPO	500000	// microseconds per quarter note

#define NUM_VOICES		9	// OPL2 melodic channels
#define NUM_CHANNELS		16	// MUS channels
#define PERCUSSION_CHANNEL	15

// Frames generated per step, and how far ahead of playback to stay.
#define MIXFRAMES		512
#define MIXAHEAD		(OPL_RATE/12)

// OPL register banks. Operator registers are offset by the
// operator's slot, channel registers by the channel number.
#define OPL_REG_WAVEFORM_ENABLE	0x01
#define OPL_REG_FM_MODE		0x08
#define OPL_REGS_TREMOLO	0x20
#define OPL_REGS_LEVEL		0x40
#define OPL_REGS_ATTACK		0x60
#define OPL_REGS_SUSTAIN	0x80
#define OPL_REGS_FREQ_1		0xA0
#define OPL_REGS_FREQ_2		0xB0
#define OPL_REGS_FEEDBACK	0xC0
#define OPL_REGS_WAVEFORM	0xE0

#define OPL_KEYON		0x20

// GENMIDI instrument flags.
#define GENMIDI_FLAG_FIXED	0x0001	// always plays fixed_note
#define GENMIDI_FLAG_2VOICE	0x0004	// layers both voices

#define GENMIDI_NUM_INSTRS	128
#define GENMIDI_NUM_PERCUSSION	47	// keys 35 to 81
#define GENMIDI_HEADER		"#OPL_II#"

// MUS event types.
enum
{
    mus_releasekey,
    mus_presskey,
    mus_pitchwheel,
    mus_systemevent,
    mus_changecontroller,
    mus_measureend,
    mus_scoreend,
    mus_unused
};

// MUS controllers.
enum
{
    mus_ctrl_instrument,
    mus_ctrl_bank,
    mus_ctrl_modulation,
    mus_ctrl_volume,
    mus_ctrl_pan,
    mus_ctrl_expression,
    mus_ctrl_reverb,
    mus_ctrl_chorus,
    mus_ctrl_sustain,
    mus_ctrl_soft,
    // System events, sent with mus_systemevent.
    mus_ctrl_soundsoff,
    mus_ctrl_notesoff,
    mus_ctrl_mono,
    mus_ctrl_poly,
    mus_ctrl_resetall
};


//
// GENMIDI lump layout, as stored on disk.
//
typedef struct
{
    byte	tremolo;	// tremolo, vibrato, sustain, KSR, multiplier
    byte	attack;		// attack and decay rates
    byte	sustain;	// sustain level and release rate
    byte	waveform;
    byte	scale;		// key scale level, top two bits
    byte	level;		// output level
} genmidi_op_t;

typedef struct
{
    genmidi_op_t	modulator;
    byte		feedback;
    genmidi_op_t	carrier;
    byte		unused;
    short		base_note_offset;
} genmidi_voice_t;

typedef struct
{
    unsigned short	flags;
    byte		fine_tuning;
    byte		fixed_note;
    genmidi_voice_t	voices[2];
} genmidi_instr_t;

_Static_assert (sizeof(genmidi_instr_t) == 36, "GENMIDI record is 36 bytes");


typedef struct
{
    int		instrument;
    int		volume_base;	// as set by the score, 0-127
    int		volume;		// volume_base capped by the music volume
    int		bend;		// in 1/32 semitones, -64 to 63
    int		lastvolume;	// note volume when a key press omits it
} channel_t;

typedef struct
{
    int			index;		// OPL channel
    int			channel;	// MUS channel, -1 when free
    int			key;		// MUS key that started it
    int			note;		// note to sound: key, fixed or 60
    int			note_volume;

    genmidi_instr_t*	instr;
    int			instr_voice;	// 0 or 1 of the instrument

    int			op1;		// modulator operator offset
    int			op2;		// carrier operator offset
    int			car_level;	// last written level registers
    int			mod_level;
    int			freq;		// last written A0/B0 value, no key on
} voice_t;


// DMX's volume curve, 0-127 in to 0-127 out.
static const byte volume_mapping_table[128] =
{
    0, 1, 3, 5, 6, 8, 10, 11,
    13, 14, 16, 17, 19, 20, 22, 23,
    25, 26, 27, 29, 30, 32, 33, 34,
    36, 37, 39, 41, 43, 45, 47, 49,
    50, 52, 54, 55, 57, 59, 60, 61,
    63, 64, 66, 67, 68, 69, 71, 72,
    73, 74, 75, 76, 77, 79, 80, 81,
    82, 83, 84, 84, 85, 86, 87, 88,
    89, 90, 91, 92, 92, 93, 94, 95,
    96, 96, 97, 98, 99, 99, 100, 101,
    101, 102, 103, 103, 104, 105, 105, 106,
    107, 107, 108, 109, 109, 110, 110, 111,
    112, 112, 113, 113, 114, 114, 115, 115,
    116, 117, 117, 118, 118, 119, 119, 120,
    120, 121, 121, 122, 122, 123, 123, 123,
    124, 124, 125, 125, 126, 126, 127, 127
};

// DMX's F-number table: 32 steps per semitone. The first 284
// entries cover the lowest notes; after that one octave repeats
// with the block number above the F-number.
static const unsigned short frequency_curve[668] =
{
    0x133, 0x133, 0x134, 0x134, 0x135, 0x136, 0x136, 0x137,
    0x137, 0x138, 0x138, 0x139, 0x139, 0x13a, 0x13b, 0x13b,
    0x13c, 0x13c, 0x13d, 0x13d, 0x13e, 0x13f, 0x13f, 0x140,
    0x140, 0x141, 0x142, 0x142, 0x143, 0x143, 0x144, 0x144,
    0x145, 0x146, 0x146, 0x147, 0x147, 0x148, 0x149, 0x149,
    0x14a, 0x14a, 0x14b, 0x14c, 0x14c, 0x14d, 0x14d, 0x14e,
    0x14f, 0x14f, 0x150, 0x150, 0x151, 0x152, 0x152, 0x153,
    0x153, 0x154, 0x155, 0x155, 0x156, 0x157, 0x157, 0x158,
    0x158, 0x159, 0x15a, 0x15a, 0x15b, 0x15b, 0x15c, 0x15d,
    0x15d, 0x15e, 0x15f, 0x15f, 0x160, 0x161, 0x161, 0x162,
    0x162, 0x163, 0x164, 0x164, 0x165, 0x166, 0x166, 0x167,
    0x168, 0x168, 0x169, 0x16a, 0x16a, 0x16b, 0x16c, 0x16c,
    0x16d, 0x16e, 0x16e, 0x16f, 0x170, 0x170, 0x171, 0x172,
    0x172, 0x173, 0x174, 0x174, 0x175, 0x176, 0x176, 0x177,
    0x178, 0x178, 0x179, 0x17a, 0x17a, 0x17b, 0x17c, 0x17c,
    0x17d, 0x17e, 0x17e, 0x17f, 0x180, 0x181, 0x181, 0x182,
    0x183, 0x183, 0x184, 0x185, 0x185, 0x186, 0x187, 0x188,
    0x188, 0x189, 0x18a, 0x18a, 0x18b, 0x18c, 0x18d, 0x18d,
    0x18e, 0x18f, 0x18f, 0x190, 0x191, 0x192, 0x192, 0x193,
    0x194, 0x194, 0x195, 0x196, 0x197, 0x197, 0x198, 0x199,
    0x19a, 0x19a, 0x19b, 0x19c, 0x19d, 0x19d, 0x19e, 0x19f,
    0x1a0, 0x1a0, 0x1a1, 0x1a2, 0x1a3, 0x1a3, 0x1a4, 0x1a5,
    0x1a6, 0x1a6, 0x1a7, 0x1a8, 0x1a9, 0x1a9, 0x1aa, 0x1ab,
    0x1ac, 0x1ad, 0x1ad, 0x1ae, 0x1af, 0x1b0, 0x1b0, 0x1b1,
    0x1b2, 0x1b3, 0x1b4, 0x1b4, 0x1b5, 0x1b6, 0x1b7, 0x1b8,
    0x1b8, 0x1b9, 0x1ba, 0x1bb, 0x1bc, 0x1bc, 0x1bd, 0x1be,
    0x1bf, 0x1c0, 0x1c0, 0x1c1, 0x1c2, 0x1c3, 0x1c4, 0x1c4,
    0x1c5, 0x1c6, 0x1c7, 0x1c8, 0x1c9, 0x1c9, 0x1ca, 0x1cb,
    0x1cc, 0x1cd, 0x1ce, 0x1ce, 0x1cf, 0x1d0, 0x1d1, 0x1d2,
    0x1d3, 0x1d3, 0x1d4, 0x1d5, 0x1d6, 0x1d7, 0x1d8, 0x1d8,
    0x1d9, 0x1da, 0x1db, 0x1dc, 0x1dd, 0x1de, 0x1de, 0x1df,
    0x1e0, 0x1e1, 0x1e2, 0x1e3, 0x1e4, 0x1e5, 0x1e5, 0x1e6,
    0x1e7, 0x1e8, 0x1e9, 0x1ea, 0x1eb, 0x1ec, 0x1ed, 0x1ed,
    0x1ee, 0x1ef, 0x1f0, 0x1f1, 0x1f2, 0x1f3, 0x1f4, 0x1f5,
    0x1f6, 0x1f6, 0x1f7, 0x1f8, 0x1f9, 0x1fa, 0x1fb, 0x1fc,
    0x1fd, 0x1fe, 0x1ff, 0x200, 0x201, 0x201, 0x202, 0x203,
    0x204, 0x205, 0x206, 0x207, 0x208, 0x209, 0x20a, 0x20b,
    0x20c, 0x20d, 0x20e, 0x20f, 0x210, 0x210, 0x211, 0x212,
    0x213, 0x214, 0x215, 0x216, 0x217, 0x218, 0x219, 0x21a,
    0x21b, 0x21c, 0x21d, 0x21e, 0x21f, 0x220, 0x221, 0x222,
    0x223, 0x224, 0x225, 0x226, 0x227, 0x228, 0x229, 0x22a,
    0x22b, 0x22c, 0x22d, 0x22e, 0x22f, 0x230, 0x231, 0x232,
    0x233, 0x234, 0x235, 0x236, 0x237, 0x238, 0x239, 0x23a,
    0x23b, 0x23c, 0x23d, 0x23e, 0x23f, 0x240, 0x241, 0x242,
    0x244, 0x245, 0x246, 0x247, 0x248, 0x249, 0x24a, 0x24b,
    0x24c, 0x24d, 0x24e, 0x24f, 0x250, 0x251, 0x252, 0x253,
    0x254, 0x256, 0x257, 0x258, 0x259, 0x25a, 0x25b, 0x25c,
    0x25d, 0x25e, 0x25f, 0x260, 0x262, 0x263, 0x264, 0x265,
    0x266, 0x267, 0x268, 0x269, 0x26a, 0x26c, 0x26d, 0x26e,
    0x26f, 0x270, 0x271, 0x272, 0x273, 0x275, 0x276, 0x277,
    0x278, 0x279, 0x27a, 0x27b, 0x27d, 0x27e, 0x27f, 0x280,
    0x281, 0x282, 0x284, 0x285, 0x286, 0x287, 0x288, 0x289,
    0x28b, 0x28c, 0x28d, 0x28e, 0x28f, 0x290, 0x292, 0x293,
    0x294, 0x295, 0x296, 0x298, 0x299, 0x29a, 0x29b, 0x29c,
    0x29e, 0x29f, 0x2a0, 0x2a1, 0x2a2, 0x2a4, 0x2a5, 0x2a6,
    0x2a7, 0x2a9, 0x2aa, 0x2ab, 0x2ac, 0x2ae, 0x2af, 0x2b0,
    0x2b1, 0x2b2, 0x2b4, 0x2b5, 0x2b6, 0x2b7, 0x2b9, 0x2ba,
    0x2bb, 0x2bd, 0x2be, 0x2bf, 0x2c0, 0x2c2, 0x2c3, 0x2c4,
    0x2c5, 0x2c7, 0x2c8, 0x2c9, 0x2cb, 0x2cc, 0x2cd, 0x2ce,
    0x2d0, 0x2d1, 0x2d2, 0x2d4, 0x2d5, 0x2d6, 0x2d8, 0x2d9,
    0x2da, 0x2dc, 0x2dd, 0x2de, 0x2e0, 0x2e1, 0x2e2, 0x2e4,
    0x2e5, 0x2e6, 0x2e8, 0x2e9, 0x2ea, 0x2ec, 0x2ed, 0x2ee,
    0x2f0, 0x2f1, 0x2f2, 0x2f4, 0x2f5, 0x2f6, 0x2f8, 0x2f9,
    0x2fb, 0x2fc, 0x2fd, 0x2ff, 0x300, 0x302, 0x303, 0x304,
    0x306, 0x307, 0x309, 0x30a, 0x30b, 0x30d, 0x30e, 0x310,
    0x311, 0x312, 0x314, 0x315, 0x317, 0x318, 0x31a, 0x31b,
    0x31c, 0x31e, 0x31f, 0x321, 0x322, 0x324, 0x325, 0x327,
    0x328, 0x329, 0x32b, 0x32c, 0x32e, 0x32f, 0x331, 0x332,
    0x334, 0x335, 0x337, 0x338, 0x33a, 0x33b, 0x33d, 0x33e,
    0x340, 0x341, 0x343, 0x344, 0x346, 0x347, 0x349, 0x34a,
    0x34c, 0x34d, 0x34f, 0x350, 0x352, 0x353, 0x355, 0x357,
    0x358, 0x35a, 0x35b, 0x35d, 0x35e, 0x360, 0x361, 0x363,
    0x365, 0x366, 0x368, 0x369, 0x36b, 0x36c, 0x36e, 0x370,
    0x371, 0x373, 0x374, 0x376, 0x378, 0x379, 0x37b, 0x37c,
    0x37e, 0x380, 0x381, 0x383, 0x384, 0x386, 0x388, 0x389,
    0x38b, 0x38d, 0x38e, 0x390, 0x392, 0x393, 0x395, 0x397,
    0x398, 0x39a, 0x39c, 0x39d, 0x39f, 0x3a1, 0x3a2, 0x3a4,
    0x3a6, 0x3a7, 0x3a9, 0x3ab, 0x3ac, 0x3ae, 0x3b0, 0x3b1,
    0x3b3, 0x3b5, 0x3b7, 0x3b8, 0x3ba, 0x3bc, 0x3bd, 0x3bf,
    0x3c1, 0x3c3, 0x3c4, 0x3c6, 0x3c8, 0x3ca, 0x3cb, 0x3cd,
    0x3cf, 0x3d1, 0x3d2, 0x3d4, 0x3d6, 0x3d8, 0x3da, 0x3db,
    0x3dd, 0x3df, 0x3e1, 0x3e3, 0x3e4, 0x3e6, 0x3e8, 0x3ea,
    0x3ec, 0x3ed, 0x3ef, 0x3f1, 0x3f3, 0x3f5, 0x3f6, 0x3f8,
    0x3fa, 0x3fc, 0x3fe, 0x36c
};

// Modulator operator slot of each OPL2 channel; the carrier is +3.
static const int voice_operators[NUM_VOICES] =
{
    0x00, 0x01, 0x02, 0x08, 0x09, 0x0a, 0x10, 0x11, 0x12
};


static boolean		music_ready;
static opl3_chip	opl;

static genmidi_instr_t*	instruments;	// 128 melodic + 47 percussion

static channel_t	channels[NUM_CHANNELS];
static voice_t		voices[NUM_VOICES];

// DMX keeps voices in two ordered lists: allocation order decides
// which voice is stolen, and freed voices go to the back.
static voice_t*		freevoices[NUM_VOICES];
static int		numfree;
static voice_t*		usedvoices[NUM_VOICES];
static int		numused;

// 0-120, from the menu's 0-15.
static int		music_volume = 120;

// The registered song.
static const byte*	song;
static int		songlength;
static int		scorestart;
static boolean		songmidi;	// Standard MIDI file, not MUS

// A Standard MIDI file track being read.
typedef struct
{
    const byte*	start;		// first event
    const byte*	end;
    const byte*	pos;
    int		ticks_left;	// MIDI ticks until the next event
    int		status;		// running status
    boolean	done;
} miditrack_t;

static miditrack_t	miditracks[MIDI_MAXTRACKS];
static int		nummiditracks;
static int		mididivision;	// ticks per quarter note
static int		miditempo;	// microseconds per quarter note
static double		midiwait;	// samples until the next event

// Playback state.
static boolean		playing;
static boolean		songpaused;
static boolean		looping;
static int		scorepos;
static int		ticks_to_event;	// score ticks until the next event
static int		tickfrac;	// sample accumulator for tick timing



//
// OPL access
//

//
// OPL_Write
// Buffered, so writes take effect a couple of samples apart as on
// real hardware. Otherwise a key off and key on in the same tick
// would land on the same sample and the note would not retrigger.
//
static void OPL_Write (int reg, int value)
{
    OPL3_WriteRegBuffered (&opl, (uint16_t) reg, (uint8_t) value);
}


static void OPL_Init (void)
{
    int		i;

    OPL3_Reset (&opl, OPL_RATE);

    OPL_Write (OPL_REG_WAVEFORM_ENABLE, 0x20);
    OPL_Write (OPL_REG_FM_MODE, 0x40);

    // Silence every operator.
    for (i = 0; i < 0x16; i++)
    {
	OPL_Write (OPL_REGS_LEVEL + i, 0x3f);
	OPL_Write (OPL_REGS_TREMOLO + i, 0);
	OPL_Write (OPL_REGS_ATTACK + i, 0);
	OPL_Write (OPL_REGS_SUSTAIN + i, 0);
	OPL_Write (OPL_REGS_WAVEFORM + i, 0);
    }

    for (i = 0; i < NUM_VOICES; i++)
    {
	OPL_Write (OPL_REGS_FREQ_1 + i, 0);
	OPL_Write (OPL_REGS_FREQ_2 + i, 0);
	OPL_Write (OPL_REGS_FEEDBACK + i, 0);
    }
}



//
// Voices
//
// This follows the DMX library of DOOM v1.9, as worked out by
// Chocolate Doom's OPL music code.
//

static void InitVoices (void)
{
    int		i;

    memset (voices, 0, sizeof(voices));
    for (i = 0; i < NUM_VOICES; i++)
    {
	voices[i].index = i;
	voices[i].channel = -1;
	voices[i].op1 = voice_operators[i];
	voices[i].op2 = voice_operators[i] + 3;
	freevoices[i] = &voices[i];
    }
    numfree = NUM_VOICES;
    numused = 0;
}


//
// LoadOperator
// Returns the level register value written.
//
static int LoadOperator (int op, genmidi_op_t* data, boolean silent)
{
    int		level;

    // The level is set properly when the volume is.
    level = data->scale;
    level |= silent ? 0x3f : data->level;

    OPL_Write (OPL_REGS_LEVEL + op, level);
    OPL_Write (OPL_REGS_TREMOLO + op, data->tremolo);
    OPL_Write (OPL_REGS_ATTACK + op, data->attack);
    OPL_Write (OPL_REGS_SUSTAIN + op, data->sustain);
    OPL_Write (OPL_REGS_WAVEFORM + op, data->waveform);

    return level;
}


static void SetVoiceInstrument (voice_t* v, genmidi_instr_t* instr,
				int instr_voice)
{
    genmidi_voice_t*	data;
    boolean		modulating;

    if (v->instr == instr && v->instr_voice == instr_voice)
	return;

    v->instr = instr;
    v->instr_voice = instr_voice;
    data = &instr->voices[instr_voice];

    // Bit 0 of feedback clear: the modulator modulates the carrier,
    // and only the carrier is heard. Set: both are heard.
    modulating = (data->feedback & 0x01) == 0;

    // Like DMX, carrier first, then modulator.
    v->car_level = LoadOperator (v->op2, &data->carrier, true);
    v->mod_level = LoadOperator (v->op1, &data->modulator, !modulating);

    OPL_Write (OPL_REGS_FEEDBACK + v->index, data->feedback | 0x30);
}


static void SetVoiceVolume (voice_t* v, int volume)
{
    genmidi_voice_t*	data = &v->instr->voices[v->instr_voice];
    int			midi_volume;
    int			full_volume;
    int			car_level;
    int			mod_level;

    v->note_volume = volume;

    midi_volume = 2 * (volume_mapping_table[channels[v->channel].volume] + 1);
    full_volume = (volume_mapping_table[v->note_volume] * midi_volume) >> 9;

    car_level = 0x3f - full_volume;
    if (car_level == (v->car_level & 0x3f))
	return;

    v->car_level = car_level | (v->car_level & 0xc0);
    OPL_Write (OPL_REGS_LEVEL + v->op2, v->car_level);

    // In additive mode the modulator is heard too.
    if ((data->feedback & 0x01) && data->modulator.level != 0x3f)
    {
	mod_level = data->modulator.level;
	if (mod_level < car_level)
	    mod_level = car_level;
	mod_level |= v->mod_level & 0xc0;

	if (mod_level != v->mod_level)
	{
	    v->mod_level = mod_level;
	    OPL_Write (OPL_REGS_LEVEL + v->op1,
		       mod_level | (data->modulator.scale & 0xc0));
	}
    }
}


//
// VoiceFrequency
// The A0/B0 register pair (block and F-number) for a voice.
//
static int VoiceFrequency (voice_t* v)
{
    genmidi_voice_t*	data = &v->instr->voices[v->instr_voice];
    int			note = v->note;
    int			index;
    int			octave;

    if (!(SHORT(v->instr->flags) & GENMIDI_FLAG_FIXED))
	note += (short) SHORT(data->base_note_offset);

    while (note < 0)
	note += 12;
    while (note > 95)
	note -= 12;

    index = 64 + 32*note + channels[v->channel].bend;

    // The second voice of a layered instrument is detuned.
    if (v->instr_voice != 0)
	index += v->instr->fine_tuning / 2 - 64;

    if (index < 0)
	index = 0;

    if (index < 284)
	return frequency_curve[index];

    octave = (index - 284) / (12*32);
    if (octave > 7)
	octave = 7;

    return frequency_curve[284 + (index - 284) % (12*32)] | (octave << 10);
}


static void UpdateVoiceFrequency (voice_t* v)
{
    int		freq = VoiceFrequency (v);

    if (freq == v->freq)
	return;

    OPL_Write (OPL_REGS_FREQ_1 + v->index, freq & 0xff);
    OPL_Write (OPL_REGS_FREQ_2 + v->index, (freq >> 8) | OPL_KEYON);
    v->freq = freq;
}


static voice_t* GetFreeVoice (void)
{
    voice_t*	v;

    if (numfree == 0)
	return NULL;

    v = freevoices[0];
    memmove (freevoices, freevoices + 1, --numfree * sizeof(*freevoices));
    usedvoices[numused++] = v;
    return v;
}


//
// ReleaseVoice
// Keys off the voice at a position in the used list.
//
static void ReleaseVoice (int i)
{
    voice_t*	v = usedvoices[i];

    OPL_Write (OPL_REGS_FREQ_2 + v->index, v->freq >> 8);
    v->channel = -1;

    memmove (usedvoices + i, usedvoices + i + 1,
	     (--numused - i) * sizeof(*usedvoices));

    // Freed voices go to the back of the queue.
    freevoices[numfree++] = v;
}


//
// ReplaceExistingVoice
// Makes room when every voice is busy: a second voice of a layered
// instrument goes first, otherwise the highest-numbered channel.
//
static void ReplaceExistingVoice (void)
{
    int		i;
    int		result = 0;

    for (i = 0; i < numused; i++)
    {
	if (usedvoices[i]->instr_voice != 0
	    || usedvoices[i]->channel >= usedvoices[result]->channel)
	    result = i;
    }

    ReleaseVoice (result);
}


static void VoiceKeyOn (int channel, genmidi_instr_t* instr,
			int instr_voice, int note, int key, int volume)
{
    voice_t*	v = GetFreeVoice ();

    if (!v)
	return;

    v->channel = channel;
    v->key = key;

    if (SHORT(instr->flags) & GENMIDI_FLAG_FIXED)
	v->note = instr->fixed_note;
    else
	v->note = note;

    SetVoiceInstrument (v, instr, instr_voice);
    SetVoiceVolume (v, volume);

    // Always write the frequency: it is what keys the note on.
    v->freq = -1;
    UpdateVoiceFrequency (v);
}


static void AllVoicesOff (void)
{
    while (numused > 0)
	ReleaseVoice (0);
}



//
// Score events
//

static void KeyOff (int channel, int key)
{
    int		i;

    // Two voices for a layered instrument.
    for (i = 0; i < numused; i++)
    {
	if (usedvoices[i]->channel == channel && usedvoices[i]->key == key)
	{
	    ReleaseVoice (i);
	    i--;
	}
    }
}


static void KeyOn (int channel, int key, int volume)
{
    genmidi_instr_t*	instr;
    int			note = key;

    if (volume == 0)
    {
	KeyOff (channel, key);
	return;
    }

    if (channel == PERCUSSION_CHANNEL)
    {
	if (key < 35 || key > 81)
	    return;
	instr = &instruments[GENMIDI_NUM_INSTRS + key - 35];
	note = 60;
    }
    else
	instr = &instruments[channels[channel].instrument];

    if (numfree == 0)
	ReplaceExistingVoice ();

    VoiceKeyOn (channel, instr, 0, note, key, volume);

    // The second voice only plays if one is still free.
    if (SHORT(instr->flags) & GENMIDI_FLAG_2VOICE)
	VoiceKeyOn (channel, instr, 1, note, key, volume);
}


static void SetChannelVolume (int channel, int volume)
{
    int		i;

    channels[channel].volume_base = volume;

    if (volume > music_volume)
	volume = music_volume;
    channels[channel].volume = volume;

    for (i = 0; i < numused; i++)
	if (usedvoices[i]->channel == channel)
	    SetVoiceVolume (usedvoices[i], usedvoices[i]->note_volume);
}


static void PitchWheel (int channel, int bend)
{
    int		i;

    // DMX only uses the top bits: 1/32 semitone steps, +-2 semitones.
    channels[channel].bend = bend/2 - 64;

    for (i = 0; i < numused; i++)
	if (usedvoices[i]->channel == channel)
	    UpdateVoiceFrequency (usedvoices[i]);
}


static void Controller (int channel, int ctrl, int value)
{
    if (value > 127)
	value = 127;

    switch (ctrl)
    {
      case mus_ctrl_instrument:
	channels[channel].instrument = value;
	break;

      case mus_ctrl_volume:
	SetChannelVolume (channel, value);
	break;

      default:
	// Panning is lost on a mono OPL2; the rest DMX ignored too.
	break;
    }
}


static void SystemEvent (int channel, int ctrl)
{
    int		i;

    switch (ctrl)
    {
      case mus_ctrl_soundsoff:
      case mus_ctrl_notesoff:
	for (i = 0; i < numused; i++)
	{
	    if (usedvoices[i]->channel == channel)
	    {
		ReleaseVoice (i);
		i--;
	    }
	}
	break;

      default:
	break;
    }
}


static void ResetChannels (void)
{
    int		i;

    for (i = 0; i < NUM_CHANNELS; i++)
    {
	channels[i].instrument = 0;
	channels[i].bend = 0;
	channels[i].lastvolume = 127;
	SetChannelVolume (i, 100);
    }
}


static int ReadByte (void)
{
    if (scorepos >= songlength)
	return -1;
    return song[scorepos++];
}


//
// ProcessEvents
// Plays events until the score asks for a delay.
//
static void ProcessEvents (void)
{
    int		desc;
    int		channel;
    int		key;
    int		value;
    int		delay;

    while (playing && ticks_to_event == 0)
    {
	desc = ReadByte ();
	if (desc < 0)
	    goto end;

	channel = desc & 0x0f;

	switch ((desc >> 4) & 0x07)
	{
	  case mus_releasekey:
	    KeyOff (channel, ReadByte () & 0x7f);
	    break;

	  case mus_presskey:
	    key = ReadByte ();
	    if (key & 0x80)
		channels[channel].lastvolume = ReadByte () & 0x7f;
	    KeyOn (channel, key & 0x7f, channels[channel].lastvolume);
	    break;

	  case mus_pitchwheel:
	    PitchWheel (channel, ReadByte ());
	    break;

	  case mus_systemevent:
	    SystemEvent (channel, ReadByte () & 0x7f);
	    break;

	  case mus_changecontroller:
	    key = ReadByte () & 0x7f;
	    value = ReadByte ();
	    Controller (channel, key, value);
	    break;

	  case mus_measureend:
	    break;

	  case mus_scoreend:
	  default:
	    goto end;
	}

	// The last event of a group is followed by a delay.
	if (desc & 0x80)
	{
	    delay = 0;
	    do
	    {
		value = ReadByte ();
		if (value < 0)
		    goto end;
		delay = (delay << 7) | (value & 0x7f);
	    } while (value & 0x80);
	    ticks_to_event = delay;
	}
	continue;

      end:
	AllVoicesOff ();
	if (looping)
	{
	    scorepos = scorestart;
	    ResetChannels ();
	    // Don't spin on a score that has no delays at all.
	    ticks_to_event = 1;
	}
	else
	    playing = false;
    }
}


//
// Standard MIDI files
//
// Every track is read at once, each keeping the MIDI ticks until its
// next event. The events are passed to the MUS handlers above, with
// MIDI channels moved to MUS channels: MIDI percussion (channel 10)
// is MUS channel 15, and the channels above it move down one.
//

static int MIDI_ReadVarLen (miditrack_t* t)
{
    int		value = 0;
    int		i;
    int		c;

    // At most four bytes.
    for (i = 0; i < 4; i++)
    {
	if (t->pos >= t->end)
	    return -1;
	c = *t->pos++;
	value = (value << 7) | (c & 0x7f);
	if (!(c & 0x80))
	    return value;
    }
    return -1;
}


static int MIDI_Channel (int channel)
{
    if (channel == MIDI_PERCUSSION)
	return PERCUSSION_CHANNEL;
    if (channel > MIDI_PERCUSSION)
	return channel - 1;
    return channel;
}


static void MIDI_Controller (int channel, int ctrl, int value)
{
    switch (ctrl)
    {
      case 7:	// channel volume
	SetChannelVolume (channel, value);
	break;

      case 120:	// all sound off
	SystemEvent (channel, mus_ctrl_soundsoff);
	break;

      case 123:	// all notes off
	SystemEvent (channel, mus_ctrl_notesoff);
	break;

      default:
	// Panning is lost on a mono OPL2, and DMX ignored the rest.
	break;
    }
}


//
// MIDI_TrackEvent
// Plays one event of a track. False at the end of the track.
//
static boolean MIDI_TrackEvent (miditrack_t* t)
{
    int		status;
    int		channel;
    int		data1;
    int		data2;
    int		type;
    int		length;

    if (t->pos >= t->end)
	return false;

    status = *t->pos;
    if (status & 0x80)
	t->pos++;
    else if (t->status)
	status = t->status;	// running status: data byte comes first
    else
	return false;

    // System exclusive: skipped.
    if (status == 0xf0 || status == 0xf7)
    {
	t->status = 0;
	length = MIDI_ReadVarLen (t);
	if (length < 0 || length > t->end - t->pos)
	    return false;
	t->pos += length;
	return true;
    }

    // Meta events: only the tempo and the end of the track matter.
    if (status == 0xff)
    {
	t->status = 0;
	if (t->pos >= t->end)
	    return false;
	type = *t->pos++;
	length = MIDI_ReadVarLen (t);
	if (length < 0 || length > t->end - t->pos)
	    return false;
	if (type == 0x2f)
	    return false;
	if (type == 0x51 && length == 3)
	{
	    miditempo = (t->pos[0] << 16) | (t->pos[1] << 8) | t->pos[2];
	    if (miditempo <= 0)
		miditempo = MIDI_DEFAULT_TEMPO;
	}
	t->pos += length;
	return true;
    }

    // No other system messages belong in a file.
    if (status >= 0xf0)
	return false;

    t->status = status;
    channel = MIDI_Channel (status & 0x0f);

    if (t->pos >= t->end)
	return false;
    data1 = *t->pos++ & 0x7f;

    // Program change and channel pressure have one data byte.
    switch (status & 0xf0)
    {
      case 0xc0:
	channels[channel].instrument = data1;
	return true;

      case 0xd0:
	return true;
    }

    if (t->pos >= t->end)
	return false;
    data2 = *t->pos++ & 0x7f;

    switch (status & 0xf0)
    {
      case 0x80:
	KeyOff (channel, data1);
	break;

      case 0x90:
	// Velocity 0 is a key release; KeyOn handles it.
	KeyOn (channel, data1, data2);
	break;

      case 0xb0:
	MIDI_Controller (channel, data1, data2);
	break;

      case 0xe0:
	// 14 bits, where MUS has 8.
	PitchWheel (channel, ((data2 << 7) | data1) >> 6);
	break;

      default:
	// Key pressure.
	break;
    }

    return true;
}


static void MIDI_ReadDelta (miditrack_t* t)
{
    int		delta = MIDI_ReadVarLen (t);

    if (delta < 0)
	t->done = true;
    else
	t->ticks_left = delta;
}


static void MIDI_Restart (void)
{
    int		i;

    for (i = 0; i < nummiditracks; i++)
    {
	miditracks[i].pos = miditracks[i].start;
	miditracks[i].status = 0;
	miditracks[i].done = false;
	MIDI_ReadDelta (&miditracks[i]);
    }

    miditempo = MIDI_DEFAULT_TEMPO;
    midiwait = 0;
}


//
// MIDI_ProcessEvents
// Plays every event that is due, then works out the wait until
// the next one.
//
static void MIDI_ProcessEvents (void)
{
    miditrack_t*	t;
    int			i;
    int			delta;

    while (playing && midiwait < 1)
    {
	// Tracks are played in order, so a tempo change in the first
	// track applies to notes at the same time in the others.
	for (i = 0; i < nummiditracks; i++)
	{
	    t = &miditracks[i];
	    while (!t->done && t->ticks_left == 0)
	    {
		if (MIDI_TrackEvent (t))
		    MIDI_ReadDelta (t);
		else
		    t->done = true;
	    }
	}

	// The nearest event of any track.
	delta = -1;
	for (i = 0; i < nummiditracks; i++)
	{
	    t = &miditracks[i];
	    if (!t->done && (delta < 0 || t->ticks_left < delta))
		delta = t->ticks_left;
	}

	if (delta < 0)
	{
	    // Every track has ended.
	    AllVoicesOff ();
	    if (looping)
	    {
		MIDI_Restart ();
		ResetChannels ();
		// Don't spin on a song that has no delays at all.
		midiwait += 1;
	    }
	    else
		playing = false;
	    continue;
	}

	for (i = 0; i < nummiditracks; i++)
	    if (!miditracks[i].done)
		miditracks[i].ticks_left -= delta;

	midiwait += (double) delta * miditempo * OPL_RATE
		    / ((double) mididivision * 1000000);
    }
}


//
// MIDI_GenerateMusic
//
static void MIDI_GenerateMusic (short* buffer, int frames)
{
    int		n;

    while (frames > 0)
    {
	n = frames;

	if (playing && !songpaused)
	{
	    MIDI_ProcessEvents ();
	    if (playing && midiwait < n)
		n = (int) midiwait;
	}

	OPL3_GenerateStream (&opl, buffer, n);
	buffer += n*2;
	frames -= n;

	if (playing && !songpaused)
	    midiwait -= n;
    }
}


//
// MIDI_Register
// Finds the tracks of a Standard MIDI file. False if it isn't one
// that can be played.
//
static boolean MIDI_Register (const byte* data, int length)
{
    const byte*	p = data;
    const byte*	end = data + length;
    int		headerlength;
    int		chunklength;
    int		division;

    if (length < 14 || memcmp (p, "MThd", 4))
	return false;

    headerlength = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
    division = (p[12] << 8) | p[13];

    // SMPTE timing is not used by music lumps.
    if (headerlength < 6 || headerlength > length - 8
	|| division == 0 || (division & 0x8000))
	return false;

    p += 8 + headerlength;
    nummiditracks = 0;

    // Format 0 has one track, format 1 plays its tracks together.
    // Unknown chunks are skipped.
    while (end - p >= 8 && nummiditracks < MIDI_MAXTRACKS)
    {
	chunklength = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
	if (chunklength < 0 || chunklength > end - p - 8)
	    chunklength = end - p - 8;	// truncated: play what is there

	if (!memcmp (p, "MTrk", 4))
	{
	    miditracks[nummiditracks].start = p + 8;
	    miditracks[nummiditracks].end = p + 8 + chunklength;
	    nummiditracks++;
	}

	p += 8 + chunklength;
    }

    if (nummiditracks == 0)
	return false;

    mididivision = division;
    return true;
}


//
// GenerateMusic
// Produces frames of music, advancing the score in real time.
//
static void GenerateMusic (short* buffer, int frames)
{
    int		n;
    int		ticksamples;

    if (songmidi)
    {
	MIDI_GenerateMusic (buffer, frames);
	return;
    }

    while (frames > 0)
    {
	if (playing && !songpaused)
	    ProcessEvents ();

	// Samples until the next score tick.
	ticksamples = (OPL_RATE - tickfrac + MUS_TICRATE - 1) / MUS_TICRATE;
	n = frames < ticksamples ? frames : ticksamples;

	OPL3_GenerateStream (&opl, buffer, n);
	buffer += n*2;
	frames -= n;

	tickfrac += n * MUS_TICRATE;
	if (tickfrac >= OPL_RATE)
	{
	    tickfrac -= OPL_RATE;
	    if (playing && !songpaused && ticks_to_event > 0)
		ticks_to_event--;
	}
    }
}



//
// MUSIC API
//

void I_InitMusic (void)
{
    int		lump;

    if (M_CheckParm ("-nomusic"))
	return;

    lump = W_CheckNumForName ("GENMIDI");
    if (lump < 0
	|| W_LumpLength (lump) < 8 + (GENMIDI_NUM_INSTRS
				      + GENMIDI_NUM_PERCUSSION)
				     * (int)sizeof(genmidi_instr_t))
    {
	fprintf (stderr, "I_InitMusic: no GENMIDI lump, music disabled\n");
	return;
    }

    if (memcmp (W_CacheLumpNum (lump, PU_CACHE), GENMIDI_HEADER, 8))
    {
	fprintf (stderr, "I_InitMusic: bad GENMIDI lump, music disabled\n");
	return;
    }

    if (!RL_OpenStream (RL_MUSIC, OPL_RATE))
    {
	fprintf (stderr, "I_InitMusic: could not open a music stream\n");
	return;
    }

    // Keep the instruments for good.
    instruments = (genmidi_instr_t *)
	((byte *) W_CacheLumpNum (lump, PU_STATIC) + 8);

    OPL_Init ();
    InitVoices ();
    ResetChannels ();

    music_ready = true;
    fprintf (stderr, "I_InitMusic: OPL2 music ready\n");
}


void I_ShutdownMusic (void)
{
    music_ready = false;
    playing = false;
    song = NULL;
}


void I_UpdateMusic (void)
{
    short	buffer[MIXFRAMES*2];
    int		needed;

    if (!music_ready)
	return;

    // Measured once, so a device that drains faster than real
    // time can't keep us here.
    needed = MIXAHEAD - RL_AudioQueued (RL_MUSIC);
    while (needed > 0)
    {
	GenerateMusic (buffer, MIXFRAMES);

	// The chip's quietest level is -47 dB, not silent.
	if (music_volume == 0)
	    memset (buffer, 0, sizeof(buffer));

	RL_QueueAudio (RL_MUSIC, buffer, MIXFRAMES);
	needed -= MIXFRAMES;
    }
}


void I_SetMusicVolume (int volume)
{
    int		i;

    // Internal state variable.
    snd_MusicVolume = volume;

    // The menu gives 0-15; DMX works in 0-127.
    music_volume = volume * 8;
    if (music_volume > 127)
	music_volume = 127;

    if (!music_ready)
	return;

    for (i = 0; i < NUM_CHANNELS; i++)
    {
	if (i == PERCUSSION_CHANNEL)
	    SetChannelVolume (i, music_volume);
	else
	    SetChannelVolume (i, channels[i].volume_base);
    }
}


void I_PauseSong (int handle)
{
    if (!music_ready)
	return;

    songpaused = true;
    AllVoicesOff ();
}


void I_ResumeSong (int handle)
{
    songpaused = false;
}


//
// I_RegisterSong
// Takes a MUS lump or a Standard MIDI file, told apart by their
// headers. Anything else registers, but stays silent.
//
int I_RegisterSong (void* data, int length)
{
    const byte*	mus = (const byte *) data;

    song = NULL;
    songmidi = false;

    if (!music_ready || !mus || length < 4)
	return 1;

    if (!memcmp (mus, "MThd", 4))
    {
	if (MIDI_Register (mus, length))
	{
	    songmidi = true;
	    song = mus;
	}
	else
	    fprintf (stderr, "I_RegisterSong: unplayable MIDI file\n");
	return 1;
    }

    if (length < 8 || memcmp (mus, "MUS\x1a", 4))
	return 1;

    songlength = (unsigned short) SHORT(*(short *)(mus + 4));
    scorestart = (unsigned short) SHORT(*(short *)(mus + 6));
    songlength += scorestart;

    song = mus;
    return 1;
}


void I_PlaySong (int handle, int loop)
{
    if (!music_ready || !song)
	return;

    AllVoicesOff ();
    ResetChannels ();

    scorepos = scorestart;
    ticks_to_event = 0;
    if (songmidi)
	MIDI_Restart ();
    looping = loop;
    songpaused = false;
    playing = true;
}


void I_StopSong (int handle)
{
    if (!music_ready)
	return;

    playing = false;
    AllVoicesOff ();
}


void I_UnRegisterSong (int handle)
{
    // The lump is about to become purgable.
    playing = false;
    song = NULL;
}


// Is the song playing?
int I_QrySongPlaying (int handle)
{
    return playing;
}
