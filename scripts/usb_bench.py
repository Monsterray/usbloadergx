"""usb_bench.py [--mode io|launch|dump] [--allow-writes] [--skip 0|1] [--limit S] [--dol PATH] [--wait S] [--dry-run]
-- time USB Loader GX's USB work on the bench Wii's cIOS 249.

Runs the bench build (`bash scripts/build.sh bench` -> .dev/usbbench/boot.dol,
source/utils/UsbBench.cpp) with `hbc.py send boot.dol -- -ios=249 -bench=MODE`,
waits for GX's agent to answer and then for HBC to answer again (GX sends no
netlog, so `run` would wait out its timeout), keeps GX's output (`hbc.py
lastlog`), then fetches the two small logs it leaves in the review
build's folder (sd:/apps/usbloader_gx_review/, scripts/wii_install.py):
usbbench.log (the measurements) and exit.log (the exit, its unmount timed).
Everything is kept in .runs/usb_bench/<time>-<mode>/.

GX exits to HBC by itself at the end, so the job leaves the Wii in HBC. It
writes to the review folder on SD, installs nothing and never touches a cIOS.
It writes nothing to the USB drive unless --allow-writes is given: the build
refuses every USB write by default, since the d2x fork's debug cIOS was found
(2026-10-24) to land writes as junk on the bench drive. With --allow-writes,
io mode rewrites one FSInfo sector with its own bytes and saves
<usb>:/usbbench/GXGlobal.cfg. --mode dump only reads: FAT1 against FAT2, the
FSInfo, root directory and usbbench sectors, for what earlier writes left.

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
import time

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


def version(tool):
    """What answers on 4299: "1.10.9" (HBC), "... agent" (GX's agent), or None."""
    try:
        p = subprocess.run([sys.executable, tool, "version"], capture_output=True, text=True, timeout=20)
    except subprocess.TimeoutExpired:
        return None
    return p.stdout.strip() if p.returncode == 0 and p.stdout.strip() else None


def wait_for_hbc(tool, limit_s):
    """After the send: GX's agent answers while it runs, nothing answers during its
    IOS reloads and exit, then HBC answers again. True once HBC is back."""
    start = time.monotonic()
    seen_gx = False
    last = ""
    while time.monotonic() - start < limit_s:
        v = version(tool)
        state = "gx" if v and v.endswith("agent") else "hbc" if v else "none"
        if state != last:
            print(f"[{time.monotonic() - start:6.0f} s] {state}: {v or 'no answer'}", flush=True)
            last = state
        seen_gx = seen_gx or state in ("gx", "none")
        if state == "hbc" and seen_gx and time.monotonic() - start > 30:
            return True
        if state == "hbc" and not seen_gx and time.monotonic() - start > 120:
            print("GX was never seen: HBC answered throughout (did it start, or leave within 2 s?)", flush=True)
            return True
        # Quick polls at first: a start-up that fails leaves for HBC within seconds
        time.sleep(2 if time.monotonic() - start < 120 else 10)
    return False


def main():
    a = sys.argv[1:]
    if "-h" in a or "--help" in a:
        print(__doc__)
        return 0
    mode = opt(a, "--mode", "io")
    if mode not in ("io", "launch", "dump"):
        sys.exit("--mode is io, launch or dump")
    dol = pathlib.Path(opt(a, "--dol", REPO / ".dev" / "usbbench" / "boot.dol"))
    tool = os.environ.get("HBC_TOOL", "C:/projects/hbc-reborn/tools/hbc.py")
    args = ["-ios=249", "-bench=" + mode]
    if "--allow-writes" in a:
        args.append("-benchwrites=1")
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
    send_rc, _ = hbc(tool, "send", str(dol), "--", *args)
    if send_rc != 0:
        sys.exit("HBC did not start the bench build")
    back = wait_for_hbc(tool, int(opt(a, "--wait", "3000")))
    if not back:
        print("STUCK: HBC did not answer again in time; the Wii needs someone to bring it back", flush=True)
        return 1
    hbc(tool, "lastlog", log=out / "lastlog.txt")
    hbc(tool, "crash", log=out / "crash.txt")
    ok = True
    for name in ("usbbench.log", "exit.log"):
        rc, _ = hbc(tool, "get", REVIEW + name, str(out / name))
        if rc != 0 or not (out / name).exists():
            ok = False
            continue
        print(f"== {name}\n" + (out / name).read_text(encoding="utf-8", errors="replace"), flush=True)
    bench = (out / "usbbench.log").read_text(errors="replace") if (out / "usbbench.log").exists() else ""
    exitlog = (out / "exit.log").read_text(errors="replace") if (out / "exit.log").exists() else ""
    ok = ok and "exit: back to HBC" in bench and "unmounting SD" in exitlog
    print("RESULT: " + ("complete" if ok else "INCOMPLETE") + f", logs in {out}", flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
