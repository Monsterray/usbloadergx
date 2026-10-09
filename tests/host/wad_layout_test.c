/* wad_layout_test.c - WadLayout_Parse() and WadLayout_Content() on built and broken WADs.
 *
 * Runs on the host: source/wad/WadLayout.c has no libogc in it, so the code under
 * test is the exact code the Wii runs. The channel updater hands it a downloaded
 * WAD and installs to NAND what it reports, so every truncation and every bad
 * size has to be refused without reading outside the buffer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wad/WadLayout.c"

static int failures;

static void check(const char *what, bool cond)
{
	if (!cond)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = v >> 8; p[1] = v;
}

static void put64(uint8_t *p, uint64_t v)
{
	put32(p, (uint32_t)(v >> 32));
	put32(p + 4, (uint32_t)v);
}

#define ALIGN64(x) (((x) + 63) & ~63u)
#define TITLE 0x00010001554c4e52ULL
#define CERTS_LEN 0x100
#define TIK_LEN 0x2A4
#define BODY 0x140 /* RSA-2048 signature block */
#define TMD_LEN(n) (BODY + 0xA4 + (n) * 0x24)

/* A WAD with two contents of 100 and 0x30 bytes. Returns its size. */
static size_t build(uint8_t *wad, size_t cap)
{
	const uint32_t sizes[2] = {100, 0x30};
	const uint32_t dataLen = ALIGN64(sizes[0]) + 0x30; /* the last content unpadded */
	memset(wad, 0, cap);
	put32(wad, 0x20);
	put16(wad + 4, 0x4973);
	put32(wad + 0x08, CERTS_LEN);
	put32(wad + 0x0C, 0);
	put32(wad + 0x10, TIK_LEN);
	put32(wad + 0x14, TMD_LEN(2));
	put32(wad + 0x18, dataLen);
	put32(wad + 0x1C, 0);

	uint32_t off = 0x40 + ALIGN64(CERTS_LEN);
	uint8_t *tik = wad + off;
	put32(tik, 0x10001);
	put64(tik + BODY + 0x9C, TITLE);

	off += ALIGN64(TIK_LEN);
	uint8_t *tmd = wad + off;
	put32(tmd, 0x10001);
	put64(tmd + BODY + 0x4C, TITLE);
	put16(tmd + BODY + 0x9E, 2);
	for (int i = 0; i < 2; i++)
	{
		uint8_t *rec = tmd + BODY + 0xA4 + i * 0x24;
		put32(rec, 0x10 + i);           /* cid */
		put16(rec + 4, i);              /* index */
		put16(rec + 6, i ? 0x8001 : 1); /* type */
		put64(rec + 8, sizes[i]);
	}

	off += ALIGN64(TMD_LEN(2));
	for (uint32_t i = 0; i < dataLen; i++)
		wad[off + i] = (uint8_t)i;
	return off + dataLen;
}

static uint8_t wad[0x2000];

static bool parse_copy(const uint8_t *src, size_t size, WadLayout *layout)
{
	uint8_t *copy = malloc(size ? size : 1);
	memcpy(copy, src, size);
	bool ok = WadLayout_Parse(copy, size, layout);
	if (ok)
	{
		WadContent c;
		for (uint16_t i = 0; i < layout->numContents; i++)
			ok = ok && WadLayout_Content(copy, layout, i, &c);
	}
	free(copy);
	return ok;
}

/* Changes one field of a good WAD and expects the parse to fail. */
static void expect_broken(const char *what, size_t at, int width, uint64_t value)
{
	size_t size = build(wad, sizeof(wad));
	if (width == 2) put16(wad + at, (uint16_t)value);
	else if (width == 4) put32(wad + at, (uint32_t)value);
	else put64(wad + at, value);
	WadLayout layout;
	if (parse_copy(wad, size, &layout))
	{
		printf("FAIL: %s parsed\n", what);
		failures++;
	}
}

