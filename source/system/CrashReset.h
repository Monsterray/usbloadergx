/****************************************************************************
 * CrashReset: a crash in a risky window resets the console instead of jumping
 * to the reload stub.
 *
 * After a crash libogc shows its dump, counts down and calls __reload(), which
 * jumps to the stub at 0x80001800; the stub reloads IOS and launches HBC. A
 * crash while IOS itself reloads can leave IOS unable to serve the stub, and
 * the Wii then sits on the crash screen for good (lrwii, 2026-10-24: a crash
 * during GX's start-up reload to cIOS 249). The hbc agent cannot help there:
 * it starts at the end of start-up, and an exception leaves through libogc.
 *
 * GX marks two windows: its start-up until the agent runs, and every IOS
 * reload. A crash inside one, once libogc's countdown ends, resets the
 * console through HW_RESETS (needs AHB access, never on a Wii U), as a power-
 * on would: boot2, then the System Menu (Priiloader is told to skip its autoboot,
 * so a crash on every start-up cannot loop). Every
 * other crash, and every normal exit, still goes through the stub.
 ***************************************************************************/
#ifndef CRASHRESET_H_
#define CRASHRESET_H_

#ifdef __cplusplus
extern "C" {
#endif

//! Hook libogc's panic; first thing in main()
void CrashReset_Init(void);
//! A risky window opens or closes; they nest
void CrashReset_Enter(void);
void CrashReset_Leave(void);

#ifdef __cplusplus
}
#endif

#endif
