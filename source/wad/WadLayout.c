/****************************************************************************
 * WadLayout.c
 *
 * WAD layout: a 0x20-byte header, then the certificate chain, CRL, ticket,
 * TMD, content data and footer, each starting on a 64-byte boundary. Every
 * number in it is big-endian.
 ***************************************************************************/
#include <string.h>

#include "WadLayout.h"

#define WAD_HEADER_SIZE 0x20
#define WAD_TYPE_INSTALLABLE 0x4973 // "Is"

// Offsets inside the body of a signed ticket and TMD
#define TIK_TITLE_ID     0x9C
#define TMD_TITLE_ID     0x4C
#define TMD_NUM_CONTENTS 0x9E
#define TMD_CONTENTS     0xA4
#define TMD_CONTENT_SIZE 0x24

static uint16_t ReadBE16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t ReadBE32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t ReadBE64(const uint8_t *p)
{
	return ((uint64_t)ReadBE32(p) << 32) | ReadBE32(p + 4);
}

static uint64_t RoundUp(uint64_t value, uint64_t align)
{
	return (value + align - 1) & ~(align - 1);
}

// Offset of the body of a signed blob (ticket or TMD) of size bytes, after the
// signature type, the signature and the padding to 64 bytes. 0 if it does not fit.
static uint32_t SignedBody(const uint8_t *blob, uint32_t size)
{
	if (size < 4)
		return 0;

	uint32_t sigLen;
	switch (ReadBE32(blob))
	{
		case 0x10000: sigLen = 0x200; break; // RSA-4096
		case 0x10001: sigLen = 0x100; break; // RSA-2048
		case 0x10002: sigLen = 0x3C; break;  // ECC
		default: return 0;
	}
	uint32_t body = (uint32_t)RoundUp(4 + sigLen, 0x40);
	return body < size ? body : 0;
}

// The next section of len bytes at *offset; moves *offset to the 64-byte
// boundary after it.
static bool NextSection(uint64_t *offset, uint32_t len, size_t size, WadSection *out)
{
	if (*offset + len > size)
		return false;
	out->offset = (uint32_t)*offset;
	out->size = len;
	*offset = RoundUp(*offset + len, 0x40);
	return true;
}

// Walks contents 0..last, so every content before the one asked for is checked too.
static bool WalkContents(const uint8_t *wad, const WadLayout *layout, uint16_t last, WadContent *out)
{
	const uint8_t *tmd = wad + layout->tmd.offset;
	const uint8_t *records = tmd + SignedBody(tmd, layout->tmd.size) + TMD_CONTENTS;
	uint64_t pos = layout->data.offset;
	uint64_t dataEnd = (uint64_t)layout->data.offset + layout->data.size;

	for (uint32_t i = 0; i <= last; i++)
	{
		const uint8_t *record = records + i * TMD_CONTENT_SIZE;
		uint64_t size = ReadBE64(record + 8);
		// AES-CBC works on 16-byte blocks; the content is padded to 64 bytes in the WAD
		uint64_t length = RoundUp(size, 16);
		if (size == 0 || size > layout->data.size || pos + length > dataEnd)
			return false;

		if (i == last)
		{
			out->cid = ReadBE32(record);
			out->index = ReadBE16(record + 4);
			out->type = ReadBE16(record + 6);
			out->size = size;
			out->offset = (uint32_t)pos;
			out->length = (uint32_t)length;
		}
		pos += RoundUp(size, 0x40);
	}
	return true;
}

bool WadLayout_Parse(const uint8_t *wad, size_t size, WadLayout *out)
{
	if (!wad || !out || size < WAD_HEADER_SIZE || size > 0xFFFFFFFFu)
		return false;

	memset(out, 0, sizeof(*out));
	if (ReadBE32(wad) != WAD_HEADER_SIZE || ReadBE16(wad + 4) != WAD_TYPE_INSTALLABLE)
		return false;

	uint32_t certsLen = ReadBE32(wad + 0x08);
	uint32_t crlLen = ReadBE32(wad + 0x0C);
	uint32_t tikLen = ReadBE32(wad + 0x10);
	uint32_t tmdLen = ReadBE32(wad + 0x14);
	uint32_t dataLen = ReadBE32(wad + 0x18);
	if (certsLen == 0 || tikLen == 0 || tmdLen == 0 || dataLen == 0)
		return false;

	uint64_t offset = RoundUp(WAD_HEADER_SIZE, 0x40);
	if (!NextSection(&offset, certsLen, size, &out->certs) ||
		!NextSection(&offset, crlLen, size, &out->crl) ||
		!NextSection(&offset, tikLen, size, &out->tik) ||
		!NextSection(&offset, tmdLen, size, &out->tmd) ||
		!NextSection(&offset, dataLen, size, &out->data))
		return false;

	const uint8_t *tik = wad + out->tik.offset;
	uint32_t tikBody = SignedBody(tik, out->tik.size);
	if (tikBody == 0 || (uint64_t)tikBody + TIK_TITLE_ID + 8 > out->tik.size)
		return false;

	const uint8_t *tmd = wad + out->tmd.offset;
	uint32_t tmdBody = SignedBody(tmd, out->tmd.size);
	if (tmdBody == 0 || (uint64_t)tmdBody + TMD_CONTENTS > out->tmd.size)
		return false;

	out->numContents = ReadBE16(tmd + tmdBody + TMD_NUM_CONTENTS);
	if (out->numContents == 0 || out->numContents > WAD_MAX_CONTENTS ||
		(uint64_t)tmdBody + TMD_CONTENTS + (uint64_t)out->numContents * TMD_CONTENT_SIZE > out->tmd.size)
		return false;

	out->titleId = ReadBE64(tmd + tmdBody + TMD_TITLE_ID);
	if (ReadBE64(tik + tikBody + TIK_TITLE_ID) != out->titleId)
		return false;

	WadContent last;
	return WalkContents(wad, out, (uint16_t)(out->numContents - 1), &last);
}

bool WadLayout_Content(const uint8_t *wad, const WadLayout *layout, uint16_t i, WadContent *out)
{
	if (!wad || !layout || !out || i >= layout->numContents)
		return false;
	return WalkContents(wad, layout, i, out);
}
