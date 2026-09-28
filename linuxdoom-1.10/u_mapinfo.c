// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	UMAPINFO parser, after the spec shipped with PrBoom+ and
//	DSDA-Doom:
//
//	map E6M1
//	{
//		LevelName = "Cursed Darkness"
//		Episode = "M_EPI6", "SIGIL II", "S"
//		InterText = "line one", "line two"
//	}
//
//	Keys are case-insensitive. Keys this engine has no use
//	for (Label, Author, ...) are skipped. BossAction entries
//	other than "clear" are skipped too: the default boss
//	behaviour stays.
//
//-----------------------------------------------------------------------------

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "u_mapinfo.h"

umapepisode_t	umapepisodes[MAXUMAPEPISODES];
int		numumapepisodes;
int		umapepisodesclear;

static umapinfo_t*	maps;
static int		nummaps;
static int		maxmaps;


//
// Tokenizer
//
enum
{
    TK_EOF,
    TK_IDENT,	// also numbers
    TK_STRING,
    TK_CHAR	// = , { }
};

typedef struct
{
    const char*	p;
    const char*	end;
    const char*	source;
    int		line;

    int		type;
    char	text[512];
} scanner_t;

static void U_Next (scanner_t* s)
{
    int	len = 0;

    // skip white space and comments
    for (;;)
    {
	while (s->p < s->end && isspace((unsigned char)*s->p))
	{
	    if (*s->p == '\n')
		s->line++;
	    s->p++;
	}
	if (s->end - s->p >= 2 && s->p[0] == '/' && s->p[1] == '/')
	{
	    while (s->p < s->end && *s->p != '\n')
		s->p++;
	    continue;
	}
	if (s->end - s->p >= 2 && s->p[0] == '/' && s->p[1] == '*')
	{
	    s->p += 2;
	    while (s->p < s->end
		   && !(s->end - s->p >= 2 && s->p[0] == '*' && s->p[1] == '/'))
	    {
		if (*s->p == '\n')
		    s->line++;
		s->p++;
	    }
	    s->p = s->end - s->p >= 2 ? s->p + 2 : s->end;
	    continue;
	}
	break;
    }

    s->text[0] = 0;

    if (s->p >= s->end || *s->p == 0)
    {
	s->type = TK_EOF;
	return;
    }

    if (*s->p == '"')
    {
	s->p++;
	while (s->p < s->end && *s->p != '"')
	{
	    if (*s->p == '\\' && s->p + 1 < s->end)
		s->p++;
	    if (*s->p == '\n')
		s->line++;
	    if (len < (int)sizeof(s->text) - 1)
		s->text[len++] = *s->p;
	    s->p++;
	}
	if (s->p < s->end)
	    s->p++;
	s->text[len] = 0;
	s->type = TK_STRING;
	return;
    }

    if (strchr("={},", *s->p))
    {
	s->text[0] = *s->p++;
	s->text[1] = 0;
	s->type = TK_CHAR;
	return;
    }

    while (s->p < s->end
	   && !isspace((unsigned char)*s->p)
	   && !strchr("={},\"", *s->p))
    {
	if (s->end - s->p >= 2 && s->p[0] == '/'
	    && (s->p[1] == '/' || s->p[1] == '*'))
	    break;
	if (len < (int)sizeof(s->text) - 1)
	    s->text[len++] = *s->p;
	s->p++;
    }
    s->text[len] = 0;
    s->type = TK_IDENT;
}

static int U_IsChar (scanner_t* s, char c)
{
    return s->type == TK_CHAR && s->text[0] == c;
}

static int U_Expect (scanner_t* s, char c)
{
    if (!U_IsChar (s, c))
    {
	fprintf (stderr, "UMAPINFO (%s:%d): expected '%c', got '%s'\n",
		 s->source, s->line, c, s->text);
	return 0;
    }
    U_Next (s);
    return 1;
}

static char* U_StrDup (const char* str)
{
    char*	d = malloc (strlen (str) + 1);

    if (d)
	strcpy (d, str);
    return d;
}

static void U_CopyLumpName (lumpname_t dest, const char* src)
{
    int	i;

    for (i=0 ; i<8 && src[i] ; i++)
	dest[i] = toupper ((unsigned char)src[i]);
    dest[i] = 0;
}

static void U_FreeEntry (umapinfo_t* mi)
{
    free (mi->levelname);
    free (mi->intertext);
    free (mi->intertextsecret);
}

