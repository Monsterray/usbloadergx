#!/bin/sh
# Regression guard for GameBooter::BootNintendont().
#
# The Nintendont binary that gets launched is chosen from the *effective*
# loader path (per-game override, else the global setting). The version /
# build-date probe that decides the NIN_CFG layout must read the same path,
# otherwise USB Loader GX can launch one Nintendont while building a config
# for a different one (see the memory card emulation flags and CFG version).
#
# Usage: tests/check-nintendont-loader-path.sh [path/to/GameBooter.cpp]
set -u

FILE="${1:-$(dirname "$0")/../source/usbloader/GameBooter.cpp}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# Extract only the body of BootNintendont() (from its definition up to the
# next GameBooter:: method) so unrelated uses of the global path are ignored.
body=$(awk '
	/^int GameBooter::BootNintendont\(/ { on = 1 }
	on && /^int GameBooter::/ && !/BootNintendont/ { exit }
	on { print }
' "$FILE")

[ -n "$body" ] || fail "could not locate GameBooter::BootNintendont() in $FILE"

# 1. The version probe must use the effective path.
echo "$body" | grep -q 'nintendontVersion(ninLoaderPath,' \
	|| fail "nintendontVersion() is not called with ninLoaderPath"
echo "$body" | grep -q 'nintendontBuildDate(ninLoaderPath,' \
	|| fail "nintendontBuildDate() is not called with ninLoaderPath"

# 2. The launched .dol must be built from the effective path.
echo "$body" | grep -q '"%sboot.dol", ninLoaderPath' \
	|| fail "boot.dol is not resolved from ninLoaderPath"

# 3. The global setting may appear exactly once: when ninLoaderPath is derived.
count=$(echo "$body" | grep -c 'Settings\.NINLoaderPath')
[ "$count" -eq 1 ] \
	|| fail "expected exactly 1 use of Settings.NINLoaderPath in BootNintendont(), found $count"

[ "$status" -eq 0 ] && echo "OK: BootNintendont() launches and probes Nintendont from the same path"
exit "$status"
