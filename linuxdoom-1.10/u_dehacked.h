// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	The text parts of a DEHACKED lump: the BEX [STRINGS] and [PARS]
//	sections. That is how SIGIL_COMPAT names the SIGIL maps it puts
//	in E3 ("E3M1: Baphomet's Demesne") and how Freedoom names its
//	own. Thing, frame, weapon and code pointer changes are skipped.
//	Like u_mapinfo.c, this file has no WAD or game code in it:
//	d_main.c hands it the lumps and the strings it may replace.
//
//-----------------------------------------------------------------------------

#ifndef __U_DEHACKED__
#define __U_DEHACKED__

// A string a [STRINGS] line can replace: its BEX mnemonic
// ("HUSTR_E3M1", "E3TEXT") and the pointer the game reads it from.
typedef struct
{
    const char*	name;
    char**	text;
} dehstring_t;

// Called for each "par E M seconds" (episode >= 1) or
// "par M seconds" (episode 0, DOOM II) line. Returns false for a
// map the game has no par time slot for.
typedef int (*dehpar_t) (int episode, int map, int seconds);

typedef struct
{
    int		strings;	// [STRINGS] entries applied
    int		pars;		// [PARS] entries applied
} dehcount_t;

// Applies one DEHACKED lump. Later lumps override earlier ones,
// as later PWADs do. Replaced strings are malloc'ed and never
// freed; the ones they replace may be literals.
void U_ParseDehacked (const char* data, int length,
		      const dehstring_t* strings, int numstrings,
		      dehpar_t setpar, dehcount_t* count);

#endif
