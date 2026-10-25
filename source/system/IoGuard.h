/****************************************************************************
 * IoGuard: a time limit on the IOS requests that can block for good.
 *
 * A USB write through an old d2x cIOS can get no status and never return
 * (lrwii, cIOS 249 r65535, 2026-10-24: a disc dump stopped at 16.53 MB and
 * the Wii stayed frozen; its exit's USB unmount would wait on the same
 * cIOS). Each USB sector read and write through the cIOS and each disc read
 * is timed here. A watch thread at the highest priority checks every second;
 * a request still running after IOGUARD_LIMIT_S seconds is logged through
 * stdout only (no SD write: that is an IOS request too) and ends GX through
 * hbc_agent_fatal_now(), whose way out does not need the stuck module.
 *
 * Every request also counts in per-kind stats, and one that takes longer
 * than IOGUARD_SLOW_MS is logged with its sector when it ends.
 ***************************************************************************/
#ifndef IOGUARD_H_
#define IOGUARD_H_

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IOGUARD_LIMIT_S 120
#define IOGUARD_SLOW_MS 2000

enum
{
	IOGUARD_USB_READ,
	IOGUARD_USB_WRITE,
	IOGUARD_DI_READ,
	IOGUARD_KINDS
};

//! Start the watch thread; once is enough, before the first request is fine too
void IoGuard_Start(void);
//! A request of `count` sectors at `sector` starts; returns the token for IoGuard_End()
int IoGuard_Begin(int kind, u32 sector, u32 count, u32 bytes);
void IoGuard_End(int token);
//! The stats since the last reset, one gprintf line per kind that ran
void IoGuard_Report(const char *where);
void IoGuard_ResetStats(void);

#ifdef __cplusplus
}
#endif

#endif
