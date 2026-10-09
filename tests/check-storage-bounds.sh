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

# 5. On IOS58 USB goes through usbstorage_libogc.c. Shutdown() freed the arena while
#    __inited stayed set, so Initialize() returned early on the next Startup() and every
#    remount ran on freed memory; libogc walks that heap with interrupts disabled, and
#    the console froze, Reset and Power included (switching the display type to Wii games
#    with none listed did it). Initialize() also returned with interrupts disabled.
awk '/^static bool __usbstorage_ogc_Shutdown/,/^}/' "$SRC/usbloader/usbstorage_libogc.c" | grep -q 'MEM2_free' 	&& fail "the IOS58 USB Shutdown() frees the arena Initialize() keeps using"
awk '/^s32 USBStorage_OGC_Initialize/,/^}/' "$SRC/usbloader/usbstorage_libogc.c" | grep -B2 'return IPC_ENOMEM' | grep -q '_CPU_ISR_Restore' 	|| fail "USBStorage_OGC_Initialize() returns with interrupts disabled"

# 6. A drive with no partition table (an SD card formatted as a "superfloppy", as
#    Dolphin's SD image is) used to get the made-up size 0xdeadbeaf, 1.7 TiB, which
#    the partition menus showed and WBFS formatting would have sized its free block
#    table by. The size comes from the boot sector now (BootSector.c, tested by
#    tests/host/boot_sector_test.c), 0 means unknown, and a format refuses 0.
#    libwbfs shifts and allocates by its header and checks none of it.
grep -qi '0xdeadbeaf' "$SRC/Controls/PartitionHandle.cpp" \
	&& fail "PartitionHandle.cpp gives a partition a made-up sector count"
grep -q 'BootSectorSecCount(' "$SRC/Controls/PartitionHandle.cpp" \
	|| fail "AddPartition() does not take an unknown size from the boot sector"
grep -q '1 << head->hd_sec_sz_s' "$SRC/Controls/PartitionHandle.cpp" \
	&& fail "AddPartition() shifts by a WBFS header field it has not checked"
grep -q 'WbfsHeadSecCount(' "$SRC/usbloader/wbfs/wbfs_wbfs.cpp" \
	|| fail "Wbfs_Wbfs::Open() hands libwbfs a WBFS header it has not checked"
awk '/^s32 Wbfs_Wbfs::Format/,/^}/' "$SRC/usbloader/wbfs/wbfs_wbfs.cpp" | grep -q 'if (size == 0)' \
	|| fail "Wbfs_Wbfs::Format() formats a partition whose size is unknown"

[ "$status" -eq 0 ] && echo "OK: storage bounds guards are in place"
exit "$status"