int U_ParseMapName (const char* name, int* episode, int* map)
{
    char	up[9];
    int		i;

    for (i=0 ; i<8 && name[i] ; i++)
	up[i] = toupper ((unsigned char)name[i]);
    up[i] = 0;

    if (up[0] == 'E' && up[2] == 'M'
	&& up[1] >= '1' && up[1] <= '9'
	&& up[3] >= '1' && up[3] <= '9' && !up[4])
    {
	*episode = up[1] - '0';
	*map = up[3] - '0';
	return 1;
    }
    if (!strncmp (up, "MAP", 3)
	&& isdigit ((unsigned char)up[3]) && isdigit ((unsigned char)up[4])
	&& !up[5])
    {
	*episode = 0;
	*map = (up[3]-'0')*10 + up[4]-'0';
	return *map > 0;
    }
    return 0;
}

// Reads "a", "b", ... into one string joined with newlines.
// A bare "clear" gives an empty text, which cancels the default.
static char* U_ReadText (scanner_t* s)
{
    char*	text;
    size_t	len = 0;

    if (s->type == TK_IDENT && !strcasecmp (s->text, "clear"))
    {
	U_Next (s);
	return U_StrDup ("");
    }

    text = U_StrDup ("");
    for (;;)
    {
	size_t	add = strlen (s->text);
	char*	grown = realloc (text, len + add + 2);

	if (!grown)
	    break;
	text = grown;
	if (len)
	    text[len++] = '\n';
	memcpy (text + len, s->text, add + 1);
	len += add;

	U_Next (s);
	if (!U_IsChar (s, ','))
	    break;
	U_Next (s);
    }
    return text;
}

static int U_IsTrue (scanner_t* s)
{
    return s->type != TK_IDENT || strcasecmp (s->text, "false");
}

// Parses the value(s) after "key =", leaving s on the next key.
static void U_ParseValue (scanner_t* s, umapinfo_t* mi, const char* key)
{
    lumpname_t*	name = NULL;

    if (!strcasecmp (key, "levelpic"))		name = &mi->levelpic;
    else if (!strcasecmp (key, "next"))		name = &mi->next;
    else if (!strcasecmp (key, "nextsecret"))	name = &mi->nextsecret;
    else if (!strcasecmp (key, "music"))	name = &mi->music;
    else if (!strcasecmp (key, "skytexture"))	name = &mi->skytexture;
    else if (!strcasecmp (key, "exitpic"))	name = &mi->exitpic;
    else if (!strcasecmp (key, "enterpic"))	name = &mi->enterpic;
    else if (!strcasecmp (key, "endpic"))	name = &mi->endpic;
    else if (!strcasecmp (key, "interbackdrop"))name = &mi->interbackdrop;
    else if (!strcasecmp (key, "intermusic"))	name = &mi->intermusic;

    if (name)
    {
	U_CopyLumpName (*name, s->text);
	U_Next (s);
    }
    else if (!strcasecmp (key, "levelname"))
    {
	free (mi->levelname);
	mi->levelname = U_StrDup (s->text);
	U_Next (s);
    }
    else if (!strcasecmp (key, "intertext"))
    {
	free (mi->intertext);
	mi->intertext = U_ReadText (s);
    }
    else if (!strcasecmp (key, "intertextsecret"))
    {
	free (mi->intertextsecret);
	mi->intertextsecret = U_ReadText (s);
    }
    else if (!strcasecmp (key, "partime"))
    {
	mi->partime = atoi (s->text);
	U_Next (s);
    }
    else if (!strcasecmp (key, "endgame"))
    {
	mi->endgame = U_IsTrue (s);
	U_Next (s);
    }
    else if (!strcasecmp (key, "endbunny"))
    {
	mi->endbunny = U_IsTrue (s);
	U_Next (s);
    }
    else if (!strcasecmp (key, "endcast"))
    {
	mi->endcast = U_IsTrue (s);
	U_Next (s);
    }
    else if (!strcasecmp (key, "nointermission"))
    {
	mi->nointermission = U_IsTrue (s);
	U_Next (s);
    }
    else if (!strcasecmp (key, "bossaction")
	     && s->type == TK_IDENT && !strcasecmp (s->text, "clear"))
    {
	mi->bossactionclear = 1;
	U_Next (s);
    }
    else if (!strcasecmp (key, "episode"))
    {
	if (s->type == TK_IDENT && !strcasecmp (s->text, "clear"))
	{
	    umapepisodesclear = 1;
	    numumapepisodes = 0;
	    U_Next (s);
	}
	else
	{
	    umapepisode_t	ep;
	    int			i;

	    memset (&ep, 0, sizeof(ep));
	    U_CopyLumpName (ep.patch, s->text);
	    U_Next (s);
	    if (U_IsChar (s, ','))
	    {
		U_Next (s);
		ep.name = U_StrDup (s->text);
		U_Next (s);
	    }
	    if (U_IsChar (s, ','))
	    {
		U_Next (s);
		ep.key = tolower ((unsigned char)s->text[0]);
		U_Next (s);
	    }
	    ep.episode = mi->episode;
	    ep.map = mi->map;

	    // A PWAD loaded twice, or redefining an episode,
	    // replaces the entry instead of adding another.
	    for (i=0 ; i<numumapepisodes ; i++)
		if (umapepisodes[i].episode == ep.episode
		    && umapepisodes[i].map == ep.map)
		    break;
	    if (i < MAXUMAPEPISODES)
	    {
		if (i < numumapepisodes)
		    free (umapepisodes[i].name);
		else
		    numumapepisodes++;
		umapepisodes[i] = ep;
	    }
	    else
		free (ep.name);
	}
    }
    else
    {
	// Unknown or unsupported: skip the value list.
	U_Next (s);
	while (U_IsChar (s, ','))
	{
	    U_Next (s);
	    U_Next (s);
	}
    }
}

