/* lz77_test.c - the LZ77 decompressors (flavours 0x10 and 0x11) on valid and hostile input.
 *
 * Runs on the host: source/utils/lz77.c needs only gctypes.h and malloc, so the code
 * under test is the exact code the Wii runs. Every banner (U8Archive) and every
 * channel DOL (Channels::GetDol) goes through it, and the data comes off a disc, a
 * NAND title or the user's card, so no input may make it read before or past a
 * buffer, or write past the output it allocated.
 *
 * Every input is a malloc of exactly its length, so ASan sees a read past the end.
 * With a case number as argument only that case runs (to see each failure alone).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils/lz77.c"

static int failures;

static void check(const char *what, int cond)
{
	if (!cond)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* ---- a small encoder: a list of literals and back-references ---- */

typedef struct
{
	int match;
	uint32_t pos, len; /* match: copy len bytes from pos back */
	uint8_t c;         /* literal */
} op_t;

static size_t put_match(int flavour, uint8_t *p, uint32_t pos, uint32_t len)
{
	uint32_t d = pos - 1;
	if (flavour == 0x10)
	{
		p[0] = ((len - 3) << 4) | (d >> 8);
		p[1] = d & 0xFF;
		return 2;
	}
	if (len <= 16)
	{
		p[0] = ((len - 1) << 4) | (d >> 8);
		p[1] = d & 0xFF;
		return 2;
	}
	if (len <= 272)
	{
		uint32_t v = len - 17;
		p[0] = v >> 4;
		p[1] = ((v & 0xF) << 4) | (d >> 8);
		p[2] = d & 0xFF;
		return 3;
	}
	{
		uint32_t v = len - 273;
		p[0] = 0x10 | (v >> 12);
		p[1] = (v >> 4) & 0xFF;
		p[2] = ((v & 0xF) << 4) | (d >> 8);
		p[3] = d & 0xFF;
		return 4;
	}
}

/* Encodes ops, with `size` as the declared output size. Returns the stream length. */
static size_t encode(int flavour, const op_t *ops, size_t n, uint32_t size, uint8_t *out)
{
	size_t o = 4, i = 0;
	out[0] = flavour;
	out[1] = size;
	out[2] = size >> 8;
	out[3] = size >> 16;
	while (i < n)
	{
		size_t flagAt = o++;
		out[flagAt] = 0;
		for (int b = 0; b < 8 && i < n; b++, i++)
		{
			if (ops[i].match)
			{
				out[flagAt] |= 0x80 >> b;
				o += put_match(flavour, out + o, ops[i].pos, ops[i].len);
			}
			else
				out[o++] = ops[i].c;
		}
	}
	return o;
}

static uint32_t rnd_state = 12345;
static uint32_t rnd(void)
{
	rnd_state = rnd_state * 1664525u + 1013904223u;
	return rnd_state >> 8;
}

/* Runs ops against a model buffer: the bytes the decompressor has to produce. */
static size_t model(const op_t *ops, size_t n, uint8_t *out)
{
	size_t o = 0;
	for (size_t i = 0; i < n; i++)
	{
		if (ops[i].match)
		{
			for (uint32_t y = 0; y < ops[i].len; y++, o++)
				out[o] = out[o - ops[i].pos];
		}
		else
			out[o++] = ops[i].c;
	}
	return o;
}

/* An input of exactly len bytes on the heap. */
static uint8_t *copy_in(const uint8_t *p, size_t len)
{
	uint8_t *d = malloc(len ? len : 1);
	if (len)
		memcpy(d, p, len);
	return d;
}

/* Decompresses a copy of in[0..len) and frees the result. Returns the function's result. */
static int run(const uint8_t *in, size_t len, uint32_t *outLen)
{
	uint8_t *d = copy_in(in, len);
	uint8_t *out = NULL;
	uint32_t n = 0;
	int ret = (len ? decompressLZ77content(d, (u32)len, &out, &n) : -1);
	if (outLen)
		*outLen = n;
	if (ret == 0)
		free(out);
	else
		check("a failure leaves *output alone", out == NULL);
	free(d);
	return ret;
}

/* ---- cases ---- */

static const uint8_t expect12[12] = {'a', 'b', 'c', 'a', 'b', 'c', 'a', 'b', 'c', 'a', 'b', 'c'};

static void case_valid_10(void)
{
	/* abc, then 9 bytes from 3 back: an overlapping copy */
	const uint8_t in[] = {0x10, 12, 0, 0, 0x10, 'a', 'b', 'c', 0x60, 0x02};
	uint8_t *d = copy_in(in, sizeof in), *out = NULL;
	u32 n = 0;
	check("0x10 valid returns 0", decompressLZ77content(d, sizeof in, &out, &n) == 0);
	check("0x10 valid length", n == 12);
	check("0x10 valid bytes", out && memcmp(out, expect12, 12) == 0);
	free(out);
	free(d);
}

