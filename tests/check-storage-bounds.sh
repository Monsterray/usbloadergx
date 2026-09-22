#!/bin/sh
# Regression guard for the storage layer.
#
# Every value checked here comes off a USB drive or an SD card: a GPT header, a
# directory name, a cached path. Dolphin emulates no USB mass storage, so none
# of it runs before a real device is attached.
#
# Usage: tests/check-storage-bounds.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# 1. MAX_PARTITIONS was declared and never used, so a bogus GPT entry count
#    filled PartitionList without limit. AddPartition() is the one place every
#    discovery path ends.
grep -q 'PartitionList.size() >= MAX_PARTITIONS' "$SRC/Controls/PartitionHandle.cpp" \
	|| fail "AddPartition() does not honour MAX_PARTITIONS"

# 2. CheckGPT() divides BYTES_PER_SECTOR by part_entry_size and indexes the
#    sector buffer by it. The disk supplies the value.
grep -q 'part_entry_size < sizeof(GUID_PART_ENTRY)' "$SRC/Controls/PartitionHandle.cpp" \
	|| fail "CheckGPT() does not validate part_entry_size before dividing by it"

# 3. CheckLayoutB() copies a directory name into a TITLE_LEN buffer and then
#    writes a terminator at len - 8. A name longer than the buffer wrote past it.
grep -q 'len <= 8 || len - 8 >= TITLE_LEN' "$SRC/usbloader/wbfs/wbfs_fat.cpp" \
	|| fail "CheckLayoutB() does not reject names too long for fname_title"
grep -q 'strncpy(fname_title, fname, TITLE_LEN);' "$SRC/usbloader/wbfs/wbfs_fat.cpp" \
	&& fail "CheckLayoutB() still leaves fname_title unterminated"

# 4. The GameCube header cache holds the game path in a fixed array.
grep -qE '\bstrcpy\(' "$SRC/cache/cache.cpp" \
	&& fail "cache.cpp uses an unbounded copy for a cached game path"

[ "$status" -eq 0 ] && echo "OK: storage bounds guards are in place"
exit "$status"
