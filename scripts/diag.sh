#!/usr/bin/env bash
# Diagnostic builds of USB Loader GX in the CI-pinned image. Read-only on the repo:
# the source is copied into the container, so the working tree is not touched.
#
# Usage:
#   scripts/diag.sh warnings     # build with extra GCC diagnostics, print warning lines (sorted, unique)
#   scripts/diag.sh gc-sections  # build with --gc-sections and list the functions/data the linker dropped
#   scripts/diag.sh autoinput    # test build with -DAUTOINPUT -> .dev/autoinput/boot.dol and boot.elf
#                                # (EXTRA_CFLAGS="-DDEBUG_NETWORK" adds defines to it)
#                                # (reads sd:/autoinput.txt; see source/utils/AutoInput.cpp)
#
# Output goes to stdout; redirect it to a file to keep it.
set -euo pipefail

# Git Bash (MSYS) rewrites arguments that look like absolute POSIX paths, which
# turns docker's "-v <host>:/src:ro" into a Windows path and fails the run.
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The toolchain stage of the Dockerfile: the devkitPPC image plus the packages it
# adds (deps/build.sh). Docker caches it until the Dockerfile or deps/ change.
IMAGE="usbloadergx-toolchain"
WSL_DISTRO="${WSL_DISTRO:-Ubuntu-24.04}"
MODE="${1:-warnings}"
OUT_MOUNT=()

if command -v docker >/dev/null 2>&1; then
	DOCKER=(docker)
	MOUNT_ROOT="$ROOT"
elif command -v wsl.exe >/dev/null 2>&1; then
	DOCKER=(wsl.exe -d "$WSL_DISTRO" -e docker)
	MOUNT_ROOT="$(printf '%s' "$ROOT" | sed -E 's|^/([a-zA-Z])/|/mnt/\L\1/|')"
else
	echo "docker not found, natively or through WSL." >&2
	exit 1
fi

# The Makefile's CFLAGS/LDFLAGS are recursive variables, so passing them on the
# make command line keeps $(MACHDEP)/$(INCLUDE) expansion.
BASE_CFLAGS='-ggdb -Os -Wall -Wno-multichar -Wno-unused-parameter -Wextra -Wformat-security $(MACHDEP) $(INCLUDE) -D_GNU_SOURCE -DNDEBUG -DWOLFSSL_USER_SETTINGS'
BASE_LDFLAGS='-ggdb $(MACHDEP) -Wl,-Map,$(notdir $@).map,--section-start,.init=0x80B00000,-wrap,malloc,-wrap,free,-wrap,memalign,-wrap,calloc,-wrap,realloc,-wrap,malloc_usable_size $(PROJECTDIR)/source/gx_symbols.ld'

case "$MODE" in
	warnings)
		CF="$BASE_CFLAGS -Wformat=2 -Wno-format-nonliteral -Wnull-dereference -Wduplicated-cond -Wlogical-op -Wshadow=local -Wcast-align -Wimplicit-fallthrough=3"
		SCRIPT='mkdir -p /w && tar -C /src -cf - --exclude=./.dev --exclude=./build --exclude=./usbloader_gx --exclude=./usbloader_gx.zip . | tar -C /w -xmf - && cd /w && make release -j"$(nproc)" CFLAGS="$CF" 2>&1 | grep -E "warning:" | grep -v "^/opt/" | sed -E "s|^/w/||" | sort -u'
		;;
	gc-sections)
		CF="$BASE_CFLAGS -ffunction-sections -fdata-sections"
		LF="$BASE_LDFLAGS,--gc-sections,--print-gc-sections"
		SCRIPT='mkdir -p /w && tar -C /src -cf - --exclude=./.dev --exclude=./build --exclude=./usbloader_gx --exclude=./usbloader_gx.zip . | tar -C /w -xmf - && cd /w && make release -j"$(nproc)" CFLAGS="$CF" LDFLAGS="$LF" 2>&1 | grep -i "removing unused" | grep -v "/opt/" | sed -E "s/.*section .(\.[a-z]+)\.([^ ]*). in file .([^ ]*)\.o.*/\3 \1 \2/" | while read -r f s n; do echo "$f $s $(echo "$n" | powerpc-eabi-c++filt)"; done'
		;;
	autoinput)
		# Built from a copy inside the container, so these objects never mix with a
		# normal build; only the .dol and .elf come out.
		CF="$BASE_CFLAGS -DAUTOINPUT ${EXTRA_CFLAGS:-}"
		mkdir -p "$ROOT/.dev/autoinput"
		OUT_MOUNT=(-v "$MOUNT_ROOT/.dev/autoinput:/out")
		SCRIPT='mkdir -p /w && tar -C /src -cf - --exclude=./.dev --exclude=./build --exclude=./usbloader_gx --exclude=./usbloader_gx.zip . | tar -C /w -xmf - && cd /w && make -j"$(nproc)" CFLAGS="$CF" > /out/build.log 2>&1; rc=$?; cp boot.dol boot.elf /out/ 2>/dev/null; grep -E "warning:|error:" /out/build.log | sort -u; exit $rc'
		;;
	*)
		echo "Usage: $0 [warnings|gc-sections|autoinput]" >&2
		exit 1
		;;
esac

"${DOCKER[@]}" build -q --target toolchain -t "$IMAGE" "$MOUNT_ROOT" >/dev/null
"${DOCKER[@]}" run --rm -v "$MOUNT_ROOT:/src:ro" "${OUT_MOUNT[@]}" -e CF="$CF" -e LF="${LF:-}" "$IMAGE" bash -c "$SCRIPT"
