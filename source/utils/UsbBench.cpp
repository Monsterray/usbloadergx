/****************************************************************************
 * UsbBench: times GX's USB work on a real Wii, to compare one cIOS in slot
 * 249 with another (scripts/usb_bench.py). Built only with -DUSB_BENCH
 * (scripts/build.sh bench); a release build has none of it.
 *
 * Arguments (after HBC's argv[0]; pass -ios=249 too):
 *   -bench=io       (default) the game list load from USB, one FSInfo sector
 *                   read and written back three times with the same bytes,
 *                   GXGlobal.cfg saved to <usb>:/usbbench/ twice, then the exit
 *   -bench=launch   the game list load, then what a Wii game's launch asks of
 *                   the cIOS before the apploader: the fragment list, the
 *                   DIP's USB mode, the disc and partition opened, and timed
 *                   partition reads through the DIP; then the exit
 *   -benchskip=0|1  whether the exit's unmount skips writes of unchanged
 *                   sectors (4029590c, GX's default). Default: 0 for io, so
 *                   the unmount does the writes libfat asks for, 1 for launch
 *   -benchlimit=S   a step still running after S seconds (default 600) ends
 *                   the run through hbc_agent_fatal_now(), back to HBC
 *
 * Each line goes to gprintf() (the hbc netlog) and to <config>/usbbench.log,
 * reopened per line; the exit's own steps go to exit.log as always.
 ***************************************************************************/
#ifdef USB_BENCH

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <hbc_agent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>
#include "UsbBench.h"
#include "Controls/DeviceHandler.hpp"
#include "FileOperations/fileops.h"
#include "settings/CSettings.h"
#include "settings/SettingsEnums.h"
#include "usbloader/disc.h"
#include "usbloader/frag.h"
#include "usbloader/GameList.h"
#include "usbloader/usbstorage2.h"
#include "usbloader/wdvd.h"
#include "gecko.h"
#include "sys.h"

#define REVIEW_CONFIG "sd:/apps/usbloader_gx_review/"

static bool launchMode = false;
static int skipArg = -1;
static u32 limitS = 600;
static bool logOpen = false;

static volatile u64 stepStart = 0;
static const char *volatile stepName = NULL;
static lwp_t watchThread = LWP_THREAD_NULL;

static void Log(const char *fmt, ...)
{
	char line[256];
	va_list va;
	va_start(va, fmt);
	vsnprintf(line, sizeof(line), fmt, va);
	va_end(va);
	gprintf("usbbench: %s\n", line);

	char path[sizeof(Settings.ConfigPath) + 16];
	snprintf(path, sizeof(path), "%susbbench.log", Settings.ConfigPath);
	FILE *f = fopen(path, logOpen ? "a" : "w");
	if (!f)
		return;
	logOpen = true;
	fprintf(f, "%s\n", line);
	fclose(f);
}

static u32 UsSince(u64 start)
{
	return ticks_to_microsecs(diff_ticks(start, gettime()));
}

//! "12.345 ms" from microseconds
static const char *Ms(u32 us)
{
	static char buf[4][24];
	static int n = 0;
	char *b = buf[n++ & 3];
	snprintf(b, sizeof(buf[0]), "%u.%03u ms", us / 1000, us % 1000);
	return b;
}

static void *Watch(void *)
{
	while (true)
	{
		usleep(1000 * 1000);
		u64 start = stepStart;
		if (start && ticks_to_secs(diff_ticks(start, gettime())) >= limitS)
		{
			Log("TIMEOUT: %s still running after %u s; back to HBC", stepName ? stepName : "?", limitS);
			//! _now: no exit clean-up, which writes the USB cache back and hung on
			//! a cIOS whose writes took 17 s each
			hbc_agent_fatal_now(0x55534242, "usbbench: %s over %u s", stepName ? stepName : "?", limitS);
		}
	}
	return NULL;
}

static void Step(const char *name)
{
	stepName = name;
	stepStart = name ? gettime() : 0;
}

static void Writes(const char *when)
{
	u32 calls, sectors, failed, ms, maxUs;
	USBStorage2_BenchWrites(&calls, &sectors, &failed, &ms, &maxUs);
	Log("  USB writes so far (%s): %u calls, %u sectors, %u failed, %u ms, longest %s", when, calls, sectors,
		failed, ms, Ms(maxUs));
}

