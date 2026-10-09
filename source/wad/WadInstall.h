/****************************************************************************
 * WadInstall.h
 *
 * Installs a WAD held in memory to the console's real NAND through ES, the
 * way WAD managers do: ticket, TMD, every content, then finish. The channel
 * updater uses it once the WAD's signed SHA-256 has matched.
 ***************************************************************************/
#ifndef WAD_INSTALL_H
#define WAD_INSTALL_H

#include <gctypes.h>

// 0 on success, otherwise a negative ES or WAD_INSTALL_* error. Installs only
// a downloadable channel (00010001-xxxxxxxx) whose title ID is expectedTitle.
// Any failure after ES has started the title cancels it, so ES discards the
// half-written import instead of leaving it on NAND.
int WadInstall(const u8 *wad, u32 size, u64 expectedTitle);

#define WAD_INSTALL_BAD_LAYOUT  -9001
#define WAD_INSTALL_WRONG_TITLE -9002
#define WAD_INSTALL_NO_MEMORY   -9003

#endif
