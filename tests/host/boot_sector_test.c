/* boot_sector_test.c - BootSectorSecCount() and WbfsHeadSecCount() against
 * the sectors a drive can hold.
 *
 * Runs on the host: source/Controls/BootSector.c has no libogc in it, so the
 * code under test is the exact code the Wii runs. A drive with no partition
 * table (a "superfloppy", which is how Dolphin builds its SD image) gives GX
 * no size but the one in its boot sector, and WBFS formatting and libwbfs
 * size what they write and allocate by it.
 */
#include <stdio.h>
#include "Controls/BootSector.c"

static int failures;

static void expect(const char *what, u32 got, u32 want)
{
	if (got != want)
	{
		printf("FAIL: %s = %u (0x%08x), want %u\n", what, got, got, want);
		failures++;
	}
}

static void put_le16(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; }
static void put_le32(u8 *p, u32 v) { put_le16(p, v); put_le16(p + 2, v >> 16); }
static void put_be32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void fat32(u8 *s, u32 bps, u32 total16, u32 total32)
{
	memset(s, 0, 512);
	memcpy(s + 0x03, "MSDOS5.0", 8);
	put_le16(s + 0x0B, bps);
	put_le16(s + 0x13, total16);
	put_le32(s + 0x20, total32);
	memcpy(s + 0x52, "FAT32   ", 8);
	s[0x1FE] = 0x55;
	s[0x1FF] = 0xAA;
}

static void wbfs(u8 *h, u32 n_hd_sec, u8 hd_s, u8 wbfs_s)
{
	memset(h, 0, 512);
	memcpy(h, "WBFS", 4);
	put_be32(h + 4, n_hd_sec);
	h[8] = hd_s;
	h[9] = wbfs_s;
}

int main(void)
{
	u8 s[512];

	/* Dolphin's SD image: FAT32 at sector 0, no MBR, 1131520 sectors. */
	fat32(s, 512, 0, 1131520);
	expect("FAT32 superfloppy", BootSectorSecCount(s), 1131520);

	/* A small FAT16 volume keeps its size in the 16-bit field. */
	fat32(s, 512, 40000, 0);
	memset(s + 0x52, 0, 8);
	memcpy(s + 0x36, "FAT16   ", 8);
	expect("FAT16 16-bit size", BootSectorSecCount(s), 40000);

	/* 4096-byte sectors count as eight 512-byte ones. */
	fat32(s, 4096, 0, 1000);
	expect("FAT32 with 4K sectors", BootSectorSecCount(s), 8000);

	/* A size that does not fit a u32 of 512-byte sectors is no size. */
	fat32(s, 4096, 0, 0x40000000);
	expect("FAT32 too large for a u32", BootSectorSecCount(s), 0);

	/* Bytes per sector that no drive has. */
	fat32(s, 0, 0, 1000);
	expect("bytes per sector 0", BootSectorSecCount(s), 0);
	fat32(s, 768, 0, 1000);
	expect("bytes per sector 768", BootSectorSecCount(s), 0);
	fat32(s, 8192, 0, 1000);
	expect("bytes per sector 8192", BootSectorSecCount(s), 0);

	/* NTFS keeps a 64-bit size at 0x28. */
	memset(s, 0, sizeof(s));
	memcpy(s + 0x03, "NTFS    ", 8);
	put_le16(s + 0x0B, 512);
	put_le32(s + 0x28, 0x12345678);
	expect("NTFS", BootSectorSecCount(s), 0x12345678);
	put_le32(s + 0x2C, 1);
	expect("NTFS above 2 TiB", BootSectorSecCount(s), 0);

	/* Not a boot sector at all. */
	memset(s, 0, sizeof(s));
	put_le16(s + 0x0B, 512);
	expect("empty sector", BootSectorSecCount(s), 0);
	expect("NULL sector", BootSectorSecCount(NULL), 0);

	/* WBFS as libwbfs formats 500 GB: 512-byte sectors and 8 MiB WBFS sectors,
	 * the smallest that numbers every block in 16 bits. */
	wbfs(s, 976773168, 9, 23);
	expect("WBFS 500 GB, 8 MiB sectors", WbfsHeadSecCount(s), 976773168);
	/* Over 500 GB GX formats with 2048-byte "hd" sectors. */
	wbfs(s, 488386584, 11, 24);
	expect("WBFS 1 TB, 2K sectors", WbfsHeadSecCount(s), 488386584u * 4);

	/* Fields libwbfs would shift or allocate by. */
	wbfs(s, 1000000, 31, 21);
	expect("WBFS hd sector shift 31", WbfsHeadSecCount(s), 0);
	wbfs(s, 1000000, 8, 21);
	expect("WBFS hd sector shift 8", WbfsHeadSecCount(s), 0);
	wbfs(s, 1000000, 9, 14);
	expect("WBFS sector smaller than a Wii sector", WbfsHeadSecCount(s), 0);
	wbfs(s, 1000000, 9, 40);
	expect("WBFS sector shift 40", WbfsHeadSecCount(s), 0);
	wbfs(s, 0, 9, 21);
	expect("WBFS of no sectors", WbfsHeadSecCount(s), 0);
	wbfs(s, 0xFFFFFFFF, 12, 15);
	expect("WBFS with more blocks than 16 bits number", WbfsHeadSecCount(s), 0);
	wbfs(s, 976773168, 9, 21);
	expect("WBFS 500 GB with 2 MiB sectors", WbfsHeadSecCount(s), 0);
	wbfs(s, 0x20000001, 12, 26);
	expect("WBFS too large for a u32 of 512-byte sectors", WbfsHeadSecCount(s), 0);
	wbfs(s, 1000, 9, 21);
	expect("WBFS smaller than one Wii sector", WbfsHeadSecCount(s), 0);
	memcpy(s, "WBFX", 4);
	expect("not WBFS", WbfsHeadSecCount(s), 0);
	expect("NULL head", WbfsHeadSecCount(NULL), 0);

	if (failures)
		return 1;
	printf("boot_sector_test: all cases pass\n");
	return 0;
}
