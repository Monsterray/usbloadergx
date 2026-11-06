#!/bin/sh
# Regression guard for the parental lock.
#
# Two actions undo what a parent set up: "Reset All Game Settings" removes every
# per-game Locked flag (CGameSettings::RemoveAll), and "Default Settings" runs
# CSettings::Reset(), which sets godmode = 1, empties unlockCode and sets
# ParentalBlocks back to BLOCK_ALL. Both have to ask for an unlocked console
# before they run, and the password must stay out of the log.
#
# Usage: tests/check-parental-gates.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# gate_before <file> <start-regex> <action-regex>: the lines between the line
# matching <start-regex> and the first line matching <action-regex> must hold
# "!Settings.godmode" (and not only a BLOCK_* test, which is off by default).
gate_before() {
	file="$1"; start="$2"; action="$3"; what="$4"
	[ -f "$file" ] || { fail "$file is missing"; return; }
	awk -v start="$start" -v action="$action" '
		!on && $0 ~ start { on = 1; next }
		on && $0 ~ action { exit found ? 0 : 1 }
		on && /!Settings\.godmode[ \t]*\)/ { found = 1 }
		END { if (!on) exit 2 }
	' "$file"
	case $? in
		0) ;;
		2) fail "$what: cannot find the handler in $file" ;;
		*) fail "$what runs without an unlocked-console check ($file)" ;;
	esac
}

# 1. Reset All Game Settings
gate_before "$SRC/settings/menus/FeatureSettingsMenu.cpp" \
	'//! Reset All Game Settings' 'GameSettings[.]RemoveAll' "Reset All Game Settings"

# 2. Default Settings
gate_before "$SRC/settings/menus/GlobalSettings.cpp" \
	'//! Default Settings' 'Settings[.]Reset[(][)]' "Default Settings"

# 3. The default-settings gate may not be only the optional BLOCK_RESET_SETTINGS bit.
awk '/\/\/! Default Settings/ { on = 1 } on && /Settings\.Reset\(\)/ { exit }
	on { print }' "$SRC/settings/menus/GlobalSettings.cpp" |
	grep -qE 'if\(!Settings\.godmode\)' \
	|| fail "Default Settings is gated by BLOCK_RESET_SETTINGS alone: a locked console with that bit off can reset its own password"

# 4. The on-screen keyboard is used for the parental password: never print its buffer.
grep -E 'gprintf\("[^"]*OnScreenKeyboard\([^"]*%s' "$SRC/prompts/PromptWindows.cpp" >/dev/null \
	&& fail "OnScreenKeyboard() logs the text it edits (the password) with %s"

# 5. A quick boot by ID, a disc and the disc button never pass the game list's filter:
#    GameBooter::BootGame() has to apply it itself, before anything boots.
awk '/^int GameBooter::BootGame\(/ { on = 1 } on && /ParentalBlocked\(gameHdr\)/ { found = 1; exit }
	on && /BootGCMode\(|Sys_LoadHBC\(/ { exit } END { exit !found }' "$SRC/usbloader/GameBooter.cpp" \
	|| fail "GameBooter::BootGame() boots without the parental check (ParentalBlocked)"

[ "$status" -eq 0 ] && echo "OK: parental reset gates are in place"
exit "$status"
