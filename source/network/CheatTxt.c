/****************************************************************************
 * CheatTxt: checks on a downloaded geckocodes .txt cheat file.
 * No libogc in here: tests/host/cheat_txt_test.c runs this exact code.
 ***************************************************************************/
#include <string.h>

#include "CheatTxt.h"

static bool IsIdChar(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool CheatTxt_IsSafeId(const char *id)
{
	if (!id)
		return false;
	size_t len = 0;
	while (len < 7 && id[len] != '\0')
	{
		if (!IsIdChar(id[len]))
			return false;
		len++;
	}
	return len >= 4 && len <= 6;
}

bool CheatTxt_IsValid(const char *data, size_t size, const char *id)
{
	if (!data || !CheatTxt_IsSafeId(id) || size > CHEAT_TXT_MAX_SIZE)
		return false;

	// GX reads the file as C strings
	if (memchr(data, '\0', size) != NULL)
		return false;

	size_t pos = 0;
	if (size >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
		pos = 3;

	// The first line is the game ID, as geckocodes.org wrote it
	size_t idLen = 0;
	while (pos + idLen < size && IsIdChar(data[pos + idLen]))
		idLen++;
	if (idLen < 4 || idLen > 6 || memcmp(&data[pos], id, 4) != 0)
		return false;

	// and nothing else: the end of the line or of the file follows
	size_t end = pos + idLen;
	while (end < size && (data[end] == ' ' || data[end] == '\t'))
		end++;
	return end == size || data[end] == '\r' || data[end] == '\n';
}
