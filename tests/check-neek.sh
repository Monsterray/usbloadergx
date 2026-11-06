#!/bin/sh
# Regression guard for the neek boot path (GameBooter::BootNeek, usbloader/neek.cpp).
#
# neek.cpp's file handling has a host test (tests/host/neek_test.cpp). This guard covers
# what that cannot reach: the call sites in GameBooter.cpp, which only run on a console.
#
# Usage: tests/check-neek.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# 1. NEEK_CFG::gameid is the first four bytes of the game ID as a big-endian u32.
#    `(u32)gameHeader.id` casts the ADDRESS of the u8 id[6] array (a stack local) and
#    handed neek a pointer. Every assignment must go through neekGameId4().
grep -n 'gameid *=' "$SRC/usbloader/GameBooter.cpp" | grep -v 'neekGameId4(' \
	&& fail "GameBooter.cpp assigns neek_config->gameid without neekGameId4()"
grep -q 'neek_config->gameid = neekGameId4(gameHeader.id)' "$SRC/usbloader/GameBooter.cpp" \
	|| fail "GameBooter.cpp no longer sets neek_config->gameid with neekGameId4()"
grep -n '(u32) *gameHeader\.id\|(u32) *gameHdr->id' "$SRC/usbloader/GameBooter.cpp" \
	&& fail "GameBooter.cpp casts the address of a game ID array to u32"

# 2. LoadFileToMem() returns 1 on success and a negative code on failure; a bare
#    `if (LoadFileToMem(...))` takes the failure for success. neek.cpp must compare.
grep -n 'if *(LoadFileToMem *([^)]*))' "$SRC/usbloader/neek.cpp" \
	&& fail "neek.cpp tests LoadFileToMem() as a boolean"

# 3. neek2oSetNAND() adds an EmuNAND only below NANDCONFIG_MAXNAND: a 9th made the next
#    call delete the whole nandcfg.bin as "corrupt".
grep -q 'NandCnt >= NANDCONFIG_MAXNAND' "$SRC/usbloader/neek.cpp" \
	|| fail "neek2oSetNAND() no longer refuses a NAND beyond NANDCONFIG_MAXNAND"

[ "$status" -eq 0 ] && echo "OK: neek checks pass"
exit "$status"
