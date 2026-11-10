#!/usr/bin/env bash
# Fetch, check and build the libraries USB Loader GX takes from upstream source
# because devkitPro does not package them. Everything else (zlib, libpng,
# freetype, gd, libjpeg, libogg, tremor, libmad) comes from devkitPro's ppc-*
# packages in the toolchain image.
#
#   usage: deps/build.sh [PREFIX]      default PREFIX: $DEVKITPRO/portlibs/usbloadergx
#
# The Dockerfile's toolchain stage runs this. To build GX with a native
# devkitPro instead, run it once with the same devkitPPC and libogc as the
# Dockerfile; the Makefile looks in $(PORTLIBS_PATH)/usbloadergx.
#
# Each source is pinned by URL and sha256. To update one, change its three
# lines below, build, and test what it is used for (README "Building").
set -euo pipefail

WOLFSSL_VERSION=5.9.4
WOLFSSL_URL=https://github.com/wolfSSL/wolfssl/archive/refs/tags/v5.9.4-stable.tar.gz
WOLFSSL_SHA256=7256bfc89b183a75183806c7debfa203443873b0b4a562e1b80d68e01b45ac57

PUGIXML_VERSION=1.16
PUGIXML_URL=https://github.com/zeux/pugixml/releases/download/v1.16/pugixml-1.16.tar.gz
PUGIXML_SHA256=4cee1ca4aad395170f4c7a07824f3bdd41f28316c6e1e1090a1425b278ec0b4b

# minizip comes with zlib; only contrib/minizip is built, against devkitPro's zlib.
MINIZIP_VERSION=1.3.2
MINIZIP_URL=https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
MINIZIP_SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16

# The Homebrew Channel's in-app agent (sdk/hbc_agent.h), HBC 1.15.11 (develop b01e0c8): its way
# out leaves a wedged IOS (ES launch, then a reset) when GX's IoGuard ends a stuck USB or
# disc request.
HBCAGENT_VERSION=b01e0c850ca11ec81f6fddcd891ed6a70170f2a4
HBCAGENT_URL=https://github.com/Monsterray/hbc-reborn/archive/b01e0c850ca11ec81f6fddcd891ed6a70170f2a4.tar.gz
HBCAGENT_SHA256=ec861c7d48005843de45967f3e2a6b67694f0ce884bb0e8039a6133596d3464a

# libntfs-wii: Extrems' maintained NTFS-3G port (NTFS-3G 2026.9.28). GX patches it to libogc 3's
# classic DISC_INTERFACE (deps/libfs/ntfs/*.patch) and adds _NTFS_get_fragments() for the
# WBFS/ISO loader (deps/libfs/ntfs/*_frag.*, from GX's libs branch).
NTFS_VERSION=417e8ddb7141d8519be77afccf2e88eca370ad43
NTFS_URL=https://github.com/extremscorner/libntfs/archive/417e8ddb7141d8519be77afccf2e88eca370ad43.tar.gz
NTFS_SHA256=5fbaa98b9a834f82c7ca224e96b8622c8adf88d7f5025f15fe2bfdda794c3d29

# libfat: Extrems' maintained fork (libFAT 1.2.0 plus later work), the successor of devkitPro's
# libfat 1.1.5 that GX's prebuilt libcustomfat.a was built from. GX patches it to libogc 3's
# classic DISC_INTERFACE (deps/libfs/fat/*.patch) and adds _FAT_get_fragments() for the WBFS/ISO
# loader (deps/libfs/fat/fatfile_frag.*, from GX's libs branch). libfatversion.h is generated
# by upstream's Makefile and is not in the tarball.
FAT_VERSION=c756e1e3024e2ef2e037e99d3fd057b37ac02c5b
FAT_URL=https://github.com/extremscorner/libfat/archive/c756e1e3024e2ef2e037e99d3fd057b37ac02c5b.tar.gz
FAT_SHA256=46a7bc78938c7d57a61f1712da8125bc0f7d22912029f3c67d52a98bfbd0356f

