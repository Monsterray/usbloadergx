/* Host test for usbloader/neek.cpp: kernel.bin and nandcfg.bin handling.
 *
 * It #includes the real neek.cpp. The headers neek.cpp pulls in from the rest of the
 * loader (device handler, file operations, settings, MEM2, gecko) are replaced by
 * the stubs below, by defining their include guards first; paths on the "device"
 * "dev:" are real files under a temporary directory the test is chdir'ed into
 * ("dev:/sneek/kernel.bin" is the relative path dev:/sneek/kernel.bin). MEM2_alloc is
 * malloc of the exact size, so ASan sees any read past a block neek.cpp asks for.
 *
 * Defects covered (features.md, section 12): kernel.bin of 0..5 bytes, LoadFileToMem's
 * negative return taken as success, a short or empty kernel, a 9th EmuNAND, a nandcfg.bin
 * truncated or longer than its header says, and the game ID neek is handed.
 *
 * Define NEEK_SRC to test another copy of neek.cpp (used to show a test fails on the
 * code before the fix).
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gctypes.h>

#pragma GCC diagnostic ignored "-Wmultichar"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"

/* libogc */
typedef volatile u32 vu32;
struct _ioctlv;
typedef struct _ioctlv ioctlv;
static u32 g_reg[1];
static u32 read32(u32) { return g_reg[0]; }
static void write32(u32, u32 v) { g_reg[0] = v; }
static void DCFlushRange(void *, u32) {}
static void DCInvalidateRange(void *, u32) {}
static void __IOS_ShutdownSubsystems(void) {}
static s32 IOS_Open(const char *, u32) { return -1; }
static s32 IOS_Close(s32) { return 0; }
static s32 IOS_IoctlvAsync(s32, u32, u32, u32, ioctlv *, void *, void *) { return 0; }

/* gecko.h */
#define _GECKO_H_
static int g_log_lines;
static void gprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void gprintf(const char *fmt, ...)
{
	(void) fmt;
	g_log_lines++;
}

/* memory/mem2.h */
#define __MEM2_H_
static void *MEM2_alloc(unsigned int size) { return malloc(size); }
static void MEM2_free(void *p) { free(p); }

/* sys.h */
#define _SYS_H_
static bool isWiiU(void) { return false; }

/* settings/CSettings.h */
#define _CSETTINGS_H_
static struct { char NandEmuChanPath[50]; } Settings; /* CSettings.h: char[50] */

/* Controls/DeviceHandler.hpp */
#define DEVICE_HANDLER_HPP_
class DeviceHandler
{
public:
	static const char *GetDevicePrefix(const char *) { return "dev"; }
};

/* FileOperations/fileops.h: the five calls neek.cpp makes, on stdio */
#define _FILEOPS_H_
static bool CheckFile(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0;
}
static u64 FileSize(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 ? (u64) st.st_size : 0;
}
static bool RemoveFile(const char *p) { return remove(p) == 0; }
/* as the real one: 1 on success, negative on failure, *buffer NULL and *size 0 then */
static int LoadFileToMem(const char *p, u8 **buffer, u32 *size)
{
	*buffer = NULL;
	*size = 0;
	FILE *f = fopen(p, "rb");
	if (!f)
		return -1;
	u64 n = FileSize(p);
	u8 *b = (u8 *) malloc(n);
	if (!b || fread(b, 1, n, f) != n)
	{
		free(b);
		fclose(f);
		return -3;
	}
	fclose(f);
	*buffer = b;
	*size = (u32) n;
	return 1;
}

#ifndef NEEK_SRC
#define NEEK_SRC "usbloader/neek.cpp"
#endif
#include NEEK_SRC

