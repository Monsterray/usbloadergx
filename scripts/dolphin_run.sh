#!/usr/bin/env bash
# Unattended smoke run of USB Loader GX in Dolphin with frame dumping.
#
#   usage: scripts/dolphin_run.sh <outdir> [seconds]
#   env:   DOL=<path>       .dol to boot (default: usbloader_gx/boot.dol from the last scripts/build.sh)
#          DOLPHIN=<dir>    Dolphin install (default /c/tools/Dolphin-x64)
#          KEEP=<n>         frames to keep, the last ones (default 60)
#          AUTOINPUT=<file> copy it to sd:/autoinput.txt for this run (a .dol from
#                           scripts/diag.sh autoinput plays it), removed afterwards
#          DOLPHIN_ARGS     extra arguments appended verbatim, e.g. "-C Dolphin.Core.MMU=False" to turn MMU emulation back off
#          APPS="<dir> ..." homebrew app folders (boot.dol, meta.xml) copied to sd:/apps/<dir name>/
#                           for this run and removed afterwards, e.g. APPS=.dev/exittest
#          SDGET="<path> ..." more files to copy out of the card image into <outdir>/sd/ after
#                           the run; debug.txt, exittest.log and GX's exit.log and boot.log
#                           (apps/usbloader_gx/) always are
#
# The card: Dolphin builds Load/WiiSD.raw from Load/WiiSDSync at each start and writes it
# back only on a clean stop. This script force-kills Dolphin, so what GX wrote to the card is
# only in WiiSD.raw; the files above are read out of it with 7-Zip. Emulation also stops by
# itself, cleanly, when the program powers off or resets to a System Menu the NAND does not
# have; that sync rewrites WiiSDSync with GX's settings and logs, so WiiSDSync is saved
# before each run and put back when it changed.
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
# A build with -DDEBUG_TO_FILE appends every gprintf line to sd:/debug.txt; start it empty.
rm -f "$SD/autoinput.txt" "$SD/debug.txt"
[ -n "${AUTOINPUT:-}" ] && cp "$AUTOINPUT" "$SD/autoinput.txt"
[ -f "$ROOT/HBC/meta.xml" ] && cp "$ROOT/HBC/meta.xml" "$ROOT/HBC/icon.png" "$SD/apps/usbloader_gx/" 2>/dev/null
# Analytics consent is a modal dialog on a fresh profile and -C flags do not persist it.
if ! grep -q "PermissionAsked" "$U/Config/Dolphin.ini" 2>/dev/null; then
	printf '[Analytics]\nPermissionAsked = True\nEnabled = False\n' >> "$U/Config/Dolphin.ini"
fi

U_WIN="$(cd "$U" && pwd -W | tr '/' '\\')"
DOL_WIN="$(cd "$(dirname "$DOL")" && pwd -W | tr '/' '\\')\\$(basename "$DOL")"

# Only Dolphin instances launched against THIS profile, matched on their command line.
# Every worktree has its own profile and other sessions run GX in Dolphin too: the
# old pattern *usbloadergx*dolphin_profile* matched theirs, refused to start, and
# force-killed their Dolphin at the end of the run.
gx_pids() {
	powershell.exe -NoProfile -Command \
		"Get-CimInstance Win32_Process -Filter \"Name='Dolphin.exe'\" | Where-Object { \$_.CommandLine -like '*-u $U_WIN *' } | Select-Object -ExpandProperty ProcessId" \
		2>/dev/null | tr -d '\r' | grep -E '^[0-9]+$' || true
}
# The USB Gecko port the Dolphin with process ID $1 listens on, once it has one.
gecko_port() {
	powershell.exe -NoProfile -Command \
		"Get-NetTCPConnection -State Listen -OwningProcess $1 -ErrorAction SilentlyContinue | Where-Object { \$_.LocalPort -ge 55020 -and \$_.LocalPort -le 55030 } | Select-Object -First 1 -ExpandProperty LocalPort" \
		2>/dev/null | tr -d '\r' | grep -E '^[0-9]+$' | head -n 1 || true
}
# Windows can reserve TCP port ranges for Hyper-V/WSL (dynamic, they change at boot). When
# one covers all of 55020-55030, Dolphin's USB Gecko cannot listen at all and gecko.log
# stays empty however well GX runs. Prints the covering range, or nothing.
gecko_ports_reserved() {
	netsh.exe int ipv4 show excludedportrange protocol=tcp 2>/dev/null | tr -d '\r' |
		awk '$1 ~ /^[0-9]+$/ && $1 <= 55020 && $2 >= 55030 { print $1 "-" $2; exit }'
}
GECKO_RESERVED="$(gecko_ports_reserved)"
if [ -n "$GECKO_RESERVED" ]; then
	echo "NOTE: Windows reserves TCP $GECKO_RESERVED, so Dolphin's USB Gecko (55020-55030) cannot listen" \
		"and gecko.log will be empty. A -DDEBUG_TO_FILE build's sd:/debug.txt is read back instead." >&2
