#!/usr/bin/env bash
# Build USB Loader GX with the toolchain stage of the Dockerfile (devkitpro/devkitppc
# plus the packages it adds). CI installs the same; .devcontainer builds the same stage.
#
# Usage:
#   scripts/build.sh              # docker build -o . .  -> usbloader_gx.zip (boot.dol + boot.elf)
#   scripts/build.sh make ARGS    # run make ARGS in the image with the repo mounted (e.g. "make clean")
#   scripts/build.sh shell        # interactive shell in the image, repo mounted at /w
#
# Docker is used natively when present. On Windows (Git Bash) with no native docker, the
# WSL distro named in WSL_DISTRO (default Ubuntu-24.04) must have Docker Engine installed.
# Do not build GX with the toolchains under C:\devkitPro: they are not kept in step with
# the Dockerfile's toolchain stage (see AGENTS.md).
set -euo pipefail

# Git Bash (MSYS) rewrites arguments that look like absolute POSIX paths, which
# turns docker's "-v <host>:/src:ro" into a Windows path and fails the run.
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The toolchain stage of the Dockerfile: the devkitPPC image plus the packages it
# adds (deps/build.sh). Docker caches it until the Dockerfile or deps/ change.
IMAGE="usbloadergx-toolchain"
WSL_DISTRO="${WSL_DISTRO:-Ubuntu-24.04}"

if command -v docker >/dev/null 2>&1; then
	DOCKER=(docker)
	MOUNT_ROOT="$ROOT"
elif command -v wsl.exe >/dev/null 2>&1; then
	DOCKER=(wsl.exe -d "$WSL_DISTRO" -e docker)
	# /c/projects/x -> /mnt/c/projects/x
	MOUNT_ROOT="$(printf '%s' "$ROOT" | sed -E 's|^/([a-zA-Z])/|/mnt/\L\1/|')"
else
	echo "docker not found, natively or through WSL." >&2
	exit 1
fi

cd "$ROOT"
toolchain() {
	"${DOCKER[@]}" build -q --target toolchain -t "$IMAGE" "$MOUNT_ROOT" >/dev/null
}

case "${1:-zip}" in
	zip)
		"${DOCKER[@]}" build -o . .
		echo "Output: $ROOT/usbloader_gx.zip"
		;;
	make)
		shift
		toolchain
		"${DOCKER[@]}" run --rm -v "$MOUNT_ROOT:/w" -w /w "$IMAGE" make "$@"
		;;
	shell)
		toolchain
		"${DOCKER[@]}" run --rm -it -v "$MOUNT_ROOT:/w" -w /w "$IMAGE" bash
		;;
	*)
		echo "Usage: $0 [zip|make ARGS...|shell]" >&2
		exit 1
		;;
esac
