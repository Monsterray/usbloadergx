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
#include <stdlib.h>
#include <string.h>
#include <ogcsys.h>
#include <string>
#include <set>
#include <vector>

#include "update.h"
#include "CheatTxt.h"
#include "settings/SettingsEnums.h"
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

// A file download is written to the card as it arrives, so its size is not
// bound by memory. wiitdb.zip is 10 MB; this only stops a body with no end
// (the connection has no certificate check) from filling the card.
#define DOWNLOAD_FILE_MAX_SIZE (64 * 1024 * 1024)

// Puts part in the place of dest. The old dest stays until part is in place,
// and comes back if part cannot be renamed. FAT does not rename over a file.
static bool ReplaceFile(const char *part, const char *dest)
{
	char bak[300];
	if (snprintf(bak, sizeof(bak), "%s.bak", dest) >= (int)sizeof(bak))
		return false;
	bool hadOld = CheckFile(dest);
	if (hadOld)
	{
		RemoveFile(bak);
		if (!RenameFile(dest, bak))
			return false;
	}
	if (!RenameFile(part, dest))
	{
		if (hadOld)
			RenameFile(bak, dest);
		return false;
	}
	if (hadOld)
		RemoveFile(bak);
	return true;
}

// Downloads url to the file part, writing each block as it arrives: on the
// Wii GX can have less than 2 MB of MEM2 free, too little to hold wiitdb.zip
// or a boot.dol. With report set the download reports into the progress
// window that is open and stops when it is cancelled.
// Returns the size, 0 when the download failed, -7 when the card cannot take
// it. part is left for the caller, complete or not.
static int DownloadToPart(const char *url, const char *part, bool report, u64 maxSize)
{
	FILE *f = fopen(part, "wb");
	if (!f)
		return -7;

	struct download file = {};
	file.show_progress = report;
	file.max_size = maxSize;
	file.file = f;
	downloadfile(url, &file);

	bool writeFailed = ferror(f) != 0;
	writeFailed = (fclose(f) != 0) || writeFailed;
	if (writeFailed)
		return -7;
	return file.size;
}

// Downloads url to dest through dest.part. dest is replaced only by a
// complete download, and dest.part is removed whatever happens.
// Returns the size, 0 when the download failed, -7 when the card cannot take it.
static int DownloadToFile(const char *url, const char *dest, bool report)
{
	char part[300];
	if (snprintf(part, sizeof(part), "%s.part", dest) >= (int)sizeof(part))
		return -7;

	int ret = DownloadToPart(url, part, report, DOWNLOAD_FILE_MAX_SIZE);
	if (ret > 0 && !ReplaceFile(part, dest))
		ret = -7;
	if (ret <= 0)
	{
		RemoveFile(part);
		gprintf("Download of %s to %s failed%s\n", url, dest, ret == -7 ? " (write error)" : "");
	}
	return ret;
}

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

	int ret = DownloadToFile(url, dest, showprogress);

	if (showprogress)
	{
		ProgressStop();
		ProgressCancelEnable(false);
		if (ret == -7)
			ShowError(tr("Can't write to destination."));
	}
	return ret;
}

// 1 when the server has another wiitdb.xml than the card, 0 when they are the
// same, -1 when the server did not answer (that used to read as "up to date")
static int CheckNewGameTDBVersion(const char *url)
{
	gprintf("Checking GameTDB version...\n");
	struct download file = {};
	file.gametdbcheck = true;
	downloadfile(url, &file);

	if (file.gametdbcheck == 0)
		return -1;

	std::string Filepath(Settings.titlestxt_path);
	if (Filepath.back() != '/')
		Filepath += '/';
	Filepath += "wiitdb.xml";

	GameTDB XML_DB;

	if (!XML_DB.OpenFile(Filepath.c_str()))
		return 1; // If no file exists we need the file

	u64 ExistingVersion = XML_DB.GetGameTDBVersion();
	XML_DB.CloseFile();

	gprintf("Existing GameTDB Version: %llu Online GameTDB Version: %llu\n", ExistingVersion, file.gametdbcheck);

	return (ExistingVersion != file.gametdbcheck) ? 1 : 0;
}

bool initNetwork()
{
	if (NetworkInitPrompt())
		return true;
	gprintf("No network\n");
	return false;
}