# libext2fs-wii (e2fsprogs 1.42 port) with _EXT2_get_fragments(): no maintained upstream
# exists, so this is GX's own copy of it, built from the `libs` branch of the fork (commit
# 910f6f5f, tarball of the whole branch). deps/libfs/ext2/*.patch moves it to libogc 3.
EXT2_VERSION=910f6f5f2241d6f7a01efb2b1514208a60feda62
EXT2_URL=https://github.com/Monsterray/usbloadergx/archive/910f6f5f2241d6f7a01efb2b1514208a60feda62.tar.gz
EXT2_SHA256=19a582f8d90b118e8104369ab31746b741111547d9e3f45349aa0f00693a146c

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"
PREFIX="${1:-$DEVKITPRO/portlibs/usbloadergx}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

export PATH="$DEVKITPPC/bin:$PATH"
CC=powerpc-eabi-gcc
CXX=powerpc-eabi-g++
AR=powerpc-eabi-ar
# The same machine flags as GX (devkitPPC's wii_rules MACHDEP).
MACHDEP="-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float"
CFLAGS="$MACHDEP -Os -ffunction-sections -fdata-sections -I$DEVKITPRO/libogc/include -I$DEVKITPRO/portlibs/ppc/include"
JOBS="$(nproc 2>/dev/null || echo 4)"

mkdir -p "$PREFIX/include" "$PREFIX/lib"

# fetch <url> <sha256> -> unpacks into $WORK and prints the top directory
fetch() {
	local url="$1" sha="$2" file="$WORK/$(basename "$1")"
	curl -fsSL --retry 3 -o "$file" "$url"
	echo "$sha  $file" | sha256sum -c --quiet - >&2
	tar -xzf "$file" -C "$WORK"
	tar -tzf "$file" | awk -F/ 'NR == 1 { print $1 }'
}

