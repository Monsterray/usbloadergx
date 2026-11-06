#include <stdio.h>
#include <gccore.h>
#include <tuxedo/ppc/exception.h>
#include "CrashReset.h"

#define HW_AHBPROT (*(vu32 *)0xCD800064)
#define HW_RESETS (*(vu32 *)0xCD800194)
#define HW_VERSION_WIIU (*(vu16 *)0xCD8005A0)
#define PRII_MAGIC_WORD (*(vu32 *)0xC17FEFF0)

static volatile int riskyDepth = 0;
static volatile bool inPanic = false;
static PPCExcptPanicFn prevPanic = NULL;

//! libogc's own, linked with -wrap,__reload (Makefile)
void __real___reload(void);

static void CrashPanic(unsigned exid, PPCContext *ctx)
{
	//! libogc's dump and countdown come next, then its __reload() reaches the wrapper
	inPanic = true;
	if (prevPanic)
		prevPanic(exid, ctx);
}

void CrashReset_Init(void)
{
	if (PPCExcptCurPanicFn == CrashPanic)
		return;
	prevPanic = PPCExcptCurPanicFn;
	PPCExcptCurPanicFn = CrashPanic;
}

void CrashReset_Enter(void)
{
	riskyDepth++;
}

void CrashReset_Leave(void)
{
	if (riskyDepth > 0)
		riskyDepth--;
}

void __wrap___reload(void)
{
	if (inPanic && riskyDepth > 0)
		printf("\nCrash during start-up or an IOS reload. AHBPROT %08x, chip %04x: %s\n", (unsigned)HW_AHBPROT,
			   (unsigned)HW_VERSION_WIIU,
			   HW_AHBPROT == 0xFFFFFFFF && HW_VERSION_WIIU != 0xCAFE ? "resetting the console" : "cannot reset, reloading");
	if (inPanic && riskyDepth > 0 && HW_AHBPROT == 0xFFFFFFFF && HW_VERSION_WIIU != 0xCAFE)
	{
		//! Priiloader's magic word "Pune": skip its autoboot and start the System Menu (uncached,
		//! so in RAM). Not "Abra" (run the autoboot): where Priiloader autoboots GX, a crash that
		//! happens on every start-up would reset into GX again and again.
		PRII_MAGIC_WORD = 0x50756E65;
		//! RSTBINB low: a system reset that needs no IOS
		HW_RESETS &= ~1u;
		while (true)
			;
	}
	__real___reload();
}
