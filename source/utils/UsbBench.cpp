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
 *   -bench=dump     reads only: the FAT32 boot and FSInfo sectors, FAT1
 *                   against FAT2 sector by sector, the root directory and the
 *                   usbbench folder, each flagged when it holds a Bulk-Only
 *                   command block ("USBC") or zeros; then the exit
 *   -benchwrites=1  lets GX write to the drive. Without it every USB write is
 *                   refused in usbstorage2.c before IOS sees it: a cIOS whose
 *                   writes land as junk (d2x fork, 2026-10-24) must not touch it
 *   -benchlimit=S   a step still running after S seconds (default 600) ends
 *                   the run through hbc_agent_fatal_now(), back to HBC
 *   -benchread=K    launch mode: the sequential WDVD_Read size in KiB, 32 to 1024
 *                   (default 1024); 32 MiB are read either way
 *   -benchdiread=1  launch mode: allow WDVD_Read through the DIP after the partition
 *                   opens. Off by default: on the bench drive the first such read, at
 *                   any size, hung IOS itself on stock d2x and on the fork
 *                   (2026-10-24), which no guard or agent can leave; the Wii needed a
 *                   power cycle each time. Without it the launch stops after
 *                   WDVD_OpenPartition.
 *   -benchmem1=1    launch mode: the WDVD_Read buffers come from MEM1 (GX's texture
 *                   pool), as a real launch's apploader reads go to MEM1; by default
 *                   they are memalign(32) blocks, which GX's heap puts in MEM2
 *   -benchone=B     launch mode: one WDVD_Read of B bytes (32 to 1 MiB, a multiple
 *                   of 32) at partition offset 0 instead of the read benchmarks
 *   -benchboot=S    the same for GX's start-up, from main() to the menu
 *                   (default 180): an IOS reload, a USB mount or a prompt
 *                   nobody answers (WiiStation's boot guard, Gamecube/boot_guard.c)
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
#include <dirent.h>
#include <unistd.h>
#include "UsbBench.h"
#include "Controls/DeviceHandler.hpp"
#include "FileOperations/fileops.h"
#include "settings/CSettings.h"
#include "settings/SettingsEnums.h"
#include "usbloader/disc.h"
#include "usbloader/frag.h"
#include "libs/libfat/fatfile_frag.h"
#include "libs/libwbfs/libwbfs.h"
#include "usbloader/GameList.h"
#include "usbloader/usbstorage2.h"
#include "usbloader/wdvd.h"
#include "memory/mem2.h"
#include "libs/libruntimeiospatch/runtimeiospatch.h"
#include "GUI/gui.h"
#include "gecko.h"
#include "menu.h"
#include "sys.h"

#define REVIEW_CONFIG "sd:/apps/usbloader_gx_review/"

static bool launchMode = false, dumpMode = false, writesAllowed = false, diReads = false, mem1Reads = false;
static int skipArg = -1;
static u32 limitS = 600, bootS = 180, readKiB = 1024, oneRead = 0;
static volatile u32 stepLimit = 600;
static bool logOpen = false;

static volatile u64 stepStart = 0;
static const char *volatile stepName = NULL;
static lwp_t watchThread = LWP_THREAD_NULL;

//! The TV: the last lines of the log in a panel over GX's background, so a long
//! wait (a 17 s write, a hung DI read) never looks like a frozen Wii. The GUI
//! thread keeps drawing while the main thread waits in IOS; GuiText::SetText()
//! takes the element's lock, so the bench and its watch thread can both write.
#define SCREEN_LINES 14
static GuiImage *screenPanel = NULL;
static GuiText *screenText[SCREEN_LINES];
static char screenRing[SCREEN_LINES][100];
static volatile bool screenOn = false;

static void ScreenOpen(void)
{
	HaltGui();
	screenPanel = new GuiImage(600, SCREEN_LINES * 26 + 24, (GXColor){0, 0, 0, 210});
	screenPanel->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	screenPanel->SetPosition(20, 40);
	mainWindow->Append(screenPanel);
	for (int i = 0; i < SCREEN_LINES; ++i)
	{
		screenRing[i][0] = 0;
		screenText[i] = new GuiText("", 18, (GXColor){255, 255, 255, 255});
		screenText[i]->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
		screenText[i]->SetPosition(32, 52 + i * 26);
		screenText[i]->SetMaxWidth(576, DOTTED);
		mainWindow->Append(screenText[i]);
	}
	ResumeGui();
	screenOn = true;
}

static void ScreenLine(const char *line)
{
	if (!screenOn)
		return;
	for (int i = 0; i < SCREEN_LINES - 1; ++i)
		memcpy(screenRing[i], screenRing[i + 1], sizeof(screenRing[i]));
	snprintf(screenRing[SCREEN_LINES - 1], sizeof(screenRing[0]), "%.99s", line);
	for (int i = 0; i < SCREEN_LINES; ++i)
		screenText[i]->SetText(screenRing[i]);
}

static void Log(const char *fmt, ...)
{
	char line[256];
	va_list va;
	va_start(va, fmt);
	vsnprintf(line, sizeof(line), fmt, va);
	va_end(va);
	gprintf("usbbench: %s\n", line);
	ScreenLine(line);

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
		u32 limit = stepLimit;
		if (start && ticks_to_secs(diff_ticks(start, gettime())) >= limit)
		{
			//! No Log(): its SD write is an IPC to IOS, and a step that ran over is most
			//! likely IOS stuck (the DI reads of 2026-10-24), which would hold the guard
			//! here for good. The TV and the agent's kept log need no IOS.
			char line[160];
			snprintf(line, sizeof(line), "TIMEOUT: %s still running after %u s; back to HBC in 5 s",
					 stepName ? stepName : "?", limit);
			gprintf("usbbench: %s\n", line);
			ScreenLine(line);
			usleep(5 * 1000 * 1000);
			//! _now: no exit clean-up, which writes the USB cache back and hung on
			//! a cIOS whose writes took 17 s each
			hbc_agent_fatal_now(0x55534242, "usbbench: %s over %u s", stepName ? stepName : "?", limit);
		}
	}
	return NULL;
}

