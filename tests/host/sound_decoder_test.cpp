/* sound_decoder_test.cpp - the BNS, WAV and AIFF decoders and SoundDecoder::Decode on
 * hostile sound files.
 *
 * Runs on the host: the decoders need only libc once the Wii headers are stubbed
 * (tests/host/shim), so the code under test is the exact code the Wii runs. Ogg and Mp3
 * are left out (tremor and mad). A sound comes from a theme, a banner or the user's card, so
 * whatever it says about its own sample rate, chunk sizes and loop points is hostile.
 *
 * Every case runs in a forked child with a 20 s alarm: a crash, a sanitizer report or a
 * hang fails that case and the others still run. With a case number as argument only that
 * case runs, in the parent (to read its report).
 *
 * Wii files are big-endian and the Wii is too; the host is not. A magic is stored as the
 * u32 the decoder compares it with, AIFF and BNS fields are stored as they are, and every
 * WAV field goes through le16()/le32(), which swap on any machine.
 */
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <algorithm>
#include <new>
#include <string>
#include <vector>

#pragma GCC diagnostic ignored "-Wmultichar"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#define _CSETTINGS_H_
#define __TIMER_H
struct { bool ResampleTo48kHz; } Settings;

#include "FileOperations/File.cpp"
#include "SoundOperations/BufferCircle.cpp"
#include "SoundOperations/SoundDecoder.cpp"
#include "SoundOperations/BNSDecoder.cpp"
#include "SoundOperations/WavDecoder.cpp"
#include "SoundOperations/AifDecoder.cpp"

typedef std::vector<u8> Bytes;

static int failures;

static void check(bool cond, const char *what, unsigned long a = 0)
{
	if (!cond)
	{
		printf("  FAIL: %s (%lu)\n", what, a);
		failures++;
	}
}

/* ---- helpers to build images ---- */

static void put(Bytes &b, const void *p, size_t n)
{
	b.insert(b.end(), (const u8 *)p, (const u8 *)p + n);
}
static void put8(Bytes &b, u8 v) { put(b, &v, 1); }
static void put16(Bytes &b, u16 v) { put(b, &v, 2); }
static void put32(Bytes &b, u32 v) { put(b, &v, 4); }
static void putFill(Bytes &b, size_t n, u32 seed)
{
	for (size_t i = 0; i < n; i++)
	{
		seed = seed * 1664525u + 1013904223u;
		b.push_back((u8)(seed >> 24));
	}
}

/* what a decoder hands out: every ready buffer is non-empty and fits its block */
static size_t drain(SoundDecoder *d, int cap = 200)
{
	size_t total = 0;
	for (int i = 0; i < cap; i++)
	{
		d->Decode();
		int inner = 0;
		while (d->IsBufferReady() && inner++ < 100)
		{
			u32 sz = d->GetBufferSize();
			check(sz > 0, "ready buffer is empty");
			check(sz <= 8192, "ready buffer is larger than its block", sz);
			check(d->GetBuffer() != NULL, "ready buffer is NULL");
			total += sz;
			d->LoadNext();
		}
		if (d->IsEOF() && !d->IsBufferReady())
			break;
	}
	return total;
}

static void check_state(SoundDecoder *d, const char *what)
{
	u16 rate = d->GetSampleRate();
	u8 fmt = d->GetFormat();
	if (rate < 8000 || rate > 48000)
	{
		printf("  FAIL: %s: sample rate %u outside 8000..48000\n", what, rate);
		failures++;
	}
	check(fmt <= VOICE_STEREO_16BIT, "format is not a VOICE_* value", fmt);
}

/* ---- BNS ---- */

