"""usb_bench.py [--mode io|launch] [--skip 0|1] [--limit S] [--dol PATH] [--dry-run]
-- time USB Loader GX's USB work on the bench Wii's cIOS 249.

Runs the bench build (`bash scripts/build.sh bench` -> .dev/usbbench/boot.dol,
source/utils/UsbBench.cpp) with `hbc.py run boot.dol -- -ios=249 -bench=MODE`,
prints what GX prints, then fetches the two small logs it leaves in the review
build's folder (sd:/apps/usbloader_gx_review/, scripts/wii_install.py):
usbbench.log (the measurements) and exit.log (the exit, its unmount timed).
Everything is kept in .runs/usb_bench/<time>-<mode>/.

GX exits to HBC by itself at the end, so the job leaves the Wii in HBC. It
writes to the USB drive (one FSInfo sector rewritten with its own bytes, and
<usb>:/usbbench/GXGlobal.cfg) and to the review folder on SD. It installs
nothing and never touches a cIOS.

On the shared bench Wii, run it only as a queue job:

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --wii dev --timeout 3600 \\
        --cwd C:/projects/usbloadergx --name "GX USB bench io" -- \\
        C:/Python312/python.exe scripts/usb_bench.py --mode io

Needs WII_BENCH_IP (or HBC_WII), and HBC_TOOL (default C:/projects/hbc-reborn/tools/hbc.py).
Exit status 0 when both logs came back and the bench reached its exit.
"""
import datetime
import os
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
REVIEW = "sd:/apps/usbloader_gx_review/"


def opt(a, name, default):
    return a[a.index(name) + 1] if name in a else default


def hbc(tool, *args, timeout=None, log=None):
    cmd = [sys.executable, tool, *args]
    print("$ hbc.py " + " ".join(args), flush=True)
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                         encoding="utf-8", errors="replace")
    out = []
    for line in p.stdout:
        print(line, end="", flush=True)
        out.append(line)
    rc = p.wait(timeout=timeout)
    if log:
        log.write_text("".join(out), encoding="utf-8")
    return rc, "".join(out)


def main():
    a = sys.argv[1:]
    if "-h" in a or "--help" in a:
        print(__doc__)
        return 0
    mode = opt(a, "--mode", "io")
    if mode not in ("io", "launch"):
        sys.exit("--mode is io or launch")
    dol = pathlib.Path(opt(a, "--dol", REPO / ".dev" / "usbbench" / "boot.dol"))
    tool = os.environ.get("HBC_TOOL", "C:/projects/hbc-reborn/tools/hbc.py")
    args = ["-ios=249", "-bench=" + mode]
    if "--skip" in a:
        args.append("-benchskip=" + opt(a, "--skip", "0"))
    if "--limit" in a:
        args.append("-benchlimit=" + opt(a, "--limit", "600"))
    out = REPO / ".runs" / "usb_bench" / (datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + mode)

    if not dol.exists():
        sys.exit(f"no such .dol: {dol} (run: bash scripts/build.sh bench)")
    print(f"bench build {dol} ({dol.stat().st_size} bytes), args {' '.join(args)}, logs -> {out}", flush=True)
    if "--dry-run" in a:
        print("dry run: nothing sent")
        return 0
    if not (os.environ.get("WII_BENCH_IP") or os.environ.get("HBC_WII")):
        sys.exit("WII_BENCH_IP or HBC_WII must name the Wii (run it as a bench queue job)")
    out.mkdir(parents=True, exist_ok=True)

    hbc(tool, "version")
    # A run that stops early must not leave the last run's logs to be read as its own
    for name in ("usbbench.log", "exit.log"):
        hbc(tool, "rm", REVIEW + name)
    run_rc, _ = hbc(tool, "--timeout", "3000", "run", str(dol), "--", *args, log=out / "run.log")
    print(f"run exited {run_rc}", flush=True)
    hbc(tool, "wait", "180")
    ok = True
    for name in ("usbbench.log", "exit.log"):
        rc, _ = hbc(tool, "get", REVIEW + name, str(out / name))
        if rc != 0 or not (out / name).exists():
            ok = False
            continue
        print(f"== {name}\n" + (out / name).read_text(encoding="utf-8", errors="replace"), flush=True)
    if run_rc == 3:
        hbc(tool, "crash", log=out / "crash.txt")
    bench = (out / "usbbench.log").read_text(errors="replace") if (out / "usbbench.log").exists() else ""
    exitlog = (out / "exit.log").read_text(errors="replace") if (out / "exit.log").exists() else ""
    ok = ok and "exit: back to HBC" in bench and "unmounting SD" in exitlog
    print("RESULT: " + ("complete" if ok else "INCOMPLETE") + f", logs in {out}", flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