fi
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
	# MMU emulation faults on a bad address the way the console does. With it off, Dolphin
	# reads address 0 as zeros, and a NULL dereference runs on as if nothing happened: the
	# argv[0] read in StartUpProcess::Run() and a theme font FreeType rejects both looked
	# fine without it and stop at a DSI with it.
	-C Dolphin.Core.MMU=True
	# A USB Gecko in slot B (EXIDeviceType::Gecko = 7): GX's gprintf, stdout and stderr
	# go to it, and scripts/gecko_log.py saves them as gecko.log.
	-C Dolphin.Core.SlotB=7
	# No emulated controllers: this profile maps them to the keyboard (X is A,
	# Enter is START), and each run's window takes focus, so typing elsewhere
	# pressed buttons. An X on the start-up screen switched GX to SD mode.
	# Scripted input (AUTOINPUT) goes to GX directly and does not need them.
	-C Dolphin.Core.SIDevice0=0
	-C WiiPad.Wiimote1.Source=0
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

APP_DIRS=()
for a in ${APPS:-}; do
	[ -f "$a/boot.dol" ] || [ -f "$a/boot.elf" ] || { echo "APPS: no boot.dol or boot.elf in $a" >&2; continue; }
	n="$(basename "$a")"
	[ -e "$SD/apps/$n" ] && { echo "APPS: sd:/apps/$n exists in the profile already; left alone" >&2; continue; }
	cp -r "$a" "$SD/apps/$n" && APP_DIRS+=("$SD/apps/$n")
done
rm -f "$SD/exittest.log"
tar -cf "$OUT/.sd-before.tar" -C "$SD" .
touch "$OUT/.started"
"$D/Dolphin.exe" -b -u "$U_WIN" -e "$DOL_WIN" "${CFGARGS[@]}" ${DOLPHIN_ARGS:-} >/dev/null 2>&1 &
sleep 3
PID="$(gx_pids | head -n 1)"
echo "Dolphin PID ${PID:-?}, running $SECS s..."

# The loader's own log, from this Dolphin's USB Gecko. Each Dolphin listens on the
# first free port from 55020, so with another one running that port is not ours.
# Dolphin holds what GX prints until a client connects.
GECKO_PORT=""
: > "$OUT/gecko.log"
GECKO_PID=""
if [ -z "$GECKO_RESERVED" ]; then
	for _ in $(seq 1 15); do
		[ -n "$PID" ] && GECKO_PORT="$(gecko_port "$PID")"
		[ -n "$GECKO_PORT" ] && break
		sleep 1
	done
	"${PYTHON:-python}" "$ROOT/scripts/gecko_log.py" "$OUT/gecko.log" "$((SECS + 30))" ${GECKO_PORT:-} &
	GECKO_PID=$!