static Bytes bnsImage(int chans, u32 freq, u32 blocks, bool loop, u32 loopStart, u32 loopEnd,
					  int chunkBlocks = -1)
{
	BNSHeader h;
	BNSInfo info;
	memset(&h, 0, sizeof(h));
	memset(&info, 0, sizeof(info));
	u32 dataSize = (u32)sizeof(BNSData) - 1 + 8 * blocks;
	h.fccBNS = 'BNS ';
	h.infoOffset = sizeof(BNSHeader);
	h.infoSize = sizeof(BNSInfo);
	h.dataOffset = h.infoOffset + h.infoSize;
	h.dataSize = dataSize;
	h.size = h.dataOffset + dataSize;
	info.fccINFO = 'INFO';
	info.size = sizeof(BNSInfo);
	info.codecNum = 0;
	info.loopFlag = loop;
	info.chanCount = chans;
	info.freq = (u16)freq;
	info.loopStart = loopStart;
	info.loopEnd = loopEnd;
	info.offsetToChanStarts = 0x18;
	info.chan1StartOffset = 0x20;
	info.chan2StartOffset = 0x2C;
	info.coeff1Offset = 0x38;
	info.coeff2Offset = 0x68;
	Bytes b;
	put(b, &h, sizeof(h));
	put(b, &info, sizeof(info));
	put32(b, 'DATA');
	/* chunkBlocks: the size the DATA chunk claims, when it differs from the header's */
	put32(b, 8 + 8 * (chunkBlocks >= 0 ? (u32)chunkBlocks : blocks));
	putFill(b, 8 * blocks, 7 + blocks);
	return b;
}

static void case_bns_rate(u32 chans, u32 freq)
{
	Settings.ResampleTo48kHz = true;
	Bytes img = bnsImage(chans, freq, 600, false, 0, 0);
	BNSDecoder *d = new BNSDecoder(&img[0], (int)img.size());
	check_state(d, "bns");
	size_t total = drain(d);
	check(total > 0, "bns: nothing decoded");
	check_state(d, "bns after drain");
	delete d;
}

static void case_bns_loop_end_zero(u32 chans, u32)
{
	Settings.ResampleTo48kHz = true;
	Bytes img = bnsImage(chans, 22050, 64, true, 0, 0);
	BNSDecoder *d = new BNSDecoder(&img[0], (int)img.size());
	d->Rewind();
	d->SetLoop(true);
	drain(d);
	delete d;
}

static void case_bns_loop_data(u32 chans, u32 freq)
{
	Settings.ResampleTo48kHz = true;
	Bytes img = bnsImage(chans, freq, 64, true, 10, 1000000);
	BNSDecoder *d = new BNSDecoder(&img[0], (int)img.size());
	check_state(d, "bns loop");
	drain(d, 50);
	delete d;
}

static void case_bns_empty_loop(u32 chans, u32)
{
	/* a data chunk of 0 blocks with Loop set: Read() returns 0 forever. The header must
	 * claim at least sizeof(BNSData) bytes, so it says 2 blocks and the chunk says 0. */
	Settings.ResampleTo48kHz = true;
	Bytes img = bnsImage(chans, 22050, 2, false, 0, 0, 0);
	BNSDecoder *d = new BNSDecoder(&img[0], (int)img.size());
	d->Rewind();
	d->SetLoop(true);
	drain(d, 5);
	delete d;
}

static void case_bns_truncated(u32 chans, u32 freq)
{
	Settings.ResampleTo48kHz = true;
	Bytes img = bnsImage(chans, freq, 4, false, 0, 0);
	for (size_t len = 0; len < img.size(); len++)
	{
		u8 *copy = (u8 *)malloc(len ? len : 1);
		memcpy(copy, &img[0], len);
		BNSDecoder *d = new BNSDecoder(copy, (int)len);
		check_state(d, "bns truncated");
		drain(d, 5);
		delete d;
		free(copy);
	}
}

/* ---- WAV ---- */

struct WavOpts
{
	u32 fmtSize = 16;
	u32 freq = 48000;
	u16 chans = 2;
	u16 bps = 16;
	u32 dataSize = 4096; /* the size the data chunk claims */
	u32 dataLen = 4096;  /* what is really there */
	u32 riffSize = 0;
	bool extraOdd = false;
};