void UsbBench_Init(int argc, char *argv[])
{
	for (int i = 1; i < argc; ++i)
	{
		if (!argv[i])
			continue;
		if (strcmp(argv[i], "-bench=launch") == 0)
			launchMode = true;
		else if (strncmp(argv[i], "-benchskip=", 11) == 0)
			skipArg = atoi(argv[i] + 11) ? 1 : 0;
		else if (strncmp(argv[i], "-benchlimit=", 12) == 0 && atoi(argv[i] + 12) > 0)
			limitS = atoi(argv[i] + 12);
	}
	//! A wiiload start passes only the file's name: use the review build's folder
	//! (scripts/wii_install.py), never an installed release's.
	if (argc < 1 || !argv || !argv[0] || !strchr(argv[0], ':'))
		snprintf(Settings.ConfigPath, sizeof(Settings.ConfigPath), "%s", REVIEW_CONFIG);
}

//! (d) The game list, read from the drive with no cache, then as the GUI loads it
static void GameListLoad(void)
{
	for (int pass = 1; pass <= 2; ++pass)
	{
		Step("game list read");
		u64 t = gettime();
		int n = gameList.ReadGameList(false);
		Log("game list read from USB, no cache, pass %d: %d games in %s", pass, n, Ms(UsSince(t)));
	}
	Step("game list load");
	u64 t = gettime();
	int n = gameList.LoadUnfiltered();
	Log("game list load (cached, as the menu does): %d games in %s", n, Ms(UsSince(t)));
	Step(NULL);
}

//! (a) One sector written alone: the FSInfo sector, with the bytes it holds
static void SectorWrite(void)
{
	PartitionHandle *usb = DeviceHandler::GetUSBPartitionCount() > 0 ? DeviceHandler::Instance()->GetUSBHandleFromPartition(0) : NULL;
	int part = DeviceHandler::PartitionToPortPartition(0);
	if (!usb || strncmp(usb->GetFSName(part), "FAT32", 5) != 0)
	{
		Log("sector write: SKIPPED, USB partition 0 is %s, not FAT32", usb ? usb->GetFSName(part) : "missing");
		return;
	}
	const DISC_INTERFACE *io = usb->GetDiscInterface();
	u32 lba = usb->GetLBAStart(part);
	u8 *sector = (u8 *)memalign(32, 4096);
	u8 *check = (u8 *)memalign(32, 4096);
	if (!io || !sector || !check)
	{
		Log("sector write: SKIPPED, no interface or memory");
		free(sector);
		free(check);
		return;
	}

	Step("boot sector read");
	u64 t = gettime();
	bool ok = io->readSectors(lba, 1, sector);
	Log("boot sector %u read %s in %s", lba, ok ? "ok" : "FAILED", Ms(UsSince(t)));
	u32 fsinfo = ok ? lba + (sector[0x30] | (sector[0x31] << 8)) : 0;

	Step("FSInfo read");
	t = gettime();
	ok = fsinfo > lba && io->readSectors(fsinfo, 1, sector);
	Log("FSInfo sector %u read %s in %s", fsinfo, ok ? "ok" : "FAILED", Ms(UsSince(t)));
	//! "RRaA" at 0 and "rrAa" at 0x1E4: an FSInfo sector, nothing else gets written
	if (!ok || memcmp(sector, "RRaA", 4) != 0 || memcmp(sector + 0x1E4, "rrAa", 4) != 0)
	{
		Log("sector write: SKIPPED, no FSInfo signature");
		Step(NULL);
		free(sector);
		free(check);
		return;
	}

	for (int i = 1; i <= 3; ++i)
	{
		Step("FSInfo write");
		t = gettime();
		bool written = io->writeSectors(fsinfo, 1, sector);
		Log("single-sector write %d (FSInfo, same bytes, no skip): %s in %s", i, written ? "ok" : "FAILED",
			Ms(UsSince(t)));
	}

	Step("FSInfo read back");
	t = gettime();
	ok = io->readSectors(fsinfo, 1, check);
	Log("FSInfo read back %s in %s, %s", ok ? "ok" : "FAILED", Ms(UsSince(t)),
		ok && memcmp(sector, check, 512) == 0 ? "bytes unchanged" : "BYTES DIFFER");
	if (ok && memcmp(sector, check, 512) != 0)
	{
		//! Which bytes the drive gave back changed: written -> read, the first 8 of them
		int n = 0;
		for (int i = 0; i < 512; ++i)
		{
			if (sector[i] == check[i])
				continue;
			if (n < 8)
				Log("  offset 0x%03x: wrote %02x, read %02x", i, sector[i], check[i]);
			++n;
		}
		Log("  %d of 512 bytes differ; read back starts %02x%02x%02x%02x, ends at 0x1fc %02x%02x%02x%02x", n,
			check[0], check[1], check[2], check[3], check[0x1fc], check[0x1fd], check[0x1fe], check[0x1ff]);
	}
	Step(NULL);
	free(sector);
	free(check);
	Writes("after the sector writes");
}

