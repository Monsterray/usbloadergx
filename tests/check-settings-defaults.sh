#!/bin/sh
# Regression guard for the settings layer.
#
# A setting with no default holds whatever was on the stack until a config file
# supplies it, and two of these index a string table. The title cache and the
# new-title list take their sizes and their strings from files on the device.
#
# Usage: tests/check-settings-defaults.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# 1. Saved settings that index KeyboardText[] and PromptButtonsText[], and the
#    one that picks the browser layout, must have a default.
for setting in keyset wsprompt gameDisplay; do
	awk '/^void CSettings::SetDefault/ { on = 1 } on { print } on && /^}/ { exit }' \
		"$SRC/settings/CSettings.cpp" | grep -qE "^\s+$setting = " \
		|| fail "CSettings::SetDefault() does not set $setting"
done

# 2. Runtime state that SetDefault() must NOT touch, because Reset() reads
#    SDMode after calling it. In-class initialisers instead.
grep -q 'bool skipSaving = false;' "$SRC/settings/CSettings.h" \
	|| fail "skipSaving has no initialiser"
grep -q 'short SDMode = OFF;' "$SRC/settings/CSettings.h" \
	|| fail "SDMode has no initialiser"

# 3. The cached title count comes out of TitlesCache.bin.
grep -q 'remaining / (long) sizeof(CacheTitle)' "$SRC/settings/GameTitles.cpp" \
	|| fail "ReadCachedTitles() does not bound the record count by the file size"
grep -qE '\bstrcpy\(' "$SRC/settings/GameTitles.cpp" \
	&& fail "GameTitles.cpp copies a cached string without its length"

# 4. Removing the last node has to move lastTitle, or the next CheckGame()
#    writes through a freed pointer.
grep -q 'if (lastTitle == t)' "$SRC/settings/newtitles.cpp" \
	|| fail "NewTitles::Remove() does not update lastTitle"

[ "$status" -eq 0 ] && echo "OK: settings defaults and cache guards are in place"
exit "$status"