static void case_valid_11(void)
{
	const uint8_t in[] = {0x11, 12, 0, 0, 0x10, 'a', 'b', 'c', 0x80, 0x02};
	uint8_t *d = copy_in(in, sizeof in), *out = NULL;
	u32 n = 0;
	check("0x11 valid returns 0", decompressLZ77content(d, sizeof in, &out, &n) == 0);
	check("0x11 valid length", n == 12);
	check("0x11 valid bytes", out && memcmp(out, expect12, 12) == 0);
	free(out);
	free(d);
}

static void case_valid_11_extended_size(void)
{
	/* a zero 24-bit size means the real size follows in 4 bytes */
	const uint8_t in[] = {0x11, 0, 0, 0, 12, 0, 0, 0, 0x10, 'a', 'b', 'c', 0x80, 0x02};
	uint8_t *d = copy_in(in, sizeof in), *out = NULL;
	u32 n = 0;
	check("0x11 extended size returns 0", decompressLZ77content(d, sizeof in, &out, &n) == 0);
	check("0x11 extended size length", n == 12);
	check("0x11 extended size bytes", out && memcmp(out, expect12, 12) == 0);
	free(out);
	free(d);
}

/* Random streams through the encoder: every length form of both flavours. */
static void roundtrip(int flavour)
{
	enum { MAXOPS = 400 };
	static op_t ops[MAXOPS];
	static uint8_t want[MAXOPS * 3300 + 64], enc[MAXOPS * 8 + 64];
	for (int round = 0; round < 60; round++)
	{
		size_t n = 1 + rnd() % MAXOPS, outLen = 0;
		for (size_t i = 0; i < n; i++)
		{
			op_t *o = &ops[i];
			memset(o, 0, sizeof *o);
			if (outLen > 0 && rnd() % 3)
			{
				uint32_t maxPos = outLen < 0x1000 ? (uint32_t)outLen : 0x1000;
				o->match = 1;
				o->pos = 1 + rnd() % maxPos;
				if (flavour == 0x10)
					o->len = 3 + rnd() % 16;
				else
				{
					uint32_t k = rnd() % 4;
					o->len = k == 0 ? 3 + rnd() % 14 : k == 1 ? 17 + rnd() % 256 : k == 2 ? 273 + rnd() % 3000 : 3 + rnd() % 300;
				}
			}
			else
				o->c = rnd();
			if (o->match)
				outLen += o->len;
			else
				outLen++;
		}
		size_t m = model(ops, n, want);
		size_t el = encode(flavour, ops, n, (uint32_t)m, enc);
		uint8_t *d = copy_in(enc, el), *out = NULL;
		u32 got = 0;
		int ret = decompressLZ77content(d, (u32)el, &out, &got);
		check("round trip returns 0", ret == 0);
		check("round trip length", got == m);
		check("round trip bytes", ret == 0 && out && memcmp(out, want, m) == 0);
		if (ret == 0)
			free(out);
		free(d);

		/* every strict prefix lost output, so each has to be refused */
		if (round < 8)
		{
			for (size_t cut = 0; cut < el; cut++)
				check("a truncated stream is refused", run(enc, cut, NULL) != 0);
		}
	}
}

static void case_roundtrip_10(void) { roundtrip(0x10); }
static void case_roundtrip_11(void) { roundtrip(0x11); }

static void case_short_input(void)
{
	/* 0 to 7 bytes, both flavours, every filler: none may read past the end */
	for (int flavour = 0x10; flavour <= 0x11; flavour++)
	{
		for (size_t len = 0; len <= 7; len++)
		{
			for (int fill = 0; fill < 3; fill++)
			{
				uint8_t b[8];
				memset(b, fill == 0 ? 0 : fill == 1 ? 0xFF : 0x10, sizeof b);
				b[0] = flavour;
				run(b, len, NULL);
			}
		}
	}
	/* under 4 bytes there is no header */
	for (size_t len = 0; len < 4; len++)
	{
		const uint8_t b[3] = {0x10, 0, 0};
		check("under 4 bytes is refused (0x10)", run(b, len, NULL) != 0);
		const uint8_t c[3] = {0x11, 0, 0};
		check("under 4 bytes is refused (0x11)", run(c, len, NULL) != 0);
	}
}

static void case_backref_before_start(void)
{
	/* the first item is a match: nothing is behind it */
	const uint8_t a[] = {0x10, 8, 0, 0, 0x80, 0x00, 0x00};
	check("0x10 match at the start is refused", run(a, sizeof a, NULL) != 0);
	const uint8_t b[] = {0x11, 8, 0, 0, 0x80, 0x10, 0x00};
	check("0x11 match at the start is refused", run(b, sizeof b, NULL) != 0);
	/* one literal, then a match 5 back */
	const uint8_t c[] = {0x10, 8, 0, 0, 0x40, 'x', 0x10, 0x04};
	check("0x10 match past the start is refused", run(c, sizeof c, NULL) != 0);
	const uint8_t e[] = {0x11, 8, 0, 0, 0x40, 'x', 0x60, 0x04};
	check("0x11 match past the start is refused", run(e, sizeof e, NULL) != 0);
}

