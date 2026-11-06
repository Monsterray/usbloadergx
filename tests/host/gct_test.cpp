/* gct_test.cpp - the cheat file parser (source/cheats/gct.cpp) against hostile input.
 *
 * Runs on the host: gct.cpp has no libogc in it, so the code under test is the exact
 * code the Wii runs. The loader reads a TXT cheat file and, when the cheat menu opens,
 * the .gct that went with the game, both from a card the user fills; IsCheatIncluded()
 * once subtracted the footer size from the file size without checking that the file was
 * that long, so a .gct of 0..7 bytes made the loop read far past the block.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "cheats/gct.cpp"

static int failures;

static void check(const char *what, bool cond)
{
	if (!cond)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

// Writes data to a new file and returns its name (a static buffer, one file at a time).
static const char *tmpfile_with(const void *data, size_t size)
{
	static char path[] = "/tmp/gct_test_XXXXXX";
	static bool made;
	if (made)
		unlink(path);
	strcpy(path, "/tmp/gct_test_XXXXXX");
	int fd = mkstemp(path);
	if (fd < 0)
		abort();
	made = true;
	if (size && write(fd, data, size) != (ssize_t)size)
		abort();
	close(fd);
	return path;
}

static const char *tmpfile_text(const char *text)
{
	return tmpfile_with(text, strlen(text));
}

// Asks IsCheatIncluded about an exactly sized heap copy, so ASan sees any read past its end.
static bool included(GCTCheats &g, int nr, const std::vector<unsigned char> &buf)
{
	unsigned char *copy = (unsigned char *)malloc(buf.size() ? buf.size() : 1);
	if (!copy)
		abort();
	if (!buf.empty())
		memcpy(copy, &buf[0], buf.size());
	bool r = g.IsCheatIncluded(nr, copy, (unsigned int)buf.size());
	free(copy);
	return r;
}

static void append_header(std::vector<unsigned char> &v)
{
	v.insert(v.end(), (const unsigned char *)GCT_Header, (const unsigned char *)GCT_Header + sizeof(GCT_Header));
}

static void append_footer(std::vector<unsigned char> &v)
{
	v.insert(v.end(), (const unsigned char *)GCT_Footer, (const unsigned char *)GCT_Footer + sizeof(GCT_Footer));
}

static void append_cheat(GCTCheats &g, int nr, std::vector<unsigned char> &v)
{
	std::vector<unsigned int> c = g.getCheat(nr);
	const unsigned char *p = (const unsigned char *)&c[0];
	v.insert(v.end(), p, p + c.size() * sizeof(unsigned int));
}

static const char TXT[] =
	"GALE01\n"
	"Test Game\n"
	"\n"
	"Cheat One\n"
	"12345678 9ABCDEF0 a comment after the code\n"
	"\n"
	"Cheat Two\n"
	"a comment line\n"
	"0A000000 00000001\n"
	"0A000004 00000002\n"
	"\n"
	"No Codes\n"
	"12345678 9ABCDEF\n"   // 16 characters: a truncated code is a comment
	"\n";

int main(void)
{
	// The TXT parser
	GCTCheats g;
	check("a missing file", g.openTxtfile("/nonexistent/dir/cheats.txt") == 0);
	check("an empty file", g.openTxtfile(tmpfile_with("", 0)) == -1);
	check("the sample file", g.openTxtfile(tmpfile_text(TXT)) == 1);
	check("two cheats, the one without codes dropped", g.getCnt() == 2);
	check("game ID", g.getGameID() == "GALE01");
	check("game title", g.getGameName() == "Test Game");
	check("cheat one name", g.getCheatName(0) == "Cheat One");
	check("cheat one has two words", g.getCheat(0).size() == 2);
	check("cheat one first word", g.getCheat(0).size() == 2 && g.getCheat(0)[0] == 0x12345678);
	check("cheat two name", g.getCheatName(1) == "Cheat Two");
	check("cheat two comment", g.getCheatComment(1) == "a comment line");
	check("cheat two has four words", g.getCheat(1).size() == 4);
	check("cheat number out of range", g.getCheatName(2) == ERRORRANGE && g.getCheat(-1).empty());

	check("only an ID", g.openTxtfile(tmpfile_text("GALE01")) == 1 && g.getCnt() == 0);
	check("only a newline", g.openTxtfile(tmpfile_text("\n")) == 1 && g.getCnt() == 0);
	check("CRLF line ends", g.openTxtfile(tmpfile_text("GALE01\r\nT\r\n\r\nC\r\n12345678 9ABCDEF0\r\n")) == 1 &&
		g.getCnt() == 1 && g.getGameID() == "GALE01");
	check("a code ending the file", g.openTxtfile(tmpfile_text("G\nT\n\nC\n12345678 9ABCDEF0")) == 1 && g.getCnt() == 1);
	{
		std::string longline(20000, 'A');
		std::string txt = "G\nT\n\n" + longline + "\n12345678 9ABCDEF0\n";
		check("a line longer than the line buffer", g.openTxtfile(tmpfile_text(txt.c_str())) == 1);
	}
	{
		unsigned char bin[8192];
		srand(1);
		for (size_t i = 0; i < sizeof(bin); i++)
			bin[i] = (unsigned char)(rand() >> 8);
		int r = g.openTxtfile(tmpfile_with(bin, sizeof(bin)));
		check("random bytes as a TXT file", r == 1 || r == -1);
		memset(bin, 0, sizeof(bin));
		r = g.openTxtfile(tmpfile_with(bin, sizeof(bin)));
		check("NUL bytes as a TXT file", r == 1 || r == -1);
	}

	check("IsCode: a code", g.IsCode("12345678 9ABCDEF0"));
	check("IsCode: a code and a comment", g.IsCode("12345678 9abcdef0 comment"));
	check("IsCode: 16 characters", !g.IsCode("12345678 9ABCDEF"));
	check("IsCode: no space", !g.IsCode("12345678X9ABCDEF0"));
	check("IsCode: not hex", !g.IsCode("1234567G 9ABCDEF0"));
	check("IsCode: empty", !g.IsCode(""));

	// IsCheatIncluded over the sample
	check("reload the sample", g.openTxtfile(tmpfile_text(TXT)) == 1 && g.getCnt() == 2);

	// A .gct of every length from 0 to 7 bytes, and up to header + footer less one
	for (size_t n = 0; n < sizeof(GCT_Header) + sizeof(GCT_Footer); n++)
	{
		std::vector<unsigned char> v(n, 0xAA);
		if (n >= 1)
			v[0] = 0x00;
		char what[64];
		snprintf(what, sizeof(what), "a %u byte .gct", (unsigned)n);
		check(what, !included(g, 0, v) && !included(g, 1, v));
	}

	{
		std::vector<unsigned char> v;
		append_header(v);
		append_footer(v);
		check("header and footer only", !included(g, 0, v) && !included(g, 1, v));
	}
	{
		std::vector<unsigned char> v;
		append_header(v);
		check("header only", !included(g, 0, v));
	}
	{
		std::vector<unsigned char> v;
		append_header(v);
		append_cheat(g, 0, v);
		append_footer(v);
		check("cheat one in a .gct", included(g, 0, v));
		check("cheat two not in it", !included(g, 1, v));
		check("number out of range", !included(g, 2, v) && !included(g, -1, v));
		check("no buffer", !g.IsCheatIncluded(0, NULL, 0) && !g.IsCheatIncluded(0, NULL, 100));
	}
	{
		std::vector<unsigned char> v;
		append_header(v);
		append_cheat(g, 0, v);
		append_cheat(g, 1, v);
		append_footer(v);
		check("both cheats, first", included(g, 0, v));
		check("both cheats, last one before the footer", included(g, 1, v));
	}
	{
		// A cheat cut short: its first words, then the footer
		std::vector<unsigned char> v;
		append_header(v);
		append_cheat(g, 1, v);
		v.resize(v.size() - 4);
		append_footer(v);
		check("a truncated cheat", !included(g, 1, v));
		// a cheat that runs into the footer's place
		std::vector<unsigned char> w;
		append_header(w);
		append_cheat(g, 1, w);
		w.resize(w.size() - 4);
		check("a cheat cut off by the end of the file", !included(g, 1, w));
	}
	{
		// A cheat whose first half is the last bytes before the footer
		std::vector<unsigned char> v;
		append_header(v);
		append_cheat(g, 0, v);
		v.resize(v.size() - 4);
		v.insert(v.end(), 4, 0xEE);
		append_footer(v);
		check("a cheat with a wrong second word", !included(g, 0, v));
	}
	{
		// The footer itself is not searched
		std::vector<unsigned char> v;
		append_header(v);
		v.insert(v.end(), 8, 0x11);
		append_footer(v);
		check("data that is not the cheat", !included(g, 0, v));
	}
	{
		// A large random buffer, the cheat planted last before the footer
		std::vector<unsigned char> v;
		append_header(v);
		srand(2);
		for (int i = 0; i < 1000000; i++)
			v.push_back((unsigned char)(rand() >> 8));
		append_footer(v);
		check("a large random buffer", !included(g, 0, v) && !included(g, 1, v));
		std::vector<unsigned char> w(v.begin(), v.end() - sizeof(GCT_Footer));
		append_cheat(g, 1, w);
		append_footer(w);
		check("a large buffer with the cheat at the end", included(g, 1, w));
	}

	// createGCT, read back
	{
		std::vector<int> sel;
		sel.push_back(1);
		sel.push_back(0);
		static char path[] = "/tmp/gct_test_out_XXXXXX";
		int fd = mkstemp(path);
		if (fd < 0)
			abort();
		close(fd);
		check("createGCT", g.createGCT(sel, path) == 1);
		check("createGCT with no cheats", g.createGCT(std::vector<int>(), path) == 0);
		FILE *f = fopen(path, "rb");
		std::vector<unsigned char> v;
		if (f)
		{
			unsigned char tmp[256];
			size_t n;
			while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0)
				v.insert(v.end(), tmp, tmp + n);
			fclose(f);
		}
		check("the written .gct has header, 6 words and footer", v.size() == 8 + 24 + 8);
		check("both cheats are in the written .gct", included(g, 0, v) && included(g, 1, v));
		//! an index out of range is skipped; it used to hang createGCT for good
		std::vector<int> bad(1, 9999);
		bad.push_back(0);
		check("createGCT skips an index out of range", g.createGCT(bad, path) == 1);
		unlink(path);
	}

	unlink(tmpfile_text(""));
	if (failures)
		return 1;
	printf("ok: gct_test\n");
	return 0;
}
