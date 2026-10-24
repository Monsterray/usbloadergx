#!/usr/bin/env bash
# Titles in the NAND of GX's Dolphin test profile (.dev/dolphin_profile/Wii).
#
#   usage: scripts/dolphin_nand.sh list
#          scripts/dolphin_nand.sh install-wad <file.wad>
#          scripts/dolphin_nand.sh clone <title id> <ID4> [name]
#          scripts/dolphin_nand.sh remove <title id>
#          scripts/dolphin_nand.sh backup <file.tar>
#          scripts/dolphin_nand.sh restore <file.tar>
#   env:   DOLPHIN=<dir>  Dolphin install (default /c/tools/Dolphin-x64)
#   A title id is 16 hex digits (000100014f484243) or the four characters of a
#   00010001 channel (OHBC).
#
# clone: a second channel without a second WAD. Dolphin keeps title contents decrypted in
# the NAND and checks no signature or hash of an installed title, so a copy of a title's
# directory and ticket under another ID is a working channel in Dolphin (and only there).
# With a name, the banner's ten names become it, and the IMET MD5 and the TMD's SHA-1 of
# content 0 are redone, so GX lists the copy under its own name.
#
# install-wad: Dolphin has no command-line WAD install. Booting a WAD (-e x.wad) installs
# it first through WiiUtils::InstallWAD() with signature checks off, so fakesigned WADs
# install too, and then runs it. That install is "temporary": Dolphin records the title in
# SYSCONF IPL.TID and deletes it again when the next WAD is booted (the System Menu does the
# same with IPL.TID). This boots the WAD against the profile, waits for its TMD to land in
# the NAND, stops that Dolphin, and clears IPL.TID, which makes the install permanent.
#
# The profile's Dolphin is found by "-u <profile>" on its command line; no other Dolphin is
# touched. Run it while no other run uses the profile (dolphin_run.sh refuses too).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
D="${DOLPHIN:-/c/tools/Dolphin-x64}"
U="$ROOT/.dev/dolphin_profile"
NAND="$U/Wii"
CMD="${1:-list}"
PY="${PYTHON:-python}"

case "$ROOT" in *" "*) echo "repo path contains a space; Dolphin argument passing breaks on it: $ROOT"; exit 2;; esac
mkdir -p "$NAND"
U_WIN="$(cd "$U" && pwd -W | tr '/' '\\')"
NAND_WIN="$(cd "$NAND" && pwd -W)"

gx_pids() {
	powershell.exe -NoProfile -Command \
		"Get-CimInstance Win32_Process -Filter \"Name='Dolphin.exe'\" | Where-Object { \$_.CommandLine -like '*-u $U_WIN *' } | Select-Object -ExpandProperty ProcessId" \
		2>/dev/null | tr -d '\r' | grep -E '^[0-9]+$' || true
}
refuse_if_running() {
	if [ -n "$(gx_pids)" ]; then
		echo "a Dolphin runs against $U (PID $(gx_pids | tr '\n' ' ')); wait for it or stop it first" >&2
		exit 2
	fi
}

case "$CMD" in
list)
	# title/<hi>/<lo>/content/title.tmd, with the title's name where the TMD says it is a
	# channel; a ticket under ticket/<hi>/<lo>.tik is what ES needs to launch it.
	"$PY" - "$NAND_WIN" <<'EOF'
import os, sys
nand = sys.argv[1]
root = os.path.join(nand, "title")
for hi in sorted(os.listdir(root)) if os.path.isdir(root) else []:
    for lo in sorted(os.listdir(os.path.join(root, hi))):
        tmd = os.path.join(root, hi, lo, "content", "title.tmd")
        if not os.path.isfile(tmd):
            continue
        data = open(tmd, "rb").read()
        version = int.from_bytes(data[0x1DC:0x1DE], "big") if len(data) >= 0x1DE else -1
        count = int.from_bytes(data[0x1DE:0x1E0], "big") if len(data) >= 0x1E0 else 0
        tik = os.path.isfile(os.path.join(nand, "ticket", hi, lo + ".tik"))
        try:
            ascii_id = bytes.fromhex(lo).decode("ascii") if hi != "00000001" else ""
        except ValueError:
            ascii_id = ""
        if not ascii_id.isprintable():
            ascii_id = ""
        print(f"{hi}{lo} {ascii_id:4} v{version} {count} contents{'' if tik else ', NO TICKET'}")
EOF
	;;
