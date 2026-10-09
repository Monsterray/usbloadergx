"""gecko_log.py OUTFILE [SECONDS [PORT]] -- save what the loader prints to Dolphin's emulated USB Gecko.

GX sends gprintf() and, after USBGeckoOutput(), stdout and stderr to a USB Gecko in
EXI slot B. Dolphin emulates one when Core.SlotB is 7 (EXIDeviceType::Gecko) and serves
it on TCP 55020 ("dolphin gecko", 0xd6ec), or the next free port up to 55030.
With more than one Dolphin running, pass the PORT of the one to read.

Dolphin keeps everything the game sends until the first client connects, so this can
start alongside Dolphin: it retries the connect until the listener exists, then writes
every byte to OUTFILE until Dolphin closes the connection or SECONDS run out.
"""
import socket
import sys
import time


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    limit = float(sys.argv[2]) if len(sys.argv) > 2 else 600.0
    deadline = time.time() + limit
    ports = [int(sys.argv[3])] if len(sys.argv) > 3 else range(55020, 55031)

    sock = None
    while sock is None and time.time() < deadline:
        for port in ports:
            try:
                sock = socket.create_connection(("127.0.0.1", port), timeout=1)
                break
            except OSError:
                continue
        if sock is None:
            time.sleep(0.5)
    if sock is None:
        sys.exit("no USB Gecko listener on 127.0.0.1:55020-55030 (is Core.SlotB=7 set?)")

    sock.settimeout(1)
    with open(out, "wb") as f:
        while time.time() < deadline:
            try:
                data = sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not data:
                break
            f.write(data)
            f.flush()


if __name__ == "__main__":
    main()
