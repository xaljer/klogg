#!/usr/bin/env python3
"""Continuous high-rate log writer for manually testing follow mode.

Writes real timestamps to PATH and keeps growing it until the duration elapses
or Ctrl+C is pressed. The default rate is high enough to exercise the web UI's
live refresh and search updates.

    python tests/tools/gen_live_log.py D:\\logs\\live.log
    python tests/tools/gen_live_log.py D:\\logs\\live.log --duration 300 --rate 20000
    python tests/tools/gen_live_log.py D:\\logs\\live.log --error-every 10 --rate 2000

Open PATH in klogg and enable "跟随文件"/Follow; every --error-every line carries
ERR so a search for ERR can be tested against a growing result set.
"""

from __future__ import annotations

import argparse
import datetime as dt
import sys
import time


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Grow a log file at a fixed line rate")
    parser.add_argument("path", help="log file to (re)create and grow")
    parser.add_argument("--duration", type=float, default=60.0, help="seconds to write (0 = forever)")
    parser.add_argument("--rate", type=float, default=5000.0, help="target lines per second")
    parser.add_argument("--error-every", type=int, default=20, help="mark every Nth line as ERR")
    parser.add_argument("--append", action="store_true", help="append instead of truncating")
    parser.add_argument("--flush-interval", type=float, default=0.05, help="flush cadence in seconds")
    return parser.parse_args()


def make_line(sequence: int, error_every: int) -> str:
    stamp = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
    level = "ERR " if error_every > 0 and sequence % error_every == 0 else "INFO"
    return (
        f"{stamp} {level} request id={sequence} path=/api/v1/items/{sequence % 997} "
        f"status=200 duration={sequence % 500}ms worker={sequence % 8}\n"
    )


def main() -> int:
    args = parse_args()
    if args.rate <= 0:
        print("--rate must be positive", file=sys.stderr)
        return 2
    batch_size = max(1, int(args.rate * args.flush_interval))
    mode = "a" if args.append else "w"
    started = time.time()
    sequence = 0
    written_bytes = 0

    print(f"writing {args.path} at ~{args.rate:.0f} lines/s, batch={batch_size}", flush=True)
    try:
        with open(args.path, mode, encoding="utf-8", buffering=1024 * 1024) as handle:
            next_report = started + 1.0
            while True:
                if args.duration > 0 and time.time() - started >= args.duration:
                    break
                batch_started = time.time()
                batch = []
                for _ in range(batch_size):
                    sequence += 1
                    batch.append(make_line(sequence, args.error_every))
                text = "".join(batch)
                handle.write(text)
                handle.flush()
                written_bytes += len(text.encode("utf-8"))
                if time.time() >= next_report:
                    elapsed = max(1e-6, time.time() - started)
                    print(
                        f"  {sequence} lines, {written_bytes / (1024 * 1024):.1f} MiB, "
                        f"{sequence / elapsed:.0f} lines/s",
                        flush=True,
                    )
                    next_report += 1.0
                remaining = args.flush_interval - (time.time() - batch_started)
                if remaining > 0:
                    time.sleep(remaining)
    except KeyboardInterrupt:
        print("\ninterrupted", flush=True)
    finally:
        elapsed = max(1e-6, time.time() - started)
        print(
            f"done: {sequence} lines, {written_bytes / (1024 * 1024):.1f} MiB "
            f"in {elapsed:.1f}s ({sequence / elapsed:.0f} lines/s)",
            flush=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
