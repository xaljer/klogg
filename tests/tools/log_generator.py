#!/usr/bin/env python3
"""Small continuous log generator for exercising klogg's Recorder Commands.

The recorder example writes stdout into the file currently open in klogg:

    python -u "D:\\path\\to\\tools\\log_generator.py" --interval 0.2 > "{file}"

Use --count to make a finite run and --format json for structured logs.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import random
import sys
import time

LEVELS = ("DEBUG", "INFO", "WARN", "ERROR")
MODULES = ("api", "db", "auth", "cache", "worker", "scheduler")
MESSAGES = {
    "DEBUG": (
        "cache lookup key=user:%(id)d hit=%(hit)s",
        "worker heartbeat queue=%(queue)d",
        "scheduler tick delay_ms=%(delay)d",
    ),
    "INFO": (
        "request %(id)d completed status=200 duration_ms=%(delay)d",
        "user %(id)d signed in from 10.0.0.%(octet)d",
        "job %(id)d processed rows=%(queue)d worker=%(worker)d",
    ),
    "WARN": (
        "slow request %(id)d duration_ms=%(delay)d threshold_ms=250",
        "retry %(attempt)d for job %(id)d",
        "cache eviction size=%(queue)d",
    ),
    "ERROR": (
        "request %(id)d failed status=503 reason=upstream timeout",
        "job %(id)d failed attempts=%(attempt)d",
        "database connection reset retry_in_ms=%(delay)d",
    ),
}

COLORS = {
    "DEBUG": "\x1b[36m",
    "INFO": "\x1b[32m",
    "WARN": "\x1b[33m",
    "ERROR": "\x1b[31m",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate klogg test log lines")
    parser.add_argument("--interval", type=float, default=0.25, help="seconds between lines")
    parser.add_argument("--count", type=int, default=0, help="line count; 0 means forever")
    parser.add_argument("--seed", type=int, default=7, help="random seed")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--ansi", action="store_true", help="color level labels with ANSI")
    return parser.parse_args()


def line_text(seq: int, level: str, module: str, message: str) -> str:
    stamp = dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
    return f"{stamp} {level:<5} [{module}] {message}"


def main() -> int:
    args = parse_args()
    if sys.stdout.reconfigure:
        try:
            sys.stdout.reconfigure(encoding="utf-8", newline="\n")
        except (AttributeError, ValueError):
            pass

    rng = random.Random(args.seed)
    seq = 0
    try:
        while args.count <= 0 or seq < args.count:
            seq += 1
            level = rng.choices(LEVELS, weights=(8, 82, 8, 2), k=1)[0]
            module = rng.choice(MODULES)
            message = rng.choice(MESSAGES[level]) % {
                "id": rng.randint(1000, 9999),
                "queue": rng.randint(0, 40),
                "delay": rng.randint(5, 900),
                "octet": rng.randint(2, 240),
                "worker": rng.randint(1, 8),
                "hit": rng.choice(("true", "false")),
                "attempt": rng.randint(1, 4),
            }
            if args.format == "json":
                payload = {
                    "timestamp": dt.datetime.now(dt.timezone.utc)
                    .isoformat(timespec="milliseconds")
                    .replace("+00:00", "Z"),
                    "level": level,
                    "module": module,
                    "message": message,
                    "seq": seq,
                }
                print(json.dumps(payload, ensure_ascii=False), flush=True)
            else:
                label = level
                if args.ansi:
                    label = f"{COLORS[level]}{level}\x1b[0m"
                print(line_text(seq, label, module, message), flush=True)
            if args.interval > 0:
                time.sleep(args.interval)
    except (BrokenPipeError, KeyboardInterrupt):
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
