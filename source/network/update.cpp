/***************************************************************************
 * Copyright (C) 2025 by blackb0x
 * Copyright (C) 2009 by Dimok
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 * must not claim that you wrote the original software. If you use
 * this software in a product, an acknowledgment in the product
 * documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 * must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 *
 * update.cpp
 *
 * Update operations
 * for Wii-Xplorer 2009
 ***************************************************************************/
#include <stdio.h>
#include <string.h>
#include <ogcsys.h>
#include <string>

#include "update.h"
#include "gecko.h"
#include "ZipFile.h"
#include "https.h"
#include "networkops.h"
#include "ImageDownloader.h"
#include "settings/CSettings.h"
#include "settings/GameTitles.h"
#include "language/gettext.h"
#include "language/UpdateLanguage.h"
#include "utils/StringTools.h"
#include "utils/ShowError.h"
#include "prompts/PromptWindows.h"
#include "prompts/ProgressWindow.h"
#include "FileOperations/fileops.h"
#include "xml/GameTDB.hpp"
#include "usbloader/GameList.h"
#include "version.h"
#include "UpdateKey.h"
#include "UpdateManifest.h"
#include "wad/nandtitle.h"
#include "wad/WadInstall.h"

#include <wolfssl/wolfcrypt/ed25519.h>
#include <wolfssl/wolfcrypt/hash.h>

/****************************************************************************
 * Checking if an Update is available
 ***************************************************************************/
int DownloadFileToPath(const char *url, const char *dest, const bool showprogress)
{
	if (showprogress)
	{
		// The update URL comes out of a downloaded text file
		const char *slashPos = strrchr(url, '/');
		const char *filename = slashPos ? slashPos + 1 : url;
		ProgressCancelEnable(true);
		StartProgress(tr("Downloading file..."), 0, filename, true, true);
	}

	struct download file = {};
	file.show_progress = showprogress;
	downloadfile(url, &file);
	if (file.size > 0)
	{
		FILE *savefile = fopen(dest, "wb");
		if (!savefile)
		{
			MEM2_free(file.data);
			if (showprogress)
				ShowError(tr("Can't write to destination."));
			ProgressStop();
			ProgressCancelEnable(false);
			return -7;
		}
		size_t written = fwrite(file.data, 1, file.size, savefile);
		fclose(savefile);
		MEM2_free(file.data);
		// A full card writes a short file, and the caller would install it
		if (written != file.size)
		{
			remove(dest);
			if (showprogress)
			{
				ShowError(tr("Can't write to destination."));
				ProgressStop();
				ProgressCancelEnable(false);
			}
			return -7;
		}
	}
	if (showprogress)
	{
		ProgressStop();
		ProgressCancelEnable(false);
	}
	return file.size;
}

static bool CheckNewGameTDBVersion(const char *url)
{
	gprintf("Checking GameTDB version...\n");
	struct download file = {};
	file.gametdbcheck = true;
	downloadfile(url, &file);

	if (file.gametdbcheck <= 0)
		return false;

	std::string Filepath(Settings.titlestxt_path);
	if (Filepath.back() != '/')
		Filepath += '/';
	Filepath += "wiitdb.xml";

	GameTDB XML_DB;

	if (!XML_DB.OpenFile(Filepath.c_str()))
		return true; // If no file exists we need the file

	u64 ExistingVersion = XML_DB.GetGameTDBVersion();
	XML_DB.CloseFile();

	gprintf("Existing GameTDB Version: %llu Online GameTDB Version: %llu\n", ExistingVersion, file.gametdbcheck);

	return (ExistingVersion != file.gametdbcheck);
}

bool initNetwork()
{
	if (NetworkInitPrompt())
		return true;
	gprintf("No network\n");
	return false;
}

int UpdateGameTDB()
{
	// Create the directory if it doesn't exist
	CreateSubfolder(Settings.titlestxt_path);

	if (CheckNewGameTDBVersion(Settings.URL_GameTDB) == false)
	{
		gprintf("Not updating GameTDB: Version is the same\n");
		return -2;
	}

	gprintf("Updating GameTDB...\n");

	std::string ZipPath(Settings.titlestxt_path);
	if (ZipPath.back() != '/')
		ZipPath += '/';
	ZipPath += "wiitdb.zip";

	int filesize = DownloadFileToPath(Settings.URL_GameTDB, ZipPath.c_str());

	if (filesize <= 0)
		return -1;

	ZipFile zFile(ZipPath.c_str());

	bool result = zFile.ExtractAll(Settings.titlestxt_path);

	remove(ZipPath.c_str());

	// Reload all titles and reload cached titles because the file changed now.
	GameTitles.Reset();
	GameTitles.LoadTitlesFromGameTDB(Settings.titlestxt_path);
	return (result ? filesize : -1);
}

int UpdateCheats()
{
	std::string url("https://raw.githubusercontent.com/wiidev/cheats/master/data/txt.zip");
	std::string ZipPath(Settings.ConfigPath);
	if (ZipPath.back() != '/')
		ZipPath += '/';
	ZipPath += "txt.zip";

	int filesize = DownloadFileToPath(url.c_str(), ZipPath.c_str());

	if (filesize <= 0)
		return -1;

	ZipFile zFile(ZipPath.c_str());

	bool result = zFile.ExtractAll(Settings.TxtCheatcodespath);

	remove(ZipPath.c_str());
	return (result ? filesize : -1);
}