static void Step(const char *name, u32 limit = 0)
{
	stepStart = 0;
	stepName = name;
	stepLimit = limit ? limit : limitS;
	//! On the card before the step runs: a hang only a power cycle ends (IOS itself
	//! stuck, which no guard can leave: the first 1 MiB DI read on 2026-10-24)
	//! still names its step
	if (name)
		Log("> %s", name);
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
		else if (strcmp(argv[i], "-bench=dump") == 0)
			dumpMode = true;
		else if (strcmp(argv[i], "-benchwrites=1") == 0)
			writesAllowed = true;
		else if (strcmp(argv[i], "-benchdiread=1") == 0)
			diReads = true;
		else if (strcmp(argv[i], "-benchmem1=1") == 0)
			mem1Reads = true;
		else if (strncmp(argv[i], "-benchskip=", 11) == 0)
			skipArg = atoi(argv[i] + 11) ? 1 : 0;
		else if (strncmp(argv[i], "-benchlimit=", 12) == 0 && atoi(argv[i] + 12) > 0)
			limitS = atoi(argv[i] + 12);
		else if (strncmp(argv[i], "-benchread=", 11) == 0 && atoi(argv[i] + 11) >= 32 && atoi(argv[i] + 11) <= 1024 &&
				 atoi(argv[i] + 11) % 32 == 0)
			readKiB = atoi(argv[i] + 11);
		else if (strncmp(argv[i], "-benchone=", 10) == 0 && atoi(argv[i] + 10) >= 32 && atoi(argv[i] + 10) <= 1048576 &&
				 atoi(argv[i] + 10) % 32 == 0)
			oneRead = atoi(argv[i] + 10);
		else if (strncmp(argv[i], "-benchboot=", 11) == 0 && atoi(argv[i] + 11) > 0)
			bootS = atoi(argv[i] + 11);
	}
	USBStorage2_BenchReadOnly(!writesAllowed);
	//! The guard from here: GX's start-up has no watchdog until the agent's, and a
	//! hang in it (an IOS reload, a USB mount, a prompt) left the Wii for someone
	//! to take back. hbc_agent_fatal_now() works before hbc_agent_init().
	Step("GX start-up (IOS reload, USB mount, menu)", bootS);
	LWP_CreateThread(&watchThread, Watch, NULL, NULL, 16 * 1024, LWP_PRIO_HIGHEST);
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
		Step("FSInfo write: 1 sector, UMS_WRITE_SECTORS");
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