install-wad)
	WAD="${2:?usage: dolphin_nand.sh install-wad <file.wad>}"
	[ -f "$WAD" ] || { echo "no such WAD: $WAD"; exit 2; }
	[ -x "$D/Dolphin.exe" ] || { echo "no Dolphin at $D"; exit 2; }
	refuse_if_running
	WAD_WIN="$(cd "$(dirname "$WAD")" && pwd -W | tr '/' '\\')\\$(basename "$WAD")"
	# Title ID from the WAD's TMD: header 0x20, then certificate chain, ticket and TMD, each
	# 0x40-aligned; the title ID sits at 0x18C in the TMD.
	TID="$("$PY" - "$(cd "$(dirname "$WAD")" && pwd -W)/$(basename "$WAD")" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read()
be = lambda o: int.from_bytes(d[o:o + 4], "big")
al = lambda n: (n + 0x3F) & ~0x3F
hdr, certs, tik = be(0), be(8), be(0x10)
tmd = al(hdr) + al(certs) + al(tik)
print(d[tmd + 0x18C:tmd + 0x194].hex())
EOF
)"
	case "$TID" in [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]) ;; *) echo "cannot read a title ID from $WAD"; exit 2;; esac
	HI="${TID:0:8}"; LO="${TID:8:8}"
	TMD="$NAND/title/$HI/$LO/content/title.tmd"
	# /sys/cert.sys: IOS keeps the certificate chain there, and GX's channel boot reads it for
	# ES_Identify. Dolphin writes it only when it checks signatures, which a WAD install does
	# not, so a NAND without a System Menu has none. The WAD carries the same chain.
	if [ ! -f "$NAND/sys/cert.sys" ]; then
		mkdir -p "$NAND/sys"
		"$PY" - "$(cd "$(dirname "$WAD")" && pwd -W)/$(basename "$WAD")" "$NAND_WIN/sys/cert.sys" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read()
be = lambda o: int.from_bytes(d[o:o + 4], "big")
start = (be(0) + 0x3F) & ~0x3F
open(sys.argv[2], "wb").write(d[start:start + be(8)])
print("wrote /sys/cert.sys (%d bytes) from the WAD's certificate chain" % be(8))
EOF
	fi
	echo "installing $TID from $WAD (booting it once in Dolphin)..."
	"$D/Dolphin.exe" -b -u "$U_WIN" -e "$WAD_WIN" \
		-C Dolphin.Interface.ConfirmStop=False -C Dolphin.Interface.UsePanicHandlers=False \
		-C Dolphin.Analytics.PermissionAsked=True -C Dolphin.Analytics.Enabled=False \
		-C Dolphin.Core.SIDevice0=0 -C WiiPad.Wiimote1.Source=0 >/dev/null 2>&1 &
	for _ in $(seq 1 30); do
		sleep 1
		[ -f "$TMD" ] && [ -f "$NAND/ticket/$HI/$LO.tik" ] && break
	done
	# InstallWAD() runs before emulation starts; give it a moment to finish the contents.
	sleep 3
	for p in $(gx_pids); do taskkill //F //PID "$p" >/dev/null 2>&1 || true; done
	sleep 2
	[ -f "$TMD" ] || { echo "the TMD never appeared: $TMD (see $U/Logs)"; exit 1; }
	# Clear IPL.TID in SYSCONF (a LongLong entry: 8 bytes after its name), so no later WAD
	# boot or System Menu start deletes this title again.
	"$PY" - "$NAND_WIN/shared2/sys/SYSCONF" "$TID" <<'EOF'
import sys
path, tid = sys.argv[1], sys.argv[2]
d = bytearray(open(path, "rb").read())
assert d[:4] == b"SCv0", "not a SYSCONF"
n = int.from_bytes(d[4:6], "big")
for i in range(n):
    off = int.from_bytes(d[6 + 2 * i:8 + 2 * i], "big")
    desc = d[off]
    name = bytes(d[off + 1:off + 2 + (desc & 0x1F)])
    if name == b"IPL.TID" and desc >> 5 == 6:
        at = off + 2 + (desc & 0x1F)
        old = d[at:at + 8].hex()
        d[at:at + 8] = bytes(8)
        open(path, "wb").write(d)
        print(f"SYSCONF IPL.TID {old} -> 0 (install is permanent)")
        break
else:
    print("SYSCONF has no IPL.TID; nothing to clear")
EOF
	"$0" list | grep "^$TID" || { echo "installed, but list does not show $TID"; exit 1; }
	;;
clone)
	SRC="${2:?usage: dolphin_nand.sh clone <title id> <ID4> [name]}"
	DST="${3:?usage: dolphin_nand.sh clone <title id> <ID4> [name]}"
	refuse_if_running
	"$PY" - "$NAND_WIN" "$SRC" "$DST" "${4:-}" <<'EOF'