#ifdef FULLCHANNEL
#define UPDATE_FILE "usbloadergx.wad"
#else
#define UPDATE_FILE "boot.dol"
#endif

// The signature covers every byte of update.txt before its signature line, and
// the manifest holds the size and SHA-256 of every file, so nothing GX installs
// depends on the connection, which has no certificate check.
static bool VerifyManifestSignature(const UpdateManifest &manifest, const char *data)
{
	byte publicKey[ED25519_PUB_KEY_SIZE];
	if (strlen(UPDATE_PUBLIC_KEY_HEX) != 2 * sizeof(publicKey) ||
		!UpdateHex_Decode(UPDATE_PUBLIC_KEY_HEX, 2 * sizeof(publicKey), publicKey))
		return false;

	ed25519_key key;
	if (wc_ed25519_init(&key) != 0)
		return false;

	int verified = 0;
	bool ok = wc_ed25519_import_public(publicKey, sizeof(publicKey), &key) == 0 &&
			  wc_ed25519_verify_msg(manifest.signature, sizeof(manifest.signature), (const byte *)data,
									manifest.signedLength, &verified, &key) == 0 &&
			  verified == 1;
	wc_ed25519_free(&key);
	return ok;
}

// Downloads a file the manifest lists into file. False unless it has exactly
// the size and SHA-256 the manifest gives; file then holds nothing.
static bool DownloadReleaseFile(const UpdateManifest &manifest, const UpdateFile &entry, bool showprogress,
								struct download *file)
{
	char url[300];
	snprintf(url, sizeof(url), "%s/download/v%u.%u.%u/%s", UPDATE_RELEASES_URL, (unsigned)manifest.version.major,
			 (unsigned)manifest.version.minor, (unsigned)manifest.version.patch, entry.name);

	if (showprogress)
	{
		ProgressCancelEnable(true);
		StartProgress(tr("Downloading file..."), 0, entry.name, true, true);
	}
	*file = {};
	file->show_progress = showprogress;
	file->max_size = entry.size;
	downloadfile(url, file);
	if (showprogress)
	{
		ProgressStop();
		ProgressCancelEnable(false);
	}

	byte digest[WC_SHA256_DIGEST_SIZE];
	if (file->size == entry.size && wc_Sha256Hash((const byte *)file->data, entry.size, digest) == 0 &&
		memcmp(digest, entry.sha256, sizeof(digest)) == 0)
		return true;

	gprintf("Update: %s failed (%llu of %u bytes, or a wrong SHA-256)\n", entry.name, file->size, (unsigned)entry.size);
	if (file->size > 0)
		MEM2_free(file->data);
	*file = {};
	return false;
}

// A short write (a full card) removes the file instead of leaving part of it
static bool WriteWholeFile(const char *path, const char *data, u32 size)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	bool ok = fwrite(data, 1, size, f) == size;
	ok = (fclose(f) == 0) && ok;
	if (!ok)
		remove(path);
	return ok;
}