// The steps of UpdateGameTDB(), each reporting into the one window it keeps open
static int UpdateGameTDBSteps()
{
	int check = CheckNewGameTDBVersion(Settings.URL_GameTDB);
	if (ProgressCanceled())
		return UPDATE_CANCELED;
	if (check < 0)
		return -1;
	if (check == 0)
	{
		gprintf("Not updating GameTDB: Version is the same\n");
		return -2;
	}

	gprintf("Updating GameTDB...\n");

	std::string ZipPath(Settings.titlestxt_path);
	if (ZipPath.back() != '/')
		ZipPath += '/';
	ZipPath += "wiitdb.zip";

	ShowProgress(tr("Updating WiiTDB.xml"), tr("Downloading file..."), "wiitdb.zip", 0, 1, true, true);
	int filesize = DownloadToFile(Settings.URL_GameTDB, ZipPath.c_str(), true);
	if (filesize <= 0)
		return ProgressCanceled() ? UPDATE_CANCELED : -1;

	bool result;
	{
		ZipFile zFile(ZipPath.c_str());
		result = zFile.ExtractAll(Settings.titlestxt_path, false);
	}
	remove(ZipPath.c_str());

	// Reload all titles and reload cached titles because the file changed now.
	ShowProgress(tr("Updating WiiTDB.xml"), tr("Loading game titles..."), 0, 0, 1, false, false);
	GameTitles.Reset();
	GameTitles.LoadTitlesFromGameTDB(Settings.titlestxt_path);
	if (ProgressCanceled())
		return UPDATE_CANCELED;
	return (result ? filesize : -1);
}

int UpdateGameTDB()
{
	// Create the directory if it doesn't exist
	CreateSubfolder(Settings.titlestxt_path);

	// The window opens before anything else: asking the server for the version
	// alone takes a TLS handshake, several seconds on the Wii
	ProgressCancelEnable(true);
	StartProgress(tr("Updating WiiTDB.xml"), tr("Checking for a new version..."), 0, false, false);
	int ret = UpdateGameTDBSteps();
	ProgressStop();
	ProgressCancelEnable(false);
	return ret;
}

bool CheatsURL(const char *id, char *url, size_t size)
{
	if (!CheatTxt_IsSafeId(id))
		return false;
	std::string pattern(Settings.URL_Cheats);
	size_t pos = pattern.find("{gameid}");
	if (pos == std::string::npos)
	{
		pattern = CHEATS_URL_DEFAULT;
		pos = pattern.find("{gameid}");
	}
	pattern.replace(pos, 8, id);
	return snprintf(url, size, "%s", pattern.c_str()) < (int)size;
}

// Writes size bytes to path through path.part, so a full card leaves the
// file that was there
bool WriteFileReplacing(const char *path, const char *data, size_t size)
{
	char part[300];
	if (snprintf(part, sizeof(part), "%s.part", path) >= (int)sizeof(part))
		return false;
	FILE *f = fopen(part, "wb");
	if (!f)
		return false;
	bool ok = fwrite(data, 1, size, f) == size;
	ok = (fclose(f) == 0) && ok;
	ok = ok && ReplaceFile(part, path);
	if (!ok)
		RemoveFile(part);
	return ok;
}

