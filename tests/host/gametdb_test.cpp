/* gametdb_test.cpp - GameTDB opening a wiitdb.xml: the version read and the game cache.
 *
 * host-needs: pugixml (tests/host/run.sh fetches the pinned source of deps/build.sh)
 *
 * Runs on the host: source/xml/GameTDB.cpp needs only the C and C++ libraries and
 * pugixml, so the code under test is the exact code the Wii runs. wiitdb.xml is
 * downloaded with no certificate check, so its first kilobyte, its nodes and the
 * offsets cache beside it are all outside data.
 *
 * Each case writes its own wiitdb.xml into a fresh directory (GameTDB drops
 * wiitdb_offsets.bin beside the file). With a case name as argument only that
 * case runs, to see each failure alone.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include "xml/GameTDB.cpp"

static int failures;

static void check(const char *what, bool cond)
{
	if (!cond)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static std::string dir;

static std::string write_xml(const std::string &content)
{
	std::string path = dir + "/wiitdb.xml";
	unlink((dir + "/wiitdb_offsets.bin").c_str());
	FILE *f = fopen(path.c_str(), "wb");
	fwrite(content.data(), 1, content.size(), f);
	fclose(f);
	return path;
}

static const char *HEADER = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<datafile>\n";

static std::string game(const char *id, const char *title)
{
	return std::string("<game name=\"") + id + "\" region=\"NTSC-U\">\n<id>" + id +
		"</id>\n<locale lang=\"EN\"><title>" + title + "</title></locale>\n</game>\n";
}

static std::string valid_db(void)
{
	return std::string(HEADER) + "<WiiTDB version=\"20240101123456\" games=\"2\"/>\n" +
		game("RSPE01", "Alpha") + game("RSPP01", "Beta") + "</datafile>\n";
}

/* Filler of exactly n bytes that holds neither "<WiiTDB" nor "/>". */
static std::string filler(size_t n)
{
	return std::string(n, 'x');
}

static void case_valid(void)
{
	GameTDB db(write_xml(valid_db()).c_str());
	check("valid: version", db.GetGameTDBVersion() == 20240101123456ULL);
	check("valid: two games", db.GetEntryCount() == 2);
	std::string title;
	check("valid: GetTitle", db.GetTitle("RSPP01", title) && title == "Beta");
}

static void case_offsets_cache_reused(void)
{
	std::string path = write_xml(valid_db());
	{
		GameTDB db(path.c_str());
		check("cache: first open", db.GetEntryCount() == 2);
	}
	/* second open reads wiitdb_offsets.bin: same version, same entries */
	GameTDB db(path.c_str());
	check("cache: second open", db.GetEntryCount() == 2);
	std::string title;
	check("cache: GetTitle", db.GetTitle("RSPE01", title) && title == "Alpha");
}

static void case_no_wiitdb_node_in_first_kb(void)
{
	/* "<WiiTDB" is past the first 1024 bytes: versionNode stayed NULL for pugixml */
	std::string xml = std::string(HEADER) + "<!--" + filler(1100) + "-->\n" +
		"<WiiTDB version=\"5\" games=\"1\"/>\n" + game("RSPE01", "Alpha") + "</datafile>\n";
	GameTDB db(write_xml(xml).c_str());
	check("no node in the first KB: version 0", db.GetGameTDBVersion() == 0);
}

static void case_no_wiitdb_node_full_kb_of_text(void)
{
	/* exactly 1024 bytes with no terminator and no node: strstr ran off the buffer */
	GameTDB db(write_xml(filler(1024)).c_str());
	check("1 KB of text: version 0", db.GetGameTDBVersion() == 0);
}

static void case_small_file_without_node(void)
{
	GameTDB db(write_xml("<datafile>").c_str());
	check("tiny file: version 0", db.GetGameTDBVersion() == 0);
	GameTDB empty(write_xml("").c_str());
	check("empty file: version 0", empty.GetGameTDBVersion() == 0);
}

static void case_small_file_with_node(void)
{
	GameTDB db(write_xml("<WiiTDB version=\"42\" games=\"0\"/>").c_str());
	check("tiny file with the node: version", db.GetGameTDBVersion() == 42);
}

static void case_node_ends_at_last_byte(void)
{
	/* "/>" is the last two bytes of the 1024 read: the terminator goes one past them */
	const std::string node = "<WiiTDB version=\"9\"/>";
	std::string xml = filler(1024 - node.size()) + node + filler(100);
	GameTDB db(write_xml(xml).c_str());
	check("node ends at byte 1024: version", db.GetGameTDBVersion() == 9);
}

static void case_node_never_closes(void)
{
	/* the opening tag has no "/>" inside the kilobyte */
	std::string xml = "<WiiTDB version=\"7\" " + filler(2000) + ">";
	GameTDB db(write_xml(xml).c_str());
	db.GetGameTDBVersion(); /* any answer, no crash */
}

