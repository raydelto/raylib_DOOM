// UMAPINFO parser regression test.
//
// The input mirrors what SIGIL (E5) and SIGIL II (E6) ship,
// written out here so no WAD is needed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u_mapinfo.h"

static int failures;

#define CHECK(cond) \
    do { if (!(cond)) { \
	fprintf (stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
	failures++; } } while (0)

static const char sigil2[] =
    "// UMAPINFO for SIGIL II\n"
    "\n"
    "map E6M1\n"
    "{\n"
    "\tLevelName = \"Cursed Darkness\"\n"
    "\tLevelPic = \"WILV50\"\n"
    "\tNext = \"E6M2\"\n"
    "\tMusic = \"D_E6M1\"\n"
    "\tSkyTexture = \"SKY6\"\n"
    "\tExitPic = \"SIGILIN2\"\n"
    "\tEpisode = \"M_EPI6\", \"SIGIL II\", \"S\"\n"
    "\tpartime = 480\n"
    "}\n"
    "\n"
    "map E6M3\n"
    "{\n"
    "\tNext = \"E6M4\"\n"
    "\tNextSecret = \"E6M9\"\n"
    "}\n"
    "\n"
    "/* block comment */ map e6m8\n"
    "{\n"
    "\tLevelName = \"Abyss of Despair\"\n"
    "\tEndPic = \"CREDIT\"\n"
    "\tInterText = \"Satan erred in casting you to Hell's\",\n"
    "\t\t\t\"darker depths.\",\n"
    "\t\t\t\"\",\n"
    "\t\t\t\"Prepare for HELLION!\"\n"
    "\tBossAction = clear\n"
    "\tAuthor = \"John Romero\"  // unknown key, skipped\n"
    "\tpartime = 390\n"
    "}\n"
    "\n"
    "map E6M9\n"
    "{\n"
    "\tNext = \"E6M4\"\n"
    "}\n";

// SIGIL's E5, loaded after SIGIL II, then a redefinition.
static const char sigil1[] =
    "map E5M1 { LevelName = \"Baphomet's Demesne\"\n"
    "  Episode = \"M_EPI5\", \"SIGIL\", \"S\" }\n"
    "map E6M9 { Next = \"E6M5\" }\n";

static const char broken[] =
    "map E1M1 { LevelName = \"ok\" }\n"
    "map E1M2 { LevelName \"missing equals\" }\n";

int main (void)
{
    umapinfo_t*	mi;
    int		ep, map;

    CHECK (U_ParseMapName ("E6M1", &ep, &map) && ep == 6 && map == 1);
    CHECK (U_ParseMapName ("map07", &ep, &map) && ep == 0 && map == 7);
    CHECK (!U_ParseMapName ("E6M10", &ep, &map));
    CHECK (!U_ParseMapName ("SIGILIN2", &ep, &map));

    CHECK (U_ParseMapInfo (sigil2, (int)strlen (sigil2), "sigil2"));

    mi = U_FindMap (6, 1);
    CHECK (mi != NULL);
    if (mi)
    {
	CHECK (!strcmp (mi->levelname, "Cursed Darkness"));
	CHECK (!strcmp (mi->levelpic, "WILV50"));
	CHECK (!strcmp (mi->next, "E6M2"));
	CHECK (!strcmp (mi->music, "D_E6M1"));
	CHECK (!strcmp (mi->skytexture, "SKY6"));
	CHECK (!strcmp (mi->exitpic, "SIGILIN2"));
	CHECK (mi->partime == 480);
	CHECK (!U_EndsGame (mi));
	CHECK (!mi->bossactionclear);
    }

    mi = U_FindMap (6, 3);
    CHECK (mi && !strcmp (mi->nextsecret, "E6M9"));

    mi = U_FindMap (6, 8);
    CHECK (mi != NULL);
    if (mi)
    {
	CHECK (U_EndsGame (mi));
	CHECK (!strcmp (mi->endpic, "CREDIT"));
	CHECK (mi->bossactionclear);
	CHECK (mi->partime == 390);
	CHECK (mi->next[0] == 0);
	CHECK (mi->intertext && !strcmp (mi->intertext,
	    "Satan erred in casting you to Hell's\n"
	    "darker depths.\n"
	    "\n"
	    "Prepare for HELLION!"));
    }

    CHECK (numumapepisodes == 1);
    CHECK (!strcmp (umapepisodes[0].patch, "M_EPI6"));
    CHECK (!strcmp (umapepisodes[0].name, "SIGIL II"));
    CHECK (umapepisodes[0].key == 's');
    CHECK (umapepisodes[0].episode == 6 && umapepisodes[0].map == 1);

    CHECK (U_FindMap (6, 2) == NULL);
    CHECK (U_FindMap (4, 1) == NULL);

    // A later lump adds its episode and replaces E6M9 whole.
    CHECK (U_ParseMapInfo (sigil1, (int)strlen (sigil1), "sigil1"));
    CHECK (numumapepisodes == 2);
    CHECK (umapepisodes[1].episode == 5 && !strcmp (umapepisodes[1].patch, "M_EPI5"));
    mi = U_FindMap (6, 9);
    CHECK (mi && !strcmp (mi->next, "E6M5"));
    mi = U_FindMap (5, 1);
    CHECK (mi && !strcmp (mi->levelname, "Baphomet's Demesne"));
    CHECK (mi && mi->next[0] == 0);

    // A syntax error fails the lump but keeps what came before.
    CHECK (!U_ParseMapInfo (broken, (int)strlen (broken), "broken"));
    mi = U_FindMap (1, 1);
    CHECK (mi && !strcmp (mi->levelname, "ok"));

    // Not NUL-terminated: the parser must stop at the length.
    {
	char	buf[] = "map E2M1 { Next = \"E2M2\" }XXXXXXXX";

	CHECK (U_ParseMapInfo (buf, (int)strlen ("map E2M1 { Next = \"E2M2\" }"), "len"));
	mi = U_FindMap (2, 1);
	CHECK (mi && !strcmp (mi->next, "E2M2"));
    }

    U_FreeMapInfo ();
    CHECK (U_FindMap (6, 1) == NULL);
    CHECK (numumapepisodes == 0);

    if (failures)
    {
	fprintf (stderr, "%d check(s) failed\n", failures);
	return 1;
    }
    printf ("umapinfo: all checks passed\n");
    return 0;
}
