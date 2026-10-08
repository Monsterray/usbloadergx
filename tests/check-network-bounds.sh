#!/bin/sh
# Regression guard for the network layer.
#
# Everything checked here comes off the network: an HTTP response header, the
# body of a download, a zip from GitHub, a wiiload connection from the LAN. The
# loader does not verify certificates (WOLFSSL_VERIFY_NONE), so a server in the
# path chooses all of it. None of this runs in Dolphin without a network.
#
# Usage: tests/check-network-bounds.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# 1. get_header_value() copied value_len bytes into a fixed buffer with no size.
#    A Transfer-Encoding longer than eight bytes overflowed a stack array, and a
#    Location header could write about two kilobytes past one.
grep -q 'char \*dst, size_t dst_size, char \*header' "$SRC/network/https.c" \
	|| fail "get_header_value() writes a network header into a buffer with no size"
grep -q 'headers\[i\].name_len == header_len' "$SRC/network/https.c" \
	|| fail "a short header name still matches a longer one"

# 2. update.cpp runs atoi() and strchr() over the downloaded body.
grep -q 'buffer->data\[size\] =' "$SRC/network/https.c" \
	|| fail "the download buffer is not terminated for the callers that read it as text"

# 3. realloc leaves the old block allocated when it fails, and a failed shrink
#    used to be reported as a finished download.
grep -q 'char \*grown = MEM2_realloc' "$SRC/network/https.c" \
	|| fail "a failed realloc loses the download buffer"

# 4. A redirect chose the length of a stack array.
grep -q 'domainlength <= 0 || domainlength > 255' "$SRC/network/https.c" \
	|| fail "downloadfile() sizes the host array with a length off the network"

# 5. wiitdb.zip and txt.zip are downloaded and extracted. An entry name may not
#    step out of the destination directory.
#    The check is shared with the other extractor and with the wiiload receiver;
#    tests/check-thirdparty-bounds.sh covers the other two call sites.
grep -q 'IsSafeRelativePath' "$SRC/ZipFile.cpp" \
	|| fail "ExtractAll() writes a zip entry name straight into the destination path"
grep -q 'u32 blocksize = uncompressed_size - done < maxblocksize' "$SRC/ZipFile.cpp" \
	|| fail "the zip block size is shared between entries, so one empty entry empties the rest"

# 6. The Wiinnertag URL and key come out of the user's XML, and replaceString
#    grows the string in place.
grep -q 'int replaceString(char \*string, size_t size' "$SRC/utils/StringTools.c" \
	|| fail "replaceString() expands into a buffer whose size it does not know"
grep -qE '\bstrcpy\(sendURL' "$SRC/network/Wiinnertag.cpp" \
	&& fail "Wiinnertag::Send() copies the URL with no bound"

# 7. URL_List looked five bytes behind the buffer on the first character, and
#    freed the download twice on an allocation failure.
grep -q 'cnt >= 5 && file.data\[cnt\]' "$SRC/network/URL_List.cpp" \
	|| fail "URL_List reads in front of the downloaded page"
grep -q 'ind >= urlcount || ind < 0' "$SRC/network/URL_List.cpp" \
	|| fail "URL_List::GetURL() accepts one index past the end"

# 8. Anything on the LAN can open the wiiload connection.
grep -q 'gotHeader = (net_read(local_connection, &haxx, 8) == 8)' "$SRC/network/networkops.cpp" \
	|| fail "NetworkWait() uses the wiiload header without checking the read"

# 9. A failed DNS lookup used to be cached, so one bad moment stopped that host
#    working until the next reboot.
grep -q 'if (ip == 0)' "$SRC/network/dns.c" \
	|| fail "getipbynamecached() caches a failed lookup"

[ "$status" -eq 0 ] && echo "OK: network bounds guards are in place"
exit "$status"