static void case_failed_parse_keeps_cache_honest(void)
{
	/* B's node is not well formed. After GetTitle(B) fails, GetTitle(A) has to read A
	 * again: the cache must not still claim the document holds A. */
	std::string xml = std::string(HEADER) + "<WiiTDB version=\"1\" games=\"2\"/>\n" +
		game("RSPE01", "Alpha") +
		"<game name=\"RSPB01\">\n<id>RSPB01</id>\n<locale lang=\"EN\"><title>Bad</locale>\n</game>\n" +
		"</datafile>\n";
	GameTDB db(write_xml(xml).c_str());
	check("two games found", db.GetEntryCount() == 2);
	std::string title;
	check("A reads", db.GetTitle("RSPE01", title) && title == "Alpha");
	std::string bad;
	db.GetTitle("RSPB01", bad); /* parse fails, whatever it answers */
	std::string again;
	check("A reads again after B failed", db.GetTitle("RSPE01", again) && again == "Alpha");
}

static void case_rating_age(void)
{
	/* Every board and value in the 2026-10 wiitdb.xml, plus USK and ACB/OFLC */
	static const struct { const char *board, *value; int age; } r[] = {
		{"CERO", "A", 0}, {"CERO", "B", 12}, {"CERO", "C", 15}, {"CERO", "D", 17}, {"CERO", "Z", 18},
		{"ESRB", "EC", 3}, {"ESRB", "E", 6}, {"ESRB", "E10+", 10}, {"ESRB", "T", 13},
		{"ESRB", "M", 17}, {"ESRB", "AO", 18}, {"ESRB", "RP", -1}, {"ESRB", "12", 12},
		{"PEGI", "3", 3}, {"PEGI", "6", 6}, {"PEGI", "7", 7}, {"PEGI", "12", 12},
		{"PEGI", "15", 15}, {"PEGI", "16", 16}, {"PEGI", "18", 18},
		{"USK", "0", 0}, {"USK", "6", 6}, {"USK", "16", 16},
		{"GRB", "ALL", 0}, {"GRB", "12", 12}, {"GRB", "15", 15}, {"GRB", "18", 18}, {"CSRR", "ALL", 0},
		{"ACB", "G", 0}, {"ACB", "PG", 8}, {"ACB", "M", 15}, {"ACB", "MA15+", 15}, {"ACB", "R18+", 18},
		{"OFLC", "M", 15}, {"OFLC", "R18+", 18},
		{"", "3", 3}, {"", "", -1}, {"CERO", "", -1}, {"XYZ", "Q", -1}, {"PEGI", "99", -1},
		{"PEGI", "-3", -1}, {"PEGI", "12x", -1}, {"esrb", "m", 17},
	};
	for (size_t i = 0; i < sizeof r / sizeof r[0]; i++)
	{
		char what[64];
		snprintf(what, sizeof what, "rating age %s %s", r[i].board, r[i].value);
		check(what, GameTDB::RatingAge(r[i].board, r[i].value) == r[i].age);
	}
	check("rating age: no board", GameTDB::RatingAge(NULL, "3") == -1);
	check("rating age: no value", GameTDB::RatingAge("PEGI", NULL) == -1);
}

static void case_rating_from_node(void)
{
	std::string xml = std::string(HEADER) + "<WiiTDB version=\"1\" games=\"3\"/>\n" +
		"<game name=\"RM8E01\">\n<id>RM8E01</id>\n<rating type=\"ESRB\" value=\"M\">\n</rating>\n</game>\n" +
		"<game name=\"RM8K01\">\n<id>RM8K01</id>\n<rating type=\"GRB\" value=\"ALL\"/>\n</game>\n" +
		game("RSPE01", "No rating") + "</datafile>\n";
	GameTDB db(write_xml(xml).c_str());
	check("rating node: ESRB M", db.GetRatingAge("RM8E01") == 17);
	check("rating node: GRB ALL", db.GetRatingAge("RM8K01") == 0);
	check("rating node: none", db.GetRatingAge("RSPE01") == -1);
	check("rating node: unknown game", db.GetRatingAge("XXXX01") == -1);
	check("rating node: no id", db.GetRatingAge(NULL) == -1);
}

typedef struct
{
	const char *name;
	void (*fn)(void);
} test_case;

static const test_case cases[] = {
	{"valid", case_valid},
	{"offsets_cache_reused", case_offsets_cache_reused},
	{"no_wiitdb_node_in_first_kb", case_no_wiitdb_node_in_first_kb},
	{"no_wiitdb_node_full_kb_of_text", case_no_wiitdb_node_full_kb_of_text},
	{"small_file_without_node", case_small_file_without_node},
	{"small_file_with_node", case_small_file_with_node},
	{"node_ends_at_last_byte", case_node_ends_at_last_byte},
	{"node_never_closes", case_node_never_closes},
	{"failed_parse_keeps_cache_honest", case_failed_parse_keeps_cache_honest},
	{"rating_age", case_rating_age},
	{"rating_from_node", case_rating_from_node},
};

int main(int argc, char **argv)
{
	char tmpl[] = "/tmp/gametdb_test_XXXXXX";
	if (!mkdtemp(tmpl))
	{
		printf("FAIL: no temporary directory\n");
		return 1;
	}
	dir = tmpl;

	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
	{
		if (argc > 1 && strcmp(argv[1], cases[i].name) != 0)
			continue;
		cases[i].fn();
	}

	unlink((dir + "/wiitdb.xml").c_str());
	unlink((dir + "/wiitdb_offsets.bin").c_str());
	rmdir(dir.c_str());

	if (failures)
	{
		printf("gametdb_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("gametdb_test: ok\n");
	return 0;
}