static umapinfo_t* U_NewEntry (int episode, int map)
{
    umapinfo_t*	mi;
    int		i;

    for (i=0 ; i<nummaps ; i++)
	if (maps[i].episode == episode && maps[i].map == map)
	{
	    U_FreeEntry (&maps[i]);
	    break;
	}

    if (i == nummaps)
    {
	if (nummaps == maxmaps)
	{
	    int		newmax = maxmaps ? maxmaps*2 : 32;
	    umapinfo_t*	grown = realloc (maps, newmax * sizeof(*maps));

	    if (!grown)
		return NULL;
	    maps = grown;
	    maxmaps = newmax;
	}
	nummaps++;
    }

    mi = &maps[i];
    memset (mi, 0, sizeof(*mi));
    mi->episode = episode;
    mi->map = map;
    return mi;
}

int U_ParseMapInfo (const char* data, int length, const char* source)
{
    scanner_t	s;

    s.p = data;
    s.end = data + length;
    s.source = source;
    s.line = 1;
    U_Next (&s);

    while (s.type != TK_EOF)
    {
	umapinfo_t*	mi;
	int		episode, map;

	if (s.type != TK_IDENT || strcasecmp (s.text, "map"))
	{
	    fprintf (stderr, "UMAPINFO (%s:%d): expected 'map', got '%s'\n",
		     source, s.line, s.text);
	    return 0;
	}
	U_Next (&s);
	if (!U_ParseMapName (s.text, &episode, &map))
	{
	    fprintf (stderr, "UMAPINFO (%s:%d): bad map name '%s'\n",
		     source, s.line, s.text);
	    return 0;
	}
	U_Next (&s);
	if (!U_Expect (&s, '{'))
	    return 0;

	mi = U_NewEntry (episode, map);
	if (!mi)
	    return 0;

	while (!U_IsChar (&s, '}'))
	{
	    char	key[64];

	    if (s.type != TK_IDENT)
	    {
		fprintf (stderr, "UMAPINFO (%s:%d): expected a key, got '%s'\n",
			 source, s.line, s.text);
		return 0;
	    }
	    snprintf (key, sizeof(key), "%.63s", s.text);
	    U_Next (&s);
	    if (!U_Expect (&s, '='))
		return 0;
	    U_ParseValue (&s, mi, key);
	}
	U_Next (&s);
    }
    return 1;
}

umapinfo_t* U_FindMap (int episode, int map)
{
    int	i;

    for (i=0 ; i<nummaps ; i++)
	if (maps[i].episode == episode && maps[i].map == map)
	    return &maps[i];
    return NULL;
}

int U_EndsGame (umapinfo_t* mi)
{
    return mi->endgame || mi->endpic[0] || mi->endbunny || mi->endcast;
}

void U_FreeMapInfo (void)
{
    int	i;

    for (i=0 ; i<nummaps ; i++)
	U_FreeEntry (&maps[i]);
    free (maps);
    maps = NULL;
    nummaps = maxmaps = 0;

    for (i=0 ; i<numumapepisodes ; i++)
	free (umapepisodes[i].name);
    numumapepisodes = 0;
    umapepisodesclear = 0;
}
