/*
	Code by Oddx @ GBAtemp.net
*/
#include "usbloader/disc.h"
#include "settings/CSettings.h"

#define GAME_HEADER_CACHE_VERSION 1283

#define WII_HEADER_CACHE_FILE "WII.cache"
#define GAMECUBE_HEADER_CACHE_FILE "GAMECUBE.cache"
#define EMUNAND_HEADER_CACHE_FILE "EMUNAND.cache"

struct wiiCache
{
	struct discHdr header;
	int part;
};

struct gcCache
{
	struct discHdr header;
	// Same size as discHdr::path, which is what the path list holds.
	u8 path[260];
};

void ResetGameHeaderCache();
void GetDirectoryList(const char *path, std::string &list);
void GetListWBFS(std::string &list);
bool isCacheCurrent();
bool isCacheFile(const std::string &filename);

// EmuNAND
void SaveGameHeaderCache(std::vector<struct discHdr> &list);
void LoadGameHeaderCache(std::vector<struct discHdr> &list);

// Wii
void SaveGameHeaderCache(std::vector<struct discHdr> &list, std::vector<int> &plist);
void LoadGameHeaderCache(std::vector<struct discHdr> &list, std::vector<int> &plist);

// GameCube
void SaveGameHeaderCache(std::vector<struct discHdr> &list, std::vector<std::string> &plist);
void LoadGameHeaderCache(std::vector<struct discHdr> &list, std::vector<std::string> &plist);

