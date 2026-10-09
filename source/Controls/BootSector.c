/****************************************************************************
 * BootSector.c
 *
 * Plain C with no libogc in it, so tests/host/ compiles this exact file on
 * the PC and tests the code the Wii runs. The fields are read byte by byte,
 * so the result does not depend on the host's byte order.
 ***************************************************************************/
#include <string.h>
#include "Controls/BootSector.h"

#define SECTOR_BYTES 512

static u32 rd_le16(const u8 *p) { return p[0] | p[1] << 8; }
static u32 rd_le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32) p[3] << 24; }
static u32 rd_be32(const u8 *p) { return (u32) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static u64 rd_le64(const u8 *p)
{
	return (u64) rd_le32(p) | (u64) rd_le32(p + 4) << 32;
}

u32 BootSectorSecCount(const u8 *sector)
{
	if (!sector)
		return 0;

	//! Bytes per sector: a power of two from 512 to 4096, or the BPB is not one
	u32 bps = rd_le16(sector + 0x0B);
	if (bps < SECTOR_BYTES || bps > 4096 || (bps & (bps - 1)))
		return 0;

	u64 count;
	if (memcmp(sector + 0x03, "NTFS", 4) == 0)
		count = rd_le64(sector + 0x28);
	else if (memcmp(sector + 0x36, "FAT", 3) == 0 || memcmp(sector + 0x52, "FAT", 3) == 0)
	{
		//! FAT12/16 keep a small volume's size at 0x13; 0 there means the 32-bit field
		count = rd_le16(sector + 0x13);
		if (count == 0)
			count = rd_le32(sector + 0x20);
	}
	else
		return 0;

	//! The size is reported in 512-byte sectors and as a u32, like a partition
	//! table entry. Larger than that is not a size GX can use.
	if (count > 0xFFFFFFFFULL)
		return 0;
	count *= bps / SECTOR_BYTES;
	if (count > 0xFFFFFFFFULL)
		return 0;

	return (u32) count;
}

u32 WbfsHeadSecCount(const u8 *head)
{
	if (!head || memcmp(head, "WBFS", 4) != 0)
		return 0;

	u32 n_hd_sec = rd_be32(head + 4);
	u32 hd_sec_sz_s = head[8];
	u32 wbfs_sec_sz_s = head[9];

	//! libwbfs shifts by both and never checks either. A hard drive sector is
	//! 512 to 4096 bytes. libwbfs formats with a WBFS sector of 2 MiB to 64 MiB
	//! (wbfs_sec_sz_s 21 to 26), and it needs at least a Wii sector (32 KiB).
	if (n_hd_sec == 0 || hd_sec_sz_s < 9 || hd_sec_sz_s > 12 ||
		wbfs_sec_sz_s < 15 || wbfs_sec_sz_s > 26)
		return 0;

	//! The block table entries are 16 bits: libwbfs chooses the WBFS sector so
	//! that every block has a number. More blocks than that is not a WBFS it
	//! wrote, and it would size the free block table by it.
	u32 n_wii_sec = (n_hd_sec / 0x8000) << hd_sec_sz_s;
	u32 n_wbfs_sec = n_wii_sec >> (wbfs_sec_sz_s - 15);
	if (n_wbfs_sec == 0 || n_wbfs_sec > 0x10000)
		return 0;

	u64 count = (u64) n_hd_sec << (hd_sec_sz_s - 9);
	if (count > 0xFFFFFFFFULL)
		return 0;

	return (u32) count;
}
