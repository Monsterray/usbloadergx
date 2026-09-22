#! /bin/bash
#
# Automatic resource file list generation
# Created by Dimok

outFile="./source/themes/filelist.h"

# Need to use GNU find and echo on Mac OS X
if [[ $(uname -s) == Darwin ]]; then
	ECHO=gecho
	FIND=gfind
else
	ECHO=echo
	FIND=find
fi

# One directory at a time, each sorted in the C locale. filelist.h is tracked,
# so the generated list has to be the same bytes on every machine: raw find
# order follows the filesystem, and a plain sort follows the user's locale.
count=0
for dir in ./data/images/ ./data/sounds/ ./data/fonts/ ./data/binary/
do
	for i in $($FIND $dir -maxdepth 1 -type f \( ! -printf "%f\n" \) | LC_ALL=C sort)
	do
		files[count]=$i
		count=$((count+1))
	done
done

# Write to a temporary file and keep the existing one when nothing changed.
# Comparing the contents catches a renamed or replaced resource, which a count
# of the files does not: the old header would still name a file that is gone.
tmpFile="$(mktemp)"
trap 'rm -f "$tmpFile"' EXIT

cat <<EOF > $tmpFile
/****************************************************************************
 * USB Loader GX resource files.
 * This file is generated automatically.
 * Includes $count files.
 *
 * NOTE:
 * Any manual modification of this file will be overwriten by the generation.
 ****************************************************************************/
#ifndef _FILELIST_H_
#define _FILELIST_H_

#include <gctypes.h>

EOF

for i in ${files[@]}
do
	filename=${i%.*}
	extension=${i##*.}
	$ECHO '#include "'$filename'_'$extension'.h"' >> $tmpFile
done

$ECHO '#include "Resources.h"' >> $tmpFile
$ECHO '' >> $tmpFile
$ECHO 'RecourceFile Resources::RecourceFiles[] =' >> $tmpFile
$ECHO '{' >> $tmpFile

for i in ${files[@]}
do
	filename=${i%.*}
	extension=${i##*.}
	$ECHO -e '\t{"'$i'", '$filename'_'$extension', '$filename'_'$extension'_size, NULL, 0},' >> $tmpFile
done

$ECHO -e '\t{"listBackground.png", NULL, 0, NULL, 0},\t// Optional' >> $tmpFile
$ECHO -e '\t{"carouselBackground.png", NULL, 0, NULL, 0},\t// Optional' >> $tmpFile
$ECHO -e '\t{"gridBackground.png", NULL, 0, NULL, 0},\t// Optional' >> $tmpFile
$ECHO -e '\t{NULL, NULL, 0, NULL, 0}' >> $tmpFile
$ECHO '};' >> $tmpFile

$ECHO '' >> $tmpFile
$ECHO '#endif' >> $tmpFile

if ! cmp -s "$tmpFile" "$outFile"; then
	$ECHO "Generating filelist.h for $count files." >&2
	mv "$tmpFile" "$outFile"
fi
