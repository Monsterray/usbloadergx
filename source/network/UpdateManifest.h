/****************************************************************************
 * UpdateManifest.h
 *
 * The update.txt each release of the fork carries. It names the version and
 * the size and SHA-256 of every file the updater may install, and ends with an
 * Ed25519 signature over everything before the signature line:
 *
 *   usbloadergx-update 1
 *   version 5.3.0
 *   file boot.dol 4718592 <64 lowercase hex digits>
 *   file meta.xml 812 <...>
 *   signature <128 lowercase hex digits>
 *
 * GX downloads it with no certificate check, so the parser takes it as hostile:
 * nothing here trusts a length or a number before checking it. The signature
 * itself is checked by the caller (update.cpp), with wolfSSL.
 *
 * No libogc in here, so tests/host/update_manifest_test.c runs this exact code.
 ***************************************************************************/
#ifndef UPDATE_MANIFEST_H
#define UPDATE_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_MANIFEST_MAX_SIZE  4096
#define UPDATE_MANIFEST_MAX_FILES 8
#define UPDATE_FILE_NAME_MAX      32
#define UPDATE_FILE_MAX_SIZE      (64u * 1024 * 1024)

typedef struct
{
	uint32_t major, minor, patch;
} UpdateVersion;

typedef struct
{
	char name[UPDATE_FILE_NAME_MAX + 1];
	uint32_t size;
	uint8_t sha256[32];
} UpdateFile;

typedef struct
{
	UpdateVersion version;
	int fileCount;
	UpdateFile files[UPDATE_MANIFEST_MAX_FILES];
	size_t signedLength; // bytes from the start of the file that the signature covers
	uint8_t signature[64];
} UpdateManifest;

// Fills out from the size bytes at data. False for anything that is not exactly
// the format above; out is then undefined.
bool UpdateManifest_Parse(const char *data, size_t size, UpdateManifest *out);

// The entry for a file name, or NULL when the release does not carry it.
const UpdateFile *UpdateManifest_Find(const UpdateManifest *manifest, const char *name);

// "MAJOR.MINOR.PATCH", optionally followed by "+build" metadata (LOADER_VERSION
// looks like 5.2.0+4.g1a2b3c4), which semver leaves out of comparisons.
bool UpdateVersion_Parse(const char *text, UpdateVersion *out);

// <0, 0 or >0 as a is older than, the same as or newer than b.
int UpdateVersion_Compare(const UpdateVersion *a, const UpdateVersion *b);

// Lowercase hex only; false for any other character. out gets hexLen / 2 bytes.
bool UpdateHex_Decode(const char *hex, size_t hexLen, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif
