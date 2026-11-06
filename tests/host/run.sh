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
#          CXX         host C++ compiler for *_test.cpp (default: c++, then g++)
#          XDG_CACHE_HOME  where the pugixml source for GameTDB's test is cached
#                      (default ~/.cache); it is fetched with curl on the first run
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

# Tests of C++ sources (*_test.cpp) need a C++ compiler.
CXX="${CXX:-}"
if [ -z "$CXX" ]; then
	for c in c++ g++ clang++; do
		if command -v "$c" >/dev/null 2>&1 &&
			printf '#include <string>\nint main(){std::string s;return (int)s.size();}\n' |
			"$c" -x c++ -o /dev/null - >/dev/null 2>&1; then
			CXX="$c"
			break
		fi
	done
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
status=0

# run_test <compiler> <std> <source file> [extra flags]
run_test() {
	cc="$1"; std="$2"; t="$3"; extra="${4:-}"
	name="$(basename "$t")"
	name="${name%.*}"
	# shellcheck disable=SC2086
	if ! "$cc" -std="$std" -O1 -g -Wall -Wextra $extra -fsanitize=address,undefined \
		-fno-sanitize-recover=all -I "$HERE/shim" -I "$ROOT/source" \
		-o "$OUT/$name" "$t" 2>"$OUT/$name.err"; then
		echo "FAIL: $name does not compile:" >&2
		cat "$OUT/$name.err" >&2
		status=1
		return
	fi
	# A compiler warning in the code under test is a failure too.
	if [ -s "$OUT/$name.err" ]; then
		cat "$OUT/$name.err" >&2
		echo "FAIL: $name compiles with warnings" >&2
		status=1
	fi
	"$OUT/$name" || { echo "FAIL: $name" >&2; status=1; }
}

for t in "$HERE"/*_test.c; do
	run_test "$CC" gnu11 "$t"
done

# pugixml for a test that says "host-needs: pugixml": the same pinned release
# deps/build.sh builds for the Wii, fetched once into a cache and compiled once per
# run (without the sanitizers' warnings: it is upstream code). Sets PUGI_FLAGS.
PUGI_FLAGS=""
need_pugixml() {
	[ -n "$PUGI_FLAGS" ] && return 0
	url="$(sed -n 's/^PUGIXML_URL=//p' "$ROOT/deps/build.sh" | tr -d '\r')"
	sha="$(sed -n 's/^PUGIXML_SHA256=//p' "$ROOT/deps/build.sh" | tr -d '\r')"
	cache="${XDG_CACHE_HOME:-$HOME/.cache}/usbloadergx-host-tests/$sha"
	if [ ! -f "$cache/src/pugixml.cpp" ]; then
		mkdir -p "$cache.tmp" &&
			curl -fsSL --retry 3 -o "$cache.tmp/pugixml.tar.gz" "$url" &&
			echo "$sha  $cache.tmp/pugixml.tar.gz" | sha256sum -c --quiet - &&
			tar -xzf "$cache.tmp/pugixml.tar.gz" -C "$cache.tmp" --strip-components=1 &&
			rm -rf "$cache" && mv "$cache.tmp" "$cache" || {
				rm -rf "$cache.tmp"
				echo "FAIL: cannot fetch pugixml from $url" >&2
				return 1
			}
	fi
	"$CXX" -std=gnu++17 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=all \
		-c "$cache/src/pugixml.cpp" -o "$OUT/pugixml.o" || return 1
	PUGI_FLAGS="-I $cache/src $OUT/pugixml.o"
}

for t in "$HERE"/*_test.cpp; do
	[ -e "$t" ] || continue
	if [ -z "$CXX" ]; then
		echo "FAIL: $(basename "$t"): no host C++ compiler: set CXX, or install g++" >&2
		status=1
		continue
	fi
	# -funsigned-char: plain char is unsigned on the Wii's PowerPC, and the C++ sources rely on it.
	flags="-funsigned-char"
	if grep -q 'host-needs: pugixml' "$t"; then
		if ! need_pugixml; then
			echo "FAIL: $(basename "$t"): no pugixml" >&2
			status=1
			continue
		fi
		flags="$flags $PUGI_FLAGS"
	fi
	run_test "$CXX" gnu++17 "$t" "$flags"
done

[ "$status" -eq 0 ] && echo "OK: host tests pass"
exit "$status"
