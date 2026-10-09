/****************************************************************************
 * BootSector.h
 *
 * Sizes read out of a partition's first sector. Everything here comes off a
 * drive the user formatted, so each value is checked before it is returned.
 ***************************************************************************/
#ifndef BOOTSECTOR_H_
#define BOOTSECTOR_H_

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

//! The size a FAT or NTFS boot sector states, in 512-byte sectors, or 0 when
//! the sector states none that is usable. sector must hold 512 bytes.
u32 BootSectorSecCount(const u8 *sector);

//! The size a WBFS header states, in 512-byte sectors, or 0 when the header
//! is not one libwbfs can open without shifting or allocating out of range.
//! head must hold the first 12 bytes of the partition.
u32 WbfsHeadSecCount(const u8 *head);

#ifdef __cplusplus
}
#endif

#endif
