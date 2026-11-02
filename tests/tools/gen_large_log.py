#!/usr/bin/env python3
"""Generate a large, varied log file for manual and performance testing.

Unlike gen_live_log.py (a single repeating template for measuring growth), this
generator mixes realistic record shapes: application logs, HTTP access lines,
JSON events, SQL, stack frames, CJK messages, empty lines, and occasional very
long payloads. The output is deterministic for a given seed.

    python tests/tools/gen_large_log.py D:\\logs\\big-varied.log --size-mb 800
    python tests/tools/gen_large_log.py D:\\logs\\big-varied.log --size-mb 200 --seed 42

Useful search patterns in the generated file:
    ERROR     ~7% of lines
    WARN      ~10%
    timeout   ~1%
    request   ~65%
    GET /api  ~26%
    deadlock  ~0.3%
    用户      ~0.6% (CJK)
    stack     trace/stack frame lines
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import random
import string
import sys
import time
from pathlib import Path

LEVELS = ("ERROR", "WARN", "INFO", "DEBUG")
MODULES = (
    "api",
    "auth",
    "cache",
    "checkout",
    "db",
    "gateway",
    "indexer",
    "scheduler",
    "search",
    "worker",
)
WORKERS = tuple(f"worker-{index:02d}" for index in range(1, 17))
PATHS = (
    "/api/v1/items",
    "/api/v1/orders",
    "/api/v1/search",
    "/api/v1/users",
    "/api/v1/payments",
    "/health",
    "/metrics",
)
SQL = (
    "SELECT id, name, status FROM items WHERE id = $1",
    "UPDATE orders SET status = $1 WHERE id = $2",
    "INSERT INTO audit_log (actor, action, payload) VALUES ($1, $2, $3)",
    "SELECT count(*) FROM sessions WHERE expires_at > now()",
)
CJK = (
    "用户登录成功，来源 IP=10.0.0.{octet}",
    "订单 {id} 支付完成，金额 ¥{amount}",
    "缓存命中率低于阈值，正在回源",
    "数据库连接池已耗尽，等待重试",
)
LONG_TOKEN = "".join(string.ascii_letters + string.digits + "+/=")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a varied large log")
    parser.add_argument("path", help="output file")
    parser.add_argument("--size-mb", type=float, default=800.0, help="target size in MiB")
    parser.add_argument("--seed", type=int, default=20261115)
    parser.add_argument("--long-chance", type=float, default=0.0012,
                        help="fraction of very long JSON payload lines")
    parser.add_argument("--max-long-kb", type=float, default=24.0,
                        help="upper bound for regular long payloads (KiB)")
    parser.add_argument("--ansi", action="store_true", help="color level labels")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    target_bytes = int(args.size_mb * 1024 * 1024)
    path = Path(args.path)
    rng = random.Random(args.seed)
    base = dt.datetime(2026, 11, 15, 0, 0, 0)
    long_chance = max(0.0, args.long_chance)
    max_long = max(1024, int(args.max_long_kb * 1024))
    ansi = {"ERROR": "\x1b[31m", "WARN": "\x1b[33m", "INFO": "\x1b[32m", "DEBUG": "\x1b[36m"}
    reset = "\x1b[0m"
    use_ansi = args.ansi

    written = 0
    lines = 0
    max_line = 0
    counts = {
        "ERROR": 0,
        "WARN": 0,
        "timeout": 0,
        "request": 0,
        "GET /api": 0,
        "deadlock": 0,
        "cjk": 0,
        "long": 0,
        "empty": 0,
        "stack": 0,
    }
    started = time.time()
    next_report = started + 5.0

    def stamp(sequence: int) -> str:
        seconds, millis = divmod(sequence // 2, 1000)
        moment = base + dt.timedelta(seconds=seconds)
        return f"{moment:%Y-%m-%d %H:%M:%S}.{millis:03d}"

    def level_label(level: str) -> str:
        return f"{ansi[level]}{level:<5}{reset}" if use_ansi else f"{level:<5}"

    def token(size: int) -> str:
        return base64.b64encode(rng.randbytes(max(1, size // 2))).decode("ascii")

    def standard(sequence: int) -> str:
        level = rng.choices(LEVELS, weights=(8, 12, 70, 10), k=1)[0]
        module = rng.choice(MODULES)
        worker = rng.choice(WORKERS)
        timestamp = stamp(sequence)
        roll = rng.random()
        if level == "ERROR":
            counts["ERROR"] += 1
            if roll < 0.08:
                counts["timeout"] += 1
                message = f"upstream timeout after {rng.randint(1000, 30000)} ms"
            elif roll < 0.12:
                counts["deadlock"] += 1
                message = "deadlock detected while acquiring lock"
            else:
                message = f"request id={rng.randint(1, 9999999)} failed status=503 reason=upstream"
            return f"{timestamp} {level_label(level)} [{module}] {message} worker={worker}"
        if level == "WARN":
            counts["WARN"] += 1
            if roll < 0.06:
                counts["timeout"] += 1
                return f"{timestamp} {level_label(level)} [{module}] retry after timeout attempt={rng.randint(1, 5)} worker={worker}"
            if roll < 0.2:
                return f"{timestamp} {level_label(level)} [{module}] slow request id={rng.randint(1, 9999999)} duration_ms={rng.randint(300, 5000)} worker={worker}"
            return f"{timestamp} {level_label(level)} [{module}] queue depth={rng.randint(100, 5000)} worker={worker}"
        if level == "DEBUG":
            if roll < 0.01:
                counts["request"] += 1
            return f"{timestamp} {level_label(level)} [{module}] cache lookup key=user:{rng.randint(1, 99999999)} hit={rng.choice(('true', 'false'))}"
        counts["request"] += 1
        path = rng.choice(PATHS)
        if roll < 0.4:
            counts["GET /api"] += 1
            return (
                f"{timestamp} {level_label(level)} [{module}] GET {path}/{rng.randint(1, 997)} "
                f"status=200 duration={rng.randint(1, 900)}ms user={rng.randint(1, 99999)} worker={worker}"
            )
        return (
            f"{timestamp} {level_label(level)} [{module}] request id={rng.randint(1, 9999999)} "
            f"path={path} status=200 duration={rng.randint(1, 900)}ms worker={worker}"
        )

    def access(sequence: int) -> str:
        counts["request"] += 1
        if rng.random() < 0.5:
            counts["GET /api"] += 1
        return (
            f"10.{rng.randint(0, 255)}.{rng.randint(0, 255)}.{rng.randint(1, 254)} - - "
            f"[{stamp(sequence)}] \"GET {rng.choice(PATHS)}/{rng.randint(1, 997)} HTTP/1.1\" "
            f"200 {rng.randint(100, 90000)} \"-\" \"Mozilla/5.0\""
        )

    def json_event(sequence: int) -> str:
        payload = {
            "timestamp": stamp(sequence),
            "level": rng.choice(("info", "warn", "error")),
            "event": rng.choice(("checkout", "login", "index", "search", "payment")),
            "request_id": f"req-{rng.randrange(16**16):016x}",
            "user": rng.randint(1, 9999999),
            "duration_ms": rng.randint(1, 2000),
            "ok": rng.choice(("true", "false")),
        }
        body = ", ".join(f'"{key}":"{value}"' for key, value in payload.items())
        if rng.random() < 0.25:
            body += f',"detail":"{token(rng.randint(200, 1200))}"'
        return "{" + body + "}"

    def sql(sequence: int) -> str:
        return (
            f"{stamp(sequence)} DEBUG [db] sql=\"{rng.choice(SQL)}\" "
            f"rows={rng.randint(0, 500)} duration_ms={rng.randint(1, 300)}"
        )

    def cjk(sequence: int) -> str:
        counts["cjk"] += 1
        message = rng.choice(CJK).format(
            octet=rng.randint(2, 240),
            id=rng.randint(1, 9999999),
            amount=rng.randint(1, 99999),
        )
        return f"{stamp(sequence)} INFO  [app] {message} worker={rng.choice(WORKERS)}"

    def stack(sequence: int) -> str:
        counts["stack"] += 1
        depth = rng.randint(3, 40)
        return (
            f"{stamp(sequence)} ERROR [worker] java.lang.IllegalStateException: "
            f"failed to process batch {rng.randint(1, 100000)}; "
            + " at com.example.service."
            + rng.choice(("Indexer", "Searcher", "Checkout", "Payment"))
            + f".run({rng.choice(('Indexer', 'Searcher'))}.java:{rng.randint(20, 1200)})"
            + (" -> " * min(depth, 12))
        )

    def long_line(sequence: int) -> str:
        counts["long"] += 1
        size = rng.randint(3500, max_long)
        return (
            f"{stamp(sequence)} WARN  [payload] request_id=req-{rng.randrange(16**16):016x} "
            f"document_size={size} body=\"{token(size)}\""
        )

    path.parent.mkdir(parents=True, exist_ok=True)
    print(f"writing {path} until {args.size_mb:.0f} MiB (seed={args.seed})", flush=True)
    with path.open("w", encoding="utf-8", buffering=1024 * 1024) as handle:
        while written < target_bytes:
            batch = []
            for offset in range(2000):
                sequence = lines + offset + 1
                roll = rng.random()
                if roll < long_chance:
                    line = long_line(sequence)
                elif roll < long_chance + 0.002:
                    line = stack(sequence)
                elif roll < long_chance + 0.008:
                    line = cjk(sequence)
                elif roll < long_chance + 0.06:
                    line = json_event(sequence)
                elif roll < long_chance + 0.12:
                    line = access(sequence)
                elif roll < long_chance + 0.16:
                    line = sql(sequence)
                elif roll < long_chance + 0.161:
                    line = ""
                    counts["empty"] += 1
                else:
                    line = standard(sequence)
                batch.append(line)
                batch.append("\n")
                length = len(line.encode("utf-8")) + 1
                written += length
                max_line = max(max_line, len(line))
            handle.write("".join(batch))
            lines += 2000
            if time.time() >= next_report:
                elapsed = max(0.001, time.time() - started)
                print(
                    f"  {written / (1024 * 1024):7.1f} MiB, {lines:,} lines, "
                    f"{written / elapsed / (1024 * 1024):5.1f} MiB/s",
                    flush=True,
                )
                next_report += 5.0

    elapsed = max(0.001, time.time() - started)
    print(
        f"done: {written / (1024 * 1024):.1f} MiB, {lines:,} lines, max line {max_line:,} chars, "
        f"{elapsed:.1f}s",
        flush=True,
    )
    print(
        "matches: "
        + ", ".join(f"{key}={value:,}" for key, value in counts.items()),
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