//! (c) GXGlobal.cfg saved by Settings.Save() into a folder on the USB drive
static void SettingsSave(void)
{
	PartitionHandle *usb = DeviceHandler::GetUSBPartitionCount() > 0 ? DeviceHandler::Instance()->GetUSBHandleFromPartition(0) : NULL;
	const char *mount = usb ? usb->MountName(DeviceHandler::PartitionToPortPartition(0)) : "";
	if (!mount[0])
	{
		Log("settings save: SKIPPED, USB partition 0 not mounted");
		return;
	}
	char config[sizeof(Settings.ConfigPath)], boot[sizeof(Settings.BootDevice)];
	strlcpy(config, Settings.ConfigPath, sizeof(config));
	strlcpy(boot, Settings.BootDevice, sizeof(boot));

	char folder[64];
	snprintf(folder, sizeof(folder), "%s:/usbbench/", mount);
	Step("usbbench folder");
	u64 t = gettime();
	bool ok = CreateSubfolder(folder);
	Log("folder %s %s in %s", folder, ok ? "ok" : "FAILED", Ms(UsSince(t)));

	for (int i = 1; i <= 2 && ok; ++i)
	{
		strlcpy(Settings.ConfigPath, folder, sizeof(Settings.ConfigPath));
		Step("settings save");
		t = gettime();
		bool saved = Settings.Save();
		u32 us = UsSince(t);
		strlcpy(Settings.ConfigPath, config, sizeof(Settings.ConfigPath));
		strlcpy(Settings.BootDevice, boot, sizeof(Settings.BootDevice));
		char file[80];
		snprintf(file, sizeof(file), "%sGXGlobal.cfg", folder);
		struct stat st;
		Log("settings save %d to %s: %s in %s, %lld bytes", i, file, saved ? "ok" : "FAILED", Ms(us),
			stat(file, &st) == 0 ? (long long)st.st_size : -1LL);
	}
	Step(NULL);
	Writes("after the settings saves");
}