import hashlib, os, shutil, sys
nand, src, dst, name = sys.argv[1:5]

def tid(text):
    if len(text) == 4:
        return "00010001" + text.encode("ascii").hex()
    if len(text) == 16:
        return text.lower()
    sys.exit("title id: 16 hex digits or 4 characters, not %r" % text)

src, dst = tid(src), tid(dst)
sdir = os.path.join(nand, "title", src[:8], src[8:])
ddir = os.path.join(nand, "title", dst[:8], dst[8:])
stik = os.path.join(nand, "ticket", src[:8], src[8:] + ".tik")
dtik = os.path.join(nand, "ticket", dst[:8], dst[8:] + ".tik")
if not os.path.isfile(os.path.join(sdir, "content", "title.tmd")) or not os.path.isfile(stik):
    sys.exit("no title %s with a ticket in the NAND" % src)
if os.path.exists(ddir) or os.path.exists(dtik):
    sys.exit("%s exists already; remove it first" % dst)
os.makedirs(os.path.join(ddir, "data"))
shutil.copytree(os.path.join(sdir, "content"), os.path.join(ddir, "content"))
os.makedirs(os.path.dirname(dtik), exist_ok=True)
tik = bytearray(open(stik, "rb").read())
tik[0x1DC:0x1E4] = bytes.fromhex(dst)
open(dtik, "wb").write(tik)

tmd_path = os.path.join(ddir, "content", "title.tmd")
tmd = bytearray(open(tmd_path, "rb").read())
tmd[0x18C:0x194] = bytes.fromhex(dst)
if name:
    count = int.from_bytes(tmd[0x1DE:0x1E0], "big")
    for i in range(count):
        rec = 0x1E4 + 0x24 * i
        if int.from_bytes(tmd[rec + 4:rec + 6], "big") != 0:
            continue
        app = os.path.join(ddir, "content", "%08x.app" % int.from_bytes(tmd[rec:rec + 4], "big"))
        d = bytearray(open(app, "rb").read())
        imet = d.find(b"IMET") - 0x40
        if imet < 0 or imet + 0x600 > len(d):
            sys.exit("content 0 has no IMET header; cloned without a new name")
        text = name[:41].encode("utf-16-be").ljust(84, b"\0")
        for lang in range(10):
            at = imet + 0x5C + 84 * lang
            d[at:at + 84] = text
        d[imet + 0x5F0:imet + 0x600] = bytes(16)
        d[imet + 0x5F0:imet + 0x600] = hashlib.md5(bytes(d[imet:imet + 0x600])).digest()
        open(app, "wb").write(d)
        tmd[rec + 0x10:rec + 0x24] = hashlib.sha1(bytes(d)).digest()
open(tmd_path, "wb").write(tmd)
print("cloned %s -> %s%s" % (src, dst, " as " + repr(name) if name else ""))
EOF
	[ $? -eq 0 ] && "$0" list
	;;
remove)
	RM="${2:?usage: dolphin_nand.sh remove <title id>}"
	refuse_if_running
	case "${#RM}" in
		4) RM="00010001$(printf '%s' "$RM" | od -An -tx1 | tr -d ' \n')";;
		16) ;;
		*) echo "title id: 16 hex digits or 4 characters"; exit 2;;
	esac
	RM="$(printf '%s' "$RM" | tr 'A-F' 'a-f')"
	[ -d "$NAND/title/${RM:0:8}/${RM:8:8}" ] || { echo "no title $RM in the NAND"; exit 2; }
	rm -rf "${NAND:?}/title/${RM:0:8}/${RM:8:8}" "$NAND/ticket/${RM:0:8}/${RM:8:8}.tik" && echo "removed $RM"
	;;
backup)
	OUTTAR="${2:?usage: dolphin_nand.sh backup <file.tar>}"
	refuse_if_running
	tar -cf "$OUTTAR" -C "$U" Wii && echo "NAND saved to $OUTTAR"
	;;
restore)
	INTAR="${2:?usage: dolphin_nand.sh restore <file.tar>}"
	refuse_if_running
	[ -f "$INTAR" ] || { echo "no such file: $INTAR"; exit 2; }
	rm -rf "$NAND" && tar -xf "$INTAR" -C "$U" && echo "NAND restored from $INTAR"
	;;
*)
	echo "usage: $0 list | install-wad <file.wad> | clone <title id> <ID4> [name] | remove <title id> | backup <file.tar> | restore <file.tar>" >&2
	exit 2
	;;
esac
