#!/bin/sh
# Regression guard for the system and NAND layer.
#
# Everything checked here is read out of a title the loader did not write: a
# title.tmd or a .app from NAND, the same files under the emulated NAND path on
# the user's card, a .wad the user installs, and a theme's font.ttf. None of it
# is verified before it is parsed, and almost none of it can be tested in
# Dolphin, which has no cIOS and no NAND to speak of.
#
# Usage: tests/check-system-bounds.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# 1. Four functions cast a file to a tmd and walked contents[] with num_contents
#    out of that same file. GetDol() also indexed contents[] with boot_index.
grep -q 'static tmd \*TmdFromBuffer' "$SRC/Channels/channels.cpp" \
	|| fail "a title.tmd is walked without checking the buffer holds its contents"
grep -q 'boot_index >= tmd_file->num_contents' "$SRC/Channels/channels.cpp" \
	|| fail "GetDol() indexes contents[] with a boot_index out of the file"
grep -q 'u32 tmdSize, bool &isForwarder' "$SRC/Channels/channels.h" \
	|| fail "GetDol() is given a tmd buffer with no length"

# 2. The banner header was read at 0x40 of a .app of unknown length.
grep -q 'filesize < IMET_OFFSET + sizeof(IMET)' "$SRC/Channels/channels.cpp" \
	|| fail "GetOpeningBnr() reads an IMET past the end of the content"

# 3. CONF_GetLanguage() returns a negative error code, and the language picks
#    one of the ten name arrays inside the IMET.
grep -q 'language < CONF_LANG_JAPANESE || language > CONF_LANG_KOREAN' "$SRC/Channels/channels.cpp" \
	|| fail "the IMET language index is not bounded below in channels.cpp"
grep -q 'language < CONF_LANG_JAPANESE || language > CONF_LANG_KOREAN' "$SRC/wad/nandtitle.cpp" \
	|| fail "the IMET language index is not bounded below in nandtitle.cpp"

# 4. The ten IMET names were ten separate fields, and every reader indexed past
#    the end of the first one to reach the rest.
grep -q 'u16 names\[IMET_LANGUAGE_COUNT\]\[IMET_MAX_NAME_LEN\]' "$SRC/wad/nandtitle.h" \
	|| fail "the IMET names are read by indexing past the end of name_japanese"

# 5. A channel name that fills the IMET array carries no terminator, and both
#    copies are handed straight to wString, which calls wcslen.
grep -q 'wchar_t wName\[IMET_MAX_NAME_LEN + 1\]' "$SRC/Channels/channels.cpp" \
	|| fail "the emulated channel name is passed to wString unterminated"
grep -q 'wchar_t name\[IMET_MAX_NAME_LEN + 1\]' "$SRC/wad/nandtitle.cpp" \
	|| fail "the NAND channel name is passed to wString unterminated"

# 6. The NAND extractor appended each level of the walk to one ISFS_MAXPATH
#    buffer with strcat and no bound.
grep -q 'needed > ISFS_MAXPATH' "$SRC/wad/nandtitle.cpp" \
	|| fail "InternalExtractDir() appends to the NAND path with no bound"

# 7. A short ISFS read left the tail of the buffer uninitialised while the
#    caller was told it held the whole file.
grep -q '(u32) ret != filesize' "$SRC/wad/nandtitle.cpp" \
	|| fail "LoadFileFromNand() reports a short read as a whole file"

# 8. A .wad chooses tik_len and tmd_len, and both are read through a struct.
grep -q 'IS_VALID_SIGNATURE((u32 \*) p_tmd)' "$SRC/wad/wad.cpp" \
	|| fail "Wad::Open() accepts a tmd too short for the contents it declares"
grep -q 'IS_VALID_SIGNATURE((u32 \*) p_tik)' "$SRC/wad/wad.cpp" \
	|| fail "Wad::Open() accepts a ticket too short for its title key"

# 9. UnInstall() trimmed the trailing slash off the caller's buffer, which is
#    Settings.NandEmuChanPath.
grep -q 'basepath\[960\]' "$SRC/wad/wad.cpp" \
	|| fail "Wad::UnInstall() writes through its const installpath argument"

# 10. The MIOS scan read up to 52 bytes past the bound it tested, and the date
#    it found went to strptime without a terminator.
grep -q 'i + MIOS_MATCH_LEN <= filesize' "$SRC/system/IosLoader.cpp" \
	|| fail "GetMIOSInfo() scans past the end of the content"
grep -q 'CopyReleaseDate' "$SRC/system/IosLoader.cpp" \
	|| fail "the MIOS release date reaches strptime unterminated"
grep -q 'filesize < sizeof(iosinfo_t)' "$SRC/system/IosLoader.cpp" \
	|| fail "GetIOSInfo() reads name and baseios out of a short .app"

# 11. A theme chooses font.ttf, and FreeType leaves the face null for a file it
#     will not parse.
grep -q 'bool IsLoaded() const' "$SRC/FreeTypeGX.h" \
	|| fail "FreeTypeGX cannot report a font it failed to load"
grep -q 'fontSystem->IsLoaded()' "$SRC/themes/CTheme.cpp" \
	|| fail "Theme::LoadFont() keeps a font that has no face behind it"

# 12. argv[0] was read before anything checked there was an argument.
grep -q 'argc > 0 && argv && argv\[0\]' "$SRC/StartUpProcess.cpp" \
	|| fail "StartUpProcess::Run() reads argv[0] without checking argc"

# 13. gprintf is a printf, so let the compiler check its call sites.
grep -q 'format(printf, 1, 2)' "$SRC/gecko.h" \
	|| fail "gprintf is declared without the printf format attribute"
grep -qE '\bgprintf\(tmp\)' "$SRC/StartUpProcess.cpp" \
	&& fail "SetTextf() passes an expanded message to gprintf as a format"

# 14. GX's MEM2 heap runs from 0x90200000 to the top of the SYS arena less what
#     libogc takes later. Nothing may write a fixed address inside it while GX
#     runs, and the heap must leave out the hbc agent's records at 0x91800000.
grep -q 'HBC_RECORDS_START' "$SRC/memory/mem2.cpp" \
	|| fail "the MEM2 heap does not leave out the hbc agent's records"
grep -q 'static u32 \*buffer = (u32 \*) 0x93' "$SRC/usbloader/disc.c" \
	&& fail "disc.c reads the disc into a fixed address inside the MEM2 heap"
grep -q 'homebrewbuffer = EXECUTE_ADDR' "$SRC/homebrewboot/BootHomebrew.cpp" \
	&& fail "the homebrew image is copied to EXECUTE_ADDR while GX still runs"
grep -q 'memmove(EXECUTE_ADDR, homebrewbuffer' "$SRC/homebrewboot/BootHomebrew.cpp" \
	|| fail "RunAppbooter() does not move the image to EXECUTE_ADDR at the end"

[ "$status" -eq 0 ] && echo "OK: system and NAND bounds guards are in place"
exit "$status"
