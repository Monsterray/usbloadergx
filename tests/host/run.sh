#!/bin/sh
# Compile and run every tests/host/*_test.c on the host.
#
# Each test #includes the source file it tests, so the code under test is the exact
# code the Wii runs. That only works for files with no libogc in them;
# tests/host/shim/gctypes.h stands in for libogc's integer types, and nothing else
# may be needed. Add a test here for any pure function that handles data from a
# file, a drive or the network.
#
#   usage: tests/host/run.sh
#   env:   CC          host C compiler (default: cc, then gcc)
#          WSL_DISTRO  on Windows with no host compiler, run in this WSL distro
#                      (default Ubuntu-24.04, the one scripts/build.sh uses)
#
# CI runs this in its own job on ubuntu-latest: the devkitPPC image has no host compiler.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

# A compiler counts only if it can build a C program: devkitPro's MSYS2 puts a cc on
# the PATH that has no C library headers.
usable() {
	command -v "$1" >/dev/null 2>&1 || return 1
	printf '#include <stdio.h>\nint main(void){return 0;}\n' |
		"$1" -x c -o /dev/null - >/dev/null 2>&1
}

CC="${CC:-}"
if [ -z "$CC" ]; then
	for c in cc gcc clang; do
		if usable "$c"; then CC="$c"; break; fi
	done
fi

if [ -z "$CC" ]; then
	# No host compiler, as in Git Bash on Windows: rerun the whole script in WSL.
	if command -v wsl.exe >/dev/null 2>&1 && [ -z "${HOST_TESTS_IN_WSL:-}" ]; then
		WIN_ROOT="$(cd "$ROOT" && pwd -W 2>/dev/null)"
		exec wsl.exe -d "${WSL_DISTRO:-Ubuntu-24.04}" --cd "$WIN_ROOT" \
			-e env HOST_TESTS_IN_WSL=1 sh tests/host/run.sh
	fi
	echo "no host C compiler: set CC, or install cc" >&2
	exit 2
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
status=0

for t in "$HERE"/*_test.c; do
	name="$(basename "$t" .c)"
	if ! "$CC" -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
		-fno-sanitize-recover=all -I "$HERE/shim" -I "$ROOT/source" \
		-o "$OUT/$name" "$t" 2>"$OUT/$name.err"; then
		echo "FAIL: $name does not compile:" >&2
		cat "$OUT/$name.err" >&2
		status=1
		continue
	fi
	# A compiler warning in the code under test is a failure too.
	if [ -s "$OUT/$name.err" ]; then
		cat "$OUT/$name.err" >&2
		echo "FAIL: $name compiles with warnings" >&2
		status=1
	fi
	"$OUT/$name" || { echo "FAIL: $name" >&2; status=1; }
done

[ "$status" -eq 0 ] && echo "OK: host tests pass"
exit "$status"
