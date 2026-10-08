#!/bin/sh
# Regression guard for the disc boot path.
#
# These four defects were found by reading the code, not by the compiler or by
# cppcheck, and each of them would come back from a plain copy-paste. Dolphin
# emulates no disc drive, so nothing at run time catches them either.
#
# Usage: tests/check-boot-path.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# 1. Disc_FindPartition() reads 0x20 bytes of the partition table, which holds
#    four entries. The disc supplies the entry count, so it must be clamped.
grep -q 'if (nb_partitions > 4) nb_partitions = 4;' "$SRC/usbloader/disc.c" \
	|| fail "Disc_FindPartition() does not clamp nb_partitions to the four entries it read"

# 2. Disc_Mount() gets 0x60 bytes from the disc. Copying sizeof(struct discHdr)
#    filled type, encryption and path[260] from live low memory, and the callers
#    read path.
grep -q 'memcpy(header, diskid, sizeof(struct discHdr))' "$SRC/usbloader/disc.c" \
	&& fail "Disc_Mount() copies more than the 0x60 bytes it read into struct discHdr"

# 3. load_dol_image() must flush the text sections it just copied. Using
#    data_start/data_size here leaves the text dirty in the data cache: correct
#    under Dolphin's default, wrong on a console.
text_loop=$(awk '/^u32 load_dol_image/ { on = 1 } on && /^u32 Load_Dol_from_disc/ { exit } on && /i < 7/ { body = 1 } body { print } body && /^\t}/ { exit }' "$SRC/usbloader/alternatedol.c")
[ -n "$text_loop" ] || fail "could not locate the text-section loop in load_dol_image()"
echo "$text_loop" | grep -q 'DCFlushRange((void \*) dolfile->text_start\[i\], dolfile->text_size\[i\])' \
	|| fail "load_dol_image() does not flush the text section it copied"
echo "$text_loop" | grep -q 'data_start\[i\]' \
	&& fail "load_dol_image() still uses the data sections inside the text loop"

# 4. splits.c builds split file names in a caller buffer. strcpy()/strcat() there
#    overflowed it by the length of ".tmp".
grep -qE '\b(strcpy|strcat)\(' "$SRC/usbloader/splits.c" \
	&& fail "splits.c uses an unbounded string copy for split file names"

[ "$status" -eq 0 ] && echo "OK: disc boot path guards are in place"
exit "$status"
