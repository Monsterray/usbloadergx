#!/bin/sh
# CI cannot build its container from the Dockerfile, so .github/workflows/main.yml
# names the devkitPPC image and the extra packages a second time. Both must name
# the same ones, or CI tests a different toolchain than everyone else builds with.
set -eu
cd "$(dirname "$0")/.."

dockerfile_image=$(sed -n 's/^FROM \(devkitpro\/devkitppc:[0-9]*\) AS toolchain$/\1/p' Dockerfile)
ci_image=$(sed -n 's/^ *container: \(devkitpro\/devkitppc:[0-9]*\)$/\1/p' .github/workflows/main.yml)
dockerfile_pkgs=$(grep -o 'https://pkg\.devkitpro\.org/packages/[^ ]*\.pkg\.tar\.zst' Dockerfile | sort)
ci_pkgs=$(grep -o 'https://pkg\.devkitpro\.org/packages/[^ ]*\.pkg\.tar\.zst' .github/workflows/main.yml | sort)

fail=0
if [ -z "$dockerfile_image" ] || [ "$dockerfile_image" != "$ci_image" ]; then
	echo "FAIL: Dockerfile toolchain stage uses '$dockerfile_image', CI uses '$ci_image'"
	fail=1
fi
if [ "$dockerfile_pkgs" != "$ci_pkgs" ]; then
	echo "FAIL: packages differ"
	echo "  Dockerfile: $dockerfile_pkgs"
	echo "  CI:         $ci_pkgs"
	fail=1
fi
[ "$fail" -eq 0 ] && echo "OK: Dockerfile and CI use $dockerfile_image with the same packages"
exit "$fail"