//! What GX's game scan (Wbfs_Fat::GetHeadersCount()) sees in <usb>:/wbfs, two
//! levels down, read only: for a drive whose games the scan does not find
static void ListWbfs(void)
{
	PartitionHandle *usb = DeviceHandler::GetUSBPartitionCount() > 0 ? DeviceHandler::Instance()->GetUSBHandleFromPartition(0) : NULL;
	const char *mount = usb ? usb->MountName(DeviceHandler::PartitionToPortPartition(0)) : "";
	char base[64];
	snprintf(base, sizeof(base), "%s:/wbfs", mount);
	DIR *dir = mount[0] ? opendir(base) : NULL;
	if (!dir)
	{
		Log("  %s: cannot open", base);
		return;
	}
	int lines = 0;
	struct dirent *d;
	while ((d = readdir(dir)) != NULL && lines < 40)
	{
		if (d->d_name[0] == '.')
			continue;
		char path[320];
		snprintf(path, sizeof(path), "%s/%s", base, d->d_name);
		struct stat st;
		bool isDir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
		Log("  %s%s", d->d_name, isDir ? "/" : "");
		++lines;
		DIR *sub = isDir ? opendir(path) : NULL;
		struct dirent *e;
		while (sub && (e = readdir(sub)) != NULL && lines < 40)
		{
			if (e->d_name[0] == '.')
				continue;
			char file[600];
			snprintf(file, sizeof(file), "%s/%s", path, e->d_name);
			Log("    %s, %lld bytes", e->d_name, stat(file, &st) == 0 ? (long long)st.st_size : -1LL);
			++lines;
		}
		if (sub)
			closedir(sub);
	}
	closedir(dir);
	Log("  (%d lines listed)", lines);
}


