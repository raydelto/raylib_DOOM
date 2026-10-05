// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	The text parts of a DEHACKED lump, BEX [STRINGS] and [PARS]:
//
//		[STRINGS]
//		HUSTR_E3M1 = E3M1: Baphomet's Demesne
//		E3TEXT = Lock and load.\nRip and tear.
//
//		[PARS]
//		par 3 1 90
//
//	\n in a value is a line break, and a value ending in a
//	backslash goes on to the next line. Everything else in
//	the lump (Thing, Frame, [CODEPTR], ...) is skipped.
//
//-----------------------------------------------------------------------------

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "u_dehacked.h"


enum { SEC_NONE, SEC_STRINGS, SEC_PARS };

typedef struct
{
    const char*	p;
    const char*	end;
} lines_t;

// The next line, without its line break; false at the end.
static int NextLine (lines_t* l, const char** line, int* len)
{
    const char*	p = l->p;

    if (p >= l->end)
	return 0;
    while (l->p < l->end && *l->p != '\n')
	l->p++;
    *line = p;
    *len = (int)(l->p - p);
    if (l->p < l->end)
	l->p++;
    if (*len && p[*len-1] == '\r')
	(*len)--;
    return 1;
}

static void SkipSpace (const char** p, int* len)
{
    while (*len && isspace ((unsigned char)**p))
    {
	(*p)++;
	(*len)--;
    }
}

static void TrimEnd (const char* p, int* len)
{
    while (*len && isspace ((unsigned char)p[*len-1]))
	(*len)--;
}

// True if the line starts with word, as a whole word.
static int StartsWord (const char* p, int len, const char* word)
{
    int	n = (int)strlen (word);

    return len >= n && !strncasecmp (p, word, n)
	&& (len == n || isspace ((unsigned char)p[n]));
}

// DeHackEd's own block headers. Any of them ends a BEX section.
static const char* blocks[] =
{
    "Thing", "Frame", "Pointer", "Sound", "Ammo", "Weapon",
    "Sprite", "Text", "Misc", "Cheat", "Patch", "Doom", "Include"
};

static int IsBlock (const char* p, int len)
{
    int	i;

    for (i=0 ; i<(int)(sizeof(blocks)/sizeof(blocks[0])) ; i++)
	if (StartsWord (p, len, blocks[i]))
	    return 1;
    return 0;
}


//
// Values
//
typedef struct
{
    char*	s;
    int		len;
    int		size;
} buf_t;

static void Append (buf_t* b, const char* p, int len)
{
    if (b->len + len + 1 > b->size)
    {
	b->size = (b->len + len + 1) * 2;
	b->s = realloc (b->s, b->size);
	if (!b->s)
	{
	    fprintf (stderr, "U_ParseDehacked: out of memory\n");
	    exit (1);
	}
    }
    memcpy (b->s + b->len, p, len);
    b->len += len;
    b->s[b->len] = '\0';
}

// Turns \n into a line break and \\ into a backslash, in place.
static void Unescape (char* s)
{
    char*	out = s;

    for ( ; *s ; s++)
    {
	if (*s == '\\' && s[1] == 'n')
	{
	    *out++ = '\n';
	    s++;
	}
	else if (*s == '\\' && s[1] == '\\')
	{
	    *out++ = '\\';
	    s++;
	}
	else
	    *out++ = *s;
    }
    *out = '\0';
}

static void SetString (const dehstring_t* strings, int numstrings,
		       const char* key, int keylen, char* value,
		       dehcount_t* count)
{
    int	i;

    for (i=0 ; i<numstrings ; i++)
    {
	if ((int)strlen (strings[i].name) != keylen
	    || strncasecmp (strings[i].name, key, keylen))
	    continue;
	*strings[i].text = strdup (value);
	count->strings++;
	return;
    }
}


//
// U_ParseDehacked
//
void U_ParseDehacked (const char* data, int length,
		      const dehstring_t* strings, int numstrings,
		      dehpar_t setpar, dehcount_t* count)
{
    lines_t	l = { data, data + length };
    const char*	line;
    int		len;
    int		section = SEC_NONE;
    buf_t	value = { NULL, 0, 0 };
    const char*	key = NULL;
    int		keylen = 0;
    int		more = 0;	// the last value line ended with '\'

    while (NextLine (&l, &line, &len))
    {
	SkipSpace (&line, &len);
	TrimEnd (line, &len);

	if (more)
	{
	    more = len && line[len-1] == '\\';
	    Append (&value, line, more ? len-1 : len);
	    if (!more)
	    {
		Unescape (value.s);
		SetString (strings, numstrings, key, keylen, value.s, count);
	    }
	    continue;
	}

	if (!len || line[0] == '#')
	    continue;

	if (line[0] == '[')
	{
	    if (StartsWord (line, len, "[STRINGS]"))
		section = SEC_STRINGS;
	    else if (StartsWord (line, len, "[PARS]"))
		section = SEC_PARS;
	    else
		section = SEC_NONE;
	    continue;
	}

	if (IsBlock (line, len))
	{
	    int	from, to;
	    char	header[64];

	    section = SEC_NONE;

	    // "Text 12 15" is followed by that many characters of
	    // old and new text, line breaks and all.
	    snprintf (header, sizeof(header), "%.*s", len, line);
	    if (StartsWord (line, len, "Text")
		&& sscanf (header + 4, "%d %d", &from, &to) == 2
		&& from >= 0 && to >= 0)
	    {
		int	skip = from + to;

		while (skip > 0 && l.p < l.end)
		{
		    if (*l.p != '\r')
			skip--;
		    l.p++;
		}
	    }
	    continue;
	}

	if (section == SEC_STRINGS)
	{
	    const char*	p = line;
	    int		n = len;

	    key = p;
	    while (n && (isalnum ((unsigned char)*p) || *p == '_'))
	    {
		p++;
		n--;
	    }
	    keylen = (int)(p - key);
	    SkipSpace (&p, &n);
	    if (!keylen || !n || *p != '=')
		continue;
	    p++;
	    n--;
	    SkipSpace (&p, &n);

	    value.len = 0;
	    more = n && p[n-1] == '\\';
	    Append (&value, p, more ? n-1 : n);
	    if (!more)
	    {
		Unescape (value.s);
		SetString (strings, numstrings, key, keylen, value.s, count);
	    }
	}
	else if (section == SEC_PARS && StartsWord (line, len, "par"))
	{
	    char	text[128];
	    char*	comment;
	    int		a, b, c;
	    int		n;

	    snprintf (text, sizeof(text), "%.*s", len, line);
	    if ((comment = strchr (text, '#')) != NULL)
		*comment = '\0';
	    n = sscanf (text + 3, "%d %d %d", &a, &b, &c);
	    if (n == 3 && a >= 1 && setpar && setpar (a, b, c))
		count->pars++;
	    else if (n == 2 && setpar && setpar (0, a, b))
		count->pars++;
	}
    }

    // A value still waiting for its next line at the end of the lump.
    if (more)
    {
	Unescape (value.s);
	SetString (strings, numstrings, key, keylen, value.s, count);
    }
    free (value.s);
}
