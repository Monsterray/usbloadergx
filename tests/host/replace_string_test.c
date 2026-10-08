/* replace_string_test.c - replaceString() never writes past the buffer it is given.
 *
 * Runs on the host: source/utils/StringTools.c is compiled as the Wii compiles it,
 * with tests/host/shim/gctypes.h standing in for libogc's types.
 *
 * Wiinnertag::Send() expands {ID6} and {KEY} in a URL from the user's
 * Wiinnertag.xml, and the key comes from the same file. Each case puts the buffer
 * in the middle of a guard pattern; a write past the end shows up as a changed
 * guard byte, which is how the old in-place expansion overflowed.
 */
#include <stdio.h>
#include <string.h>
#include "utils/StringTools.c"

#define GUARD 0xA5
#define PAD 32

static int failures;

static void check(const char *name, size_t size, const char *input,
                  const char *replace, const char *replacement, const char *want)
{
	unsigned char mem[PAD + 256 + PAD];
	char *buf = (char *)mem + PAD;
	size_t i;

	memset(mem, GUARD, sizeof(mem));
	memset(buf, 0, size);
	strncpy(buf, input, size - 1);

	replaceString(buf, size, replace, replacement);

	for (i = 0; i < PAD; i++)
	{
		if (mem[i] != GUARD || mem[PAD + size + i] != GUARD)
		{
			printf("FAIL %s: wrote outside the %zu byte buffer\n", name, size);
			failures++;
			return;
		}
	}
	if (memchr(buf, 0, size) == NULL)
	{
		printf("FAIL %s: no terminator inside the buffer\n", name);
		failures++;
		return;
	}
	if (want && strcmp(buf, want) != 0)
	{
		printf("FAIL %s: got \"%s\", want \"%s\"\n", name, buf, want);
		failures++;
	}
}

int main(void)
{
	check("one replacement", 64, "http://x/?id={ID6}", "{ID6}", "RMCE01", "http://x/?id=RMCE01");
	check("two replacements", 64, "{ID6}-{ID6}", "{ID6}", "AB", "AB-AB");
	check("case insensitive", 64, "{id6}", "{ID6}", "RMCE01", "RMCE01");
	check("nothing to replace", 64, "plain", "{KEY}", "secret", "plain");
	check("replacement at the end", 64, "k={KEY}", "{KEY}", "v", "k=v");
	check("shrinks", 64, "a{KEY}b", "{KEY}", "", "ab");

	/* The cases that used to write past the end. */
	check("grows to exactly fit", 9, "{KEY}", "{KEY}", "12345678", "12345678");
	check("grows one past the end", 8, "{KEY}", "{KEY}", "12345678", NULL);
	check("long key, short buffer", 16, "u={KEY}&x", "{KEY}",
	      "a-key-much-longer-than-the-buffer-it-goes-into", NULL);
	check("many expansions", 32, "{K}{K}{K}{K}{K}{K}{K}{K}", "{K}", "0123456789", NULL);
	check("one byte buffer", 1, "", "{K}", "x", "");

	if (failures)
		return 1;
	printf("replace_string_test: all cases pass\n");
	return 0;
}