int UpdateCheats(CheatUpdateResult &result)
{
	result = {};
	if (!CreateSubfolder(Settings.TxtCheatcodespath))
		return -1;

	// The window opens first: reading the game list can take a moment
	ProgressCancelEnable(true);
	StartProgress(tr("Updating cheat files"), tr("Please wait..."), 0, false, false);

	// Every game GX knows, from every source, not the filtered view
	std::vector<struct discHdr *> headers;
	gameList.GetGameListHeaders(headers, MODE_ALL);

	struct CheatGame
	{
		char id[7];
		const char *title;
	};
	std::vector<CheatGame> games;
	std::set<std::string> seen; // A game can be on NAND and on the EmuNAND
	for (struct discHdr *header : headers)
	{
		CheatGame game;
		memcpy(game.id, header->id, 6);
		game.id[6] = '\0';
		// Channels have four characters; anything else cannot go into a URL
		if (!CheatTxt_IsSafeId(game.id) || !seen.insert(game.id).second)
			continue;
		game.title = GameTitles.GetTitle(header);
		games.push_back(game);
	}
	result.games = games.size();

	for (int i = 0; i < result.games && !(result.canceled = ProgressCanceled()); ++i)
	{
		const CheatGame &game = games[i];
		char counter[64], url[300], path[300];
		snprintf(counter, sizeof(counter), "%s  (%i/%i)", game.id, i + 1, result.games);
		ShowProgress(tr("Updating cheat files"), game.title, counter, i, result.games, false, false);

		if (!CheatsURL(game.id, url, sizeof(url)) ||
			snprintf(path, sizeof(path), "%s%s.txt", Settings.TxtCheatcodespath, game.id) >= (int)sizeof(path))
		{
			result.failed++;
			continue;
		}

		// Small enough for memory, and checked before it touches the card
		struct download file = {};
		file.max_size = CHEAT_TXT_MAX_SIZE;
		downloadfile(url, &file);
		if (file.size > 0 && CheatTxt_IsValid(file.data, file.size, game.id))
		{
			// The file on the card stays unless the new one is written whole
			if (WriteFileReplacing(path, file.data, file.size))
				result.updated++;
			else
				result.failed++;
		}
		else if (file.status == 404)
			result.missing++;
		else
		{
			gprintf("Cheats: %s failed (status %i, %u bytes)\n", game.id, file.status, (unsigned)file.size);
			result.failed++;
		}
		if (file.data)
			MEM2_free(file.data);
	}

	ProgressStop();
	ProgressCancelEnable(false);
	gprintf("Cheats: %i games, %i updated, %i not on the server, %i failed%s\n", result.games, result.updated,
			result.missing, result.failed, result.canceled ? ", canceled" : "");
	return 0;
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

static void ReleaseFileURL(const UpdateManifest &manifest, const UpdateFile &entry, char *url, size_t size)
{
	snprintf(url, size, "%s/download/v%u.%u.%u/%s", UPDATE_RELEASES_URL, (unsigned)manifest.version.major,
			 (unsigned)manifest.version.minor, (unsigned)manifest.version.patch, entry.name);
}

#ifdef FULLCHANNEL
// Downloads a file the manifest lists into file. False unless it has exactly
// the size and SHA-256 the manifest gives; file then holds nothing. Only the
// WAD comes into memory, because WadInstall() installs it from there.
static bool DownloadReleaseFile(const UpdateManifest &manifest, const UpdateFile &entry, bool report,
								struct download *file)
{
	char url[300];
	ReleaseFileURL(manifest, entry, url, sizeof(url));
	*file = {};
	file->show_progress = report;
	file->max_size = entry.size;
	downloadfile(url, file);

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
#endif

// The SHA-256 of a file, read a block at a time
static bool FileSha256(const char *path, byte digest[WC_SHA256_DIGEST_SIZE])
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	const size_t blockSize = 32 * 1024;
	byte *block = (byte *)malloc(blockSize);
	wc_Sha256 sha;
	bool ok = block && wc_InitSha256(&sha) == 0;
	if (ok)
	{
		size_t n;
		while ((n = fread(block, 1, blockSize, f)) > 0)
			if (wc_Sha256Update(&sha, block, n) != 0)
				ok = false;
		ok = ok && !ferror(f) && wc_Sha256Final(&sha, digest) == 0;
		wc_Sha256Free(&sha);
	}
	free(block);
	fclose(f);
	return ok;
}

// Downloads a file the manifest lists to path, streamed through path.part: a
// boot.dol does not fit in the MEM2 GX has left on the Wii. path is replaced
// only by a file of exactly the size and SHA-256 the manifest gives, and is
// left as it was otherwise.
// Returns 1, 0 when the download failed or does not match, -7 on a write error.
static int DownloadReleaseFileTo(const UpdateManifest &manifest, const UpdateFile &entry, bool report, const char *path)
{
	char url[300], part[300];
	ReleaseFileURL(manifest, entry, url, sizeof(url));
	if (snprintf(part, sizeof(part), "%s.part", path) >= (int)sizeof(part))
		return -7;

	int ret = DownloadToPart(url, part, report, entry.size);
	byte digest[WC_SHA256_DIGEST_SIZE];
	if (ret > 0 && !((u32)ret == entry.size && FileSha256(part, digest) && memcmp(digest, entry.sha256, sizeof(digest)) == 0))
	{
		gprintf("Update: %s failed (%d of %u bytes, or a wrong SHA-256)\n", entry.name, ret, (unsigned)entry.size);
		ret = 0;
	}
	if (ret > 0 && !ReplaceFile(part, path))
		ret = -7;
	if (ret <= 0)
		RemoveFile(part);
	return ret > 0 ? 1 : ret;
}

int ApplicationDownload()
{
	// Releases of this fork, not upstream's: upstream's build would replace the fork
	// A window right away: GitHub's redirect and two TLS handshakes take seconds
	StartProgress(tr("Checking for updates..."), tr("Please wait..."), 0, false, false);
	struct download file = {};
	file.max_size = UPDATE_MANIFEST_MAX_SIZE;
	downloadfile(UPDATE_RELEASES_URL "/latest/download/update.txt", &file);
	ProgressStop();
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

#ifdef FULLCHANNEL
	ProgressCancelEnable(true);
	StartProgress(tr("Downloading file..."), 0, loader->name, true, true);
	struct download update;
	bool downloaded = DownloadReleaseFile(manifest, *loader, true, &update);
	ProgressStop();
	ProgressCancelEnable(false);
	if (!downloaded)
	{
		WindowPrompt(tr("Failed updating"), tr("The download failed or does not match the update information."), tr("OK"));
		return -1;
	}

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
	// The running boot.dol stays until a verified one replaces it
	char realpath[250];
	snprintf(realpath, sizeof(realpath), "%sboot.dol", Settings.ConfigPath);
	ProgressCancelEnable(true);
	StartProgress(tr("Downloading file..."), 0, loader->name, true, true);
	int downloaded = DownloadReleaseFileTo(manifest, *loader, true, realpath);
	ProgressStop();
	ProgressCancelEnable(false);
	if (downloaded == -7)
	{
		ShowError(tr("Can't write to destination."));
		return -1;
	}
	if (downloaded <= 0)
	{
		WindowPrompt(tr("Failed updating"), tr("The download failed or does not match the update information."), tr("OK"));
		return -1;
	}
#endif

	// The Homebrew Channel entry; the release may leave either out
	static const char *const extras[] = {"meta.xml", "icon.png"};
	for (const char *name : extras)
	{
		const UpdateFile *entry = UpdateManifest_Find(&manifest, name);
		if (!entry)
			continue;
		char path[250];
		snprintf(path, sizeof(path), "%s%s", Settings.ConfigPath, name);
		DownloadReleaseFileTo(manifest, *entry, false, path);
	}

	return 1;
}

// The Nintendont fork this loader is paired with. Its prod branch carries each release's
// built loader/loader.dol; master mirrors upstream and has no fork builds.
#define NINTENDONT_URL "https://raw.githubusercontent.com/Monsterray/Nintendont/prod/"

int UpdateNintendont()
{
	char NINUpdatePath[120];
	snprintf(NINUpdatePath, sizeof(NINUpdatePath), "%sboot.dol", Settings.NINLoaderPath);

	// Create the directory if it doesn't exist
	CreateSubfolder(Settings.NINLoaderPath);

	// One window for all three files, open until the result prompt
	ProgressCancelEnable(true);
	StartProgress(tr("Updating Nintendont"), tr("Downloading file..."), "loader.dol", true, true);

	// The boot.dol there stays until a complete one replaces it
	int ret = DownloadToFile(NINTENDONT_URL "loader/loader.dol", NINUpdatePath, true);
	if (ret > 0)
	{
		// loader.dol has priority over boot.dol; boot.bak is what this updater used to leave
		snprintf(NINUpdatePath, sizeof(NINUpdatePath), "%sloader.dol", Settings.NINLoaderPath);
		RemoveFile(NINUpdatePath);
		snprintf(NINUpdatePath, sizeof(NINUpdatePath), "%sboot.bak", Settings.NINLoaderPath);
		RemoveFile(NINUpdatePath);
		// Download icon.png if it doesn't exist
		snprintf(NINUpdatePath, sizeof(NINUpdatePath), "%sicon.png", Settings.NINLoaderPath);
		if (!CheckFile(NINUpdatePath))
		{
			ShowProgress(tr("Updating Nintendont"), tr("Downloading file..."), "icon.png", 0, 1, true, true);
			DownloadToFile(NINTENDONT_URL "nintendont/icon.png", NINUpdatePath, true);
		}
		// Download meta.xml if it doesn't exist (Nintendont will edit meta.xml when it's launched)
		snprintf(NINUpdatePath, sizeof(NINUpdatePath), "%smeta.xml", Settings.NINLoaderPath);
		if (!CheckFile(NINUpdatePath))
		{
			ShowProgress(tr("Updating Nintendont"), tr("Downloading file..."), "meta.xml", 0, 1, true, true);
			DownloadToFile(NINTENDONT_URL "nintendont/meta.xml", NINUpdatePath, true);
		}
	}

	bool canceled = ret <= 0 && ProgressCanceled();
	ProgressStop();
	ProgressCancelEnable(false);
	if (ret == -7)
		ShowError(tr("Can't write to destination."));
	return ret > 0 ? 1 : (canceled ? UPDATE_CANCELED : -1);
}

void UpdateCovers()
{
	ImageDownloader::DownloadImages(true);
}
