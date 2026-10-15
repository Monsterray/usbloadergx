#include <gccore.h>
#include <ogcsys.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ogc/machine/processor.h>
#include <wiiuse/wpad.h>
#include <vector>
#include <string>
#include "Controls/DeviceHandler.hpp"
#include "settings/CSettings.h"
#include "system/IosLoader.h"
#include "lstub.h"
#include "sys.h"
#include "gecko.h"
#include "app_booter_bin.h"
#include "usbloader/disc.h"
#include "memory/mem2.h"

#define EXECUTE_ADDR ((u8 *)0x92000000)
#define BOOTER_ADDR ((u8 *)0x93000000)
#define ARGS_ADDR ((u8 *)0x93200000)
// The homebrew may not reach the address the app booter is copied to.
#define MAX_HOMEBREW_SIZE ((u32)(BOOTER_ADDR - EXECUTE_ADDR))
// The arguments must stay below libogc's IPC buffer at 0x933E0000
#define MAX_ARGS_SIZE 0x100000

//! GX's MEM2 heap covers EXECUTE_ADDR, BOOTER_ADDR and ARGS_ADDR. The image waits
//! in a heap block and moves to EXECUTE_ADDR only in RunAppbooter(), after the last
//! allocation: it was copied there while GX still ran, over whatever the heap kept.

typedef void (*entrypoint)(void);

extern bool isWiiVC; // in sys.cpp

static u8 *homebrewbuffer = NULL;
static u32 homebrewsize = 0;
static std::vector<std::string> Arguments;

void AddBootArgument(const char *argv)
{
	std::string arg(argv);
	Arguments.push_back(arg);
}

void AddBootArgument(const char *argv, unsigned int size)
{
	std::string arg(argv, size);
	Arguments.push_back(arg);
}

int CopyHomebrewMemory(u8 *temp, u32 pos, u32 len)
{
	if (pos > MAX_HOMEBREW_SIZE || len > MAX_HOMEBREW_SIZE - pos)
		return 0;

	if (pos + len > homebrewsize)
	{
		u8 *grown = (u8 *) MEM2_realloc(homebrewbuffer, pos + len);
		if (!grown)
			return 0;
		homebrewbuffer = grown;
		homebrewsize = pos + len;
	}
	memcpy(homebrewbuffer + pos, temp, len);

	return 1;
}

void FreeHomebrewBuffer()
{
	MEM2_free(homebrewbuffer);
	homebrewbuffer = NULL;
	homebrewsize = 0;

	Arguments.clear();
}

static inline bool IsDollZ(const u8 *buf)
{
	return (buf[0x100] == 0x3C);
}

static inline bool IsSpecialELF(const u8 *buf)
{
	return (*(u32 *)buf == 0x7F454C46 && buf[0x24] == 0);
}

//! The struct __argv and its command line, laid out for ARGS_ADDR but built in MEM1,
//! which RunAppbooter() does not overwrite
static u8 *BuildArgs(u32 *size)
{
	u32 length = 1;
	for (u32 i = 0; i < Arguments.size(); i++)
		length += Arguments[i].size() + 1;

	u32 total = sizeof(struct __argv) + length;
	if (total > MAX_ARGS_SIZE)
		return NULL;
	u8 *block = (u8 *) MEM1_alloc(total);
	if (!block)
		return NULL;

	struct __argv *args = (struct __argv *) block;
	char *commandLine = (char *) (block + sizeof(struct __argv));
	bzero(args, sizeof(struct __argv));
	args->argvMagic = ARGV_MAGIC;
	args->length = length;
	args->commandLine = (char *) ARGS_ADDR + sizeof(struct __argv);

	u32 argc = 0;
	u32 position = 0;
	for (u32 i = 0; i < Arguments.size(); i++)
	{
		memcpy(&commandLine[position], Arguments[i].c_str(), Arguments[i].size() + 1);
		position += Arguments[i].size() + 1;
		argc++;
	}
	commandLine[length - 1] = '\0';

	args->argc = argc;
	//! Where commandLine will be, not where this copy is
	args->argv = (char **) (ARGS_ADDR + offsetof(struct __argv, commandLine));
	args->endARGV = args->argv + 1;

	*size = total;
	return block;
}

static int RunAppbooter()
{
	if (homebrewsize == 0 || !homebrewbuffer)
		return -1;

	u8 *args = NULL;
	u32 argsSize = 0;
	if (!IsDollZ(homebrewbuffer) && !IsSpecialELF(homebrewbuffer))
	{
		args = BuildArgs(&argsSize);
		if (!args)
			return -1;
	}

	ExitApp();

	// Reload IOS 58 if available, else reload Entry IOS
	if (IOS_GetVersion() != 58)
	{
		s32 ret = IosLoader::ReloadIosSafe(58);
		if (ret < 0 && Settings.EntryIOS != IOS_GetVersion())
			IosLoader::ReloadIosKeepingRights(Settings.EntryIOS);
		gprintf("Reloaded to IOS%d\n", IOS_GetVersion());
	}

	loadStub();
	Set_Stub(returnTo(false));

	gprintf("Exiting USB Loader GX...\n\n");

	SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
	u32 level = IRQ_Disable();

	//! From here on nothing allocates or frees, and the heap blocks under these
	//! addresses are dead. The image's own block may overlap EXECUTE_ADDR.
	memmove(EXECUTE_ADDR, homebrewbuffer, homebrewsize);
	DCFlushRange(EXECUTE_ADDR, homebrewsize);
	ICInvalidateRange(EXECUTE_ADDR, homebrewsize);

	if (args)
	{
		memcpy(ARGS_ADDR, args, argsSize);
		DCFlushRange(ARGS_ADDR, argsSize);
	}

	memcpy(BOOTER_ADDR, app_booter_bin, app_booter_bin_size);
	DCFlushRange(BOOTER_ADDR, app_booter_bin_size);
	ICInvalidateRange(BOOTER_ADDR, app_booter_bin_size);

	entrypoint entry = (entrypoint)BOOTER_ADDR;
	CloseExceptionVectors();
	entry();
	IRQ_Restore(level);

	return 0;
}

int BootHomebrew(const char *filepath)
{
	FILE *file = fopen(filepath, "rb");
	if (!file)
		return -1;

	fseek(file, 0, SEEK_END);
	u32 filesize = ftell(file);
	rewind(file);

	if (filesize > MAX_HOMEBREW_SIZE)
	{
		fclose(file);
		return -1;
	}

	MEM2_free(homebrewbuffer);
	homebrewsize = 0;
	homebrewbuffer = (u8 *) MEM2_alloc(filesize);
	if (!homebrewbuffer)
	{
		fclose(file);
		return -1;
	}

	if (fread(homebrewbuffer, 1, filesize, file) != filesize)
	{
		fclose(file);
		DeviceHandler::DestroyInstance();
		Sys_BackToLoader();
		return -1;
	}

	homebrewsize = filesize;

	fclose(file);

	AddBootArgument(filepath);
	return RunAppbooter();
}

int BootHomebrewFromMem()
{
	return RunAppbooter();
}
