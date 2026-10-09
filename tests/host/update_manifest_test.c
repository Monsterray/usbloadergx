/* update_manifest_test.c - the updater's update.txt parser against hostile input.
 *
 * Runs on the host: source/network/UpdateManifest.c has no libogc in it, so the
 * code under test is the exact code the Wii runs. update.txt arrives with no
 * certificate check, and the parser reads it before the signature is checked.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network/UpdateManifest.c"

static int failures;

#define SHA_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define SHA_B "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define SIG "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff" \
            "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
#define HEAD "usbloadergx-update 1\nversion 5.3.0\n"
#define GOOD HEAD "file boot.dol 4718592 " SHA_A "\nfile meta.xml 812 " SHA_B "\n"

static void check(const char *what, bool cond)
{
	if (!cond)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

// Parses text from an exactly sized heap copy, so ASan sees any read past the end.
static bool parse(const char *text, UpdateManifest *m)
{
	size_t len = strlen(text);
	char *copy = malloc(len ? len : 1);
	memcpy(copy, text, len);
	bool ok = UpdateManifest_Parse(copy, len, m);
	free(copy);
	return ok;
}

static void expect(const char *text, bool want)
{
	UpdateManifest m;
	if (parse(text, &m) != want)
	{
		printf("FAIL: parse(\"%s\") = %s\n", text, want ? "false" : "true");
		failures++;
	}
}

static void expect_version(const char *text, bool want, uint32_t a, uint32_t b, uint32_t c)
{
	UpdateVersion v = {0, 0, 0};
	bool got = UpdateVersion_Parse(text, &v);
	if (got != want || (want && (v.major != a || v.minor != b || v.patch != c)))
	{
		printf("FAIL: UpdateVersion_Parse(\"%s\")\n", text);
		failures++;
	}
}

int main(void)
{
	UpdateManifest m;

	/* A good manifest, with and without the final newline. */
	check("good manifest parses", parse(GOOD "signature " SIG "\n", &m));
	check("version", m.version.major == 5 && m.version.minor == 3 && m.version.patch == 0);
	check("two files", m.fileCount == 2);
	check("signed length stops before the signature line", m.signedLength == strlen(GOOD));
	check("signature bytes", m.signature[0] == 0x00 && m.signature[1] == 0x11 && m.signature[63] == 0xff);
	const UpdateFile *dol = UpdateManifest_Find(&m, "boot.dol");
	check("boot.dol found", dol && dol->size == 4718592 && dol->sha256[0] == 0xaa && dol->sha256[31] == 0xaa);
	const UpdateFile *meta = UpdateManifest_Find(&m, "meta.xml");
	check("meta.xml found", meta && meta->size == 812 && meta->sha256[0] == 0x01 && meta->sha256[31] == 0xef);
	check("absent file", UpdateManifest_Find(&m, "usbloadergx.wad") == NULL);
	expect(GOOD "signature " SIG, true);
	expect(HEAD "signature " SIG "\n", true); /* no files is valid; the caller wants one */

	/* Framing. */
	expect("", false);
	expect("usbloadergx-update 2\nversion 5.3.0\nsignature " SIG "\n", false);
	expect("usbloadergx-update 1 \nversion 5.3.0\nsignature " SIG "\n", false);
	expect("version 5.3.0\nsignature " SIG "\n", false);
	expect("usbloadergx-update 1\r\nversion 5.3.0\r\nsignature " SIG "\r\n", false);
	expect("usbloadergx-update 1\nsignature " SIG "\n", false); /* no version */
	expect(HEAD "version 5.3.1\nsignature " SIG "\n", false); /* two versions */
	expect(HEAD "nonsense here\nsignature " SIG "\n", false);
	expect(HEAD "file boot.dol 1 " SHA_A "\n", false); /* no signature */
	expect(HEAD "signature " SIG "\nfile boot.dol 1 " SHA_A "\n", false); /* after it */
	expect(HEAD "signature " SIG "\n\n", false);
	expect(HEAD "\nsignature " SIG "\n", false);
	{
		/* A NUL inside, which would end the strings the GUI makes from it. */
		static const char nul[] = "usbloadergx-update 1\nversion 5.3.0\0\nsignature " SIG "\n";
		check("NUL rejected", !UpdateManifest_Parse(nul, sizeof(nul) - 1, &m));
	}

	/* Versions. */
	expect("usbloadergx-update 1\nversion 05.3.0\nsignature " SIG "\n", false);
	expect("usbloadergx-update 1\nversion 5.3\nsignature " SIG "\n", false);
	expect("usbloadergx-update 1\nversion 5.3.0.1\nsignature " SIG "\n", false);
	expect("usbloadergx-update 1\nversion 5.3.0+1\nsignature " SIG "\n", false);
	expect("usbloadergx-update 1\nversion 1000000.0.0\nsignature " SIG "\n", false);
	expect("usbloadergx-update 1\nversion 999999.0.10\nsignature " SIG "\n", true);
	expect("usbloadergx-update 1\nversion -1.0.0\nsignature " SIG "\n", false);
	expect("usbloadergx-update 1\nversion  5.3.0\nsignature " SIG "\n", false);

	/* File lines. */
	expect(HEAD "file ../boot.dol 1 " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file a/b 1 " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file .. 1 " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file .hidden 1 " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file sd:boot.dol 1 " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file 0123456789abcdef0123456789abcdef 1 " SHA_A "\nsignature " SIG "\n", true);
	expect(HEAD "file 0123456789abcdef0123456789abcdefX 1 " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file boot.dol 0 " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file boot.dol 01 " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file boot.dol 67108864 " SHA_A "\nsignature " SIG "\n", true);
	expect(HEAD "file boot.dol 67108865 " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file boot.dol 999999999 " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file boot.dol 1 " SHA_A "a\nsignature " SIG "\n", false);
	expect(HEAD "file boot.dol 1 aaaa\nsignature " SIG "\n", false);
	expect(HEAD "file boot.dol 1 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\nsignature " SIG "\n", false);
	expect(HEAD "file boot.dol 1  " SHA_A "\nsignature " SIG "\n", false);
	expect(HEAD "file boot.dol 1 " SHA_A "\nfile boot.dol 2 " SHA_A "\nsignature " SIG "\n", false);
	{
		char text[2048];
		int n = snprintf(text, sizeof(text), "%s", HEAD);
		for (int i = 0; i < UPDATE_MANIFEST_MAX_FILES; i++)
			n += snprintf(text + n, sizeof(text) - n, "file f%d 1 %s\n", i, SHA_A);
		snprintf(text + n, sizeof(text) - n, "signature %s\n", SIG);
		expect(text, true);
		n = (int)(strstr(text, "signature") - text);
		n += snprintf(text + n, sizeof(text) - n, "file f9 1 %s\n", SHA_A);
		snprintf(text + n, sizeof(text) - n, "signature %s\n", SIG);
		expect(text, false); /* one file too many */
	}

	/* Signature. */
	expect(HEAD "signature " SHA_A "\n", false);
	expect(HEAD "signature " SIG "0\n", false);
	expect(HEAD "signature " SIG " \n", false);

	/* Too big, and every truncation of a good one fails cleanly. */
	{
		static char big[UPDATE_MANIFEST_MAX_SIZE + 2];
		memset(big, 'a', sizeof(big) - 1);
		check("oversized rejected", !UpdateManifest_Parse(big, UPDATE_MANIFEST_MAX_SIZE + 1, &m));
		const char *good = GOOD "signature " SIG "\n";
		size_t len = strlen(good);
		for (size_t cut = 0; cut + 1 < len; cut++)
		{
			char *copy = malloc(cut ? cut : 1);
			memcpy(copy, good, cut);
			if (UpdateManifest_Parse(copy, cut, &m))
			{
				printf("FAIL: truncated to %zu bytes parsed\n", cut);
				failures++;
			}
			free(copy);
		}
	}

	/* LOADER_VERSION as makexml.sh writes it. */
	expect_version("5.2.0", true, 5, 2, 0);
	expect_version("5.2.0+4.g1a2b3c4", true, 5, 2, 0);
	expect_version("5.2.0+4.g1a2b3c4.dirty", true, 5, 2, 0);
	expect_version("0.0.0+g1a2b3c4", true, 0, 0, 0);
	expect_version("10.20.30", true, 10, 20, 30);
	expect_version("5.2.0-rc1", false, 0, 0, 0);
	expect_version("v5.2.0", false, 0, 0, 0);
	expect_version("5.2", false, 0, 0, 0);
	expect_version("", false, 0, 0, 0);

	UpdateVersion a = {5, 2, 0}, b = {5, 2, 1}, c = {5, 10, 0}, d = {6, 0, 0};
	check("5.2.0 < 5.2.1", UpdateVersion_Compare(&a, &b) < 0);
	check("5.2.1 > 5.2.0", UpdateVersion_Compare(&b, &a) > 0);
	check("5.10.0 > 5.2.1", UpdateVersion_Compare(&c, &b) > 0);
	check("6.0.0 > 5.10.0", UpdateVersion_Compare(&d, &c) > 0);
	check("equal", UpdateVersion_Compare(&a, &a) == 0);

	uint8_t out[2];
	check("hex", UpdateHex_Decode("0aff", 4, out) && out[0] == 0x0a && out[1] == 0xff);
	check("hex uppercase", !UpdateHex_Decode("0AFF", 4, out));
	check("hex odd", !UpdateHex_Decode("0af", 3, out));
	check("hex junk", !UpdateHex_Decode("0g", 2, out));

	if (failures)
		return 1;
	printf("update_manifest_test: all cases pass\n");
	return 0;
}
