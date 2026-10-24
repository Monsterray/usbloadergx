/*
 * exittest: a homebrew app for Dolphin runs of USB Loader GX (scripts/diag.sh exittest).
 *
 * GX boots it from its homebrew source; it shows a line on screen, writes
 * sd:/exittest.log, waits EXITTEST_SECONDS and calls exit(0). libogc's exit()
 * jumps to the stub at 0x80001800 when one is loaded ("STUBHAXX" at 0x80001804),
 * so this tests the return path GX sets up with loadStub()/Set_Stub(): the
 * stub launches the title GX wrote into it (the Homebrew Channel, a forwarder
 * channel, or the System Menu).
 */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXITTEST_SECONDS 4

static void Log(const char *fmt, const char *arg)
{
	FILE *f = fopen("sd:/exittest.log", "a");
	if (!f)
		return;
	fprintf(f, fmt, arg);
	fclose(f);
}

int main(int argc, char **argv)
{
	VIDEO_Init();
	GXRModeObj *rmode = VIDEO_GetPreferredMode(NULL);
	void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();

	const char *stub = memcmp((const void *)0x80001804, "STUBHAXX", 8) == 0 ? "yes" : "no";
	printf("\n\n  exittest: running, argc %d, stub %s\n", argc, stub);
	printf("  exit(0) in %d seconds\n", EXITTEST_SECONDS);

	bool sd = fatInitDefault();
	if (sd)
	{
		Log("exittest: started, argv[0] %s\n", argc > 0 && argv && argv[0] ? argv[0] : "(none)");
		Log("exittest: return stub loaded: %s\n", stub);
	}

	for (int i = 0; i < EXITTEST_SECONDS * 60; ++i)
		VIDEO_WaitVSync();

	if (sd)
	{
		Log("exittest: calling exit(0)%s\n", "");
		fatUnmount("sd:");
	}
	printf("  exit(0)\n");
	exit(0);
}
