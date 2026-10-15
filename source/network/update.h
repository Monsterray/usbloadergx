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
 * update.h
 *
 * Update operations
 * for Wii-Xplorer 2009
 ***************************************************************************/
#ifndef _UPDATEOPS_H_
#define _UPDATEOPS_H_

#include <stddef.h>

// What an update returns when the user pressed Cancel in its progress window
#define UPDATE_CANCELED -3

// RiiConnect24's revival of geckocodes.org: the same .txt files, 404 for a
// game it has no codes for. wiidev/cheats and geckocodes.org are gone.
#define CHEATS_URL_DEFAULT "https://codes.rc24.xyz/txt.php?txt={gameid}"

// The cheat file URL for a game: Settings.URL_Cheats, or CHEATS_URL_DEFAULT
// when that has no {gameid}. False for an ID that cannot go into a URL.
bool CheatsURL(const char *id, char *url, size_t size);

// Writes size bytes to path through path.part; the file that was at path
// stays unless the new one is written whole
bool WriteFileReplacing(const char *path, const char *data, size_t size);

struct CheatUpdateResult
{
	int games;    // games with an ID that can be looked up
	int updated;  // cheat files written
	int missing;  // games the server has no cheats for
	int failed;   // downloads that failed or did not look like a cheat file
	bool canceled;
};

bool initNetwork();
int UpdateGameTDB();
// -1 when the cheat folder cannot be created; otherwise 0 and the counts in result
int UpdateCheats(CheatUpdateResult &result);
int ApplicationDownload();
int UpdateNintendont();
void UpdateCovers();
int DownloadFileToPath(const char *url, const char *dest, const bool showprogress = true);

#endif