int ApplicationDownload()
{
	// Releases of this fork, not upstream's: upstream's build would replace the fork
	struct download file = {};
	file.max_size = UPDATE_MANIFEST_MAX_SIZE;
	downloadfile(UPDATE_RELEASES_URL "/latest/download/update.txt", &file);
	if (file.size == 0)
	{
		WindowPrompt(tr("Failed updating"), tr("Could not download the update information."), tr("OK"));
		return -1;
	}

	UpdateManifest manifest;
	bool valid = UpdateManifest_Parse(file.data, file.size, &manifest) && VerifyManifestSignature(manifest, file.data);
	MEM2_free(file.data);
	if (!valid)
	{
		WindowPrompt(tr("Failed updating"), tr("The update information is damaged or not signed by the publisher."), tr("OK"));
		return -1;
	}

	// A version without a release tag (0.0.0+g1a2b3c4) is older than any release
	UpdateVersion current = {0, 0, 0};
	UpdateVersion_Parse(LOADER_VERSION, &current);
	char available[24];
	snprintf(available, sizeof(available), "%u.%u.%u", (unsigned)manifest.version.major,
			 (unsigned)manifest.version.minor, (unsigned)manifest.version.patch);
	gprintf("Update: installed %s, latest release %s\n", LOADER_VERSION, available);

	if (UpdateVersion_Compare(&manifest.version, &current) <= 0)
	{
		WindowPrompt(tr("No new updates."), 0, tr("OK"));
		return 0;
	}

	// tr() can return a long translation, so these are not sized for the English
	char title[600], msg[600];
	const UpdateFile *loader = UpdateManifest_Find(&manifest, UPDATE_FILE);
	if (!loader)
	{
		snprintf(msg, sizeof(msg), tr("Release %s has no %s."), available, UPDATE_FILE);
		WindowPrompt(tr("Failed updating"), msg, tr("OK"));
		return -1;
	}

	snprintf(title, sizeof(title), tr("USB Loader GX %s is available"), available);
	snprintf(msg, sizeof(msg), tr("You have %s. Update now?"), LOADER_VERSION);
	if (!WindowPrompt(title, msg, tr("Yes"), tr("No")))
		return 0;

	struct download update;
	if (!DownloadReleaseFile(manifest, *loader, true, &update))
	{
		WindowPrompt(tr("Failed updating"), tr("The download failed or does not match the update information."), tr("OK"));
		return -1;
	}

#ifdef FULLCHANNEL
	StartProgress(tr("Installing the channel..."), 0, UPDATE_FILE, true, false);
	int error = WadInstall((const u8 *)update.data, update.size, TITLE_ID(0x00010001, 0x554c4e52));
	ProgressStop();
	MEM2_free(update.data);
	// IOS refuses the release's fakesigned ticket with -2011 unless its ES is patched (a cIOS)
	if (error == -2011)
	{
		ShowError(tr("The WAD installation failed with error %i. It needs an IOS that accepts fakesigned titles, such as cIOS 249."), error);
		return -1;
	}
	if (error)
	{
		ShowError(tr("The WAD installation failed with error %i"), error);
		return -1;
	}
#else
	char realpath[250], tmppath[250], bakpath[250];
	snprintf(realpath, sizeof(realpath), "%sboot.dol", Settings.ConfigPath);
	snprintf(tmppath, sizeof(tmppath), "%sboot.tmp", Settings.ConfigPath);
	snprintf(bakpath, sizeof(bakpath), "%sboot.bak", Settings.ConfigPath);

	bool written = WriteWholeFile(tmppath, update.data, update.size);
	MEM2_free(update.data);
	if (!written)
	{
		ShowError(tr("Can't write to destination."));
		return -1;
	}

	RemoveFile(bakpath);
	bool haveBackup = CheckFile(realpath) && RenameFile(realpath, bakpath);
	if (!haveBackup)
		RemoveFile(realpath);
	if (!RenameFile(tmppath, realpath))
	{
		// Put the loader that works back
		if (haveBackup)
			RenameFile(bakpath, realpath);
		RemoveFile(tmppath);
		ShowError(tr("Error while updating USB Loader GX."));
		return -1;
	}
	RemoveFile(bakpath);
#endif

	// The Homebrew Channel entry; the release may leave either out
	static const char *const extras[] = {"meta.xml", "icon.png"};
	for (const char *name : extras)
	{
		const UpdateFile *entry = UpdateManifest_Find(&manifest, name);
		struct download extra;
		if (!entry || !DownloadReleaseFile(manifest, *entry, false, &extra))
			continue;
		char path[250];
		snprintf(path, sizeof(path), "%s%s", Settings.ConfigPath, name);
		WriteWholeFile(path, extra.data, extra.size);
		MEM2_free(extra.data);
	}

	return 1;
}

int UpdateNintendont()
{
	char NINUpdatePath[120];
	snprintf(NINUpdatePath, sizeof(NINUpdatePath), "%sboot.dol", Settings.NINLoaderPath);
	char NINUpdatePathBak[120];
	snprintf(NINUpdatePathBak, sizeof(NINUpdatePathBak), "%sboot.bak", Settings.NINLoaderPath);

	// Create the directory if it doesn't exist
	CreateSubfolder(Settings.NINLoaderPath);
	// Rename existing boot.dol file to boot.bak
	if (CheckFile(NINUpdatePath))
		RenameFile(NINUpdatePath, NINUpdatePathBak);

	if (DownloadFileToPath("https://raw.githubusercontent.com/FIX94/Nintendont/master/loader/loader.dol", NINUpdatePath) > 0)
	{
		// Remove existing loader.dol file if found as it has priority over boot.dol, and boot.bak
		snprintf(NINUpdatePath, sizeof(NINUpdatePath), "%s/loader.dol", Settings.NINLoaderPath);
		RemoveFile(NINUpdatePath);
		RemoveFile(NINUpdatePathBak);
		// Download icon.png if it doesn't exist
		snprintf(NINUpdatePath, sizeof(NINUpdatePath), "%s/icon.png", Settings.NINLoaderPath);
		if (!CheckFile(NINUpdatePath))
			DownloadFileToPath("https://raw.githubusercontent.com/FIX94/Nintendont/master/nintendont/icon.png", NINUpdatePath, false);
		// Download meta.xml if it doesn't exist (Nintendont will edit meta.xml when it's launched)
		snprintf(NINUpdatePath, sizeof(NINUpdatePath), "%s/meta.xml", Settings.NINLoaderPath);
		if (!CheckFile(NINUpdatePath))
			DownloadFileToPath("https://raw.githubusercontent.com/FIX94/Nintendont/master/nintendont/meta.xml", NINUpdatePath, false);

		return 1;
	}
	else
	{
		// Restore backup file if found
		RemoveFile(NINUpdatePath);
		if (CheckFile(NINUpdatePathBak))
			RenameFile(NINUpdatePathBak, NINUpdatePath);
	}
	return -1;
}

void UpdateCovers()
{
	ImageDownloader::DownloadImages(true);
}
