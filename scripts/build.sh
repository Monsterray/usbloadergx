#!/usr/bin/env bash
# Build USB Loader GX with the toolchain stage of the Dockerfile (devkitpro/devkitppc
# plus the packages it adds). CI installs the same; .devcontainer builds the same stage.
#
# Usage:
#   scripts/build.sh              # docker build -o . .  -> usbloader_gx.zip (boot.dol + boot.elf)
#   scripts/build.sh make ARGS    # run make ARGS in the image with the repo mounted (e.g. "make clean")
#   scripts/build.sh shell        # interactive shell in the image, repo mounted at /w
#   scripts/build.sh bench        # cIOS bench build, -DUSB_BENCH -> .dev/usbbench/boot.dol and boot.elf
#                                 # (source/utils/UsbBench.cpp; run it with scripts/usb_bench.py)
#                                 # HBCAGENT_SRC=<hbc-reborn checkout> links the agent built from
#                                 # it instead of the image's pinned one (bench builds only)
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
	bench)
		# Built from a copy inside the container, as diag.sh autoinput does, so these
		# objects never mix with a normal build; only the .dol, .elf and log come out.
		# The Makefile's CFLAGS/LDFLAGS are recursive, so $(MACHDEP)/$(INCLUDE) still expand.
		toolchain
		mkdir -p "$ROOT/.dev/usbbench"
		CF='-ggdb -Os -Wall -Wno-multichar -Wno-unused-parameter -Wextra -Wformat-security $(MACHDEP) $(INCLUDE) -D_GNU_SOURCE -DNDEBUG -DWOLFSSL_USER_SETTINGS -DUSB_BENCH'
		SCRIPT='mkdir -p /w && tar -C /src -cf - --exclude=./.dev --exclude=./.runs --exclude=./build --exclude=./usbloader_gx --exclude=./usbloader_gx.zip . | tar -C /w -xmf - && cd /w && make -j"$(nproc)" CFLAGS="$CF" > /out/build.log 2>&1; rc=$?; cp boot.dol boot.elf /out/ 2>/dev/null; grep -E "warning:|error:" /out/build.log | sort -u; exit $rc'
		AGENT_MOUNT=()
		AGENT=''
		if [ -n "${HBCAGENT_SRC:-}" ]; then
			# An hbc-reborn checkout: its agent replaces the image's pinned one in this
			# throwaway container only, as deps/build.sh builds and installs it.
			A="$(cd "$HBCAGENT_SRC" && pwd)"
			[ -f "$A/sdk/hbc_agent/Makefile" ] || { echo "HBCAGENT_SRC: no sdk/hbc_agent in $A" >&2; exit 1; }
			if [ "${DOCKER[0]}" != docker ]; then
				A="$(printf '%s' "$A" | sed -E 's|^/([a-zA-Z])/|/mnt/\L\1/|')"
			fi
			AGENT_MOUNT=(-v "$A:/agent:ro")
			AGENT='mkdir -p /tmp/agent && tar -C /agent -cf - sdk channel/channelapp/source channel/channelapp/config.h | tar -C /tmp/agent -xf - && make -C /tmp/agent/sdk/hbc_agent -B -j"$(nproc)" DEVKITPRO="$DEVKITPRO" DEVKITPPC="$DEVKITPPC" >/out/agent.log 2>&1 || { tail -20 /out/agent.log; exit 1; }; P=$DEVKITPRO/portlibs/usbloadergx; cp /tmp/agent/sdk/hbc_agent/libhbcagent.a $P/lib/ && cp /tmp/agent/sdk/hbc_agent.h /tmp/agent/sdk/hbc_netlog.h $P/include/ && grep -m1 "CHANNEL_VERSION_STR" /tmp/agent/channel/channelapp/config.h > /out/agent.txt; '
		else
			rm -f "$ROOT/.dev/usbbench/agent.txt"
		fi
		"${DOCKER[@]}" run --rm -v "$MOUNT_ROOT:/src:ro" "${AGENT_MOUNT[@]}" -v "$MOUNT_ROOT/.dev/usbbench:/out" -e CF="$CF" "$IMAGE" bash -c "$AGENT$SCRIPT"
		echo "Output: $ROOT/.dev/usbbench/boot.dol${HBCAGENT_SRC:+ (agent from $HBCAGENT_SRC)}"
		;;
	shell)
		toolchain
		"${DOCKER[@]}" run --rm -it -v "$MOUNT_ROOT:/w" -w /w "$IMAGE" bash
		;;
	*)
		echo "Usage: $0 [zip|make ARGS...|shell|bench]" >&2
		exit 1
		;;
esac
