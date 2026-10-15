#ifndef HOMEBREWAPPS_H_
#define HOMEBREWAPPS_H_

#include <string>
#include <vector>
#include "usbloader/disc.h"

//! The apps in Settings.homebrewapps_path as game list entries, so the game browser can
//! show them in every layout: type TYPE_GAME_HOMEBREW, a made-up ID ("HB" and four
//! characters from the app's path), the meta.xml name as title, the boot.dol or boot.elf
//! path in path[].
class HomebrewApps
{
	public:
		static HomebrewApps *Instance();
		//! Scans the apps folder again, for when the card may have changed
		void Reload();
		//! The entries, scanned on first use
		std::vector<struct discHdr> &GetHeaders();
		//! The app's folder, with a trailing '/'
		static std::string Folder(const struct discHdr *header);
		//! The homebrew prompt (name, author, description), then the app with its meta.xml
		//! arguments. Returns only if the user cancels or loading fails.
		static void Launch(const struct discHdr *header);
	private:
		HomebrewApps() : loaded(false) {}
		std::vector<struct discHdr> headers;
		bool loaded;
};

#endif