static void case_output_overrun(void)
{
	/* declared size 4: a literal and a copy of 18 (0x10) or 16 (0x11) bytes */
	const uint8_t a[] = {0x10, 4, 0, 0, 0x40, 'x', 0xF0, 0x00};
	check("0x10 copy past the size is refused", run(a, sizeof a, NULL) != 0);
	const uint8_t b[] = {0x11, 4, 0, 0, 0x40, 'x', 0xF0, 0x00};
	check("0x11 copy past the size is refused", run(b, sizeof b, NULL) != 0);
	/* 0x11 four byte form: 0x10110 + 273 bytes into a size of 4 */
	const uint8_t c[] = {0x11, 4, 0, 0, 0x40, 'x', 0x1F, 0xFF, 0xF0, 0x00};
	check("0x11 4-byte copy past the size is refused", run(c, sizeof c, NULL) != 0);
}

static void case_huge_declared_size(void)
{
	/* ALIGN32 of these wraps to a tiny allocation */
	const uint8_t a[] = {0x11, 0, 0, 0, 0xF0, 0xFF, 0xFF, 0xFF, 0x00, 'a'};
	check("0x11 size 0xFFFFFFF0 is refused", run(a, sizeof a, NULL) != 0);
	const uint8_t b[] = {0x11, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 'a'};
	check("0x11 size 0xFFFFFFFF is refused", run(b, sizeof b, NULL) != 0);
	/* a few bytes cannot expand to megabytes */
	const uint8_t c[] = {0x10, 0xFF, 0xFF, 0xFF, 0x00, 'a'};
	check("0x10 size 16 MB from 2 bytes is refused", run(c, sizeof c, NULL) != 0);
	const uint8_t d[] = {0x11, 0xFF, 0xFF, 0xFF, 0x00, 'a'};
	check("0x11 size 16 MB from 2 bytes is refused", run(d, sizeof d, NULL) != 0);
}

static void case_match_needs_bytes_past_end(void)
{
	/* the flag says match, and the stream ends inside it */
	const uint8_t a[] = {0x10, 8, 0, 0, 0x40, 'x', 0x10};
	check("0x10 match cut after 1 byte is refused", run(a, sizeof a, NULL) != 0);
	const uint8_t b[] = {0x11, 8, 0, 0, 0x40, 'x', 0x05};
	check("0x11 3-byte match cut is refused", run(b, sizeof b, NULL) != 0);
	const uint8_t c[] = {0x11, 8, 0, 0, 0x40, 'x', 0x10, 0x00};
	check("0x11 4-byte match cut is refused", run(c, sizeof c, NULL) != 0);
	/* a flag byte with nothing after it */
	const uint8_t d[] = {0x10, 8, 0, 0, 0xFF};
	check("0x10 flag byte alone is refused", run(d, sizeof d, NULL) != 0);
	/* a literal flag byte with fewer literals than the size */
	const uint8_t e[] = {0x10, 8, 0, 0, 0x00, 'a', 'b'};
	check("0x10 short literals are refused", run(e, sizeof e, NULL) != 0);
	const uint8_t f[] = {0x11, 8, 0, 0, 0x00, 'a', 'b'};
	check("0x11 short literals are refused", run(f, sizeof f, NULL) != 0);
}

static void case_zero_size(void)
{
	/* an empty output is a valid stream; it must not break or read anything */
	const uint8_t a[] = {0x10, 0, 0, 0};
	const uint8_t b[] = {0x11, 0, 0, 0, 0, 0, 0, 0};
	run(a, sizeof a, NULL);
	run(b, sizeof b, NULL);
}

static void case_is_lz77(void)
{
	const uint8_t a[1] = {0x10}, b[1] = {0x11}, c[1] = {0x12};
	check("isLZ77compressed 0x10", isLZ77compressed(a) == 1);
	check("isLZ77compressed 0x11", isLZ77compressed(b) == 1);
	check("isLZ77compressed 0x12", isLZ77compressed(c) == 0);
}

typedef struct
{
	const char *name;
	void (*fn)(void);
} test_case;

static const test_case cases[] = {
	{"valid_10", case_valid_10},
	{"valid_11", case_valid_11},
	{"valid_11_extended_size", case_valid_11_extended_size},
	{"roundtrip_10", case_roundtrip_10},
	{"roundtrip_11", case_roundtrip_11},
	{"short_input", case_short_input},
	{"backref_before_start", case_backref_before_start},
	{"output_overrun", case_output_overrun},
	{"huge_declared_size", case_huge_declared_size},
	{"match_needs_bytes_past_end", case_match_needs_bytes_past_end},
	{"zero_size", case_zero_size},
	{"is_lz77", case_is_lz77},
};

int main(int argc, char **argv)
{
	const size_t count = sizeof cases / sizeof cases[0];
	for (size_t i = 0; i < count; i++)
	{
		if (argc > 1 && strcmp(argv[1], cases[i].name) != 0)
			continue;
		cases[i].fn();
	}
	if (failures)
	{
		printf("lz77_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("lz77_test: ok\n");
	return 0;
}
