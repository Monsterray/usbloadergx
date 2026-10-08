#!/usr/bin/env bash
# Start USB Loader GX in Dolphin and leave it running for you to drive.
#
#   usage: scripts/dolphin_start.sh [--stop]
#   env:   DOL=<path>     .dol to boot (default: usbloader_gx/boot.dol)
#          DOLPHIN=<dir>  Dolphin install (default /c/tools/Dolphin-x64)
#
# Unlike scripts/dolphin_run.sh this never kills the instance on a timer, so it
# is the one to use when you want to look at the loader. It uses the same
# throwaway profile (.dev/dolphin_profile), so the user's own Dolphin session,
# its INIs and its SD image are untouched, and --stop only ends this one.
#
# Dolphin emulates no USB mass storage and rejects the cIOS device nodes the
# loader uses for USB, so the emulated SD card is the only storage it can see.
# Put test games in .dev/dolphin_profile/Load/WiiSDSync/wbfs (Wii) or games/
# (GameCube). Press A on the splash screen to skip the 20 s USB probe.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOL="${DOL:-$ROOT/usbloader_gx/boot.dol}"
D="${DOLPHIN:-/c/tools/Dolphin-x64}"
U="$ROOT/.dev/dolphin_profile"
SD="$U/Load/WiiSDSync"

gx_pids() {
	powershell.exe -NoProfile -Command \
		"Get-CimInstance Win32_Process -Filter \"Name='Dolphin.exe'\" | Where-Object { \$_.CommandLine -like '*usbloadergx*dolphin_profile*' } | Select-Object -ExpandProperty ProcessId" \
		2>/dev/null | tr -d '\r' | grep -E '^[0-9]+$' || true
}

if [ "${1:-}" = "--stop" ]; then
	pids="$(gx_pids)"
	[ -z "$pids" ] && { echo "not running"; exit 0; }
	# Ask first: a clean exit is what syncs the SD folder back, so anything the
	# loader wrote (its config, cache and covers) survives.
	for p in $pids; do taskkill //PID "$p" >/dev/null 2>&1 || true; done
	for _ in $(seq 1 15); do sleep 2; [ -z "$(gx_pids)" ] && break; done
	for p in $(gx_pids); do taskkill //F //PID "$p" >/dev/null 2>&1 || true; done
	echo "stopped"
	exit 0
fi

[ -x "$D/Dolphin.exe" ] || { echo "no Dolphin at $D"; exit 2; }
if [ ! -f "$DOL" ] && [ -f "$ROOT/usbloader_gx.zip" ]; then
	(cd "$ROOT" && unzip -qo usbloader_gx.zip usbloader_gx/boot.dol)
fi
[ -f "$DOL" ] || { echo "no such .dol: $DOL (run scripts/build.sh first)"; exit 2; }
case "$ROOT" in *" "*) echo "repo path contains a space; Dolphin argument passing breaks on it"; exit 2;; esac
[ -n "$(gx_pids)" ] && { echo "already running (PID $(gx_pids | tr '\n' ' ')); scripts/dolphin_start.sh --stop first"; exit 2; }

mkdir -p "$U/Config" "$SD/apps/usbloader_gx" "$SD/wbfs" "$SD/games"
cp "$DOL" "$SD/apps/usbloader_gx/boot.dol"
[ -f "$ROOT/HBC/meta.xml" ] && cp "$ROOT/HBC/meta.xml" "$ROOT/HBC/icon.png" "$SD/apps/usbloader_gx/" 2>/dev/null
# Analytics consent is a modal on a fresh profile and -C does not persist it.
grep -q "PermissionAsked" "$U/Config/Dolphin.ini" 2>/dev/null ||
	printf '[Analytics]\nPermissionAsked = True\nEnabled = False\n' >> "$U/Config/Dolphin.ini"

U_WIN="$(cd "$U" && pwd -W | tr '/' '\\')"
DOL_WIN="$(cd "$(dirname "$DOL")" && pwd -W | tr '/' '\\')\\$(basename "$DOL")"

# UsePanicHandlers=False matters: any panic alert is a modal that blocks
# emulation and leaves the render window black. Dolphin boots a .dol with no
# argv at all; the loader read argv[0] regardless until 2026-09-22 and raised
# exactly such an alert ("Invalid read from 0x00000000") on every boot.
# ImmediateXFBEnable=True matters: the menu redraws an identical frame every
# vblank, which deferred presentation shows as black.
ARGS="'-e','$DOL_WIN','-u','$U_WIN'"
for c in Dolphin.Interface.UsePanicHandlers=False Dolphin.Interface.ConfirmStop=False \
	Dolphin.Core.WiiSDCard=True Dolphin.Core.WiiSDCardAllowWrites=True \
	Dolphin.Core.WiiSDCardEnableFolderSync=True Graphics.Hacks.ImmediateXFBEnable=True \
	Dolphin.Analytics.PermissionAsked=True Dolphin.Analytics.Enabled=False \
	Dolphin.Core.SlotB=7; do
	ARGS="$ARGS,'-C','$c'"
done

# The loader's own log while you drive it: USB Gecko in slot B (Core.SlotB=7),
# read by scripts/gecko_log.py until Dolphin closes the connection.
LIVELOG="$ROOT/.dev/gecko-live.log"
nohup "${PYTHON:-python}" "$ROOT/scripts/gecko_log.py" "$LIVELOG" 86400 >/dev/null 2>&1 &
disown

powershell.exe -NoProfile -Command "Start-Process -FilePath '$(cd "$D" && pwd -W | tr '/' '\\')\\Dolphin.exe' -ArgumentList $ARGS" 2>&1 | tr -d '\r'
sleep 5
pids="$(gx_pids)"
[ -z "$pids" ] && { echo "Dolphin did not start"; exit 1; }
echo "USB Loader GX running in Dolphin, PID $pids"
echo "  games:  $SD/wbfs  and  $SD/games"
echo "  log:    $LIVELOG  (what GX prints, as it prints it)"
echo "  stop:   scripts/dolphin_start.sh --stop"