# compile <outlib> <compiler> <flags> <sources...>: one object per source, in parallel
compile() {
	local lib="$1" cc="$2" flags="$3" objdir
	shift 3
	objdir="$WORK/obj/$(basename "$lib" .a)"
	mkdir -p "$objdir"
	printf '%s\n' "$@" | xargs -P "$JOBS" -I{} sh -c \
		'f="$1"; o="$2/$(printf %s "${f%.*}" | tr / _).o"; $3 $4 -c "$f" -o "$o"' _ {} "$objdir" "$cc" "$flags"
	rm -f "$lib"
	"$AR" rcs "$lib" "$objdir"/*.o
}

echo "== wolfSSL $WOLFSSL_VERSION"
src="$WORK/$(fetch "$WOLFSSL_URL" "$WOLFSSL_SHA256")"
mkdir -p "$PREFIX/include/wolfssl"
cp -R "$src/wolfssl/." "$PREFIX/include/wolfssl/"
cp "$HERE/wolfssl/user_settings.h" "$PREFIX/include/wolfssl/wolfcrypt/user_settings.h"
# Some sources are only #included by others and say so; compiling them alone
# gives nothing but a #warning.
mapfile -t files < <(grep -L 'does not need to be compiled separately' \
	"$src"/src/*.c "$src"/wolfcrypt/src/*.c)
compile "$PREFIX/lib/libwolfssl.a" "$CC" \
	"$CFLAGS -DWOLFSSL_USER_SETTINGS -I$PREFIX/include -I$src -w" "${files[@]}"

echo "== pugixml $PUGIXML_VERSION"
src="$WORK/$(fetch "$PUGIXML_URL" "$PUGIXML_SHA256")"
cp "$src/src/pugixml.hpp" "$src/src/pugiconfig.hpp" "$PREFIX/include/"
compile "$PREFIX/lib/libpugixml.a" "$CXX" "$CFLAGS" "$src/src/pugixml.cpp"

echo "== minizip $MINIZIP_VERSION"
src="$WORK/$(fetch "$MINIZIP_URL" "$MINIZIP_SHA256")/contrib/minizip"
mkdir -p "$PREFIX/include/minizip"
cp "$src"/unzip.h "$src"/ioapi.h "$src"/ints.h "$src"/crypt.h "$PREFIX/include/minizip/"
# newlib has no fopen64 and friends; USE_FILE32API maps them to the plain calls.
compile "$PREFIX/lib/libminizip.a" "$CC" "$CFLAGS -DUSE_FILE32API" "$src/unzip.c" "$src/ioapi.c"

echo "== libntfs $NTFS_VERSION"
src="$WORK/$(fetch "$NTFS_URL" "$NTFS_SHA256")"
patch -d "$src" -p1 -s < "$HERE/libfs/ntfs/gx-classic-disc-interface.patch"
cp "$HERE"/libfs/ntfs/attrib_frag.c "$HERE"/libfs/ntfs/ntfsfile_frag.c "$HERE"/libfs/ntfs/ntfsfile_frag.h "$src/source/"
mapfile -t files < <(ls "$src"/source/*.c)
compile "$PREFIX/lib/libntfs.a" "$CC" \
	"$CFLAGS -DHAVE_CONFIG_H -include $HERE/libfs/shim.h -I$src/source -I$src/include -w" "${files[@]}"
cp "$src/include/ntfs.h" "$src/source/ntfsfile_frag.h" "$PREFIX/include/"

echo "== libext2fs $EXT2_VERSION"
src="$WORK/$(fetch "$EXT2_URL" "$EXT2_SHA256")/libcustomext2fs"
patch -d "$src" -p1 -s < "$HERE/libfs/ext2/gx-libogc3.patch"
mapfile -t files < <(ls "$src"/source/*.c)
compile "$PREFIX/lib/libext2fs.a" "$CC" \
	"$CFLAGS -DWORDS_BIGENDIAN -fgnu89-inline -include $HERE/libfs/shim.h -I$src/source -I$src/include -w" "${files[@]}"
cp "$src/include/ext2.h" "$src/source/ext2_frag.h" "$PREFIX/include/"

echo "== libfat $FAT_VERSION"
src="$WORK/$(fetch "$FAT_URL" "$FAT_SHA256")"
patch -d "$src" -p1 -s < "$HERE/libfs/fat/gx-classic-disc-interface.patch"
cp "$HERE"/libfs/fat/fatfile_frag.c "$HERE"/libfs/fat/fatfile_frag.h "$src/source/"
cp "$HERE"/libfs/fat/libfatversion.h "$src/include/"
mapfile -t files < <(ls "$src"/source/*.c)
# Named libcustomfat.a: the image's own libfat.a is a FatFs build behind libogc 3's fat.h, and
# this fat.h (installed ahead of libogc's) must be the one the app sees.
compile "$PREFIX/lib/libcustomfat.a" "$CC" \
	"-I$src/include -I$src/source $CFLAGS -Wno-stringop-truncation" "${files[@]}"
cp "$src/include/fat.h" "$src/include/libfatversion.h" "$src/source/fatfile_frag.h" "$PREFIX/include/"

echo "== hbc agent $HBCAGENT_VERSION"
src="$WORK/$(fetch "$HBCAGENT_URL" "$HBCAGENT_SHA256")"
make -C "$src/sdk/hbc_agent" -j"$JOBS" DEVKITPRO="$DEVKITPRO" DEVKITPPC="$DEVKITPPC" >/dev/null
cp "$src/sdk/hbc_agent/libhbcagent.a" "$PREFIX/lib/"
cp "$src/sdk/hbc_agent.h" "$src/sdk/hbc_netlog.h" "$PREFIX/include/"

cat > "$PREFIX/VERSIONS" <<EOF
wolfSSL  $WOLFSSL_VERSION  $WOLFSSL_URL
pugixml  $PUGIXML_VERSION  $PUGIXML_URL
minizip  $MINIZIP_VERSION  $MINIZIP_URL
libntfs  $NTFS_VERSION  $NTFS_URL
libext2fs $EXT2_VERSION  $EXT2_URL
libfat   $FAT_VERSION  $FAT_URL
hbcagent $HBCAGENT_VERSION  $HBCAGENT_URL
EOF
echo "Installed in $PREFIX:"
ls "$PREFIX/lib"