static Bytes wavImage(const WavOpts &o)
{
	Bytes b;
	put32(b, 'RIFF');
	put32(b, o.riffSize);
	put32(b, 'WAVE');
	put32(b, 'fmt ');
	put32(b, le32(o.fmtSize));
	put16(b, le16(1));
	put16(b, le16(o.chans));
	put32(b, le32(o.freq));
	put32(b, le32(o.freq * o.chans * o.bps / 8));
	put16(b, le16(o.chans * o.bps / 8));
	put16(b, le16(o.bps));
	put32(b, 'data');
	put32(b, le32(o.dataSize));
	putFill(b, o.dataLen, 3);
	return b;
}

static void case_wav_rate(u32 freq, u32)
{
	Settings.ResampleTo48kHz = true;
	WavOpts o;
	o.freq = freq;
	o.dataSize = o.dataLen = 40000;
	Bytes img = wavImage(o);
	WavDecoder *d = new WavDecoder(&img[0], (int)img.size());
	check_state(d, "wav");
	size_t total = drain(d);
	check(total > 0, "wav: nothing decoded");
	check_state(d, "wav after drain");
	delete d;
}

static void case_wav_huge_data(u32 size, u32)
{
	WavOpts o;
	o.dataSize = size;
	o.dataLen = 2000;
	Bytes img = wavImage(o);
	WavDecoder *d = new WavDecoder(&img[0], (int)img.size());
	size_t total = drain(d);
	check(total <= 2000 + 8192, "wav: decoded more than the file holds", total);
	delete d;
}

static void case_wav_wrapped_fmt(u32 fmtSize, u32)
{
	/* 12 + fmtSize + 8 wraps to 4, which would point the data chunk walk at the
	 * RIFF size field; the file makes that field say 'data'. */
	WavOpts o;
	o.fmtSize = fmtSize;
	o.dataSize = o.dataLen = 4096;
	o.riffSize = 'data';
	Bytes img = wavImage(o);
	WavDecoder *d = new WavDecoder(&img[0], (int)img.size());
	size_t total = drain(d, 20);
	check(total == 0, "wav: a wrapped fmt size was accepted and played", total);
	delete d;
}

static void case_wav_truncated(u32, u32)
{
	Settings.ResampleTo48kHz = true;
	WavOpts o;
	o.dataSize = o.dataLen = 64;
	Bytes img = wavImage(o);
	for (size_t len = 0; len < img.size(); len++)
	{
		u8 *copy = (u8 *)malloc(len ? len : 1);
		memcpy(copy, &img[0], len);
		WavDecoder *d = new WavDecoder(copy, (int)len);
		check_state(d, "wav truncated");
		drain(d, 5);
		delete d;
		free(copy);
	}
}

/* ---- AIFF ---- */

