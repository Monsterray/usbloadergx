/****************************************************************************
 * UpdateManifest.c
 *
 * Parser for the release's update.txt; the format is in UpdateManifest.h.
 * Strict on purpose: the signature covers the exact bytes, so there is no
 * reason to accept a second spelling of anything.
 ***************************************************************************/
#include <string.h>

#include "UpdateManifest.h"

#define MANIFEST_MAGIC "usbloadergx-update 1"

static int HexValue(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

bool UpdateHex_Decode(const char *hex, size_t hexLen, uint8_t *out)
{
	if (!hex || !out || hexLen % 2 != 0)
		return false;

	for (size_t i = 0; i < hexLen; i += 2)
	{
		int hi = HexValue(hex[i]);
		int lo = HexValue(hex[i + 1]);
		if (hi < 0 || lo < 0)
			return false;
		out[i / 2] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

// A decimal number of at most maxDigits digits and no leading zero, read from
// [*p, end). Moves *p past it.
static bool ParseNumber(const char **p, const char *end, int maxDigits, uint32_t maxValue, uint32_t *out)
{
	const char *s = *p;
	uint32_t value = 0;
	int digits = 0;

	while (s < end && *s >= '0' && *s <= '9')
	{
		if (++digits > maxDigits)
			return false;
		value = value * 10 + (uint32_t)(*s - '0');
		s++;
	}
	if (digits == 0 || (digits > 1 && **p == '0') || value > maxValue)
		return false;

	*p = s;
	*out = value;
	return true;
}

// MAJOR.MINOR.PATCH in [*p, end). Moves *p past it.
static bool ParseVersion(const char **p, const char *end, UpdateVersion *out)
{
	const char *s = *p;
	if (!ParseNumber(&s, end, 6, 999999, &out->major) || s >= end || *s++ != '.' ||
		!ParseNumber(&s, end, 6, 999999, &out->minor) || s >= end || *s++ != '.' ||
		!ParseNumber(&s, end, 6, 999999, &out->patch))
		return false;
	*p = s;
	return true;
}

bool UpdateVersion_Parse(const char *text, UpdateVersion *out)
{
	if (!text || !out)
		return false;

	const char *end = text + strlen(text);
	const char *p = text;
	if (!ParseVersion(&p, end, out))
		return false;
	return p == end || *p == '+';
}

int UpdateVersion_Compare(const UpdateVersion *a, const UpdateVersion *b)
{
	if (a->major != b->major)
		return a->major < b->major ? -1 : 1;
	if (a->minor != b->minor)
		return a->minor < b->minor ? -1 : 1;
	if (a->patch != b->patch)
		return a->patch < b->patch ? -1 : 1;
	return 0;
}

static bool IsNameChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		   c == '.' || c == '_' || c == '-';
}

// Does [line, end) start with word followed by one space? Moves line past both.
static bool Keyword(const char **line, const char *end, const char *word)
{
	size_t len = strlen(word);
	if ((size_t)(end - *line) <= len || memcmp(*line, word, len) != 0 || (*line)[len] != ' ')
		return false;
	*line += len + 1;
	return true;
}

// file <name> <size> <sha256>
static bool ParseFileLine(const char *p, const char *end, UpdateManifest *out)
{
	if (out->fileCount >= UPDATE_MANIFEST_MAX_FILES)
		return false;

	UpdateFile *file = &out->files[out->fileCount];
	const char *name = p;
	while (p < end && IsNameChar(*p))
		p++;
	size_t nameLen = (size_t)(p - name);
	// No hidden files and nothing that could climb out of a directory
	if (nameLen == 0 || nameLen > UPDATE_FILE_NAME_MAX || name[0] == '.' || p >= end || *p++ != ' ')
		return false;
	memcpy(file->name, name, nameLen);
	file->name[nameLen] = '\0';

	if (UpdateManifest_Find(out, file->name))
		return false;

	if (!ParseNumber(&p, end, 8, UPDATE_FILE_MAX_SIZE, &file->size) || file->size == 0 ||
		p >= end || *p++ != ' ')
		return false;

	if (end - p != 64 || !UpdateHex_Decode(p, 64, file->sha256))
		return false;

	out->fileCount++;
	return true;
}

bool UpdateManifest_Parse(const char *data, size_t size, UpdateManifest *out)
{
	if (!data || !out || size == 0 || size > UPDATE_MANIFEST_MAX_SIZE)
		return false;
	// Text only: a NUL would end the strings the GUI makes from it
	if (memchr(data, '\0', size) || memchr(data, '\r', size))
		return false;

	memset(out, 0, sizeof(*out));
	const char *end = data + size;
	const char *line = data;
	bool haveVersion = false;
	int lineNumber = 0;

	while (line < end)
	{
		const char *newline = (const char *)memchr(line, '\n', (size_t)(end - line));
		const char *lineEnd = newline ? newline : end;
		const char *next = newline ? newline + 1 : end;
		const char *p = line;

		if (lineNumber++ == 0)
		{
			if ((size_t)(lineEnd - line) != strlen(MANIFEST_MAGIC) ||
				memcmp(line, MANIFEST_MAGIC, strlen(MANIFEST_MAGIC)) != 0)
				return false;
		}
		else if (Keyword(&p, lineEnd, "version"))
		{
			if (haveVersion || !ParseVersion(&p, lineEnd, &out->version) || p != lineEnd)
				return false;
			haveVersion = true;
		}
		else if (Keyword(&p, lineEnd, "file"))
		{
			if (!ParseFileLine(p, lineEnd, out))
				return false;
		}
		else if (Keyword(&p, lineEnd, "signature"))
		{
			// The last line; the signature covers every byte before it
			if (lineEnd - p != 128 || !UpdateHex_Decode(p, 128, out->signature))
				return false;
			if (next != end || !haveVersion)
				return false;
			out->signedLength = (size_t)(line - data);
			return true;
		}
		else
			return false;

		// Every line but the signature ends with a newline
		if (!newline)
			return false;
		line = next;
	}
	return false;
}

const UpdateFile *UpdateManifest_Find(const UpdateManifest *manifest, const char *name)
{
	if (!manifest || !name)
		return NULL;
	for (int i = 0; i < manifest->fileCount; i++)
	{
		if (strcmp(manifest->files[i].name, name) == 0)
			return &manifest->files[i];
	}
	return NULL;
}