fi
sleep "$SECS"
for p in $(gx_pids); do taskkill //F //PID "$p" >/dev/null 2>&1 || true; done
sleep 2
if [ -n "$(find "$SD" -newer "$OUT/.started" -print -quit)" ]; then
	echo "Dolphin stopped by itself and synced the card back to WiiSDSync (the program powered off" \
		"or reset to a missing System Menu); WiiSDSync put back as it was before the run" >&2
	rm -rf "${SD:?}"/* && tar -xf "$OUT/.sd-before.tar" -C "$SD"
fi
rm -f "$OUT/.sd-before.tar"
rm -f "$SD/autoinput.txt"
for a in "${APP_DIRS[@]}"; do rm -rf "$a"; done
[ -z "$(gx_pids)" ] || echo "WARNING: Dolphin still running: $(gx_pids | tr '\n' ' ')" >&2

# Keep the last KEEP frames, and drop the rest: a long run dumps thousands, and the
# profile's Dump/Frames grew to 8 GB when they stayed there.
ls -v "$U"/Dump/Frames/*.png 2>/dev/null | tail -n "$KEEP" | while read -r f; do mv "$f" "$OUT/frames/"; done
rm -f "$U"/Dump/Frames/*.png
N=$(ls "$OUT/frames" | wc -l | tr -d ' ')
cp "$U"/Logs/dolphin.log "$OUT/" 2>/dev/null || true
# Dolphin closing the socket ends the reader; give it a moment, then stop it regardless.
if [ -n "$GECKO_PID" ]; then
	sleep 1; kill "$GECKO_PID" 2>/dev/null; wait "$GECKO_PID" 2>/dev/null
fi

# The forced kill skips Dolphin's sync of the SD image back to WiiSDSync, so what GX wrote
# to the card is only in WiiSD.raw. A -DDEBUG_TO_FILE build writes its log there.
SEVENZIP="${SEVENZIP:-/c/Program Files/7-Zip/7z.exe}"
rm -rf "$OUT/sd" "$OUT/debug.txt"
if [ -x "$SEVENZIP" ] && [ "$U/Load/WiiSD.raw" -nt "$OUT/.started" ]; then
	mkdir -p "$OUT/sd"
	"$SEVENZIP" x -y -o"$(cd "$OUT/sd" && pwd -W)" "$(cd "$U/Load" && pwd -W)/WiiSD.raw" \
		debug.txt exittest.log apps/usbloader_gx/exit.log apps/usbloader_gx/boot.log ${SDGET:-} >/dev/null 2>&1 || true
	[ -f "$OUT/sd/debug.txt" ] && cp "$OUT/sd/debug.txt" "$OUT/debug.txt"
	for f in exittest.log apps/usbloader_gx/exit.log apps/usbloader_gx/boot.log; do
		[ -f "$OUT/sd/$f" ] && echo "card: $OUT/sd/$f"
	done
fi
if [ ! -s "$OUT/gecko.log" ]; then
	if [ -s "$OUT/debug.txt" ]; then
		echo "gecko.log is empty${GECKO_RESERVED:+ (TCP $GECKO_RESERVED reserved by Windows)}; using sd:/debug.txt from the card image as gecko.log"
		cp "$OUT/debug.txt" "$OUT/gecko.log"
	elif [ -n "$GECKO_RESERVED" ]; then
		echo "gecko.log is empty: Windows reserves TCP $GECKO_RESERVED, so Dolphin's USB Gecko never listened." >&2
		echo "  Build with: EXTRA_CFLAGS=-DDEBUG_TO_FILE scripts/diag.sh autoinput (then DOL=.dev/autoinput/boot.dol)" >&2
		echo "  or free the range (admin, user's call): net stop winnat && net start winnat" >&2
	else
		echo "gecko.log is empty although the Gecko ports are free: did Dolphin boot? (check $OUT/dolphin.log)" >&2
	fi
fi
rm -f "$OUT/.started"
G=$(wc -l < "$OUT/gecko.log" 2>/dev/null | tr -d ' ')
echo "kept $N frames in $OUT/frames; logs: $OUT/dolphin.log, $OUT/gecko.log (${G:-0} lines)"
[ "$N" -gt 0 ] || { echo "no frames dumped: check $OUT/dolphin.log; a modal dialog or a boot failure before the first present looks the same from here"; exit 1; }

# Read the log before the pictures. With no USB device, a good start-up ends on GX's
# "USB Device not initialized" prompt, and every step before it prints a line. Some runs
# mount the SD card's partition instead (WBFS_OpenPartSD), which is a good start-up too.
# -a: a log read off the card image can hold NUL bytes where the FAT cache was not written.
if grep -aq "USB Device not initialized" "$OUT/gecko.log" 2>/dev/null; then
	echo "RESULT: start-up reached the USB prompt; last steps:"
	grep -av '^[[:space:]]*$' "$OUT/gecko.log" | tail -n 6 | tr -d '\000' | sed 's/^/  /'
elif grep -aq "WBFS_OpenPartSD partition" "$OUT/gecko.log" 2>/dev/null; then
	echo "RESULT: start-up mounted the SD card's game partition; last steps:"
	grep -av '^[[:space:]]*$' "$OUT/gecko.log" | tail -n 6 | tr -d '\000' | sed 's/^/  /'
else
	echo "RESULT: start-up did not reach the USB prompt; the last lines GX printed:"
	tail -n 15 "$OUT/gecko.log" 2>/dev/null | tr -d '\000' | sed 's/^/  /'
	exit 1
fi
