#!/bin/sh
# Regression guard for the media decoders.
#
# Every value checked here comes out of a file the loader does not control: a
# WAV, AIFF or BNS the user put in a music folder, or the opening.bnr of a game,
# a channel or a GameCube ISO. Dolphin plays the sound and draws the banner, but
# a malformed file is what triggers any of this, not a normal run.
#
# Usage: tests/check-media-bounds.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# 1. The magic sniffer read the byte before testing the counter, and then read
#    four bytes where only one was known to be there.
grep -q 'while(counter < length && check\[0\] == 0)' "$SRC/SoundOperations/SoundHandler.cpp" \
	|| fail "GetSoundDecoder() reads past the buffer while skipping leading zeroes"
grep -q 'counter + 4 > length' "$SRC/SoundOperations/SoundHandler.cpp" \
	|| fail "GetSoundDecoder() reads a four byte magic without four bytes"

# 2. A WAV chunk size out of the file could wrap and send the walk backwards,
#    which made it read the same chunk forever.
grep -q 'NextOffset <= DataOffset' "$SRC/SoundOperations/WavDecoder.cpp" \
	|| fail "WavDecoder chunk walk can go backwards"

# 3. AIFF did the same, and left the COMM chunk uninitialised on a short file.
grep -q 'memset(&CommHdr, 0, sizeof(SAIFFCommChunk))' "$SRC/SoundOperations/AifDecoder.cpp" \
	|| fail "AifDecoder reads an uninitialised COMM chunk"
grep -q 'SSndChunk.size < 8' "$SRC/SoundOperations/AifDecoder.cpp" \
	|| fail "AifDecoder does not guard the SSND size underflow"

# 4. BNS made pointers out of two offsets before it checked either of them, and
#    Read() turned the file's loop points into an offset into the decoded buffer.
grep -q 'hdr.infoOffset + hdr.infoSize > size' "$SRC/SoundOperations/BNSDecoder.cpp" \
	|| fail "DecodefromBNS() does not bound infoOffset before using it"
grep -q 'loadBNSInfo(BNSInfo &bnsInfo, const u8 \*buffer, u32 avail)' "$SRC/SoundOperations/BNSDecoder.cpp" \
	|| fail "loadBNSInfo() follows the file's offsets without a length"
grep -q 'OutBlock.loopEnd > length / factor' "$SRC/SoundOperations/BNSDecoder.cpp" \
	|| fail "BNS loop points are not cut down to the decoded buffer"

# 5. A cached .bnr shorter than 0x40 made filesize wrap and copy about 4 GB.
grep -q 'filesize < 0x40 + sizeof(IMETHeader)' "$SRC/banner/OpeningBNR.cpp" \
	|| fail "LoadCachedBNR() can underflow filesize on the channel .app path"
grep -q 'filesize < sizeof(IMETHeader) || lang < 0' "$SRC/banner/OpeningBNR.cpp" \
	|| fail "GetIMETTitle() reads names without checking the banner length"
grep -q 'bnrSize < BNR_MIN_SIZE' "$SRC/banner/OpeningBNR.cpp" \
	|| fail "LoadGCBNR() returns a banner too short to read"
grep -q 'sizeof(openingBnr->description\[0\]) \* (language + 1)' "$SRC/banner/OpeningBNR.cpp" \
	|| fail "the BNR2 language bound is one description block short"

# 6. Every offset and count in a U8 archive comes out of the archive. The banner
#    layout, animation, texture and font readers all take what GetFile() returns.
grep -q 'root node is outside the archive' "$SRC/utils/U8Archive.cpp" \
	|| fail "U8Archive::SetData() does not bound rootNodeOffset"
grep -q 'InArchive( fst\[ entryNo \].fileoffset' "$SRC/utils/U8Archive.cpp" \
	|| fail "U8Archive::GetFile() can return a pointer outside the archive"
grep -q 'offset >= name_table_len' "$SRC/utils/U8Archive.cpp" \
	|| fail "FstName() can point up to 16 MB past the name table"
grep -qE '^\s+free\(name_table\);' "$SRC/utils/U8Archive.cpp" \
	&& fail "U8NandArchive::SetFile() frees a pointer into the fst allocation"

[ "$status" -eq 0 ] && echo "OK: media bounds guards are in place"
exit "$status"
