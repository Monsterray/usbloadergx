#!/usr/bin/env bash
# Check that the build system itself still behaves: build modes, packaging,
# clean, the warning budget and the generated resource list.
#
#   usage: scripts/verify-build.sh
#
# Runs in the CI-pinned image against a copy of the tree, so it never touches
# your working directory. Takes a few minutes: it builds the project several
# times. The fast checks that run on every CI push live in tests/ instead.
set -u

# Git Bash (MSYS) rewrites arguments that look like absolute POSIX paths, which
# turns docker's "-v <host>:/src:ro" into a Windows path and fails the run.
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The toolchain stage of the Dockerfile: the devkitPPC image plus the packages it
# adds (deps/build.sh). Docker caches it until the Dockerfile or deps/ change.
IMAGE="usbloadergx-toolchain"
WSL_DISTRO="${WSL_DISTRO:-Ubuntu-24.04}"

if [ "${GX_IN_CONTAINER:-}" != "1" ]; then
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
	"${DOCKER[@]}" build -q --target toolchain -t "$IMAGE" "$MOUNT_ROOT" >/dev/null
	exec "${DOCKER[@]}" run --rm -v "$MOUNT_ROOT:/src:ro" -e GX_IN_CONTAINER=1 \
		"$IMAGE" bash /src/scripts/verify-build.sh
fi

# ---- inside the container, on a throwaway copy ----------------------------
mkdir -p /w && tar -C /src -cf - --exclude=./.dev --exclude=./build --exclude=./usbloader_gx --exclude=./usbloader_gx.zip . | tar -C /w -xmf - && cd /w && rm -f boot.dol boot.elf
J="$(nproc)"
fail=0
ok()  { echo "PASS: $1"; }
bad() { echo "FAIL: $1"; fail=1; }

echo "=== make all on a clean tree"
# "all" builds the binary and then the language and theme files. The language
# step needs xgettext and msgmerge, which the CI image does not carry: Windows
# contributors get them from the bundled gettext-bin. Check whichever half this
# machine can actually run, and say so.
if command -v xgettext >/dev/null 2>&1 && command -v msgmerge >/dev/null 2>&1; then
	if make all -j"$J" >/tmp/all.log 2>&1 && [ -s boot.dol ]; then ok "make all"; else bad "make all"; tail -5 /tmp/all.log; fi
else
	if make -j"$J" >/tmp/all.log 2>&1 && [ -s boot.dol ]; then ok "make on a clean tree (no gettext here, so the lang step of 'all' is not covered)"; else bad "make on a clean tree"; tail -5 /tmp/all.log; fi
fi

echo
echo "=== switching build mode starts from clean"
touch build/canary.o
make release -j"$J" >/tmp/rel.log 2>&1
grep -q "Build mode changed" /tmp/rel.log && ok "clean announced" || bad "no clean on mode switch"
[ -f build/canary.o ] && bad "stale objects survived the switch" || ok "stale objects removed"
[ "$(cat build/.buildmode)" = "release" ] && ok "mode recorded" || bad "mode not recorded"

echo
echo "=== repeating a mode builds incrementally"
touch build/canary2.o
make release -j"$J" >/tmp/rel2.log 2>&1
grep -q "Build mode changed" /tmp/rel2.log && bad "cleaned without a mode change" || ok "no needless clean"
[ -f build/canary2.o ] && ok "objects kept" || bad "objects dropped"

echo
echo "=== make zip packages a build, with nothing stale in it"
mkdir -p usbloader_gx && echo stale > usbloader_gx/stale.txt
echo stale > usbloader_gx.zip
make zip -j"$J" >/tmp/zip.log 2>&1
unzip -l usbloader_gx.zip 2>/dev/null | grep -q stale.txt && bad "stale file in the archive" || ok "archive has no stale entries"
unzip -l usbloader_gx.zip | grep -q 'boot\.dol' && ok "archive has boot.dol" || bad "archive missing boot.dol"
# zip stays on the default mode on purpose: only CI mints an official build.
[ -z "$(cat build/.buildmode)" ] && ok "zip built in the default mode" || bad "zip mode is $(cat build/.buildmode)"

echo
echo "=== clean removes what it names"
make clean >/dev/null 2>&1
for f in build boot.elf boot.dol usbloader_gx.zip usbloader_gx; do
	[ -e "$f" ] && bad "clean left $f" || ok "clean removed $f"
done

echo
echo "=== first-party sources compile without warnings"
make release -j"$J" > /tmp/build.log 2>&1
warnings=$(grep 'warning:' /tmp/build.log | grep -v 'source/libs/' || true)
if [ -n "$warnings" ]; then bad "first-party warnings"; echo "$warnings" | head -10; else ok "no first-party warnings"; fi

echo
echo "=== filelist.h does not churn when regenerated"
cp source/themes/filelist.h /tmp/fl.a
bash ./filelist.sh
cmp -s /tmp/fl.a source/themes/filelist.h && ok "filelist.h stable" || bad "filelist.h churns"

echo
echo "RESULT: $([ $fail -eq 0 ] && echo 'ALL PASS' || echo FAILURES)"
exit $fail
