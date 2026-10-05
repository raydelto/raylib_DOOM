// DEHACKED text regression test.
//
// The input mirrors what SIGIL_COMPAT (SIGIL's maps in E3) and
// Freedoom ship, written out here so no WAD is needed. Without
// these strings, SIGIL_COMPAT's E3M1 shows the IWAD's E3M1 name.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u_dehacked.h"

static int failures;

#define CHECK(cond) \
    do { if (!(cond)) { \
	fprintf (stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
	failures++; } } while (0)

// SIGIL_COMPAT_V1_23.wad's DEHACKED, shortened, with CRLF line ends.
static const char compat[] =
    "Patch File for DeHackEd v3.0\r\n"
    "# Created with WhackEd4 1.2.0 BETA\r\n"
    "Doom version = 21\r\n"
    "Patch format = 6\r\n"
    "\r\n"
    "[CODEPTR]\r\n"
    "FRAME 631 = NULL\r\n"
    "\r\n"
    "[PARS]\r\n"
    "par 3 1 90\r\n"
    "par 3 9 660\r\n"
    "\r\n"
    "[STRINGS]\r\n"
    "E3TEXT = Baphomet was only doing Satan's bidding\\nby bringing you back to Hell.\r\n"
    "HUSTR_E3M1 = E3M1: Baphomet's Demesne\r\n"
    "HUSTR_E3M9 = E3M9: Realm of Iblis\r\n";

// Freedoom: frame changes before the BEX sections, a par time with
// a comment, a "Text" block whose text looks like a [STRINGS] line,
// a value continued over two lines, and SIGIL's E5 par times that
// have no slot.
static const char other[] =
    "Patch File for DeHackEd v3.0\n"
    "Frame 185\n"
    "Sprite subnumber = 32773\n"
    "\n"
    "[PARS]\n"
    "par  1 1   30  # 00:30\n"
    "par 5 1 90\n"
    "PAR 7 120\n"
    "\n"
    "Text 6 10\n"
    "HangarE1M1 = Nope\n"
    "HUSTR_E2M1 = E2M1: Wrong\n"
    "[strings]\n"
    "  hustr_e1m1   =   E1M1: Outer Prison   \n"
    "C1TEXT = one\\\n"
    "    two\n"
    "UNKNOWN = ignored\n"
    "NOEQUALS ignored\n"
    "Thing 1 (Player)\n"
    "HUSTR_E1M2 = not in [STRINGS] any more\n";

static char*	e3m1 = "E3M1: Hell Keep";
static char*	e3m9 = "E3M9: Warrens";
static char*	e1m1 = "E1M1: Hangar";
static char*	e1m2 = "E1M2: Nuclear Plant";
static char*	e2m1 = "E2M1: Deimos Anomaly";
static char*	e3text = "old";
static char*	c1text = "old";

static int	pars[4][10];
static int	cpars[32];

static int SetPar (int episode, int map, int seconds)
{
    if (episode == 0 && map >= 1 && map <= 32)
	cpars[map-1] = seconds;
    else if (episode >= 1 && episode <= 3 && map >= 1 && map <= 9)
	pars[episode][map] = seconds;
    else
	return 0;
    return 1;
}

static const dehstring_t strings[] =
{
    { "HUSTR_E3M1", &e3m1 },
    { "HUSTR_E3M9", &e3m9 },
    { "HUSTR_E1M1", &e1m1 },
    { "HUSTR_E1M2", &e1m2 },
    { "HUSTR_E2M1", &e2m1 },
    { "E3TEXT", &e3text },
    { "C1TEXT", &c1text },
};
#define NUMSTRINGS	(int)(sizeof(strings)/sizeof(strings[0]))

static void Parse (const char* text, dehcount_t* count)
{
    U_ParseDehacked (text, (int)strlen (text), strings, NUMSTRINGS,
		     SetPar, count);
}

int main (void)
{
    dehcount_t	count = { 0, 0 };

    Parse (compat, &count);
    CHECK (!strcmp (e3m1, "E3M1: Baphomet's Demesne"));
    CHECK (!strcmp (e3m9, "E3M9: Realm of Iblis"));
    CHECK (!strcmp (e3text, "Baphomet was only doing Satan's bidding\n"
			    "by bringing you back to Hell."));
    CHECK (pars[3][1] == 90);
    CHECK (pars[3][9] == 660);
    CHECK (count.strings == 3);
    CHECK (count.pars == 2);

    count.strings = count.pars = 0;
    Parse (other, &count);
    CHECK (!strcmp (e1m1, "E1M1: Outer Prison"));
    CHECK (!strcmp (c1text, "onetwo"));
    CHECK (!strcmp (e2m1, "E2M1: Deimos Anomaly"));
    CHECK (!strcmp (e1m2, "E1M2: Nuclear Plant"));
    CHECK (pars[1][1] == 30);
    CHECK (cpars[6] == 120);
    CHECK (count.strings == 2);
    CHECK (count.pars == 2);

    // A later lump wins, an empty one changes nothing, and a value
    // cut off by the end of the lump is still used. The game never
    // frees replaced strings; the test does, for LeakSanitizer.
    free (e3m1);
    Parse ("[STRINGS]\nHUSTR_E3M1 = E3M1: Later\n", &count);
    CHECK (!strcmp (e3m1, "E3M1: Later"));
    Parse ("", &count);
    free (e3m9);
    Parse ("[STRINGS]\nHUSTR_E3M9 = cut\\", &count);
    CHECK (!strcmp (e3m9, "cut"));

    free (e3m1);
    free (e3m9);
    free (e1m1);
    free (e3text);
    free (c1text);

    if (failures)
    {
	fprintf (stderr, "%d check(s) failed\n", failures);
	return 1;
    }
    printf ("dehacked: all checks passed\n");
    return 0;
}
