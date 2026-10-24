"""usb_bench.py --mode io|launch|dump [--allow-writes] [--skip 0|1] [--limit S] [--boot S]
                [--dol PATH] [--wait S] [--dry-run]
-- time USB Loader GX's USB work on the bench Wii's cIOS 249.

Sends the bench build (`bash scripts/build.sh bench` -> .dev/usbbench/boot.dol,
source/utils/UsbBench.cpp) with `-ios=249 -bench=MODE`, waits until HBC answers
again, then keeps GX's output (`hbc.py lastlog`, `crash`) and the two small logs
it leaves in the review build's folder on SD (sd:/apps/usbloader_gx_review/):
usbbench.log (the measurements) and exit.log (the exit, its unmount timed).
Everything goes to .runs/usb_bench/<time>-<mode>/.

  io      game list load, one FSInfo sector rewritten 3x, GXGlobal.cfg saved to USB 2x, exit
  launch  game list load, then a USB Wii game's launch up to its apploader: fragment list,
          DIP USB mode, disc and partition open, partition reads through the DIP; exit
  dump    reads only: boot/FSInfo sectors, FAT1 against FAT2, root directory, usbbench folder

Safety, as WiiStation's bench scripts do it (scripts/wii_targets.py, Gamecube/hbc_home.c,
Gamecube/boot_guard.c in C:/projects/WiiStation):
  - Runs only as a wii-bench queue job, and never against a production-style Wii.
  - Every hbc.py call has a timeout, so a wedged Wii never holds the queue lease.
  - Read-only by default: the build refuses every USB write in usbstorage2.c, before IOS
    sees it, unless --allow-writes. The d2x fork's debug cIOS landed writes as junk on the
    bench drive (2026-10-24: a root-directory sector became a USB command block). On IOS58,
    whose driver has no refusal, the build goes back to HBC before measuring.
  - GX's agent runs without a listener, so no other queue takes it for an idle HBC; a crash
    returns to HBC after 10 s; the agent's exit never writes the USB cache back.
  - A guard from main(): start-up past --boot seconds (default 180; a prompt nobody answers),
    or any later step past --limit (default 600), returns to HBC with no clean-up.
  - No abbreviated flags: a typo is an error, never another option.
It installs nothing, never touches a cIOS, and writes on SD only the two logs and GX's own
review-folder files (settings, caches).

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --wii dev --timeout 1800 \\
        --cwd C:/projects/usbloadergx-bench --name "GX USB bench dump (read-only)" -- \\
        C:/Python312/python.exe scripts/usb_bench.py --mode dump

HBC_TOOL overrides the hbc.py path (default C:/projects/hbc-reborn/tools/hbc.py).
Exit status 0 when both logs came back and the bench reached its exit; 2 when HBC did not
come back (STUCK: someone has to bring the Wii back).
"""
import argparse
import datetime
import os
import pathlib
import socket
import subprocess
import sys
import time

REPO = pathlib.Path(__file__).resolve().parents[1]
REVIEW = "sd:/apps/usbloader_gx_review/"
HBC_TOOL = os.environ.get("HBC_TOOL", "C:/projects/hbc-reborn/tools/hbc.py")
# Production-style Wiis with real data on them (WiiStation scripts/wii_targets.py)
PROTECTED_WIIS = ("192.168.8.200", "192.168.8.243")


def bench_wii():
    """The bench Wii's address inside a wii-bench queue job; refuses to run outside one,
    and refuses a production-style Wii, whatever the environment says."""
    if not os.environ.get("WII_BENCH_JOB_START"):
        sys.exit("usb_bench.py: run it as a wii-bench queue job, not directly (nothing was sent)")
    wii = (os.environ.get("WII_BENCH_IP") or "").strip()
    if not wii:
        sys.exit("usb_bench.py: no WII_BENCH_IP in this job")
    try:
        packed = socket.inet_aton(wii)
    except OSError:
        sys.exit(f"usb_bench.py: {wii} is not an IPv4 address")
    if any(packed == socket.inet_aton(p) for p in PROTECTED_WIIS):
        sys.exit(f"usb_bench.py: refused, {wii} is a production-style Wii (real data)")
    return wii


