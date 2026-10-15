/* cheat_txt_test.c - the downloaded cheat file check against hostile input.
 *
 * Runs on the host: source/network/CheatTxt.c has no libogc in it, so the code
 * under test is the exact code the Wii runs. Cheat files come from
 * codes.rc24.xyz with no certificate check, and a file that passes is written
 * over the one on the card.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network/CheatTxt.c"

static int failures;

static void check(const char *what, bool cond)
{
	if (!cond)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

// Checks size bytes from an exactly sized heap copy, so ASan sees any read past the end.
static bool valid(const char *data, size_t size, const char *id)
{
	char *copy = malloc(size ? size : 1);
	if (!copy)
		abort();
	memcpy(copy, data, size);
	bool ok = CheatTxt_IsValid(copy, size, id);
	free(copy);
	return ok;
}

static bool valids(const char *text, const char *id)
{
	return valid(text, strlen(text), id);
}

int main(void)
{
	// What codes.rc24.xyz sends
	check("Mario Kart Wii", valids("RMCE01\nMario Kart Wii\n\nEverlasting Items v2 [Anarion]\nC26FE200 00000005\n", "RMCE01"));
	check("CRLF line ends", valids("GALE01\r\nSuper Smash Bros. Melee\r\n", "GALE01"));
	check("UTF-8 BOM", valids("\xEF\xBB\xBFRMCP01\nMario Kart Wii\n", "RMCP01"));
	check("another maker code", valids("RMCE99\nMario Kart Wii\n", "RMCE01"));
	check("another region", !valids("RMCP01\nMario Kart Wii\n", "RMCE01"));
	check("a four character channel ID", valids("HACA\nMii Channel\n", "HACA"));
	check("only the ID", valids("RMCE01", "RMCE01"));
	check("trailing blanks on the ID line", valids("RMCE01  \nMario Kart Wii\n", "RMCE01"));

	// Not the file that was asked for
	check("another game", !valids("GALE01\nSuper Smash Bros. Melee\n", "RMCE01"));
	check("empty", !valid("", 0, "RMCE01"));
	check("a short ID", !valids("RMC\nMario\n", "RMCE01"));
	check("an HTML error page", !valids("<html><head><title>404 Not Found</title></head></html>", "RMCE01"));
	check("a Cloudflare page", !valids("<!DOCTYPE html>\n<title>Just a moment...</title>", "RMCE01"));
	check("text after the ID", !valids("RMCE01 Mario Kart Wii\n", "RMCE01"));
	check("a seven character ID", !valids("RMCE01X\n", "RMCE01"));
	check("a NUL byte", !valid("RMCE01\nMario\0Kart\n", 18, "RMCE01"));
	check("the BOM alone", !valid("\xEF\xBB\xBF", 3, "RMCE01"));
	check("a truncated BOM", !valids("\xEF\xBBRMCE01\n", "RMCE01"));

	// Bounds
	size_t big = CHEAT_TXT_MAX_SIZE + 1;
	char *large = malloc(big);
	if (!large)
		abort();
	memset(large, 'A', big);
	memcpy(large, "RMCE01\n", 7);
	check("one byte over the limit", !CheatTxt_IsValid(large, big, "RMCE01"));
	check("exactly the limit", CheatTxt_IsValid(large, CHEAT_TXT_MAX_SIZE, "RMCE01"));
	free(large);
	check("no data", !CheatTxt_IsValid(NULL, 10, "RMCE01"));

	// The ID goes into a URL and a file name
	check("ID6", CheatTxt_IsSafeId("RMCE01"));
	check("ID4", CheatTxt_IsSafeId("HACA"));
	check("no ID", !CheatTxt_IsSafeId(NULL));
	check("an empty ID", !CheatTxt_IsSafeId(""));
	check("ID3", !CheatTxt_IsSafeId("RMC"));
	check("ID7", !CheatTxt_IsSafeId("RMCE01X"));
	check("a path in the ID", !CheatTxt_IsSafeId("../a"));
	check("a query in the ID", !CheatTxt_IsSafeId("RM&x=1"));
	check("a space in the ID", !CheatTxt_IsSafeId("RMC E1"));
	check("a high byte in the ID", !CheatTxt_IsSafeId("RMC\xC3\xA9"));
	check("an unsafe ID makes any file invalid", !valids("../a\n", "../a"));

	if (failures)
		return 1;
	printf("cheat_txt_test: all checks pass\n");
	return 0;
}
