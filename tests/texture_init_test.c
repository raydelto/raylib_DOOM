// R_InitTextures regression test: textures whose patches are missing.
//
// SIGIL replaces PNAMES, TEXTURE1 and TEXTURE2 with The Ultimate
// DOOM's, so with DOOM 1.9 (no Episode 4) its SKY4 names a patch no
// WAD has, which used to stop the game with "R_InitTextures: Missing
// patch in texture SKY4". The lumps are built here, so no WAD is
// needed; the WAD and zone functions r_data.c calls are stubbed.

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "doomstat.h"
#include "i_system.h"
#include "p_local.h"
#include "r_data.h"
#include "r_sky.h"
#include "r_state.h"
#include "w_wad.h"
#include "z_zone.h"

void R_InitTextures (void);
extern short**	texturecolumnlump;

static int failures;

#define CHECK(cond) \
    do { if (!(cond)) { \
	fprintf (stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
	failures++; } } while (0)

// What r_data.c links against, besides the WAD and the zone.
boolean		demoplayback;
lumpinfo_t*	lumpinfo;
int		numlumps;
int		numsectors;
sector_t*	sectors;
int		numsides;
side_t*		sides;
int		numsprites;
spritedef_t*	sprites;
int		skytexture;
thinker_t	thinkercap;
void P_MobjThinker (mobj_t* mobj) { (void)mobj; }

static jmp_buf	fatal;
static char	fatalmsg[256];

void I_Error (char *error, ...)
{
    va_list	ap;

    va_start (ap, error);
    vsnprintf (fatalmsg, sizeof(fatalmsg), error, ap);
    va_end (ap);
    longjmp (fatal, 1);
}

// The zone: malloc behind a header that passes Z_ChangeTag's check.
void* Z_Malloc (int size, int tag, void *user)
{
    memblock_t*	block = calloc (1, sizeof(memblock_t) + size);

    (void)tag;
    block->id = 0x1d4a11;
    block->user = user;
    if (user)
	*(void **)user = block + 1;
    return block + 1;
}

void Z_Free (void *ptr)
{
    free ((memblock_t *)ptr - 1);
}

void Z_ChangeTag2 (void *ptr, int tag)
{
    (void)ptr;
    (void)tag;
}

// W_CacheLumpNum never frees what PU_CACHE would let the zone purge.
const char* __asan_default_options (void);
const char* __asan_default_options (void)
{
    return "detect_leaks=0";
}

// The WAD: a few lumps in memory.
typedef struct { const char* name; unsigned char* data; int size; } lump_t;
static lump_t	lumps[16];
static int	nlumps;

static void AddLump (const char* name, unsigned char* data, int size)
{
    lumps[nlumps].name = name;
    lumps[nlumps].data = data;
    lumps[nlumps].size = size;
    nlumps++;
}

int W_CheckNumForName (char* name)
{
    int	i;

    for (i = nlumps-1; i >= 0; i--)
	if (!strncasecmp (lumps[i].name, name, 8))
	    return i;
    return -1;
}

int W_GetNumForName (char* name)
{
    int	i = W_CheckNumForName (name);

    if (i == -1)
	I_Error ("W_GetNumForName: %s not found!", name);
    return i;
}

int W_LumpLength (int lump)
{
    return lumps[lump].size;
}

void W_ReadLump (int lump, void *dest)
{
    memcpy (dest, lumps[lump].data, lumps[lump].size);
}

void* W_CacheLumpNum (int lump, int tag)
{
    void*	p = Z_Malloc (lumps[lump].size, tag, NULL);

    W_ReadLump (lump, p);
    return p;
}

void* W_CacheLumpName (char* name, int tag)
{
    return W_CacheLumpNum (W_GetNumForName (name), tag);
}

static void Put16 (unsigned char* p, int v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
}

static void Put32 (unsigned char* p, int v)
{
    Put16 (p, v);
    Put16 (p+2, v >> 16);
}

// A 4x8 patch, every pixel 7.
static unsigned char	patch[8 + 4*4 + 4*(3+8+1+1)];

static void MakePatch (void)
{
    unsigned char*	col;
    int			x;

    Put16 (patch, 4);	// width
    Put16 (patch+2, 8);	// height
    for (x = 0; x < 4; x++)
    {
	Put32 (patch+8+4*x, 8+16 + 13*x);
	col = patch+8+16 + 13*x;
	col[0] = 0;	// topdelta
	col[1] = 8;	// length
	memset (col+3, 7, 8);
	col[12] = 0xff;	// after the pad byte
    }
}

static unsigned char	pnames[4 + 3*8];

// PNAMES: 0 is the patch, 1 is missing, as SKY4 is from DOOM 1.9.
static void MakePnames (void)
{
    Put32 (pnames, 3);
    memcpy (pnames+4, "PATCHA\0\0", 8);
    memcpy (pnames+12, "SKY4\0\0\0\0", 8);
    memcpy (pnames+20, "PATCHA\0\0", 8);
}

// TEXTURE1, each texture 8x8 from patch indices at x = 0, 4, ...
// (an index past PNAMES included).
static unsigned char	texture1[1024];

static void MakeTextures (void)
{
    static const struct { const char* name; int n; int patches[2]; } defs[] = {
	{ "GOOD",	2, { 0, 2 } },
	{ "HALF",	2, { 0, 1 } },
	{ "SKY4",	1, { 1 } },
	{ "BADINDEX",	2, { 0, 99 } },
    };
    int		ntex = sizeof(defs)/sizeof(defs[0]);
    int		ofs = 4 + 4*ntex;
    int		i;
    int		j;
    unsigned char*	t;

    Put32 (texture1, ntex);
    for (i = 0; i < ntex; i++)
    {
	Put32 (texture1+4+4*i, ofs);
	t = texture1 + ofs;
	memset (t, 0, 22);
	memcpy (t, defs[i].name, strlen (defs[i].name));
	Put16 (t+12, 8);	// width
	Put16 (t+14, 8);	// height
	Put16 (t+20, defs[i].n);
	for (j = 0; j < defs[i].n; j++)
	{
	    Put16 (t+22+10*j, 4*j);	// originx
	    Put16 (t+24+10*j, 0);	// originy
	    Put16 (t+26+10*j, defs[i].patches[j]);
	    Put16 (t+28+10*j, 1);
	    Put16 (t+30+10*j, 0);
	}
	ofs += 22 + 10*defs[i].n;
    }
}

static int AllBytes (const byte* p, int n, byte v)
{
    int	i;

    for (i = 0; i < n; i++)
	if (p[i] != v)
	    return 0;
    return 1;
}

// Walks a column's posts as R_DrawMaskedColumn does, from
// R_GetColumn - 3 (r_segs.c), so ASan sees any read past the
// column. Returns the pixels drawn, or -1 if one is not 7.
static int MaskedPixels (int tex, int x)
{
    column_t*	column = (column_t *)(R_GetColumn (tex, x) - 3);
    const byte*	source;
    int		drawn = 0;
    int		posts;

    for (posts = 0; column->topdelta != 0xff; posts++)
    {
	if (posts > 8)
	    return -1;
	source = (byte *)column + 3;
	if (!AllBytes (source, column->length, 7))
	    return -1;
	drawn += column->length;
	column = (column_t *)((byte *)column + column->length + 4);
    }
    return drawn;
}

int main (void)
{
    int		tex;
    int		x;

    MakePatch ();
    MakePnames ();
    MakeTextures ();
    AddLump ("PNAMES", pnames, sizeof(pnames));
    AddLump ("TEXTURE1", texture1, sizeof(texture1));
    AddLump ("S_START", NULL, 0);
    AddLump ("S_END", NULL, 0);
    AddLump ("PATCHA", patch, sizeof(patch));

    if (setjmp (fatal))
    {
	fprintf (stderr, "R_InitTextures failed: %s\n", fatalmsg);
	return 1;
    }
    R_InitTextures ();
    printf ("\n");

    // Untouched: both halves from the patch.
    tex = R_TextureNumForName ("GOOD");
    for (x = 0; x < 8; x++)
    {
	CHECK (texturecolumnlump[tex][x] == W_GetNumForName ("PATCHA"));
	CHECK (AllBytes (R_GetColumn (tex, x), 8, 7));
	CHECK (MaskedPixels (tex, x) == 8);
    }

    // The patch that is there is drawn, the gap left blank, and
    // transparent when it is a masked middle texture.
    tex = R_TextureNumForName ("HALF");
    for (x = 0; x < 4; x++)
    {
	CHECK (AllBytes (R_GetColumn (tex, x), 8, 7));
	CHECK (MaskedPixels (tex, x) == 8);
    }
    for (x = 4; x < 8; x++)
    {
	CHECK (AllBytes (R_GetColumn (tex, x), 8, 0));
	CHECK (MaskedPixels (tex, x) == 0);
    }

    // No patch at all: a blank texture, not a crash, and
    // nothing drawn as a masked texture.
    tex = R_TextureNumForName ("SKY4");
    for (x = 0; x < 8; x++)
    {
	CHECK (AllBytes (R_GetColumn (tex, x), 8, 0));
	CHECK (MaskedPixels (tex, x) == 0);
    }

    tex = R_TextureNumForName ("BADINDEX");
    for (x = 0; x < 4; x++)
    {
	CHECK (AllBytes (R_GetColumn (tex, x), 8, 7));
	CHECK (MaskedPixels (tex, x) == 8);
    }
    for (x = 4; x < 8; x++)
    {
	CHECK (AllBytes (R_GetColumn (tex, x), 8, 0));
	CHECK (MaskedPixels (tex, x) == 0);
    }

    if (failures)
    {
	fprintf (stderr, "%d check(s) failed\n", failures);
	return 1;
    }
    printf ("texture init: ok\n");
    return 0;
}
