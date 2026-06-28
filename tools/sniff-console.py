#!/usr/bin/env python3
"""Sniff every /dev/cu.usbmodem* port at once, labeled and timestamped.

The Tanmatsu exposes more than one USB serial interface (P4 host console and the
C6 radio slave console), their device numbers shift across the launcher<->app
reboot, and AppFS launches re-enumerate USB. Rather than guess which port the
output is on, this opens them ALL and re-scans for new ports each second, so the
bench table (or any printf) is captured wherever it lands.

Usage:
  tools/sniff-console.py [--seconds N] [--glob PATTERN] [--log FILE]

Run it in one terminal, then `make bench-device` in another. Ctrl-C to stop.
Needs pyserial — the badgelink venv has it; this script auto-finds that venv.
"""
import sys, os, glob, time, argparse

# Prefer the badgelink venv's pyserial if the system python lacks it.
if "serial" not in sys.modules:
    try:
        import serial  # noqa: F401
    except ModuleNotFoundError:
        here = os.path.dirname(os.path.abspath(__file__))
        venv = os.path.join(here, "..", "badgelink", "tools", ".venv")
        sp = glob.glob(os.path.join(venv, "lib", "python*", "site-packages"))
        if sp:
            sys.path.insert(0, sp[0])
import serial  # type: ignore


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=0,
                    help="stop after N seconds (0 = until Ctrl-C)")
    ap.add_argument("--glob", default="/dev/cu.usbmodem*",
                    help="port glob (default: /dev/cu.usbmodem*)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--log", default=None, help="also append output to this file")
    args = ap.parse_args()

    logf = open(args.log, "a") if args.log else None
    ports = {}  # path -> Serial
    t0 = time.time()

    def emit(line):
        sys.stdout.write(line)
        sys.stdout.flush()
        if logf:
            logf.write(line)
            logf.flush()

    emit(f"# sniffing {args.glob} (baud {args.baud}); Ctrl-C to stop\n")
    try:
        while True:
            # (Re)open any port matching the glob that we don't have open.
            for path in sorted(glob.glob(args.glob)):
                if path not in ports:
                    try:
                        ports[path] = serial.Serial(path, args.baud, timeout=0)
                        emit(f"\n# + opened {path}\n")
                    except Exception as e:
                        # Port may be momentarily busy mid-enumeration; retry next scan.
                        ports[path] = None
                        emit(f"# (cannot open {path} yet: {e})\n")
            for path, s in list(ports.items()):
                if s is None:
                    if path in ports:
                        del ports[path]  # force a reopen attempt next scan
                    continue
                try:
                    data = s.read(8192)
                except Exception:
                    emit(f"# - lost {path}\n")
                    try:
                        s.close()
                    except Exception:
                        pass
                    del ports[path]
                    continue
                if data:
                    tag = path.rsplit("usbmodem", 1)[-1]
                    txt = data.decode(errors="replace")
                    for ln in txt.splitlines(keepends=True):
                        emit(f"[{tag}] {ln if ln.endswith(chr(10)) else ln + chr(10)}")
            if args.seconds and (time.time() - t0) >= args.seconds:
                emit("\n# done (timeout)\n")
                break
            time.sleep(0.05)
    except KeyboardInterrupt:
        emit("\n# stopped\n")
    finally:
        for s in ports.values():
            if s:
                try:
                    s.close()
                except Exception:
                    pass
        if logf:
            logf.close()


if __name__ == "__main__":
    main()
