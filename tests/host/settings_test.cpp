/* settings_test.cpp - the main settings file parser (source/settings/CSettings.cpp)
 * against hostile and boundary values.
 *
 * Runs on the host: CSettings.cpp is compiled as it is, with the few libogc and GUI
 * names it touches stood in by tests/host/settings_shim/ (run.sh adds that path for
 * this test only). GXGlobal.cfg sits on the user's card and is edited by hand, so
 * every number in it is outside data. SetSetting() used to store atoi() of the line
 * into the field whatever it said: volume = 99999 wrapped a short, videomode = -1
 * indexed a table, NandEmuMode = 7 selected a mode that does not exist.
 *
 * The range table below is the spec. It is written by hand from the menu cycle
 * limits, not read back from the parser: a key whose range the parser forgot fails
 * here. Keys the parser deliberately leaves alone (godmode and the bit masks) are not
 * in it and are named in settings-parser notes in CSettings.cpp.
 *
 * For every key in the table:
 *  - the default lies in the range;
 *  - the key is one Save() writes, and SetSetting() knows it (returns true);
 *  - every integer from lo-2 to hi+2 (up to 1000 values) is stored when it is valid
 *    and otherwise leaves the default (or the key's own fail-safe value);
 *  - -1, 99999, "abc", "" and -99999 (floats: 0, -1, nan, inf, 1e30, 1e-30) do the same;
 *  - lo and hi are accepted unchanged (negative control: the clamp is not "reject all").
 * Then a save -> load -> save round trip, with every key at its maximum, must not
 * change a byte, and the password line must never write past unlockCode.
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <set>
#include <string>

// CSettings.cpp's quote-includes of these resolve from source/settings first: they
// are not needed, so mark them as already included.
#define _GAME_SETTINGS_H_
#define _GAME_STATISTICS_H_

extern "C" {
#include "utils/encrypt.c"
}
#include "settings/CSettings.cpp"

struct T : CSettings
{
	using CSettings::ParseLine;
	using CSettings::SetSetting;
};

static int failures;

static void check(const char *what, bool cond)
{
	if (!cond)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static bool put(T &t, const char *key, const char *val)
{
	char n[64], v[128];
	snprintf(n, sizeof(n), "%s", key);
	snprintf(v, sizeof(v), "%s", val);
	return t.SetSetting(n, v);
}

// ---------------------------------------------------------------------------------
// The spec
// ---------------------------------------------------------------------------------

struct Key
{
	const char *key;
	double (*get)(const CSettings &);
	double lo, hi;
	bool isFloat;
	bool (*valid)(long);      // when the range has holes; null: every value lo..hi
	bool hasFallback;         // an invalid value gives `fallback`, not the default
	double fallback;
};

#define KEY(key, field, lo, hi) \
	{ key, [](const CSettings &s) -> double { return s.field; }, (double)(lo), (double)(hi), false, nullptr, false, 0 }
#define FKEY(key, field, lo, hi) \
	{ key, [](const CSettings &s) -> double { return s.field; }, (double)(lo), (double)(hi), true, nullptr, false, 0 }
#define OO(name) KEY(#name, name, 0, 1)
#define EN(name, max) KEY(#name, name, 0, (max) - 1)
#define RG(name, lo, hi) KEY(#name, name, lo, hi)

static bool validVideoScale(long v) { return v == 0 || (v >= 40 && v <= 120); }
static bool validDisplayType(long v) { return v == 0 || v == 1 || v == 2 || v == 4 || v == 8 || v == 0x10; }

static const Key table[] = {
	// 0..1
	OO(videopatch), OO(patchFix480p), OO(sneekVideoPatch), OO(patchcountrystrings), OO(quickboot),
	OO(WiirdDebugger), OO(WiirdDebuggerPause), OO(UseChanLauncher), OO(wpadMotor), OO(wpadSpeaker),
	OO(AutobootDiscs), OO(DMLProgPatch), OO(DMLWidescreen), OO(MultiDiscPrompt), OO(DMLPADHOOK),
	OO(DMLNoDisc2), OO(DMLScreenshot), OO(DMLActivityLED), OO(DMLJPNPatch), OO(NINAutoboot),
	OO(NINPal50Patch), OO(NINWiiUWide), OO(NINRemlimit), OO(NINArcadeMode), OO(NINCCRumble),
	OO(NINSkipIPL), OO(NINBBA), OO(NINUSBHID), OO(NINNativeSI), OO(NINLED), OO(NINOSReport),
	OO(NINLog), OO(DEVOWidescreen), OO(DEVOActivityLED), OO(DEVOFZeroAX), OO(DEVOTimerFix),
	OO(DEVODButtons), OO(DEVOCropOverscan), OO(DEVODiscDelay), OO(tooltips), OO(wsprompt),
	OO(RememberLastGame), OO(marknewtitles), OO(ShowPlayCount), OO(ShowFreeSpace),
	OO(ShowGameCount), OO(SilentHomeMenu), OO(UseSystemFont), OO(ResampleTo48kHz),
	OO(CacheBNRFiles), OO(CacheTitles), OO(rumble), OO(autonetwork), OO(ProxyUseSystem),
	OO(PlaylogUpdate), OO(Wiinnertag), OO(MultiplePartitions), OO(GCInstallCompressed),
	OO(GCInstallAligned), OO(RememberUnlock), OO(CacheCheck),

	// 0..MAX-1 (the menu cycles)
	EN(videomode, VIDEO_MODE_MAX), EN(videoPatchDol, VIDEO_PATCH_DOL_MAX),
	EN(deflicker, DEFLICKER_MAX), KEY("videowidth", videoWidth, 0, WIDTH_MAX - 1),
	EN(language, MAX_LANGUAGE), EN(ocarina, OCARINA_MAX), EN(hddinfo, CLOCK_MAX),
	EN(sinfo, GAMEINFO_MAX), KEY("autoios", AutoIOS, 0, GAME_IOS_MAX - 1),
	EN(keyset, KEYBOARD_MAX), EN(xflip, XFLIP_MAX), EN(screensaver, SCREENSAVER_MAX),
	EN(wiilight, WIILIGHT_MAX), EN(discart, DISCARTS_MAX_CHOICE),
	EN(coversfull, COVERSFULL_MAX_CHOICE), EN(CoverAction, COVER_ACTION_MAX),
	EN(bannerFavIcon, BANNER_FAVICON_MAX_CHOICE), EN(HomeMenu, HOME_MENU_MAX_CHOICE),
	EN(InstallToDir, INSTALL_TO_MAX), EN(GameSplit, GAMESPLIT_MAX),
	EN(SearchMode, SEARCH_MAX_CHOICE), EN(GameAspectRatio, ASPECT_MAX),
	EN(ScreenMode, SCREEN_MAX), EN(GameWindowMode, GAMEWINDOW_MAX),
	EN(PrivateServer, PRIVSERV_MAX_CHOICE), EN(GameCubeMode, CG_MODE_MAX_CHOICE),
	EN(GameCubeSource, CG_SOURCE_MAX_CHOICE), EN(DMLVideo, DML_VIDEO_MAX_CHOICE),
	EN(NINBBAProfile, NIN_BBA_MAX_CHOICE), EN(NINMCEmulation, NIN_MC_MAX_CHOICE),
	EN(DEVOMCEmulation, DEVO_MC_MAX_CHOICE), EN(NandEmuMode, EMUNAND_MAX),
	EN(NandEmuChanMode, EMUNAND_MAX),

	// fixed ranges
	RG(TitlesType, 0, 2), RG(musicloopmode, 0, 3), RG(gamesound, 0, 2), RG(BlockIOSReload, 0, 2),
	RG(gameDisplay, 0, 3), RG(BannerAnimStart, 0, 1), RG(AutobootDiscsDelay, 0, 5),
	RG(Hooktype, 0, 7), RG(DMLNMM, 0, 2), RG(DMLDebug, 0, 2), RG(NINWiiUGamepadSlot, 0, 4),
	RG(NINMCSize, 0, 5), RG(NINSettings, 0, 2), RG(NINMaxPads, 0, 4),
	RG(volume, 0, 100), RG(sfxvolume, 0, 100), RG(gamesoundvolume, 0, 100),
	RG(LoaderIOS, 0, 255), RG(cios, 0, 255), RG(ProxyPort, 0, 65535), RG(USBPort, 0, 2),
	RG(SelectedGame, 0, 32767), RG(GameListOffset, 0, 32767),
	RG(AdjustOverscanX, -320, 320), RG(AdjustOverscanY, -320, 320),
	RG(TooltipDelay, 0, 30000), RG(BannerZoomDuration, 0, 600), RG(NINVideoOffset, -20, 20),

	// special
	{ "NINVideoScale", [](const CSettings &s) -> double { return s.NINVideoScale; }, 0, 120, false,
	  validVideoScale, false, 0 },
	{ "GameDisplayType", [](const CSettings &s) -> double { return s.GameDisplayType; }, 0, 0x10, false,
	  validDisplayType, false, 0 },
	// fail closed: a value nobody can read as a level is the most restrictive one
	{ "parentalcontrol", [](const CSettings &s) -> double { return s.parentalcontrol; }, 0, 4, false,
	  nullptr, true, PARENTAL_LVL_EVERYONE },

	// floats
	FKEY("WSFactor", WSFactor, 0.01, 4.0), FKEY("FontScaleFactor", FontScaleFactor, 0.01, 4.0),
	FKEY("ClockFontScaleFactor", ClockFontScaleFactor, 0.01, 4.0),
	FKEY("PointerSpeed", PointerSpeed, 0.01, 5.0), FKEY("BannerGridSpeed", BannerGridSpeed, 0.1, 200.0),
	FKEY("BannerProjectionOffsetX", BannerProjectionOffsetX, -1000.0, 1000.0),
	FKEY("BannerProjectionOffsetY", BannerProjectionOffsetY, -1000.0, 1000.0),
	FKEY("BannerProjectionWidth", BannerProjectionWidth, 100.0, 2000.0),
	FKEY("BannerProjectionHeight", BannerProjectionHeight, 100.0, 2000.0),
	FKEY("GCBannerScale", GCBannerScale, 0.1, 8.0),
};
static const size_t tableSize = sizeof(table) / sizeof(table[0]);

static bool near(double a, double b)
{
	return fabs(a - b) <= 1e-6 * fabs(b) + 1e-37;
}

static bool inRange(const Key &k, double v)
{
	if (!(v >= k.lo && v <= k.hi)) // also false for NaN
		return false;
	if (k.valid && !k.valid((long)v))
		return false;
	return true;
}

static std::set<std::string> failedKeys;

static void failKey(const Key &k, const char *how, const char *input, double got, double want)
{
	// one line per key: the first input that went wrong
	if (failedKeys.insert(k.key).second)
		printf("FAIL: %s: %s '%s' gave %g, wanted %g\n", k.key, how, input, got, want);
}

static double expectInvalid(const Key &k, const T &dflt)
{
	return k.hasFallback ? k.fallback : k.get(dflt);
}

static void checkKey(const Key &k)
{
	T dflt;
	double d = k.get(dflt);
	char buf[64];

	// the default is a value the parser itself must accept
	if (!inRange(k, d))
		failKey(k, "default out of range:", "", d, d);

	// the key is one Save() writes and SetSetting() knows
	{
		T t;
		snprintf(buf, sizeof(buf), "%g", k.lo);
		if (!put(t, k.key, buf))
			failKey(k, "SetSetting() returned false for", buf, 0, 1);
	}

	// negative control: the ends of the range are accepted unchanged
	for (int end = 0; end < 2; end++)
	{
		T t;
		double v = end ? k.hi : k.lo;
		if (k.isFloat)
			snprintf(buf, sizeof(buf), "%g", v);
		else
			snprintf(buf, sizeof(buf), "%ld", (long)v);
		put(t, k.key, buf);
		if (!near(k.get(t), v))
			failKey(k, "rejected a valid value", buf, k.get(t), v);
	}

	if (!k.isFloat)
	{
		// every integer around the range
		long from = (long)k.lo - 2, to = (long)k.hi + 2;
		if (to - from <= 1000)
		{
			for (long v = from; v <= to; v++)
			{
				T t;
				snprintf(buf, sizeof(buf), "%ld", v);
				put(t, k.key, buf);
				double want = inRange(k, (double)v) ? (double)v : expectInvalid(k, dflt);
				if (k.get(t) != want)
					failKey(k, "stored", buf, k.get(t), want);
			}
		}
		// the hostile ones
		static const char *const bad[] = { "-1", "99999", "abc", "", "-99999", "4294967297", "2147483648" };
		for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		{
			T t;
			put(t, k.key, bad[i]);
			char *end = nullptr;
			errno = 0;
			long n = strtol(bad[i], &end, 10);
			bool numeric = bad[i][0] && *end == 0 && errno == 0;
			double want = numeric && inRange(k, (double)n) ? (double)n : expectInvalid(k, dflt);
			if (k.get(t) != want)
				failKey(k, "stored", bad[i], k.get(t), want);
		}
	}
	else
	{
		static const char *const bad[] = { "0", "-1", "nan", "inf", "-inf", "1e30", "1e-30", "abc", "" };
		for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		{
			T t;
			put(t, k.key, bad[i]);
			char *end = nullptr;
			double n = strtod(bad[i], &end);
			bool numeric = bad[i][0] && *end == 0;
			double want = numeric && inRange(k, n) ? n : expectInvalid(k, dflt);
			double got = k.get(t);
			if (!near(got, want) && !(isnan(got) && isnan(want)))
				failKey(k, "stored", bad[i], got, want);
		}
	}
}

// ---------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------

static std::string slurp(const char *path)
{
	std::string s;
	FILE *f = fopen(path, "rb");
	if (!f)
		return s;
	char b[4096];
	size_t n;
	while ((n = fread(b, 1, sizeof(b), f)) > 0)
		s.append(b, n);
	fclose(f);
	return s;
}

// The keys of a saved file: every "key = value" line.
static std::set<std::string> savedKeys(const std::string &text)
{
	std::set<std::string> keys;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t eol = text.find('\n', pos);
		if (eol == std::string::npos)
			eol = text.size();
		std::string line = text.substr(pos, eol - pos);
		pos = eol + 1;
		size_t eq = line.find(" = ");
		if (line.empty() || line[0] == '#' || eq == std::string::npos)
			continue;
		keys.insert(line.substr(0, eq));
	}
	return keys;
}

static const char *CFG = "d1:/GXGlobal.cfg";

static void setup(T &t)
{
	snprintf(t.ConfigPath, sizeof(t.ConfigPath), "d1:/");
	t.SetDefault();
}

static void testRoundTrip(bool atMaximum)
{
	const char *what = atMaximum ? "round trip, every ranged key at its maximum" : "round trip of the defaults";
	char msg[200];

	T a;
	setup(a);
	if (atMaximum)
	{
		for (size_t i = 0; i < tableSize; i++)
		{
			char v[64];
			if (table[i].isFloat)
				snprintf(v, sizeof(v), "%g", table[i].hi);
			else
				snprintf(v, sizeof(v), "%ld", (long)table[i].hi);
			put(a, table[i].key, v);
		}
	}
	snprintf(msg, sizeof(msg), "%s: Save()", what);
	check(msg, a.Save());
	std::string first = slurp(CFG);

	std::set<std::string> keys = savedKeys(first);
	snprintf(msg, sizeof(msg), "%s: Save() wrote the keys (%zu)", what, keys.size());
	check(msg, keys.size() > 150);
	if (!atMaximum)
	{
		for (size_t i = 0; i < tableSize; i++)
		{
			if (!keys.count(table[i].key))
			{
				snprintf(msg, sizeof(msg), "table key %s is not one Save() writes", table[i].key);
				check(msg, false);
			}
		}
	}

	T b;
	setup(b);
	snprintf(msg, sizeof(msg), "%s: Load()", what);
	check(msg, b.Load());
	snprintf(msg, sizeof(msg), "%s: Save() again", what);
	check(msg, b.Save());
	std::string second = slurp(CFG);
	snprintf(msg, sizeof(msg), "%s: the file does not change", what);
	check(msg, first == second);
	if (first != second)
	{
		// name the first line that differs
		size_t i = 0;
		while (i < first.size() && i < second.size() && first[i] == second[i])
			i++;
		printf("  first difference at byte %zu: '%.40s' versus '%.40s'\n", i, first.c_str() + i, second.c_str() + i);
	}
}

static void testHostileFile()
{
	// A file with the kind of values a hand edit, a bad card or a malicious download holds.
	FILE *f = fopen(CFG, "w");
	check("hostile file: written", f != nullptr);
	if (!f)
		return;
	fprintf(f, "# USB Loader GX R1283 - Main settings file\n");
	fprintf(f, "volume = 99999\nvideomode = -1\nNandEmuMode = 7\nTooltipDelay = -5\n");
	fprintf(f, "WSFactor = nan\nBannerGridSpeed = 1e30\nparentalcontrol = 99\nCoverAction = abc\n");
	fclose(f);

	T t, dflt;
	setup(t);
	setup(dflt);
	check("hostile file: Load()", t.Load());
	check("hostile file: volume", t.volume == dflt.volume);
	check("hostile file: videomode", t.videomode == dflt.videomode);
	check("hostile file: NandEmuMode", t.NandEmuMode == dflt.NandEmuMode);
	check("hostile file: TooltipDelay", t.TooltipDelay == dflt.TooltipDelay);
	check("hostile file: WSFactor", t.WSFactor == dflt.WSFactor);
	check("hostile file: BannerGridSpeed", t.BannerGridSpeed == dflt.BannerGridSpeed);
	check("hostile file: parentalcontrol fails closed", t.parentalcontrol == PARENTAL_LVL_EVERYONE);
	check("hostile file: CoverAction", t.CoverAction == dflt.CoverAction);
}

static void testPassword()
{
	T t;
	setup(t);
	t.RememberUnlock = ON;

	// 0..19 characters round trip through Save() and Load()
	const char *codes[] = { "", "1", "0123", "abcdefghijklmnopqrs" /* 19 */ };
	for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
	{
		snprintf(t.unlockCode, sizeof(t.unlockCode), "%s", codes[i]);
		t.Save();
		T u;
		setup(u);
		u.Load();
		check("password: round trip", strcmp(u.unlockCode, codes[i]) == 0);
	}

	// a long value must not reach db_language or returnTo, which follow unlockCode
	T u;
	setup(u);
	memset(u.db_language, 'D', sizeof(u.db_language) - 1);
	u.db_language[sizeof(u.db_language) - 1] = 0;
	memset(u.returnTo, 'R', sizeof(u.returnTo) - 1);
	u.returnTo[sizeof(u.returnTo) - 1] = 0;
	char longhex[64];
	memset(longhex, 'a', sizeof(longhex) - 1);
	longhex[sizeof(longhex) - 1] = 0;
	put(u, "password", longhex);
	check("password: a long value is cut to unlockCode", strlen(u.unlockCode) < sizeof(u.unlockCode));
	check("password: db_language is intact", strspn(u.db_language, "D") == sizeof(u.db_language) - 1);
	check("password: returnTo is intact", strspn(u.returnTo, "R") == sizeof(u.returnTo) - 1);
}

int main()
{
	char dir[] = "/tmp/gxsettingsXXXXXX";
	if (!mkdtemp(dir) || chdir(dir) != 0 || mkdir("d1:", 0700) != 0)
	{
		printf("FAIL: cannot make a work directory\n");
		return 1;
	}

	for (size_t i = 0; i < tableSize; i++)
		checkKey(table[i]);

	// no key twice in the table
	std::set<std::string> seen;
	for (size_t i = 0; i < tableSize; i++)
		check("table key appears once", seen.insert(table[i].key).second);

	testRoundTrip(false);
	testRoundTrip(true);
	testHostileFile();
	testPassword();

	unlink(CFG);
	rmdir("d1:");
	if (chdir("/") == 0)
		rmdir(dir);

	if (!failedKeys.empty())
	{
		printf("FAIL: %zu of %zu table keys fail\n", failedKeys.size(), tableSize);
		failures += (int)failedKeys.size();
	}
	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("OK: settings_test (%zu keys)\n", tableSize);
	return 0;
}
