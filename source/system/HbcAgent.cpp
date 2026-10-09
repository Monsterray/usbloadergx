#include <gccore.h>
#include <hbc_agent.h>
#include "HbcAgent.h"
#include "settings/CSettings.h"
#include "SoundOperations/MusicPlayer.h"
#include "menu.h"
#include "lstub.h"
#include "video.h"
#include "version.h"
#include "sys.h"
#include "gecko.h"

static bool OnSave(void *)
{
	return Settings.Save();
}

//! Runs in the agent's thread just before it calls exit(0) for `hbc.py exit`
//! or `hbc.py run`, while GX's threads still run: keep it to the settings.
static void OnExit(void *)
{
	Settings.Save();
}

//! The overlay's Exit menu, run in GX's main thread once the overlay closed.
//! Each choice leaves through GX's own exit, so AppCleanUp() runs first.
static bool OnExitChoice(int choice, void *)
{
	switch (choice)
	{
		case HBC_AGENT_EXIT_HBC:
			Sys_LoadHBC();
			break;
		case HBC_AGENT_EXIT_SYSTEM_MENU:
			Sys_LoadMenu();
			break;
		case HBC_AGENT_EXIT_RESTART:
			Sys_Reboot();
			break;
		case HBC_AGENT_EXIT_POWER_OFF:
			Sys_Shutdown();
			break;
	}
	return true;
}

static void PressPriiloader(void *)
{
	Sys_LoadPriiloader();
}

static void PressIdle(void *)
{
	Sys_ShutdownToIdle();
}

//! GX's exits the overlay's Exit menu does not have, in the slot left of it.
static const hbc_agent_item LoaderItems[] = {
	{ "Priiloader", NULL, PressPriiloader, NULL, HBC_AGENT_ITEM_CLOSE },
	{ "Standby (WiiConnect24)", NULL, PressIdle, NULL, HBC_AGENT_ITEM_CLOSE },
};

void HbcAgent_Init(void)
{
	static hbc_agent_config cfg = {};
	cfg.name = "USB Loader GX";
	cfg.version = LOADER_VERSION;
	cfg.on_exit = OnExit;
	cfg.on_save = OnSave;
	cfg.on_exit_choice = OnExitChoice;
	// SetupPads() calls PAD_Init(); START on a GameCube controller is HOME.
	cfg.gc_pads = true;
	// The GUI thread feeds the hang watchdog every frame (menu.cpp); a frozen
	// screen for this long is a hang. HaltGui() pauses the frames while the
	// main thread works, so this leaves room for its slowest step.
	cfg.hang_s = 120;

	s32 ret = hbc_agent_init(&cfg);
	gprintf("hbc agent: %d\n", ret);
	if (ret < 0)
		return;

	hbc_agent_set_slot_menu(0, "Loader", "USB Loader GX", LoaderItems,
							sizeof(LoaderItems) / sizeof(LoaderItems[0]));
}

bool HbcAgent_Home(void)
{
	// The exits go back through the return stub, as from GX's own menu.
	loadStub();

	if (Settings.SilentHomeMenu)
		MusicPlayer::Instance()->SetVolume(0);

	// The overlay draws its own frames and reads the controllers itself;
	// GX's GUI thread must not draw or read them at the same time. It draws
	// into GX's framebuffer that is not on screen: hbc_agent_home() would
	// allocate two, and GX's malloc() puts anything that big in MEM2.
	HaltGui();
	s32 ret = hbc_agent_home_fb(vmode, Video_SpareFramebuffer(), NULL);
	ResumeGui();

	MusicPlayer::Instance()->SetVolume(Settings.volume);
	if (ret < 0)
		gprintf("hbc agent: HOME overlay: %d\n", ret);
	return ret >= 0;
}
