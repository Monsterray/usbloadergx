/****************************************************************************
 * USB Loader GX Team
 *
 * Main loadup of the application
 *
 * libwiigui
 * Tantric 2009
 ***************************************************************************/
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <ogcsys.h>
#include <unistd.h>
#include <locale.h>
#include <wiiuse/wpad.h>
#include <di/di.h>
#include <sys/iosupport.h>

#include "video.h"
#include "menu/menus.h"
#include "memory/mem2.h"
#include "wad/nandtitle.h"
#include "StartUpProcess.h"
#include "sys.h"
#include "system/IoGuard.h"
#include "system/CrashReset.h"

extern "C"
{
	extern s32 MagicPatches(s32);
	void __exception_setreload(int t);
}

int main(int argc, char *argv[])
{
	__exception_setreload(20);
	// a crash during start-up (it reloads IOS) resets the console rather than wait on the stub;
	// the window closes once the hbc agent runs (HbcAgent_Init)
	CrashReset_Init();
	CrashReset_Enter();
	// activate magic access rights
	MagicPatches(1);
	// init video
	InitVideo();
	// video frame buffers must be in mem1
	MEM2_init();
	// init gecko
	InitGecko();
	// redirect stdout and stderr to gecko
	USBGeckoOutput();
	// a USB or disc request that never returns ends GX instead of freezing the Wii
	IoGuard_Start();
	MEM_Report("start");

	setlocale(LC_ALL, "en_US.UTF-8");

	if(StartUpProcess::Run(argc, argv) < 0)
		return -1;
	// MEM1 for the menus' textures; after start-up, whose IOS reloads are done by now
	TEX_init();
	MEM_Report("startup done");

	MainMenu(MENU_DISCLIST);
	return 0;
}
