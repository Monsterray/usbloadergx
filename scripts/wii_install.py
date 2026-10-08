"""wii_install.py [--dol PATH] [--folder NAME] [--dry-run] -- put a build of USB Loader GX on a Wii's SD card.

It stages what a release holds, in .runs/wii_install/, and uploads it with hbc.py from
hbc-reborn (`hbc.py sync`: only new or changed files go over, and nothing on the card is
deleted, so settings, saves and games stay):

    sd:/apps/<folder>/   boot.dol, meta.xml, icon.png

The default folder is usbloader_gx_review, not usbloader_gx. GX takes its config folder
from the path HBC starts it with, so a build installed there keeps its own settings and
caches and leaves an installed release, and its GXGlobal.cfg, as they were. The staged
meta.xml names it "USB Loader GX (review)" so the two can be told apart in HBC.

Needs a Homebrew Channel that answers hbc.py (hbc-reborn), and:
    WII_BENCH_IP   the Wii's address (the bench queue sets it for every job; HBC_WII also works)
    HBC_TOOL       path to hbc.py (default: C:/projects/hbc-reborn/tools/hbc.py)

On a shared bench Wii, run it only as a queue job, never directly:

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --cwd C:/projects/usbloadergx \\
        --name "USB Loader GX install" -- C:/Python312/python.exe scripts/wii_install.py
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile

REPO = pathlib.Path(__file__).resolve().parents[1]
STAGE = REPO / ".runs" / "wii_install"


def find_dol(arg):
    if arg:
        return pathlib.Path(arg)
    dol = REPO / "usbloader_gx" / "boot.dol"
    zipped = REPO / "usbloader_gx.zip"
    if zipped.exists() and (not dol.exists() or zipped.stat().st_mtime > dol.stat().st_mtime):
        with zipfile.ZipFile(zipped) as z:
            z.extract("usbloader_gx/boot.dol", REPO)
    return dol


def stage(dol, folder):
    if STAGE.exists():
        shutil.rmtree(STAGE)
    app = STAGE / "apps" / folder
    app.mkdir(parents=True)
    shutil.copy2(dol, app / "boot.dol")
    shutil.copy2(REPO / "HBC" / "icon.png", app / "icon.png")
    meta = (REPO / "HBC" / "meta.xml").read_text(encoding="utf-8")
    if folder != "usbloader_gx":
        meta = re.sub(r"<name>[^<]*</name>", "<name> USB Loader GX (review)</name>", meta, count=1)
    (app / "meta.xml").write_text(meta, encoding="utf-8")
    return app, "sd:/apps/" + folder


def main():
    a = sys.argv[1:]
    if "-h" in a or "--help" in a:
        print(__doc__)
        return
    dol = find_dol(a[a.index("--dol") + 1] if "--dol" in a else None)
    folder = a[a.index("--folder") + 1] if "--folder" in a else "usbloader_gx_review"
    dry = "--dry-run" in a
    if not dol.exists():
        sys.exit(f"no such .dol: {dol} (run scripts/build.sh first)")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", folder):
        sys.exit(f"folder must be one plain name: {folder}")

    local, remote = stage(dol, folder)
    n = sum(f.stat().st_size for f in local.rglob("*") if f.is_file())
    print(f"{local.relative_to(STAGE)} -> {remote} ({n} bytes staged)", flush=True)
    if dry:
        print("dry run: nothing sent")
        return

    wii = os.environ.get("WII_BENCH_IP") or os.environ.get("HBC_WII")
    if not wii:
        sys.exit("no Wii address: set WII_BENCH_IP (the bench queue does) or HBC_WII")
    hbc = pathlib.Path(os.environ.get("HBC_TOOL", "C:/projects/hbc-reborn/tools/hbc.py"))
    if not hbc.exists():
        sys.exit(f"no hbc.py at {hbc}: set HBC_TOOL")

    r = subprocess.run([sys.executable, str(hbc), "--wii", wii, "sync", str(local), remote])
    if r.returncode:
        sys.exit(f"sync of {remote} failed (exit {r.returncode})")
    print(f"installed {dol.name} ({dol.stat().st_size} bytes) as {remote}/boot.dol")


if __name__ == "__main__":
    main()
