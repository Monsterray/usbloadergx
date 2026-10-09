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

# The Homebrew Channel's in-app agent (sdk/hbc_agent.h), HBC 1.10.1.
HBCAGENT_VERSION=f0fe12dbb3888f8a7e501bfa5bb3263518c90bed
HBCAGENT_URL=https://github.com/Monsterray/hbc-reborn/archive/f0fe12dbb3888f8a7e501bfa5bb3263518c90bed.tar.gz
HBCAGENT_SHA256=586a816331a634c4dcd7c3577f6f0aa643bacef43d8bbe569c359a742bf0dbc8

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

echo "== hbc agent $HBCAGENT_VERSION"
src="$WORK/$(fetch "$HBCAGENT_URL" "$HBCAGENT_SHA256")"
make -C "$src/sdk/hbc_agent" -j"$JOBS" DEVKITPRO="$DEVKITPRO" DEVKITPPC="$DEVKITPPC" >/dev/null
cp "$src/sdk/hbc_agent/libhbcagent.a" "$PREFIX/lib/"
cp "$src/sdk/hbc_agent.h" "$src/sdk/hbc_netlog.h" "$PREFIX/include/"

cat > "$PREFIX/VERSIONS" <<EOF
wolfSSL  $WOLFSSL_VERSION  $WOLFSSL_URL
pugixml  $PUGIXML_VERSION  $PUGIXML_URL
minizip  $MINIZIP_VERSION  $MINIZIP_URL
hbcagent $HBCAGENT_VERSION  $HBCAGENT_URL
EOF
echo "Installed in $PREFIX:"
ls "$PREFIX/lib"
