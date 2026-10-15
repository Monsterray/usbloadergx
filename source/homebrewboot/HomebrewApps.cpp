#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "HomebrewApps.h"
#include "BootHomebrew.h"
#include "HomebrewXML.h"
#include "FileOperations/DirList.h"
#include "FileOperations/fileops.h"
#include "GUI/gui.h"
#include "prompts/HomebrewPrompt.hpp"
#include "prompts/PromptWindows.h"
#include "settings/CSettings.h"
#include "settings/CGameStatistics.h"
#include "settings/SettingsEnums.h"
#include "language/gettext.h"
#include "menu.h"
#include "gecko.h"

HomebrewApps *HomebrewApps::Instance()
{
	static HomebrewApps instance;
	return &instance;
}

//! "HB" and four base-36 characters of the path's FNV-1a hash: stable while the app stays
//! where it is, so favourites, play counts and categories keep to it.
static void MakeID(const char *path, u8 *id, const std::vector<struct discHdr> &taken)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	u32 hash = 2166136261u;
	for (const char *p = path; *p; ++p)
		hash = (hash ^ (u8)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p)) * 16777619u;

	for (;; ++hash)
	{
		u32 h = hash;
		id[0] = 'H';
		id[1] = 'B';
		for (int i = 5; i >= 2; --i, h /= 36)
			id[i] = digits[h % 36];

		bool used = false;
		for (const struct discHdr &other : taken)
			used |= memcmp(other.id, id, 6) == 0;
		if (!used)
			return;
	}
}

void HomebrewApps::Reload()
{
	headers.clear();
	loaded = true;

	DirList apps(Settings.homebrewapps_path, ".dol,.elf", DirList::Files | DirList::Dirs | DirList::CheckSubfolders | DirList::MainDOL, 1);
	for (int i = 0; i < apps.GetFilecount(); ++i)
	{
		const char *path = apps.GetFilepath(i);
		if (!path || strlen(path) >= sizeof(((struct discHdr *)0)->path))
			continue;

		struct discHdr header;
		memset(&header, 0, sizeof(header));
		header.type = TYPE_GAME_HOMEBREW;
		snprintf(header.path, sizeof(header.path), "%s", path);
		MakeID(path, header.id, headers);

		//! The meta.xml name, else the folder's
		std::string folder = Folder(&header);
		HomebrewXML meta;
		if (meta.LoadHomebrewXMLData((folder + "meta.xml").c_str()) > 0 && meta.GetName() && *meta.GetName())
			snprintf(header.title, sizeof(header.title), "%s", meta.GetName());
		else
		{
			std::string name = folder.substr(0, folder.size() - 1);
			size_t slash = name.rfind('/');
			snprintf(header.title, sizeof(header.title), "%s", name.c_str() + (slash == std::string::npos ? 0 : slash + 1));
		}
		headers.push_back(header);
	}
	gprintf("HomebrewApps: %d apps in %s\n", (int)headers.size(), Settings.homebrewapps_path);
}

std::vector<struct discHdr> &HomebrewApps::GetHeaders()
{
	if (!loaded)
		Reload();
	return headers;
}

std::string HomebrewApps::Folder(const struct discHdr *header)
{
	std::string folder(header->path, strnlen(header->path, sizeof(header->path)));
	size_t slash = folder.rfind('/');
	folder.erase(slash == std::string::npos ? 0 : slash + 1);
	return folder;
}

void HomebrewApps::Launch(const struct discHdr *header)
{
	if (!header || header->type != TYPE_GAME_HOMEBREW)
		return;

	std::string folder = Folder(header);
	HomebrewXML meta;
	meta.LoadHomebrewXMLData((folder + "meta.xml").c_str());

	GuiImageData *icon = new GuiImageData((folder + "icon.png").c_str());
	if (!icon->GetImage())
	{
		delete icon;
		icon = NULL;
	}

	struct stat st;
	u64 filesize = stat(header->path, &st) == 0 ? st.st_size : 0;

	HomebrewPrompt *prompt = new HomebrewPrompt(header->title, meta.GetCoder(), meta.GetVersion(),
												meta.GetReleasedate(), meta.GetLongDescription(), icon, filesize);
	mainWindow->SetState(STATE_DISABLED);
	mainWindow->Append(prompt);
	int choice = prompt->MainLoop();
	delete prompt;
	delete icon;
	mainWindow->SetState(STATE_DEFAULT);

	if (choice != 1)
		return;

	u8 *buffer = NULL;
	u32 size = 0;
	LoadFileToMem(header->path, &buffer, &size);
	if (!buffer)
	{
		WindowPrompt(tr("Error:"), tr("Not enough memory."), tr("OK"));
		return;
	}
	FreeHomebrewBuffer();
	int copied = CopyHomebrewMemory(buffer, 0, size);
	free(buffer);
	if (!copied)
	{
		WindowPrompt(tr("Error:"), tr("Not enough memory."), tr("OK"));
		return;
	}

	AddBootArgument(header->path);
	for (u32 i = 0; i < meta.GetArguments().size(); ++i)
		AddBootArgument(meta.GetArguments().at(i).c_str());

	GameStatistics.SetPlayCount(header, GameStatistics.GetPlayCount(header) + 1);
	GameStatistics.Save();

	BootHomebrewFromMem();
}
