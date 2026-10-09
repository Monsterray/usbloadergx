/****************************************************************************
 * WadLayout.h
 *
 * Where the certificates, ticket, TMD and contents sit inside a WAD held in
 * memory, with every size and offset checked against the buffer. The updater
 * installs a WAD only after its signed SHA-256 matched, but the layout is still
 * read as if it were hostile: a packaging mistake must fail here, not inside
 * ES with the channel half written.
 *
 * No libogc in here, so tests/host/wad_layout_test.c runs this exact code.
 ***************************************************************************/
#ifndef WAD_LAYOUT_H
#define WAD_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAD_MAX_CONTENTS 512

typedef struct
{
	uint32_t offset; // from the start of the WAD
	uint32_t size;
} WadSection;

typedef struct
{
	WadSection certs, crl, tik, tmd, data;
	uint64_t titleId;      // from the TMD; the ticket's matches it
	uint16_t numContents;
} WadLayout;

typedef struct
{
	uint32_t cid;
	uint16_t index;
	uint16_t type;
	uint64_t size;         // decrypted size, as the TMD records it
	uint32_t offset;       // of the encrypted data, from the start of the WAD
	uint32_t length;       // bytes of encrypted data to hand to ES
} WadContent;

// Fills out from the size bytes at wad. False for a WAD whose sections or
// contents do not fit in the buffer.
bool WadLayout_Parse(const uint8_t *wad, size_t size, WadLayout *out);

// Content i (0 <= i < numContents) in TMD order. Contents are stored one after
// another, each padded to 64 bytes.
bool WadLayout_Content(const uint8_t *wad, const WadLayout *layout, uint16_t i, WadContent *out);

#ifdef __cplusplus
}
#endif

#endif
