/* theme_test.c - getThemeInt / getThemeIntRange on a hostile .them file.
 *
 * Runs on the host: themes/gettheme.c needs only libc once CSettings.h is stubbed out, so
 * the code under test is the exact code the Wii runs. A theme is a downloaded file, so
 * a value in it must never size an allocation unchecked: gui_gamelist.cpp allocates
 * `new GuiButton*[pagesize]` from "9 - game list browser page size", and GuiText does
 * `new wchar_t[maxWidth]` from the settings option width keys.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gctypes.h>
#include "themes/gettheme.h"

#define _CSETTINGS_H_ /* the real header pulls in libogc; only this one function is used */
u8 GetLayoutVersion(void);
#include "themes/gettheme.c"
u8 GetLayoutVersion(void) { return 1; }

static int failures;

static void check_int(const char *what, int got, int want)
{
	if (got != want)
	{
		printf("FAIL: %s: got %d, want %d\n", what, got, want);
		failures++;
	}
}

#define KEY "9 - game list browser page size"

static bool load_value(const char *value)
{
	char path[] = "/tmp/theme_test_XXXXXX";
	int fd = mkstemp(path);
	if (fd < 0)
		return false;
	FILE *f = fdopen(fd, "w");
	if (!f)
		return false;
	fprintf(f, "# test theme\nmsgid \"" KEY "\"\nmsgstr \"%s\"\n", value);
	fclose(f);
	ThemeCleanUp();
	bool ok = LoadTheme(path);
	unlink(path);
	return ok;
}

int main(void)
{
	const char *hostile[] = {"-1", "0", "100000", "abc", "-2147483648", "2147483647",
							 "99999999999999999999", "31", "-0", "  7x"};
	char what[96];

	/* no theme loaded: the key's leading number is the default */
	check_int("default, no theme", getThemeIntRange(KEY, 1, 30), 9);

	for (size_t i = 0; i < sizeof(hostile) / sizeof(hostile[0]); i++)
	{
		if (!load_value(hostile[i]))
		{
			printf("FAIL: cannot load a theme with value %s\n", hostile[i]);
			failures++;
			continue;
		}
		snprintf(what, sizeof(what), "range 1..30 with msgstr '%s'", hostile[i]);
		int v = getThemeIntRange(KEY, 1, 30);
		int want = 9;
		if (strcmp(hostile[i], "  7x") == 0)
			want = 7; /* atoi reads 7: a legal value stays */
		check_int(what, v, want);
	}

	/* values inside the range are kept, and both ends are inclusive */
	{
		/* a one-character msgstr is never stored by LoadTheme(), so the values have 2 digits */
		const char *ok[] = {"01", "05", "30"};
		int want[] = {1, 5, 30};
		for (int i = 0; i < 3; i++)
		{
			load_value(ok[i]);
			snprintf(what, sizeof(what), "range 1..30 with msgstr '%s'", ok[i]);
			check_int(what, getThemeIntRange(KEY, 1, 30), want[i]);
		}
	}

	/* a default outside the range is clamped */
	ThemeCleanUp();
	check_int("default above max", getThemeIntRange("100 - some width", 1, 30), 30);
	check_int("default below min", getThemeIntRange("0 - some width", 1, 30), 1);
	load_value("abc");
	check_int("bad value, default above max", getThemeIntRange("100 - other key", 1, 30), 30);

	/* getThemeInt itself keeps handing out whatever the file says (positions can be negative) */
	load_value("-1");
	check_int("getThemeInt unchanged", getThemeInt(KEY), -1);

	ThemeCleanUp();
	if (failures)
		return 1;
	printf("theme_test: ok\n");
	return 0;
}
