#!/bin/sh
# Regression guard for the vendored trees and the code that sits in them.
#
# Two of the files under these directories are not third party at all:
# source/xml/GameTDB.cpp is this project's reader for a downloaded wiitdb.xml,
# and source/utils/minizip/miniunz.c is minizip with this project's extractZip()
# built on top of it. Both were excluded from cppcheck as third party and had
# never been analysed. The zip they extract and the name that goes with it both
# arrive over the network.
#
# Usage: tests/check-thirdparty-bounds.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# 1. There are two zip extractors and a wiiload receiver, and all three join a
#    name they were given to a destination directory. One shared check.
#    tests/host/safe_path_test.c runs it against the names that matter.
grep -q 'bool IsSafeRelativePath(const char \*name)' "$SRC/FileOperations/SafePath.c" \
	|| fail "the shared path check is missing"
grep -q 'IsSafeRelativePath' "$SRC/ZipFile.cpp" \
	|| fail "ZipFile::ExtractAll() does not check the entry name"
grep -q 'IsSafeRelativePath' "$SRC/utils/minizip/miniunz.c" \
	|| fail "do_extract_currentfile() does not check the entry name"
grep -q 'IsSafeRelativePath' "$SRC/homebrewboot/HomebrewBrowser.cpp" \
	|| fail "the wiiload filename is used without checking it"

# 2. fullfilename() called strlen on basedir before testing it for null, which
#    extractZipOnefile() passes, and had no room for the separator it writes.
grep -q 'size_t baselen = basedir ? strlen(basedir) : 0' "$SRC/utils/minizip/miniunz.c" \
	|| fail "fullfilename() reads basedir before checking it for null"
grep -q 'malloc(baselen + strlen(filename) + 2)' "$SRC/utils/minizip/miniunz.c" \
	|| fail "fullfilename() has no room for the separator it writes"

# 3. strcpy writes one more byte than strlen reports, and strstr can miss.
grep -q 'malloc(strlen(filename_withpath) + 1)' "$SRC/utils/minizip/miniunz.c" \
	|| fail "do_extract_currentfile() copies a path into a buffer one byte short"
grep -qE 'char \*ptr = strstr\(path, filename_withoutpath\);' "$SRC/utils/minizip/miniunz.c" \
	&& fail "do_extract_currentfile() writes through an unchecked strstr result"

# 4. The receiver acted on a buffer before the transfer had finished, and the
#    sender chooses infilesize.
grep -q 'read != infilesize || infilesize < 4' "$SRC/homebrewboot/HomebrewBrowser.cpp" \
	|| fail "the wiiload buffer is read before the transfer has finished"

# 5. gameID is char[7]: six characters and a terminator.
grep -q 'i < 6 && \*idNode' "$SRC/xml/GameTDB.cpp" \
	|| fail "ParseFile() writes the gameID terminator past the end of the field"

# 6. A game node with no </game> inside one read window used to leave the read
#    position where it was, so the same window was parsed for ever.
grep -q 'if (gameNode == Line)' "$SRC/xml/GameTDB.cpp" \
	|| fail "ParseFile() loops for ever on a game node longer than the window"

# 7. std::stoul throws, and the value is an attribute of a downloaded xml.
grep -q 'strtoul(color.c_str()' "$SRC/xml/GameTDB.cpp" \
	|| fail "GetCaseColor() throws on a case colour that is not hex"

# 8. The offsets cache on the card gives a count that sizes an allocation, and
#    fread returns size_t, so the old short-read test could never be true.
grep -q 'NodeCount > MAX_OFFSET_ENTRIES' "$SRC/xml/GameTDB.cpp" \
	|| fail "the offsets cache count is not bounded before it sizes the vector"
grep -q '!= NodeCount \* sizeof(GameOffsets)' "$SRC/xml/GameTDB.cpp" \
	|| fail "a truncated offsets cache is accepted"
grep -q 'offset->nodesize > MAXREADSIZE' "$SRC/xml/GameTDB.cpp" \
	|| fail "LoadGameNode() takes a node size out of the cache with no bound"

# 9. OffsetDBPath.back() on an empty string is undefined, and the relative-path
#    branch used to produce one.
grep -q 'OffsetDBPath.empty() || OffsetDBPath.back()' "$SRC/xml/GameTDB.cpp" \
	|| fail "LoadGameOffsets() calls back() on a string that may be empty"

# 10. ParseGameNode() compares GameIDCache before anything has written it, so
#     both constructors have to set it.
cachedinit=$(grep -c "GameIDCache\[0\] = " "$SRC/xml/GameTDB.cpp")
[ "$cachedinit" -ge 3 ] \
	|| fail "GameIDCache is not initialised in both constructors"

# 11. unzOpen2() wrote through the allocation without checking it.
grep -q 'if (s == NULL)' "$SRC/utils/unzip.c" \
	|| fail "unzOpen2() writes through an unchecked allocation"

# 12. Excluding whole directories from cppcheck hid GameTDB and miniunz.
grep -q 'source/xml/pugixml.cpp' "$(dirname "$0")/../scripts/cppcheck.sh" \
	|| fail "cppcheck still excludes the whole of source/xml"
grep -q 'i "$ROOT/source/utils/minizip"' "$(dirname "$0")/../scripts/cppcheck.sh" \
	&& fail "cppcheck still excludes the whole of source/utils/minizip"

[ "$status" -eq 0 ] && echo "OK: third party bounds guards are in place"
exit "$status"