static const u8 kFreq22050[10] = {0x40, 0x0D, 0xAC, 0x44, 0, 0, 0, 0, 0, 0};
static const u8 kFreq44100[10] = {0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0};
static const u8 kFreq1[10] = {0x3F, 0xFF, 0x80, 0, 0, 0, 0, 0, 0, 0};
static const u8 kFreq0[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

struct AifOpts
{
	const u8 *freq = kFreq44100;
	u16 chans = 2;
	u16 bps = 16;
	u32 commSize = 18;
	bool odd = false;      /* an 'ANNO' chunk of 3 bytes plus its pad byte before SSND */
	u32 oddSize = 3;
	u32 ssndSize = 0;      /* 0: 8 + dataLen */
	u32 dataLen = 4096;
};

static Bytes aifImage(const AifOpts &o)
{
	Bytes b;
	put32(b, 'FORM');
	put32(b, 0);
	put32(b, 'AIFF');
	put32(b, 'COMM');
	put32(b, o.commSize);
	put16(b, o.chans);
	putFill(b, 4, 1); /* frames */
	put16(b, o.bps);
	put(b, o.freq, 10);
	if (o.odd)
	{
		put32(b, 'ANNO');
		put32(b, o.oddSize);
		put8(b, 'a');
		put8(b, 'b');
		put8(b, 'c');
		put8(b, 0); /* pad */
	}
	put32(b, 'SSND');
	put32(b, o.ssndSize ? o.ssndSize : 8 + o.dataLen);
	put32(b, 0);
	put32(b, 0);
	putFill(b, o.dataLen, 5);
	return b;
}

static const u8 *aifFreq(u32 i)
{
	switch (i)
	{
		case 0: return kFreq22050;
		case 1: return kFreq44100;
		case 2: return kFreq1;
		default: return kFreq0;
	}
}

static void case_aif_rate(u32 fi, u32 odd)
{
	Settings.ResampleTo48kHz = true;
	AifOpts o;
	o.freq = aifFreq(fi);
	o.odd = odd;
	o.dataLen = 40000;
	Bytes img = aifImage(o);
	AifDecoder *d = new AifDecoder(&img[0], (int)img.size());
	check_state(d, "aif");
	size_t total = drain(d);
	check(total > 0, odd ? "aif with an odd-sized chunk: nothing decoded" : "aif: nothing decoded");
	check_state(d, "aif after drain");
	delete d;
}

static void case_aif_hostile(u32 which, u32)
{
	AifOpts o;
	switch (which)
	{
		case 0: o.commSize = 0xFFFFFFF0; break;
		case 1: o.commSize = 0xFFFFFFFF; break;
		case 2: o.odd = true; o.oddSize = 0xFFFFFFFF; break;
		case 3: o.odd = true; o.oddSize = 0xFFFFFFFD; break; /* odd, plus pad wraps */
		case 4: o.ssndSize = 0xFFFFFFFF; break;
		case 5: o.ssndSize = 7; break;
		case 6: o.dataLen = 0; break;
	}
	Bytes img = aifImage(o);
	AifDecoder *d = new AifDecoder(&img[0], (int)img.size());
	size_t total = drain(d, 20);
	check(total <= 4096 + 8192, "aif: decoded more than the file holds", total);
	delete d;
}

static void case_aif_truncated(u32 odd, u32)
{
	Settings.ResampleTo48kHz = true;
	AifOpts o;
	o.odd = odd;
	o.dataLen = 64;
	Bytes img = aifImage(o);
	for (size_t len = 0; len < img.size(); len++)
	{
		u8 *copy = (u8 *)malloc(len ? len : 1);
		memcpy(copy, &img[0], len);
		AifDecoder *d = new AifDecoder(copy, (int)len);
		check_state(d, "aif truncated");
		drain(d, 5);
		delete d;
		free(copy);
	}
}

/* ---- SoundDecoder itself ---- */

class Probe : public SoundDecoder
{
public:
	Probe() : SoundDecoder() {}
	Probe(const u8 *b, int n) : SoundDecoder(b, n) {}
	u8 fmt() { return Format; }
	u16 rate() { return SampleRate; }
	u16 which() { return whichLoad; }
	void setup(u8 f, u16 r, bool resample)
	{
		Format = f;
		SampleRate = r;
		ResampleTo48kHz = resample;
	}
};

static void case_init_defaults(u32, u32)
{
	static unsigned char mem[sizeof(Probe)] __attribute__((aligned(32)));
	memset(mem, 0xAA, sizeof(mem));
	Probe *p = new (mem) Probe();
	check(p->fmt() == VOICE_STEREO_16BIT, "Init left Format unset", p->fmt());
	check(p->rate() == 48000, "Init left SampleRate unset", p->rate());
	check(p->which() == 0, "Init left whichLoad unset", p->which());
	p->~Probe();
}

static void case_probe_recursion(u32 rate, u32)
{
	/* stereo 16 bit at a rate whose upsampled block is empty: Decode() must return */
	Bytes data;
	putFill(data, 16384, 9);
	Probe *p = new Probe(&data[0], (int)data.size());
	p->setup(VOICE_STEREO_16BIT, (u16)rate, true);
	drain(p, 20);
	delete p;
}

static void case_probe_loop_empty(u32 rate, u32)
{
	u8 dummy = 0;
	Probe *p = new Probe(&dummy, 0);
	p->setup(VOICE_STEREO_16BIT, (u16)rate, rate != 0);
	p->SetLoop(true);
	drain(p, 5);
	delete p;
}

/* ---- runner ---- */

typedef void (*case_fn)(u32, u32);
struct Case
{
	const char *name;
	case_fn fn;
	u32 a, b;
};

static std::vector<Case> cases;
static void add(const char *name, case_fn fn, u32 a = 0, u32 b = 0)
{
	Case c = {name, fn, a, b};
	cases.push_back(c);
}

static void build_cases()
{
	static const u32 freqs[] = {0, 1, 23, 24, 100, 22050, 65535};
	add("init defaults", case_init_defaults);
	for (u32 chans = 1; chans <= 2; chans++)
	{
		for (u32 f : freqs)
		{
			add("bns rate (chans, freq)", case_bns_rate, chans, f);
			add("bns loop data (chans, freq)", case_bns_loop_data, chans, f);
		}
		add("bns truncated prefixes (chans, freq 22050)", case_bns_truncated, chans, 22050);
		add("bns loopEnd 0 with Loop (chans)", case_bns_loop_end_zero, chans);
		add("bns 0 blocks with Loop (chans)", case_bns_empty_loop, chans, 1);
	}
	static const u32 wavFreqs[] = {0, 1, 23, 7999, 8000, 22050, 48000, 48001, 65536 + 22050, 0xFFFFFFFF};
	for (u32 f : wavFreqs)
		add("wav rate (freq)", case_wav_rate, f);
	static const u32 sizes[] = {0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFF0, 0xFFFFFFFF};
	for (u32 s : sizes)
		add("wav data chunk size (size)", case_wav_huge_data, s);
	static const u32 fmts[] = {0xFFFFFFF0, 0xFFFFFFF4, 0xFFFFFFFC, 0xFFFFFFFF};
	for (u32 s : fmts)
		add("wav wrapped fmt size (size)", case_wav_wrapped_fmt, s);
	add("wav truncated prefixes", case_wav_truncated);
	for (u32 fi = 0; fi < 4; fi++)
		for (u32 odd = 0; odd < 2; odd++)
			add("aif rate (freq index, odd chunk)", case_aif_rate, fi, odd);
	for (u32 w = 0; w < 7; w++)
		add("aif hostile (kind)", case_aif_hostile, w);
	add("aif truncated prefixes", case_aif_truncated, 0);
	add("aif truncated prefixes with odd chunk", case_aif_truncated, 1);
	static const u32 probeRates[] = {0, 1, 10, 23, 24, 25, 100, 8000, 22050, 32000, 44100, 47999, 48000};
	for (u32 r : probeRates)
		add("Decode with stereo16 resample (rate)", case_probe_recursion, r);
	add("Decode with Loop on a 0 byte file (rate 22050)", case_probe_loop_empty, 22050);
	add("Decode with Loop on a 0 byte file (rate 0)", case_probe_loop_empty, 0);
}

static int run_case(size_t i, bool inChild)
{
	failures = 0;
	const Case &c = cases[i];
	if (inChild)
		alarm(20);
	c.fn(c.a, c.b);
	return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	build_cases();
	if (argc > 1)
	{
		size_t i = (size_t)atoi(argv[1]);
		if (i >= cases.size())
			return 2;
		alarm(30);
		return run_case(i, false);
	}

	int bad = 0;
	for (size_t i = 0; i < cases.size(); i++)
	{
		fflush(stdout);
		fflush(stderr);
		pid_t pid = fork();
		if (pid == 0)
		{
			int r = run_case(i, true);
			fflush(stdout);
			_exit(r);
		}
		int st = 0;
		waitpid(pid, &st, 0);
		if (WIFEXITED(st) && WEXITSTATUS(st) == 0)
			continue;
		bad++;
		if (WIFSIGNALED(st))
			printf("FAIL: case %zu: %s %u %u: killed by signal %d%s\n", i, cases[i].name, cases[i].a,
				   cases[i].b, WTERMSIG(st), WTERMSIG(st) == SIGALRM ? " (hang)" : "");
		else
			printf("FAIL: case %zu: %s %u %u: exit %d\n", i, cases[i].name, cases[i].a, cases[i].b,
				   WEXITSTATUS(st));
	}
	printf("sound_decoder_test: %zu cases, %d failed\n", cases.size(), bad);
	return bad ? 1 : 0;
}