//! A .wbfs file GX's scan does not name (wbfs/<Title>/<ID6>.wbfs): its
//! fragment list built the way get_frag_list_for_file() builds one, from reads
//! only. libfat maps the file's clusters; the WBFS header and block table come
//! from the file itself, opened read-only; frag_remap() joins the two.
static FragList *FileFragList(const char *path, u8 *id)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	u8 head[512];
	FragList *fa = NULL, *fw = NULL, *list = NULL;
	u16 *wlba = NULL;
	if (fread(head, 1, sizeof(head), f) != sizeof(head) || memcmp(head, "WBFS", 4) != 0)
	{
		Log("  %s: no WBFS header", path);
		fclose(f);
		return NULL;
	}
	u32 hdSz = 1u << head[8], wbfsSz = 1u << head[9];
	u32 perDisc = (143432u * 2) >> (head[9] - 15); // n_wbfs_sec_per_disc, as libwbfs sets it
	u32 nlb = wbfsSz / 512;
	wlba = (u16 *)malloc(perDisc * 2);
	u8 disc[0x100];
	if (hdSz < 512 || wbfsSz < 0x8000 || !wlba || fseek(f, hdSz, SEEK_SET) != 0 ||
		fread(disc, 1, sizeof(disc), f) != sizeof(disc) || fread(wlba, 2, perDisc, f) != perDisc)
	{
		Log("  %s: WBFS disc table unreadable (hd %u, wbfs %u)", path, hdSz, wbfsSz);
		goto out;
	}
	fclose(f);
	f = NULL;
	memcpy(id, disc, 6);

	fa = (FragList *)malloc(sizeof(FragList));
	fw = (FragList *)malloc(sizeof(FragList));
	list = (FragList *)memalign(32, (sizeof(FragList) + 31) & ~31);
	if (!fa || !fw || !list)
		goto fail;
	frag_init(fa, MAX_FRAG);
	if (_FAT_get_fragments(path, &frag_append, fa) != 0)
	{
		Log("  %s: libfat gave no fragments", path);
		goto fail;
	}
	{
		frag_init(fw, MAX_FRAG);
		u32 last = 0;
		for (u32 i = 0; i < perDisc; ++i)
		{
			u32 w = wlba[i]; // big-endian on disc, as the Wii reads it
			if (!w)
				continue;
			if (frag_append(fw, i * nlb, w * nlb, nlb))
				goto fail;
			last = i;
		}
		if (last < perDisc / 2)
			last = perDisc / 2;
		frag_append(fw, last * nlb, 0, 0); // the size, as wbfs_get_fragments() sets it
	}
	frag_init(list, MAX_FRAG);
	if (frag_remap(list, fw, fa) != 0)
	{
		Log("  %s: frag_remap failed", path);
		goto fail;
	}
	Log("  %s: WBFS sector %u bytes, %u file fragments, %u disc fragments", path, wbfsSz, fa->num, list->num);
	goto out;
fail:
	free(list);
	list = NULL;
out:
	if (f)
		fclose(f);
	free(wlba);
	free(fa);
	free(fw);
	return list;
}

//! The first <usb>:/wbfs/*/<ID6>.wbfs, for a drive whose layout GX's scan skips
static bool FindWbfsFile(char *path, size_t size)
{
	PartitionHandle *usb = DeviceHandler::GetUSBPartitionCount() > 0 ? DeviceHandler::Instance()->GetUSBHandleFromPartition(0) : NULL;
	const char *mount = usb ? usb->MountName(DeviceHandler::PartitionToPortPartition(0)) : "";
	char base[64];
	snprintf(base, sizeof(base), "%s:/wbfs", mount);
	DIR *dir = mount[0] ? opendir(base) : NULL;
	bool found = false;
	struct dirent *d;
	while (dir && !found && (d = readdir(dir)) != NULL)
	{
		if (d->d_name[0] == '.')
			continue;
		char sub[320];
		snprintf(sub, sizeof(sub), "%s/%s", base, d->d_name);
		DIR *in = opendir(sub);
		struct dirent *e;
		while (in && !found && (e = readdir(in)) != NULL)
		{
			const char *dot = strrchr(e->d_name, '.');
			if (dot && dot - e->d_name == 6 && strcasecmp(dot, ".wbfs") == 0)
			{
				snprintf(path, size, "%s/%.11s", sub, e->d_name); // <ID6>.wbfs
				found = true;
			}
		}
		if (in)
			closedir(in);
	}
	if (dir)
		closedir(dir);
	return found;
}

