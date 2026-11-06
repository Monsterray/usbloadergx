/* encrypt_test.c - EncryptString()/DecryptString() on the host.
 *
 * The "password" line of GXGlobal.cfg sits on the card and anyone can edit it.
 * CSettings hands its value to DecryptString() with unlockCode[20] as the target,
 * so a long value used to write past the field into db_language and returnTo.
 * EncryptString() also overflowed its own scratch buffer on a byte >= 0x80.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils/encrypt.c"

static int failures;

static void fail(const char *what, const char *detail)
{
	printf("FAIL: %s (%s)\n", what, detail);
	failures++;
}

/* Encrypt into 50 bytes (EncryptedTxt) and decrypt into 20 (unlockCode), as CSettings does. */
static void round_trip(const char *code)
{
	char enc[50], dec[20];
	EncryptString(code, enc, sizeof(enc));
	if (strlen(enc) != 2 * strlen(code))
		fail("encrypted length", code);
	DecryptString(enc, dec, sizeof(dec));
	if (strcmp(code, dec) != 0)
		fail("round trip", code);
}

static void decrypts_to(const char *src, size_t dstsize, const char *want)
{
	char *buf = malloc(dstsize);
	DecryptString(src, buf, dstsize);
	if (strcmp(buf, want) != 0)
		fail("DecryptString", src);
	free(buf);
}

int main(void)
{
	char code[24];
	char enc[50];
	size_t len, i;

	/* 40+ hex characters into a 20-byte heap destination: ASan sees any byte past it,
	 * and 19 characters plus the terminator is all that may arrive. */
	{
		char *buf = malloc(20);
		char longhex[80];
		memset(longhex, 'a', 78);
		longhex[78] = 0;
		DecryptString(longhex, buf, 20);
		if (strlen(buf) != 19)
			fail("long value not cut at dstsize - 1", longhex);
		free(buf);
	}

	/* 0..19 characters: every valid unlockCode, including all-high-bit ones. */
	for (len = 0; len <= 19; len++)
	{
		for (i = 0; i < len; i++) code[i] = (char) ('1' + (i % 4));
		code[len] = 0;
		round_trip(code);

		for (i = 0; i < len; i++) code[i] = (char) (0x80 + i * 7 + 1);
		code[len] = 0;
		round_trip(code);
	}

	/* Upper-case hex decodes like lower case. */
	EncryptString("1234", enc, sizeof(enc));
	for (i = 0; enc[i]; i++)
		if (enc[i] >= 'a' && enc[i] <= 'f') enc[i] = (char) (enc[i] - 'a' + 'A');
	decrypts_to(enc, 20, "1234");

	/* A pair that is not two hex digits ends the string; so does an odd tail. */
	EncryptString("1234", enc, sizeof(enc));
	{
		char bad[50];
		strcpy(bad, enc);
		bad[4] = 'g';          /* third character's pair is "g?" */
		decrypts_to(bad, 20, "12");
		strcpy(bad, enc);
		bad[strlen(bad) - 1] = 0;   /* odd length: last digit has no partner */
		decrypts_to(bad, 20, "123");
	}
	decrypts_to("", 20, "");
	decrypts_to("zz", 20, "");
	decrypts_to("-1", 20, "");

	/* Tiny destinations: size 1 holds the terminator only, size 0 is not touched. */
	{
		char c = 0x55;
		DecryptString("aabb", &c, 0);
		if (c != 0x55) fail("dstsize 0 wrote", "");
		EncryptString("abc", &c, 0);
		if (c != 0x55) fail("encrypt dstsize 0 wrote", "");
		decrypts_to("aabb", 1, "");
		decrypts_to("aabb", 2, "\xff");   /* 0xaa ^ 'U' (0x55) */
	}

	/* EncryptString into a short buffer keeps whole pairs and the terminator. */
	{
		char *buf = malloc(6);
		EncryptString("12345678", buf, 6);
		if (strlen(buf) != 4)
			fail("encrypt not cut at a whole pair", buf);
		free(buf);
	}

	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("encrypt_test: ok\n");
	return 0;
}