static int failures;
#define CHECK(c)                                                       \
	do                                                                 \
	{                                                                  \
		if (!(c))                                                      \
		{                                                              \
			fprintf(stderr, "neek_test:%d: CHECK(%s)\n", __LINE__, #c); \
			failures++;                                                \
		}                                                              \
	} while (0)

static const char *KERNEL = "dev:/sneek/kernel.bin";
static const char *CFG = "dev:/sneek/nandcfg.bin";

static void put(const char *path, const void *data, size_t len)
{
	FILE *f = fopen(path, "wb");
	if (!f)
	{
		perror(path);
		exit(2);
	}
	if (len)
		fwrite(data, 1, len, f);
	fclose(f);
}

static void put_zero(const char *path, size_t len)
{
	u8 *z = (u8 *) calloc(1, len ? len : 1);
	put(path, z, len);
	free(z);
}

static void nand_path(char *out, size_t n, int i) { snprintf(out, n, "dev:/nands/n%d", i); }

static u32 cfg_u32(int word)
{
	u32 v = 0;
	FILE *f = fopen(CFG, "rb");
	if (f)
	{
		fseek(f, word * 4, SEEK_SET);
		if (fread(&v, 4, 1, f) != 1)
			v = 0xdead;
		fclose(f);
	}
	return v;
}

static void cfg_entry_path(int i, char *out, size_t n)
{
	out[0] = 0;
	FILE *f = fopen(CFG, "rb");
	if (!f)
		return;
	fseek(f, NANDCONFIG_HEADER_SIZE + i * NANDCONFIG_NANDINFO_SIZE, SEEK_SET);
	if (fread(out, 1, n - 1, f) > 0)
		out[n - 1] = 0;
	fclose(f);
}

static void test_kernel_tag(void)
{
	CHECK(neekIsNeek2o("dev:/nands/n0") == -1); // no kernel.bin

	/* 0..5 bytes: shorter than the tag. Used to wrap filesize-6 and read off the block. */
	for (size_t len = 0; len <= 5; len++)
	{
		put(KERNEL, "NEEK2O", len);
		CHECK(neekIsNeek2o("dev:/nands/n0") == 0);
	}
	put(KERNEL, "NEEK2O", 6);
	CHECK(neekIsNeek2o("dev:/nands/n0") == 1);
	put(KERNEL, "xNEEK2Oyz", 9);
	CHECK(neekIsNeek2o("dev:/nands/n0") == 1);
	put(KERNEL, "sneek kernel without the tag at all", 35);
	CHECK(neekIsNeek2o("dev:/nands/n0") == 0);

	/* a kernel.bin the loader cannot open: LoadFileToMem returns a negative code */
	chmod(KERNEL, 0);
	if (access(KERNEL, R_OK) != 0) // not root
		CHECK(neekIsNeek2o("dev:/nands/n0") == -1);
	chmod(KERNEL, 0644);
}

static void test_kernel_load(void)
{
	put(KERNEL, "", 0);
	CHECK(neekLoadKernel("dev:/nands/n0") == false); // an empty kernel is not a kernel
	CHECK(kernel == NULL);

	u8 data[100];
	for (int i = 0; i < 100; i++)
		data[i] = (u8) i;
	put(KERNEL, data, sizeof(data));
	CHECK(neekLoadKernel("dev:/nands/n0") == true);
	CHECK(kernel != NULL && memcmp(kernel, data, sizeof(data)) == 0);
	CHECK(neekLoadKernel("dev:/nands/n0") == true); // a second load does not leak (ASan)
	MEM2_free(kernel);
	kernel = NULL;

	unlink(KERNEL);
	CHECK(neekLoadKernel("dev:/nands/n0") == false);
}

static void test_nandcfg_fill(void)
{
	unlink(CFG);
	char p[64];

	/* new file, then up to the limit: every entry where neek reads it */
	for (int i = 0; i < NANDCONFIG_MAXNAND; i++)
	{
		nand_path(p, sizeof(p), i);
		CHECK(neek2oSetNAND(p) == i);
		CHECK(FileSize(CFG) == (u64)(NANDCONFIG_HEADER_SIZE + (i + 1) * NANDCONFIG_NANDINFO_SIZE));
		CHECK(cfg_u32(0) == (u32)(i + 1));
		CHECK(cfg_u32(1) == (u32) i);
	}
	char got[64];
	for (int i = 0; i < NANDCONFIG_MAXNAND; i++)
	{
		nand_path(p, sizeof(p), i);
		cfg_entry_path(i, got, sizeof(got));
		CHECK(strcmp(got, p + 4) == 0); // "/nands/nI"
	}

	/* the 9th must fail and leave the file alone; it used to write NandCnt 9 */
	u8 before[NANDCONFIG_HEADER_SIZE + NANDCONFIG_MAXNAND * NANDCONFIG_NANDINFO_SIZE];
	FILE *f = fopen(CFG, "rb");
	CHECK(f && fread(before, 1, sizeof(before), f) == sizeof(before));
	if (f)
		fclose(f);
	nand_path(p, sizeof(p), 8);
	CHECK(neek2oSetNAND(p) == -1);
	CHECK(CheckFile(CFG));
	CHECK(FileSize(CFG) == sizeof(before));
	CHECK(cfg_u32(0) == (u32) NANDCONFIG_MAXNAND);
	u8 after[sizeof(before)];
	f = fopen(CFG, "rb");
	CHECK(f && fread(after, 1, sizeof(after), f) == sizeof(after));
	if (f)
		fclose(f);
	CHECK(memcmp(before, after, sizeof(before)) == 0);

	/* ... and the next call (the one that used to delete the file) still works */
	nand_path(p, sizeof(p), 3);
	CHECK(neek2oSetNAND(p) == 3);
	CHECK(CheckFile(CFG));
	CHECK(cfg_u32(0) == (u32) NANDCONFIG_MAXNAND);
	CHECK(cfg_u32(1) == 3);
}

static void test_nandcfg_damaged(void)
{
	char p[64];
	nand_path(p, sizeof(p), 0);

	/* header promises 3 NANDs, the file holds the header and one: no read past the block */
	unlink(CFG);
	CHECK(neek2oSetNAND(p) == 0);
	u8 *buf = (u8 *) calloc(1, NANDCONFIG_HEADER_SIZE + NANDCONFIG_NANDINFO_SIZE);
	FILE *f = fopen(CFG, "rb");
	CHECK(f && fread(buf, 1, NANDCONFIG_HEADER_SIZE + NANDCONFIG_NANDINFO_SIZE, f) == NANDCONFIG_HEADER_SIZE + NANDCONFIG_NANDINFO_SIZE);
	if (f)
		fclose(f);
	u32 cnt = 3;
	memcpy(buf, &cnt, 4);
	memset(buf + NANDCONFIG_HEADER_SIZE, 'x', NANDCONFIG_NANDINFO_SIZE); // no NUL anywhere in the entry
	put(CFG, buf, NANDCONFIG_HEADER_SIZE + NANDCONFIG_NANDINFO_SIZE);
	free(buf);
	nand_path(p, sizeof(p), 5);
	CHECK(neek2oSetNAND(p) == -1);
	CHECK(FileSize(CFG) == NANDCONFIG_HEADER_SIZE + NANDCONFIG_NANDINFO_SIZE); // kept, not deleted

	/* 1..16 bytes: recreated as an empty header, then the NAND added */
	for (size_t len = 0; len <= NANDCONFIG_HEADER_SIZE; len++)
	{
		put_zero(CFG, len);
		CHECK(neek2oSetNAND(p) == 0);
		CHECK(FileSize(CFG) == NANDCONFIG_HEADER_SIZE + NANDCONFIG_NANDINFO_SIZE);
	}

	/* bigger than the header says: the junk after the entries is not read and not kept,
	   and the new entry lands right after the last real one */
	nand_path(p, sizeof(p), 0);
	unlink(CFG);
	CHECK(neek2oSetNAND(p) == 0);
	f = fopen(CFG, "ab");
	CHECK(f != NULL);
	if (f)
	{
		u8 junk[10000];
		memset(junk, 'J', sizeof(junk));
		fwrite(junk, 1, sizeof(junk), f);
		fclose(f);
	}
	nand_path(p, sizeof(p), 1);
	CHECK(neek2oSetNAND(p) == 1);
	CHECK(FileSize(CFG) == NANDCONFIG_HEADER_SIZE + 2 * NANDCONFIG_NANDINFO_SIZE);
	char got[64];
	cfg_entry_path(1, got, sizeof(got));
	CHECK(strcmp(got, "/nands/n1") == 0);

	/* the header written for a new file is 16 bytes, and entry 0 follows at offset 16 */
	unlink(CFG);
	nand_path(p, sizeof(p), 2);
	CHECK(neek2oSetNAND(p) == 0);
	CHECK(FileSize(CFG) == NANDCONFIG_HEADER_SIZE + NANDCONFIG_NANDINFO_SIZE);
	cfg_entry_path(0, got, sizeof(got));
	CHECK(strcmp(got, "/nands/n2") == 0);

	/* a header that cannot be right (NandCnt 9) is the old "corrupt, delete" case */
	u32 bad[4] = {9, 0, 0, 0};
	put(CFG, bad, sizeof(bad));
	CHECK(FileSize(CFG) == 16);
	u8 *big = (u8 *) calloc(1, NANDCONFIG_HEADER_SIZE + 9 * NANDCONFIG_NANDINFO_SIZE);
	memcpy(big, bad, sizeof(bad));
	put(CFG, big, NANDCONFIG_HEADER_SIZE + 9 * NANDCONFIG_NANDINFO_SIZE);
	free(big);
	CHECK(neek2oSetNAND(p) == -1);
	CHECK(!CheckFile(CFG));

	/* a path that does not fit NandInfo::Path is refused, not stored cut */
	unlink(CFG);
	char longp[300] = "dev:/nands/";
	memset(longp + 11, 'a', 150);
	longp[161] = 0;
	CHECK(neek2oSetNAND(longp) == -1);
	CHECK(!CheckFile(CFG) || FileSize(CFG) == NANDCONFIG_HEADER_SIZE);

	/* no '/' in the path */
	CHECK(neek2oSetNAND("dev") == -1);
	CHECK(neek2oSetNAND(NULL) == -1);
}

static void test_path_and_gameid(void)
{
	char out[16];
	CHECK(neekPathFormat(out, "usb1:/nands/a/", sizeof(out)) == 1 && strcmp(out, "/nands/a") == 0);
	CHECK(neekPathFormat(out, "usb1:/", sizeof(out)) == 1 && strcmp(out, "") == 0);
	CHECK(neekPathFormat(out, "usb1:", sizeof(out)) == -1);
	CHECK(neekPathFormat(out, NULL, sizeof(out)) == -1);
	CHECK(neekPathFormat(out, "usb1:/x", 0) == -1);
	CHECK(neekPathFormat(out, "usb1:/nands/averylongname", 1) == 1 && out[0] == 0); // cut to nothing

	/* a failed path format must not set NCON_EXT_NAND_PATH with an empty path */
	NEEK_CFG cfg;
	CHECK(neek2oSetBootSettings(&cfg, 0, 0, 0, "usb1") == true);
	CHECK((cfg.config & NCON_EXT_NAND_PATH) == 0);
	CHECK(neek2oSetBootSettings(&cfg, 0, 0, 0, "usb1:/nands/a") == true);
	CHECK((cfg.config & NCON_EXT_NAND_PATH) != 0 && strcmp(cfg.nandpath, "/nands/a") == 0);

	/* the game ID neek's DI compares with is the first four ID bytes, big-endian; the
	   loader used to store the address of the 6-byte ID array instead */
	u8 *id = (u8 *) malloc(6); // heap, so its address is not the ID by accident
	memcpy(id, "RSPE01", 6);
	CHECK(neekGameId4(id) == 0x52535045u);
	memcpy(id, "GALP01", 6);
	CHECK(neekGameId4(id) == 0x47414c50u);
	CHECK(neekGameId4(id) != (u32)(uintptr_t) id);
	free(id);
}

/* usage: neek_test [section]  (no argument: all; sections: tag load fill damaged path) */
static const char *only;
static bool want(const char *name) { return !only || strcmp(only, name) == 0; }

int main(int argc, char **argv)
{
	only = argc > 1 ? argv[1] : NULL;
	char tmpl[] = "/tmp/neek_test_XXXXXX";
	char *dir = mkdtemp(tmpl);
	if (!dir || chdir(dir) != 0)
	{
		perror("mkdtemp");
		return 2;
	}
	mkdir("dev:", 0755);
	mkdir("dev:/sneek", 0755);

	if (want("tag"))
		test_kernel_tag();
	if (want("load"))
		test_kernel_load();
	if (want("fill"))
		test_nandcfg_fill();
	if (want("damaged"))
		test_nandcfg_damaged();
	if (want("path"))
		test_path_and_gameid();

	if (chdir("/") == 0)
	{
		char cmd[128];
		snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
		if (system(cmd) != 0)
			fprintf(stderr, "neek_test: could not remove %s\n", dir);
	}
	if (failures)
	{
		fprintf(stderr, "neek_test: %d check(s) failed\n", failures);
		return 1;
	}
	printf("neek_test: ok\n");
	return 0;
}
