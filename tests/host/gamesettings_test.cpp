/* gamesettings_test.cpp - the per-game settings file (GXGameSettings.cfg) and its parental level.
 *
 * Runs on the host: CGameSettings.cpp, with the stand-ins in tests/host/settings_shim/ (run.sh
 * adds that path for this test). A game's level indexes the menus' level names, so a value out
 * of range was an out-of-bounds read; it is the most restrictive level now. The legacy key
 * "parentalcontrol" came before the 17+ level, so its 4 (18+) has to become 18+ again.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
typedef struct GXRModeObjShim GXRModeObj; // disc.h names it; no test here uses it
#include "settings/CGameSettings.cpp"

static int failures;

static void check(const char *what, bool cond)
{
	if (!cond)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static char dir[] = "/tmp/gamesettings_test_XXXXXX";

// Loads a file holding one game line with this setting and returns that game's level
static int level_after(const char *setting)
{
	std::string path = std::string(dir) + "/GXGameSettings.cfg";
	FILE *f = fopen(path.c_str(), "w");
	if (!f)
		abort();
	fprintf(f, "# USB Loader GX R1283 - Individual game settings file\ngame:RM8E01 = %s\n", setting);
	fclose(f);

	CGameSettings g;
	if (!g.Load((std::string(dir) + "/").c_str()))
		return -100;
	return g.GetGameCFG("RM8E01")->parentalcontrol;
}

int main(void)
{
	if (!mkdtemp(dir))
		return 1;

	static const struct { const char *setting; int want; } cases[] = {
		{ "Locked:1;", PARENTAL_LVL_EVERYONE }, // the default
		{ "ParentalLevel:0;", PARENTAL_LVL_EVERYONE },
		{ "ParentalLevel:4;", PARENTAL_LVL_MATURE17 },
		{ "ParentalLevel:5;", PARENTAL_LVL_ADULT },
		{ "ParentalLevel:6;", PARENTAL_LVL_ADULT },
		{ "ParentalLevel:99;", PARENTAL_LVL_ADULT },
		{ "ParentalLevel:-1;", PARENTAL_LVL_ADULT },
		{ "ParentalLevel:abc;", PARENTAL_LVL_ADULT },
		{ "ParentalLevel:;", PARENTAL_LVL_ADULT },
		{ "parentalcontrol:3;", PARENTAL_LVL_MATURE },
		{ "parentalcontrol:4;", PARENTAL_LVL_ADULT },
		{ "parentalcontrol:5;", PARENTAL_LVL_ADULT },
		{ "parentalcontrol:2;", PARENTAL_LVL_TEEN },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		int got = level_after(cases[i].setting);
		char what[96];
		snprintf(what, sizeof(what), "%s gives level %d (got %d)", cases[i].setting, cases[i].want, got);
		check(what, got == cases[i].want);
	}

	// Save() writes the new key, so a migrated file reads back the same
	{
		CGameSettings g;
		level_after("parentalcontrol:4;");
		check("load the legacy file", g.Load((std::string(dir) + "/").c_str()));
		check("save it", g.Save());
		FILE *f = fopen((std::string(dir) + "/GXGameSettings.cfg").c_str(), "r");
		char buf[4096] = "";
		size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
		buf[n] = 0;
		if (f)
			fclose(f);
		check("the saved file has ParentalLevel:5", strstr(buf, "ParentalLevel:5;") != NULL);
		check("and no legacy key", strstr(buf, "parentalcontrol:") == NULL);
		CGameSettings h;
		check("reload", h.Load((std::string(dir) + "/").c_str()) && h.GetGameCFG("RM8E01")->parentalcontrol == PARENTAL_LVL_ADULT);
	}

	unlink((std::string(dir) + "/GXGameSettings.cfg").c_str());
	rmdir(dir);
	if (failures)
	{
		printf("gamesettings_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("gamesettings_test: ok\n");
	return 0;
}