//! A DI read buffer: MEM1 from GX's texture pool with -benchmem1=1, else MEM2.
//! The pool is for textures because boot paths overwrite it after AppCleanUp()
//! closes it; a buffer taken and freed while the menu runs is safe there.
static u8 *ReadBuffer(u32 size)
{
	u8 *p = (u8 *)(mem1Reads ? TEX_alloc(size) : memalign(32, size));
	u32 a = (u32)p;
	bool mem1 = a >= 0x80000000 && a < 0x81800000;
	if (p && mem1Reads && !mem1)
	{
		Log("read buffer: no %u bytes free in MEM1 (got 0x%08x); not reading", size, a);
		free(p);
		return NULL;
	}
	if (p)
		Log("read buffer: %u bytes at 0x%08x (%s)", size, a, mem1 ? "MEM1" : "MEM2");
	return p;
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
	u64 t;
	u32 us;
	int ret;
	if (game)
	{
		Log("launch: %.6s \"%.40s\" on partition %d (%s)", game->id, game->title, gameList.GetPartitionNumber(game->id),
			DeviceHandler::GetFSName(USB1 + gameList.GetPartitionNumber(game->id)));

		Step("fragment list");
		t = gettime();
		ret = get_frag_list(game->id);
		Log("fragment list built: %d in %s", ret, Ms(UsSince(t)));
		if (ret < 0)
			return;

		Step("set fragment list: WDVD_SetFragList, then a 32-byte disc ID read");
		DeviceHandler::Instance()->UnMountSD();
		t = gettime();
		ret = set_frag_list(game->id, false);
		us = UsSince(t);
		DeviceHandler::Instance()->MountSD();
		Log("fragment list sent to the DIP: %d in %s", ret, Ms(us));
		if (ret < 0)
			return;
	}
	else
	{
		//! GX's scan skips wbfs/<Title>/<ID6>.wbfs: build the same list from the file
		Log("launch: GX's scan found no Wii game (partition %d, MultiplePartitions %d); the files:",
			Settings.partition, Settings.MultiplePartitions);
		ListWbfs();
		char path[400];
		u8 id[8] = {0};
		if (!FindWbfsFile(path, sizeof(path)))
		{
			Log("launch: SKIPPED, no <usb>:/wbfs/*/<ID6>.wbfs");
			return;
		}
		Step("fragment list");
		t = gettime();
		FragList *list = FileFragList(path, id);
		Log("fragment list built from %s (%.6s): %s in %s", path, id, list ? "ok" : "FAILED", Ms(UsSince(t)));
		if (!list)
			return;

		//! As set_frag_list() sends it, and the same disc ID check after
		Step("set fragment list: WDVD_SetFragList, then a 32-byte disc ID read");
		DeviceHandler::Instance()->UnMountSD();
		t = gettime();
		ret = WDVD_SetFragList(WBFS_DEVICE_USB, list, sizeof(Fragment) * (list->num + 1));
		char discid[32] ATTRIBUTE_ALIGN(32);
		memset(discid, 0, sizeof(discid));
		if (ret == 0)
			ret = WDVD_UnencryptedRead(discid, 32, 0) < 0 ? -4 : strncasecmp((char *)id, discid, 6) != 0 ? -3 : 0;
		us = UsSince(t);
		DeviceHandler::Instance()->MountSD();
		free(list);
		Log("fragment list sent to the DIP: %d (disc ID read back %.6s) in %s", ret, discid, Ms(us));
		if (ret < 0)
			return;
	}

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

	char what[100];
	if (!diReads)
	{
		Log("partition reads: SKIPPED. They hung IOS on this drive (needs a power cycle); -benchdiread=1 allows them");
		WDVD_ClosePartition();
		Step(NULL);
		return;
	}
	if (oneRead)
	{
		//! -benchone=BYTES: one WDVD_Read at partition offset 0 and nothing more, so a
		//! hang names its size (every read size hung IOS on 2026-10-24)
		u8 *one = ReadBuffer(oneRead);
		if (one)
		{
			snprintf(what, sizeof(what), "one WDVD_Read of %u bytes at partition offset 0 through the DIP", oneRead);
			Step(what);
			t = gettime();
			ret = WDVD_Read(one, oneRead, 0);
			Log("one partition read of %u bytes: %d in %s; first bytes %02x%02x%02x%02x", oneRead, ret,
				Ms(UsSince(t)), one[0], one[1], one[2], one[3]);
			free(one);
		}
		WDVD_ClosePartition();
		Step(NULL);
		return;
	}

	const u32 chunk = readKiB * 1024, total = 32 * 1024 * 1024;
	u8 *buf = ReadBuffer(chunk);
	if (buf)
	{
		snprintf(what, sizeof(what), "sequential reads: %u x WDVD_Read of %u KiB through the DIP", total / chunk, readKiB);
		Step(what);
		u32 failed = 0;
		t = gettime();
		for (u32 off = 0; off < total; off += chunk)
			failed += WDVD_Read(buf, chunk, off) < 0;
		us = UsSince(t);
		Log("partition read, %u x %u KiB from offset 0: %u failed, %s, %u KiB/s", total / chunk, readKiB, failed, Ms(us),
			us ? (u32)((u64)total * 1000000 / 1024 / us) : 0);

		//! The same offsets every run: a fixed LCG over the first 256 MiB, 32 KiB aligned
		Step("random reads: 128 x WDVD_Read of 32 KiB through the DIP");
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

//! -bench=dump: what a cIOS's writes left on the drive, read only

static u32 Le16(const u8 *p)
{
	return p[0] | (p[1] << 8);
}

static u32 Le32(const u8 *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

static const char *Kind(const u8 *s)
{
	if (memcmp(s, "USBC", 4) == 0)
		return "USBC";
	for (int i = 0; i < 512; ++i)
		if (s[i])
			return "data";
	return "zeros";
}

//! The first 32 bytes as hex
static const char *Hex32(const u8 *s)
{
	static char buf[2][72];
	static int n = 0;
	char *b = buf[n++ & 1];
	for (int i = 0; i < 32; ++i)
		snprintf(b + i * 2, 3, "%02x", s[i]);
	return b;
}

static void DumpSector(const DISC_INTERFACE *io, u8 *buf, u32 lba, const char *what)
{
	if (!io->readSectors(lba, 1, buf))
		Log("LBA %u %s: READ FAILED", lba, what);
	else
		Log("LBA %u %s: %s %s", lba, what, Kind(buf), Hex32(buf));
}

//! One FAT entry, from the FAT that starts at fat
static u32 FatEntry(const DISC_INTERFACE *io, u8 *buf, u32 fat, u32 cluster)
{
	if (!io->readSectors(fat + cluster / 128, 1, buf))
		return 0xFFFFFFFF;
	return Le32(buf + (cluster % 128) * 4) & 0x0FFFFFFF;
}

static void Dump(void)
{
	PartitionHandle *usb = DeviceHandler::GetUSBPartitionCount() > 0 ? DeviceHandler::Instance()->GetUSBHandleFromPartition(0) : NULL;
	int part = DeviceHandler::PartitionToPortPartition(0);
	const DISC_INTERFACE *io = usb ? usb->GetDiscInterface() : NULL;
	const u32 chunk = 64;
	u8 *a = (u8 *)memalign(32, chunk * 512), *b = (u8 *)memalign(32, chunk * 512);
	if (!io || !a || !b)
	{
		Log("dump: SKIPPED, no USB partition or memory");
		free(a);
		free(b);
		return;
	}
	u32 lba = usb->GetLBAStart(part);
	Step("dump boot sector");
	if (!io->readSectors(lba, 1, a))
	{
		Log("dump: boot sector %u READ FAILED", lba);
		free(a);
		free(b);
		Step(NULL);
		return;
	}
	u32 bps = Le16(a + 11), spc = a[13], res = Le16(a + 14), nfats = a[16];
	u32 fatsz = Le32(a + 0x24), rootclus = Le32(a + 0x2C), fsinfo = Le16(a + 0x30), backup = Le16(a + 0x32);
	Log("dump: boot sector %u: %s; %u bytes/sector, %u sectors/cluster, %u reserved, %u FATs of %u sectors, "
		"root cluster %u, FSInfo +%u, backup +%u",
		lba, Kind(a), bps, spc, res, nfats, fatsz, rootclus, fsinfo, backup);
	if (bps != 512 || !spc || (spc & (spc - 1)) || nfats < 1 || nfats > 2 || !fatsz || rootclus < 2 ||
		memcmp(a + 0x52, "FAT32", 5) != 0)
	{
		Log("dump: no FAT32 BPB here; boot sector %s", Hex32(a));
		free(a);
		free(b);
		Step(NULL);
		return;
	}
	DumpSector(io, a, lba + fsinfo, "FSInfo");
	DumpSector(io, a, lba + backup, "backup boot sector");
	DumpSector(io, a, lba + backup + 1, "backup FSInfo");

	u32 fat1 = lba + res, fat2 = fat1 + fatsz, data = fat1 + nfats * fatsz;

	//! FAT1 against FAT2: libfat writes both copies, so a sector that differs, or
	//! holds a command block, is where a write went wrong
	Step("dump FATs");
	u32 differ = 0, usbc = 0, failed = 0;
	u64 t = gettime();
	for (u32 i = 0; i < fatsz; i += chunk)
	{
		u32 n = fatsz - i < chunk ? fatsz - i : chunk;
		bool okA = io->readSectors(fat1 + i, n, a);
		bool okB = nfats < 2 || io->readSectors(fat2 + i, n, b);
		if (!okA || !okB)
		{
			failed += n;
			continue;
		}
		for (u32 j = 0; j < n; ++j)
		{
			const u8 *sa = a + j * 512, *sb = b + j * 512;
			bool ca = memcmp(sa, "USBC", 4) == 0, cb = nfats == 2 && memcmp(sb, "USBC", 4) == 0;
			bool d = nfats == 2 && memcmp(sa, sb, 512) != 0;
			usbc += ca + cb;
			differ += d;
			if ((d || ca || cb) && differ + usbc <= 40)
				Log("FAT sector %u: FAT1 LBA %u %s %s | FAT2 LBA %u %s %s", i + j, fat1 + i + j, Kind(sa), Hex32(sa),
					fat2 + i + j, nfats == 2 ? Kind(sb) : "-", nfats == 2 ? Hex32(sb) : "-");
		}
	}
	Log("dump: FATs compared, %u sectors each: %u differ, %u start with USBC, %u unreadable, in %s", fatsz, differ,
		usbc, failed, Ms(UsSince(t)));
	if (io->readSectors(fat1, 1, a))
		Log("FAT1 sector 0 entries 0-1: %08x %08x (expect 0ffffff8 0fffffff, or with clean flags)", Le32(a),
			Le32(a + 4));

	//! The root directory: each sector's kind, its short names, and usbbench
	Step("dump root directory");
	u32 cluster = rootclus, folder = 0, names = 0, folderLba = 0;
	char list[200] = "";
	for (int c = 0; c < 8 && cluster >= 2 && cluster < 0x0FFFFFF7; ++c)
	{
		u32 first = data + (cluster - 2) * spc;
		for (u32 sct = 0; sct < spc; ++sct)
		{
			if (!io->readSectors(first + sct, 1, a))
			{
				Log("root LBA %u: READ FAILED", first + sct);
				continue;
			}
			const char *k = Kind(a);
			if (strcmp(k, "zeros") != 0 || sct == 0)
				Log("root cluster %u sector %u, LBA %u: %s %s", cluster, sct, first + sct, k, Hex32(a));
			for (int e = 0; e < 16; ++e)
			{
				const u8 *ent = a + e * 32;
				if (ent[0] == 0 || ent[0] == 0xE5 || ent[11] == 0x0F)
					continue;
				++names;
				if (strlen(list) + 14 < sizeof(list))
				{
					char name[16];
					snprintf(name, sizeof(name), "%.8s.%.3s ", (const char *)ent, (const char *)ent + 8);
					strcat(list, name);
				}
				if (memcmp(ent, "USBBENCH   ", 11) == 0)
				{
					folder = (Le16(ent + 20) << 16) | Le16(ent + 26);
					folderLba = first + sct;
				}
			}
		}
		cluster = FatEntry(io, a, fat1, cluster);
	}
	Log("root directory: %u entries: %s", names, list);

	if (!folder)
		Log("usbbench folder: no entry in the root directory");
	else
	{
		Step("dump usbbench folder");
		u32 first = data + (folder - 2) * spc;
		u32 e1 = FatEntry(io, a, fat1, folder), e2 = nfats == 2 ? FatEntry(io, a, fat2, folder) : 0;
		Log("usbbench folder: entry in LBA %u, cluster %u at LBA %u; FAT1 entry %08x, FAT2 entry %08x", folderLba,
			folder, first, e1, e2);
		Log("usbbench FAT entries live in FAT1 LBA %u and FAT2 LBA %u", fat1 + folder / 128, fat2 + folder / 128);
		for (u32 sct = 0; sct < spc; ++sct)
			DumpSector(io, a, first + sct, sct == 0 ? "usbbench sector 0 (expect . and ..)" : "usbbench");
	}
	Step(NULL);
	free(a);
	free(b);
}

void UsbBench_Run(void)
{
	bool skip = skipArg >= 0 ? skipArg == 1 : launchMode;
	PartitionHandle *usb = DeviceHandler::GetUSBPartitionCount() > 0 ? DeviceHandler::Instance()->GetUSBHandleFromPartition(0) : NULL;
	int part = DeviceHandler::PartitionToPortPartition(0);

	ScreenOpen();
	Log("USB Loader GX cIOS bench, mode %s, USB writes %s, exit unmount %s, step limit %u s",
		dumpMode ? "dump" : launchMode ? "launch" : "io", writesAllowed ? "ALLOWED" : "refused (read-only)",
		skip ? "skips unchanged sectors (default)" : "writes everything (no skip)", limitS);
	//! AHB access: the agent's last way out of a wedged IOS (HW_RESETS) needs it
	Log("IOS %d r%d (loader IOS setting %d)%s, AHB access %s", IOS_GetVersion(), IOS_GetRevision(),
		Settings.LoaderIOS, IOS_GetVersion() == 249 ? "" : " -- NOT cIOS 249", AHBPROT_DISABLED ? "yes" : "no");
	//! The refusal is in usbstorage2.c, the cIOS driver; IOS58's libogc driver has none
	if (!writesAllowed && IOS_GetVersion() < 200)
	{
		Log("not on a cIOS, so writes cannot be refused: back to HBC in 5 s, no clean-up");
		usleep(5 * 1000 * 1000);
		hbc_agent_fatal_now(0x55534242, "usbbench: IOS %d, read-only needs a cIOS", IOS_GetVersion());
	}
	Log("USB partitions: %d; partition 0: %s, mounted as %s:, LBA %u, %u sectors", DeviceHandler::GetUSBPartitionCount(),
		usb ? usb->GetFSName(part) : "none", usb ? usb->MountName(part) : "-", usb ? usb->GetLBAStart(part) : 0,
		usb ? usb->GetSecCount(part) : 0);
	Writes("at start");

	Step(NULL);

	if (dumpMode)
		Dump();
	else
	{
		GameListLoad();
		if (launchMode)
			LaunchReads();
		else
		{
			SectorWrite();
			SettingsSave();
		}
	}
	if (!writesAllowed)
		Log("USB writes refused so far: %u", USBStorage2_BenchRefused());

	//! (b) The exit: its unmount is timed in exit.log, as on every exit
	USBStorage2_BenchNoSkip(!skip);
	Log("exit: back to HBC in 5 s; exit.log times the unmount");
	usleep(5 * 1000 * 1000); // the result stays on the TV a moment
	screenOn = false;        // the exit takes the GUI down
	//! The watch stays on: an unmount past the limit leaves exit.log at that step
	Step("exit");
	Sys_BackToLoader();
}

#endif