def hbc(wii, *args, timeout=120, log=None):
    """One hbc.py call, printed; rc 124 after a timeout, never a hang."""
    print("$ hbc.py " + " ".join(map(str, args)), flush=True)
    try:
        r = subprocess.run([sys.executable, HBC_TOOL, "--wii", wii, *map(str, args)],
                           capture_output=True, text=True, encoding="utf-8", errors="replace",
                           timeout=timeout)
        rc, out = r.returncode, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        rc, out = 124, f"timed out after {timeout} s\n"
    print(out, end="" if out.endswith("\n") else "\n", flush=True)
    if log:
        log.write_text(out, encoding="utf-8")
    return rc, out


def answer(wii):
    """Who answers on TCP 4299: 'hbc' (HBC's menu), 'agent' (an app's agent), or 'none'."""
    try:
        r = subprocess.run([sys.executable, HBC_TOOL, "--wii", wii, "version"],
                           capture_output=True, text=True, timeout=20)
    except subprocess.TimeoutExpired:
        return "none"
    v = r.stdout.strip()
    if r.returncode or not v:
        return "none"
    return "agent" if v.endswith("agent") else "hbc"


def wait_for_hbc(wii, limit_s):
    """After the send: nothing answers while GX runs (its agent has no listener in this build),
    then HBC answers again. True once HBC is back after GX was seen gone."""
    start = time.monotonic()
    gone = False
    last = ""
    while time.monotonic() - start < limit_s:
        state = answer(wii)
        t = time.monotonic() - start
        if state != last:
            print(f"[{t:6.0f} s] {state}", flush=True)
            last = state
        gone = gone or state in ("none", "agent")
        if state == "hbc" and gone:
            return True
        if state == "hbc" and t > 120:
            print("GX was never seen: HBC answered throughout (did it start, or leave within 2 s?)",
                  flush=True)
            return True
        # Quick polls at first: a start-up that fails leaves for HBC within seconds
        time.sleep(2 if t < 120 else 10)
    return False


def main():
    p = argparse.ArgumentParser(description=__doc__, allow_abbrev=False,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--mode", choices=("io", "launch", "dump"), required=True)
    p.add_argument("--allow-writes", action="store_true",
                   help="let GX write to the USB drive (refused by default)")
    p.add_argument("--skip", choices=("0", "1"), help="the exit's unmount skips unchanged sectors")
    p.add_argument("--limit", type=int, help="seconds for any one bench step (default 600)")
    p.add_argument("--boot", type=int, help="seconds for GX's start-up (default 180)")
    p.add_argument("--dol", type=pathlib.Path, default=REPO / ".dev" / "usbbench" / "boot.dol")
    p.add_argument("--wait", type=int, default=1800, help="seconds to wait for HBC to come back")
    p.add_argument("--dry-run", action="store_true", help="check and print, send nothing")
    o = p.parse_args()

    args = ["-ios=249", "-bench=" + o.mode]
    if o.allow_writes:
        args.append("-benchwrites=1")
    if o.skip is not None:
        args.append("-benchskip=" + o.skip)
    if o.limit:
        args.append(f"-benchlimit={o.limit}")
    if o.boot:
        args.append(f"-benchboot={o.boot}")
    out = REPO / ".runs" / "usb_bench" / (datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + o.mode)

    if not o.dol.is_file():
        sys.exit(f"no such .dol: {o.dol} (run: bash scripts/build.sh bench)")
    print(f"bench build {o.dol} ({o.dol.stat().st_size} bytes), args {' '.join(args)}, "
          f"USB writes {'ALLOWED' if o.allow_writes else 'refused'}, logs -> {out}", flush=True)
    if o.dry_run:
        print("dry run: nothing sent")
        return 0
    wii = bench_wii()
    out.mkdir(parents=True, exist_ok=True)

    if answer(wii) != "hbc":
        sys.exit("HBC is not what answers on the Wii: not sending anything")
    # A run that stops early must not leave the last run's logs to be read as its own
    for name in ("usbbench.log", "exit.log"):
        hbc(wii, "rm", REVIEW + name, timeout=60)
    rc, _ = hbc(wii, "send", o.dol, "--", *args, timeout=180)
    if rc != 0:
        sys.exit("HBC did not start the bench build")
    if not wait_for_hbc(wii, o.wait):
        print(f"STUCK: HBC did not answer again in {o.wait} s; the Wii needs someone to bring it back",
              flush=True)
        return 2
    hbc(wii, "lastlog", timeout=60, log=out / "lastlog.txt")
    hbc(wii, "crash", timeout=60, log=out / "crash.txt")
    ok = True
    for name in ("usbbench.log", "exit.log"):
        rc, _ = hbc(wii, "get", REVIEW + name, out / name, timeout=120)
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
