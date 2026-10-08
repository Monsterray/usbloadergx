#!/bin/sh
# Check that every section of a DOL can be loaded the way IOS, the apploaders,
# the Homebrew Channel and Dolphin load it: each size rounded up to 32 bytes,
# read from its file offset. A section that is not a multiple of 32 bytes, or
# whose rounded size reads past the end of the file, fails. Dolphin refuses such
# a DOL with "Failed to init core".
#
#   usage: scripts/check-dol.sh boot.dol
set -eu

dol="${1:?usage: $0 file.dol}"
filesize=$(wc -c < "$dol")

# 7 text + 11 data sections: file offsets at 0x00, sizes at 0x90.
od -An -v -tu1 -N216 "$dol" | awk -v filesize="$filesize" -v dol="$dol" '
	{ for (i = 1; i <= NF; i++) b[n++] = $i }
	function be32(o) { return ((b[o] * 256 + b[o + 1]) * 256 + b[o + 2]) * 256 + b[o + 3] }
	END {
		if (n < 216) { print dol ": too short for a DOL header"; exit 1 }
		bad = 0
		for (s = 0; s < 18; s++) {
			off = be32(s * 4); size = be32(0x90 + s * 4)
			if (size == 0) continue
			name = s < 7 ? sprintf("text%d", s) : sprintf("data%d", s - 7)
			end = off + int((size + 31) / 32) * 32
			if (size % 32 != 0) { printf "%s: %s size 0x%x is not a multiple of 32\n", dol, name, size; bad = 1 }
			if (end > filesize) { printf "%s: %s at 0x%x reads to 0x%x, past the end of the file (0x%x)\n", dol, name, off, end, filesize; bad = 1 }
		}
		if (!bad) print dol ": all sections load"
		exit bad
	}'
