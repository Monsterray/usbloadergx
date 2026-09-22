#!/usr/bin/env bash
# Diagnostic builds of USB Loader GX in the CI-pinned image. Read-only on the repo:
# the source is copied into the container, so the working tree is not touched.
#
# Usage:
#   scripts/diag.sh warnings     # build with extra GCC diagnostics, print warning lines (sorted, unique)
#   scripts/diag.sh gc-sections  # build with --gc-sections and list the functions/data the linker dropped
#
# Output goes to stdout; redirect it to a file to keep it.
set -euo pipefail

# Git Bash (MSYS) rewrites arguments that look like absolute POSIX paths, which
# turns docker's "-v <host>:/src:ro" into a Windows path and fails the run.
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="devkitpro/devkitppc:20250527"
WSL_DISTRO="${WSL_DISTRO:-Ubuntu-24.04}"
MODE="${1:-warnings}"

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
BASE_LDFLAGS='-ggdb $(MACHDEP) -Wl,-Map,$(notdir $@).map,--section-start,.init=0x80B00000,-wrap,malloc,-wrap,free,-wrap,memalign,-wrap,calloc,-wrap,realloc,-wrap,malloc_usable_size'

case "$MODE" in
	warnings)
		CF="$BASE_CFLAGS -Wformat=2 -Wno-format-nonliteral -Wnull-dereference -Wduplicated-cond -Wlogical-op -Wshadow=local -Wcast-align -Wimplicit-fallthrough=3"
		SCRIPT='cp -r /src /w && cd /w && rm -rf build && make release -j"$(nproc)" CFLAGS="$CF" 2>&1 | grep -E "warning:" | grep -v "^/opt/|portlibs/" | sed -E "s|^/w/||" | sort -u'
		;;
	gc-sections)
		CF="$BASE_CFLAGS -ffunction-sections -fdata-sections"
		LF="$BASE_LDFLAGS,--gc-sections,--print-gc-sections"
		SCRIPT='cp -r /src /w && cd /w && rm -rf build && make release -j"$(nproc)" CFLAGS="$CF" LDFLAGS="$LF" 2>&1 | grep -i "removing unused" | grep -v "portlibs/\|/opt/" | sed -E "s/.*section .(\.[a-z]+)\.([^ ]*). in file .([^ ]*)\.o.*/\3 \1 \2/" | while read -r f s n; do echo "$f $s $(echo "$n" | powerpc-eabi-c++filt)"; done'
		;;
	*)
		echo "Usage: $0 [warnings|gc-sections]" >&2
		exit 1
		;;
esac

"${DOCKER[@]}" run --rm -v "$MOUNT_ROOT:/src:ro" -e CF="$CF" -e LF="${LF:-}" "$IMAGE" bash -c "$SCRIPT"