int main(void)
{
	size_t size = build(wad, sizeof(wad));
	WadLayout layout;
	check("good WAD parses", parse_copy(wad, size, &layout));
	check("title", layout.titleId == TITLE);
	check("two contents", layout.numContents == 2);
	check("certs", layout.certs.offset == 0x40 && layout.certs.size == CERTS_LEN);
	check("crl empty", layout.crl.size == 0);
	check("tik", layout.tik.offset == 0x40 + ALIGN64(CERTS_LEN) && layout.tik.size == TIK_LEN);
	uint32_t tmdOff = layout.tik.offset + ALIGN64(TIK_LEN);
	check("tmd", layout.tmd.offset == tmdOff && layout.tmd.size == TMD_LEN(2));
	uint32_t dataOff = tmdOff + ALIGN64(TMD_LEN(2));
	check("data", layout.data.offset == dataOff);

	WadContent c;
	check("content 0", WadLayout_Content(wad, &layout, 0, &c) && c.cid == 0x10 && c.index == 0 &&
		  c.type == 1 && c.size == 100 && c.offset == dataOff && c.length == 112);
	check("content 1", WadLayout_Content(wad, &layout, 1, &c) && c.cid == 0x11 && c.index == 1 &&
		  c.type == 0x8001 && c.size == 0x30 && c.offset == dataOff + 128 && c.length == 0x30);
	check("content 2 does not exist", !WadLayout_Content(wad, &layout, 2, &c));

	/* Every truncation fails, without reading past the end (ASan). */
	for (size_t cut = 0; cut < size; cut++)
	{
		if (parse_copy(wad, cut, &layout))
		{
			printf("FAIL: truncated to %zu of %zu bytes parsed\n", cut, size);
			failures++;
		}
	}

	uint32_t tikOff = 0x40 + ALIGN64(CERTS_LEN);
	expect_broken("header length", 0, 4, 0x40);
	expect_broken("boot2 WAD type", 4, 2, 0x6962);
	expect_broken("no certs", 0x08, 4, 0);
	expect_broken("huge certs", 0x08, 4, 0xFFFFFFC0);
	expect_broken("huge tik", 0x10, 4, 0xFFFFFFFF);
	expect_broken("tik too short for its title", 0x10, 4, BODY + 0x9C + 4);
	expect_broken("huge tmd", 0x14, 4, 0x7FFFFFFF);
	expect_broken("huge data", 0x18, 4, 0xFFFFFFFF);
	expect_broken("no data", 0x18, 4, 0);
	expect_broken("unknown tik signature", tikOff, 4, 0x10003);
	expect_broken("unknown tmd signature", tmdOff, 4, 0);
	expect_broken("tik for another title", tikOff + BODY + 0x9C, 8, 0x0000000100000002ULL);
	expect_broken("no contents", tmdOff + BODY + 0x9E, 2, 0);
	expect_broken("more contents than records", tmdOff + BODY + 0x9E, 2, 3);
	expect_broken("65535 contents", tmdOff + BODY + 0x9E, 2, 0xFFFF);
	expect_broken("content of 0 bytes", tmdOff + BODY + 0xA4 + 8, 8, 0);
	expect_broken("content past the data", tmdOff + BODY + 0xA4 + 0x24 + 8, 8, 0x31 + 15);
	expect_broken("content of 2^64-1 bytes", tmdOff + BODY + 0xA4 + 8, 8, ~0ULL);
	expect_broken("content of 2^32 bytes", tmdOff + BODY + 0xA4 + 8, 8, 0x100000000ULL);

	/* An ECC signature block is 0x80 bytes, not 0x140: the body moves. */
	{
		size = build(wad, sizeof(wad));
		put32(wad + tmdOff, 0x10002);
		check("ECC-signed TMD reads the wrong title", !parse_copy(wad, size, &layout));
	}

	if (failures)
		return 1;
	printf("wad_layout_test: all cases pass\n");
	return 0;
}
