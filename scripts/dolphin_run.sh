#!/usr/bin/env bash
# Unattended smoke run of USB Loader GX in Dolphin with frame dumping.
#
#   usage: scripts/dolphin_run.sh <outdir> [seconds]
#   env:   DOL=<path>       .dol to boot (default: usbloader_gx/boot.dol from the last scripts/build.sh)
#          DOLPHIN=<dir>    Dolphin install (default /c/tools/Dolphin-x64)
#          KEEP=<n>         frames to keep, the last ones (default 60)
#          DOLPHIN_ARGS     extra arguments appended verbatim, e.g. "-C Dolphin.Core.MMU=True"
#
# What it proves: the loader boots, the GUI renders, the menus open. What it cannot prove:
# anything that needs a cIOS, USB storage or a real disc drive (Dolphin has none of them), so
# GX runs here in its IOS58 fallback with the emulated SD card only.
#
# Isolation (the WiiStation / Wii64 pattern): the run gets its own persistent profile at
# .dev/dolphin_profile (gitignored locally), never the user's Dolphin User/ dir or SD image;
# the instance is found by that profile path on its command line and only that PID is killed.
# Every setting is passed with -C <System>.<Section>.<Key>=<Value> for this run only. The
# two exceptions are written once into the profile's own Dolphin.ini: analytics consent
# (a modal that -C cannot persist) and nothing else.
#
# Three Dolphin behaviours that make a correct build look broken in a headless run:
#   - A modal dialog (analytics consent, PanicAlert) blocks a -b run forever: no frames, no
#     log lines, looks like a boot hang. UsePanicHandlers=False and the consent ini fix it.
#   - Deferred XFB presentation shows black when a menu redraws an identical frame each
#     vblank. ImmediateXFBEnable=True.
#   - Frame dumps read the XFB and bypass presentation: they prove rendering, not display.
set -u
OUT="${1:?usage: dolphin_run.sh <outdir> [seconds]}"; SECS="${2:-60}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOL="${DOL:-$ROOT/usbloader_gx/boot.dol}"
D="${DOLPHIN:-/c/tools/Dolphin-x64}"
U="$ROOT/.dev/dolphin_profile"
KEEP="${KEEP:-60}"

[ -x "$D/Dolphin.exe" ] || { echo "no Dolphin at $D"; exit 2; }
if [ ! -f "$DOL" ] && [ -f "$ROOT/usbloader_gx.zip" ]; then
	(cd "$ROOT" && unzip -qo usbloader_gx.zip usbloader_gx/boot.dol)
fi
[ -f "$DOL" ] || { echo "no such .dol: $DOL (run scripts/build.sh first)"; exit 2; }
case "$ROOT" in *" "*) echo "repo path contains a space; Dolphin argument passing breaks on it: $ROOT"; exit 2;; esac

SD="$U/Load/WiiSDSync"
mkdir -p "$OUT/frames" "$U/Config" "$SD/apps/usbloader_gx" "$SD/wbfs" "$SD/games" "$U/Dump/Frames" "$U/Logs"
rm -f "$U"/Dump/Frames/*.png "$U"/Logs/dolphin.log
# GX keeps its settings and cache next to its own boot.dol, and scans wbfs/ and
# games/ on every mounted device. The emulated SD card is the only device it can
# see here: Dolphin emulates no USB mass storage and rejects the cIOS nodes the
# loader uses for USB (/dev/usb2, /dev/usb123), so drop test games in "$SD/wbfs".
cp "$DOL" "$SD/apps/usbloader_gx/boot.dol"
[ -f "$ROOT/HBC/meta.xml" ] && cp "$ROOT/HBC/meta.xml" "$ROOT/HBC/icon.png" "$SD/apps/usbloader_gx/" 2>/dev/null
# Analytics consent is a modal dialog on a fresh profile and -C flags do not persist it.
if ! grep -q "PermissionAsked" "$U/Config/Dolphin.ini" 2>/dev/null; then
	printf '[Analytics]\nPermissionAsked = True\nEnabled = False\n' >> "$U/Config/Dolphin.ini"
fi

U_WIN="$(cd "$U" && pwd -W | tr '/' '\\')"
DOL_WIN="$(cd "$(dirname "$DOL")" && pwd -W | tr '/' '\\')\\$(basename "$DOL")"

# Only Dolphin instances launched against THIS profile, matched on their command line.
gx_pids() {
	powershell.exe -NoProfile -Command \
		"Get-CimInstance Win32_Process -Filter \"Name='Dolphin.exe'\" | Where-Object { \$_.CommandLine -like '*usbloadergx*dolphin_profile*' } | Select-Object -ExpandProperty ProcessId" \
		2>/dev/null | tr -d '\r' | grep -E '^[0-9]+$' || true
}
if [ -n "$(gx_pids)" ]; then
	echo "a Dolphin already runs against $U (PID $(gx_pids | tr '\n' ' ')); stop it first: taskkill //F //PID <pid>" >&2
	exit 2
fi

CFGARGS=(
	-C Dolphin.Interface.ConfirmStop=False
	-C Dolphin.Interface.UsePanicHandlers=False
	-C Dolphin.Interface.OnScreenDisplayMessages=False
	-C Dolphin.Analytics.PermissionAsked=True
	-C Dolphin.Analytics.Enabled=False
	-C Dolphin.Core.CPUThread=True
	-C Dolphin.Core.WiiSDCard=True
	-C Dolphin.Core.WiiSDCardAllowWrites=True
	-C Dolphin.Core.WiiSDCardEnableFolderSync=True
	-C Dolphin.Core.AccurateCPUCache=True
	-C Dolphin.Movie.DumpFrames=True
	-C Graphics.Settings.DumpFramesAsImages=True
	-C Graphics.Settings.PNGCompressionLevel=1
	-C Graphics.Hacks.ImmediateXFBEnable=True
	-C Graphics.Hacks.XFBToTextureEnable=False
	# Log to <profile>/Logs/dolphin.log. Timestamps are MM:SS:mmm.
	-C Logger.Options.WriteToFile=True
	-C Logger.Options.Verbosity=2
	-C Logger.Logs.BOOT=True
	-C Logger.Logs.IOS=True
	-C Logger.Logs.IOS_ES=True
	-C Logger.Logs.IOS_FS=True
	-C Logger.Logs.IOS_SD=True
	-C Logger.Logs.OSREPORT=True
)

"$D/Dolphin.exe" -b -u "$U_WIN" -e "$DOL_WIN" "${CFGARGS[@]}" ${DOLPHIN_ARGS:-} >/dev/null 2>&1 &
sleep 3
echo "Dolphin PID $(gx_pids | tr '\n' ' '), running $SECS s..."
sleep "$SECS"
for p in $(gx_pids); do taskkill //F //PID "$p" >/dev/null 2>&1 || true; done
sleep 2
[ -z "$(gx_pids)" ] || echo "WARNING: Dolphin still running: $(gx_pids | tr '\n' ' ')" >&2

# Keep the last KEEP frames.
ls "$U"/Dump/Frames/*.png 2>/dev/null | tail -n "$KEEP" | while read -r f; do cp "$f" "$OUT/frames/"; done
N=$(ls "$OUT/frames" | wc -l | tr -d ' ')
cp "$U"/Logs/dolphin.log "$OUT/" 2>/dev/null || true
echo "kept $N frames in $OUT/frames; log: $OUT/dolphin.log"
[ "$N" -gt 0 ] || { echo "no frames dumped: check $OUT/dolphin.log; a modal dialog or a boot failure before the first present looks the same from here"; exit 1; }
