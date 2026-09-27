//
// Regression tests for the Standard MIDI file parser in i_music.c:
// MIDI_Register must reject or clamp malformed chunk lengths,
// including ones with the high bit set, and never point a track
// outside the lump.
//
// i_music.c is included directly so that its static functions can be
// called; the few game and platform functions it uses are stubbed.
//

#include "../linuxdoom-1.10/i_music.c"

int	snd_MusicVolume = 8;

int M_CheckParm (char* check) { return 0; }
int W_CheckNumForName (char* name) { return -1; }
int W_LumpLength (int lump) { return 0; }
void* W_CacheLumpNum (int lump, int tag) { return NULL; }
int RL_OpenStream (int stream, int samplerate) { return 0; }
int RL_AudioQueued (int stream) { return 0; }
void RL_QueueAudio (int stream, const short* samples, int frames) { }

static int	failures;

#define CHECK(cond)							\
    do {								\
	if (!(cond))							\
	{								\
	    fprintf (stderr, "%s:%d: %s: check failed: %s\n",		\
		     __FILE__, __LINE__, name, #cond);			\
	    failures++;							\
	}								\
    } while (0)

static void PutLength (byte* p, uint32_t length)
{
    p[0] = length >> 24;
    p[1] = length >> 16;
    p[2] = length >> 8;
    p[3] = length;
}

// A format 0 file: a 6-byte MThd, then one MTrk holding a single
// end-of-track event. Returns its size.
static int MakeFile (byte* buf, uint32_t headerlength, uint32_t tracklength)
{
    static const byte track[] = { 0x00, 0xff, 0x2f, 0x00 };

    memcpy (buf, "MThd", 4);
    PutLength (buf + 4, headerlength);
    buf[8] = 0; buf[9] = 0;		// format 0
    buf[10] = 0; buf[11] = 1;		// one track
    buf[12] = 0; buf[13] = 96;		// 96 ticks per quarter note
    memcpy (buf + 14, "MTrk", 4);
    PutLength (buf + 18, tracklength);
    memcpy (buf + 22, track, sizeof(track));
    return 22 + sizeof(track);
}

// Every registered track must lie inside the lump.
static void CheckTracks (const char* name, const byte* buf, int length)
{
    int		i;

    for (i = 0; i < nummiditracks; i++)
    {
	CHECK (miditracks[i].start >= buf);
	CHECK (miditracks[i].start <= miditracks[i].end);
	CHECK (miditracks[i].end <= buf + length);
    }
}

static void TestHeaderLength (const char* name, uint32_t headerlength)
{
    byte	buf[64];
    int		length = MakeFile (buf, headerlength, 4);

    CHECK (!MIDI_Register (buf, length));
}

static void TestTrackLength (const char* name, uint32_t tracklength)
{
    byte	buf[64];
    int		length = MakeFile (buf, 6, tracklength);

    // Truncated tracks are clamped to the lump and still play.
    CHECK (MIDI_Register (buf, length));
    CHECK (nummiditracks == 1);
    CheckTracks (name, buf, length);
    if (nummiditracks == 1)
	CHECK (miditracks[0].end == buf + length);
}

int main (void)
{
    byte	buf[64];
    int		length;
    const char*	name;

    name = "valid file";
    length = MakeFile (buf, 6, 4);
    CHECK (MIDI_Register (buf, length));
    CHECK (nummiditracks == 1);
    CHECK (mididivision == 96);
    CheckTracks (name, buf, length);

    name = "not MIDI";
    memcpy (buf, "MUS\x1a", 4);
    CHECK (!MIDI_Register (buf, length));

    name = "too short";
    length = MakeFile (buf, 6, 4);
    CHECK (!MIDI_Register (buf, 13));

    name = "no track";
    CHECK (!MIDI_Register (buf, 14));

    TestHeaderLength ("header length 5", 5);
    TestHeaderLength ("header length past the lump", 64);
    TestHeaderLength ("header length 0x7fffffff", 0x7fffffff);
    TestHeaderLength ("header length 0x80000000", 0x80000000);
    TestHeaderLength ("header length 0x80000006", 0x80000006);
    TestHeaderLength ("header length 0xffffffff", 0xffffffff);

    TestTrackLength ("track length past the lump", 5);
    TestTrackLength ("track length 0x7fffffff", 0x7fffffff);
    TestTrackLength ("track length 0x80000000", 0x80000000);
    TestTrackLength ("track length 0xfffffff8", 0xfffffff8);
    TestTrackLength ("track length 0xffffffff", 0xffffffff);

    if (failures)
    {
	fprintf (stderr, "%d check(s) failed\n", failures);
	return 1;
    }
    printf ("midi_register_test: all checks passed\n");
    return 0;
}
