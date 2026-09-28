// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	UMAPINFO: per-map metadata carried by PWADs (SIGIL, SIGIL II, ...).
//	Only the parser and lookups live here; d_main.c feeds it
//	the lumps, so the tests can link it without the WAD code.
//
//-----------------------------------------------------------------------------

#ifndef __U_MAPINFO__
#define __U_MAPINFO__

// Lump names are 8 characters, plus the terminator.
typedef char	lumpname_t[9];

typedef struct
{
    // The map this entry is for, from its "map ExMy" header.
    // episode is 0 for MAPxx.
    int		episode;
    int		map;

    char*	levelname;
    lumpname_t	levelpic;
    lumpname_t	next;
    lumpname_t	nextsecret;
    lumpname_t	music;
    lumpname_t	skytexture;
    lumpname_t	exitpic;
    lumpname_t	enterpic;
    lumpname_t	endpic;
    lumpname_t	interbackdrop;
    lumpname_t	intermusic;

    // Text shown after the level; NULL for none.
    char*	intertext;
    char*	intertextsecret;

    // In seconds; 0 when not given.
    int		partime;

    int		endgame;
    int		endbunny;
    int		endcast;
    int		nointermission;

    // "BossAction = clear": bosses dying does nothing on this map.
    int		bossactionclear;

} umapinfo_t;

typedef struct
{
    lumpname_t	patch;
    char*	name;
    char	key;
    int		episode;
    int		map;
} umapepisode_t;

#define MAXUMAPEPISODES	8

extern umapepisode_t	umapepisodes[MAXUMAPEPISODES];
extern int		numumapepisodes;

// Set when an "Episode = clear" asks to drop the IWAD's episodes.
extern int		umapepisodesclear;

// Parses one UMAPINFO lump. Later entries for a map replace
// earlier ones, so PWADs loaded later win. Returns 0 on a
// syntax error (the entries before it are kept).
int U_ParseMapInfo (const char* data, int length, const char* source);

umapinfo_t* U_FindMap (int episode, int map);

// "E6M1" -> episode 6, map 1; "MAP07" -> 0, 7. Returns 0 if
// the name is neither.
int U_ParseMapName (const char* name, int* episode, int* map);

// True when the level ends the game: EndPic, EndGame and so on.
int U_EndsGame (umapinfo_t* mi);

void U_FreeMapInfo (void);

#endif