//! (e) What a USB Wii game's launch asks of the cIOS before its apploader runs
//! (GameBooter::SetupDisc() and BootPartition()), then partition reads through
//! the DIP. Nothing is booted.
static void LaunchReads(void)
{
	struct discHdr *game = NULL;
	for (int i = 0; i < gameList.size() && !game; ++i)
		if (gameList[i] && gameList[i]->type == TYPE_GAME_WII_IMG)
			game = gameList[i];
	if (!game)
	{
		Log("launch: SKIPPED, no Wii game on USB");
		return;
	}
	Log("launch: %.6s \"%.40s\" on partition %d (%s)", game->id, game->title, gameList.GetPartitionNumber(game->id),
		DeviceHandler::GetFSName(USB1 + gameList.GetPartitionNumber(game->id)));

	Step("fragment list");
	u64 t = gettime();
	int ret = get_frag_list(game->id);
	Log("fragment list built: %d in %s", ret, Ms(UsSince(t)));
	if (ret < 0)
		return;

	Step("set fragment list");
	DeviceHandler::Instance()->UnMountSD();
	t = gettime();
	ret = set_frag_list(game->id, false);
	u32 us = UsSince(t);
	DeviceHandler::Instance()->MountSD();
	Log("fragment list sent to the DIP: %d in %s", ret, Ms(us));
	if (ret < 0)
		return;

	Step("Disc_Open");
	t = gettime();
	ret = Disc_Open(false);
	Log("Disc_Open: %d in %s", ret, Ms(UsSince(t)));
	if (ret < 0)
		return;

	u64 offset = 0;
	Step("find partition");
	t = gettime();
	ret = Disc_FindPartition(&offset);
	Log("Disc_FindPartition: %d, offset 0x%llx, in %s", ret, offset, Ms(UsSince(t)));
	if (ret < 0)
		return;

	Step("open partition");
	t = gettime();
	ret = WDVD_OpenPartition(offset, NULL);
	Log("WDVD_OpenPartition: %d in %s", ret, Ms(UsSince(t)));
	if (ret < 0)
		return;

	const u32 chunk = 1024 * 1024, total = 32 * chunk;
	u8 *buf = (u8 *)memalign(32, chunk);
	if (buf)
	{
		Step("sequential reads");
		u32 failed = 0;
		t = gettime();
		for (u32 off = 0; off < total; off += chunk)
			failed += WDVD_Read(buf, chunk, off) < 0;
		us = UsSince(t);
		Log("partition read, 32 x 1 MiB from offset 0: %u failed, %s, %u KiB/s", failed, Ms(us),
			us ? (u32)((u64)total * 1000000 / 1024 / us) : 0);

		//! The same offsets every run: a fixed LCG over the first 256 MiB, 32 KiB aligned
		Step("random reads");
		u32 seed = 0x55534242, maxUs = 0;
		failed = 0;
		t = gettime();
		for (int i = 0; i < 128; ++i)
		{
			seed = seed * 1664525 + 1013904223;
			u64 off = (u64)((seed >> 8) % 8192) * 32768;
			u64 r = gettime();
			failed += WDVD_Read(buf, 32768, off) < 0;
			u32 one = UsSince(r);
			if (one > maxUs)
				maxUs = one;
		}
		us = UsSince(t);
		Log("partition read, 128 x 32 KiB at fixed random offsets: %u failed, %s, mean %s, longest %s", failed,
			Ms(us), Ms(us / 128), Ms(maxUs));
		free(buf);
	}
	WDVD_ClosePartition();
	Step(NULL);
}

void UsbBench_Run(void)
{
	bool skip = skipArg >= 0 ? skipArg == 1 : launchMode;
	PartitionHandle *usb = DeviceHandler::GetUSBPartitionCount() > 0 ? DeviceHandler::Instance()->GetUSBHandleFromPartition(0) : NULL;
	int part = DeviceHandler::PartitionToPortPartition(0);

	Log("USB Loader GX cIOS bench, mode %s, exit unmount %s, step limit %u s", launchMode ? "launch" : "io",
		skip ? "skips unchanged sectors (default)" : "writes everything (no skip)", limitS);
	Log("IOS %d r%d (loader IOS setting %d)%s", IOS_GetVersion(), IOS_GetRevision(), Settings.LoaderIOS,
		IOS_GetVersion() == 249 ? "" : " -- NOT cIOS 249");
	Log("USB partitions: %d; partition 0: %s, mounted as %s:, LBA %u, %u sectors", DeviceHandler::GetUSBPartitionCount(),
		usb ? usb->GetFSName(part) : "none", usb ? usb->MountName(part) : "-", usb ? usb->GetLBAStart(part) : 0,
		usb ? usb->GetSecCount(part) : 0);
	Writes("at start");

	LWP_CreateThread(&watchThread, Watch, NULL, NULL, 16 * 1024, LWP_PRIO_HIGHEST);

	GameListLoad();
	if (launchMode)
		LaunchReads();
	else
	{
		SectorWrite();
		SettingsSave();
	}

	//! (b) The exit: its unmount is timed in exit.log, as on every exit
	USBStorage2_BenchNoSkip(!skip);
	Log("exit: back to HBC; exit.log times the unmount");
	//! The watch stays on: an unmount past the limit leaves exit.log at that step
	Step("exit");
	Sys_BackToLoader();
}

#endif
