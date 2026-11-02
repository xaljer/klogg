#!/usr/bin/env python3
"""Generate the test fixtures that used to be committed under test_data/.

The repository stores the generator, never the data: everything is written to
out/testdata (gitignored) and regenerated on demand. The content is
deterministic: the same seed always produces byte-identical files, so a fixture
can be recreated on any machine without shipping it in git.

The shapes follow the fixtures that were committed before: the same names, the
same encodings, and the same sizes where the original used a round number
(512 KiB / 1 MiB / 1.5 MiB blocks, 1 MiB UTF-16 blocks). The bytes are not the
historical bytes - nothing in the tree depends on them.

    python tests/tools/gen_test_data.py            # small/medium fixtures
    python tests/tools/gen_test_data.py --big      # also the 500 MB log
    python tests/tools/gen_test_data.py --force    # rewrite existing files
"""

from __future__ import annotations

import argparse
import pathlib
import random
import sys

SEED = 20261108

LEVELS = ("DEBUG", "INFO", "WARN", "ERROR")
MODULES = ("api", "db", "auth", "cache", "worker", "scheduler")
ANSI = {
    "DEBUG": "\x1b[36m",
    "INFO": "\x1b[32m",
    "WARN": "\x1b[33m",
    "ERROR": "\x1b[31m",
}

# CJK pool for the wide-character fixture; deterministic through the seed.
CJK_POOL = (
    "大供型払活刊乗情著強後訪挙貫行禁保年設会済産任技肉動載並細需康女項非韓慶支了科割律掲"
    "検真必闘世多紙称昌注連率筋川評系残源雨入完読転投去州代趣方信邦属男問月吉利治接定急健"
    "綴器記物藤民権聞込席荘心芸手捕小症申登進写全渡線区速撲無内義本暮疑朝予新表住慎碁官天"
)

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


def log_line(rng: random.Random, seq: int, ansi: bool = False) -> str:
    level = rng.choices(LEVELS, weights=(8, 82, 8, 2), k=1)[0]
    module = rng.choice(MODULES)
    label = f"{ANSI[level]}{level}\x1b[0m" if ansi else f"{level:<5}"
    message = rng.choice(
        (
            f"request {rng.randint(1000, 9999)} completed status=200 duration_ms={rng.randint(5, 900)}",
            f"job {rng.randint(1000, 9999)} processed rows={rng.randint(1, 5000)} worker={rng.randint(1, 8)}",
            f"cache lookup key=user:{rng.randint(1000, 9999)} hit={rng.choice(('true', 'false'))}",
            f"retry {rng.randint(1, 4)} for job {rng.randint(1000, 9999)}",
            f"user {rng.randint(1000, 9999)} signed in from 10.0.0.{rng.randint(2, 240)}",
        )
    )
    return f"2026-11-08T12:{seq % 60:02d}:{seq % 60:02d}.{seq % 1000:03d}Z {label} [{module}] {message}"


def random_log_bytes(rng: random.Random, size: int, *, newline: bool = True) -> bytes:
    """Fill exactly `size` bytes with log lines (no trailing LF when newline=False)."""
    out = bytearray()
    seq = 0
    separator = b"\n" if newline else b""
    while len(out) < size:
        seq += 1
        out += log_line(rng, seq).encode("utf-8") + separator
    return bytes(out[:size])


def utf16_bytes(text: str, encoding: str, total_size: int) -> bytes:
    """Encode `text` as UTF-16 with a BOM, trimmed to exactly `total_size` bytes."""
    bom = b"\xff\xfe" if encoding == "utf-16-le" else b"\xfe\xff"
    body = text.encode(encoding)[: total_size - len(bom)]
    return bom + body


def ansi_matrix() -> str:
    """SGR showcase used for manual ANSI checks: reset, bold, 30-37, 1;3x, 90-97."""
    labels = ["m", "1m"]
    for base in range(30, 38):
        labels += [f"{base}m", f"1;{base}m", f"{base + 60}m"]
    return "\n".join(f"{label:>5}  \x1b[{label}  Test" for label in labels)


def tab_sample() -> str:
    """Log-like lines with tab stops at different columns (tab rendering checks)."""
    stamps = "2017-08-07 08:19:08.656"
    return "\n".join(
        (
            f"{stamps} INFO        [19653] [main@115] Solver 19653Olympia started",
            f"{stamps} INFO\t  [19653] [Solver::Run@45] Connecting...",
            f"{stamps} INFO\t\t[19655] [main@115] tab after level",
            f"{stamps}\tINFO  \t[19655] [Solver::Run@64] Connected",
            f"\t{stamps} INFO  \t[19655] [Solver::Run@70] Waiting state...",
            f"{stamps} INFO\t  [19655] [Solver::Run@72] wide\tTab\tmixed",
            f"{stamps} WARN\t  [19655] [Solver::Run@80] Retry\t1",
            f"{stamps} ERROR\t  [19655] [Solver::Run@90] Failed\tcode=42",
            f"{stamps} DEBUG\t[19655] [Solver::Run@95] Done\telapsed=12ms",
            f"{stamps} INFO\t\t\t[19655] [Solver::Run@99] Three tabs",
        )
    ) + "\n"


def chinese_lipsum(rng: random.Random, lines: int = 272, width: int = 85) -> str:
    rows = []
    for _ in range(lines):
        rows.append("".join(rng.choice(CJK_POOL) for _ in range(width)) + "。")
    # Keep the committed fixture size: 46922 bytes = BOM + 23460 UTF-16 code units.
    return ("\n".join(rows) + "\n")[:23460]


def write(path: pathlib.Path, data: bytes, force: bool) -> str:
    if path.exists() and not force and path.stat().st_size == len(data):
        return f"skip   {path.name} ({len(data)} bytes)"
    path.write_bytes(data)
    return f"write  {path.name} ({len(data)} bytes)"


def build_fixtures(big: bool) -> dict[str, bytes]:
    rng = random.Random(SEED)
    files: dict[str, bytes] = {}

    # Random log blocks in the sizes the smoke and perf scenarios use.
    files["random_block_100byte.txt"] = random_log_bytes(rng, 101)
    files["random_block_512k.txt"] = random_log_bytes(rng, 512 * 1024)
    files["random_block_512k_no_lf.txt"] = random_log_bytes(rng, 512 * 1024, newline=False)
    files["random_block_1Mb.txt"] = random_log_bytes(rng, 1024 * 1024)
    files["random_block_1Mb_no_lf.txt"] = random_log_bytes(rng, 1024 * 1024, newline=False)
    files["random_block_1.5Mb.txt"] = random_log_bytes(rng, 1536 * 1024)

    # UTF-16 variants of the 1 MiB block (encoding detection / decoding paths).
    # The committed files were 1 MiB in total, BOM included.
    base_text = files["random_block_1Mb.txt"].decode("utf-8", "ignore")
    files["random_block_1Mb_utf16le.txt"] = utf16_bytes(base_text, "utf-16-le", 1024 * 1024)
    files["random_block_1Mb_utf16be.txt"] = utf16_bytes(base_text, "utf-16-be", 1024 * 1024)

    # 202 bytes each: BOM + 100 UTF-16 code units.
    small = (log_line(rng, 7) + " " * 120)[:100]
    files["random_block_100_byte_utf16le.txt"] = b"\xff\xfe" + small.encode("utf-16-le")
    files["random_block_100_byte_utf16be.txt"] = b"\xfe\xff" + small.encode("utf-16-be")

    # Tab handling in UTF-8 / UTF-16LE / UTF-16BE.
    # The committed tab fixture was 519 bytes of ASCII (1040 bytes as UTF-16LE).
    tabbed = tab_sample().encode("ascii")[:519].decode("ascii")
    files["utf8_tab_test.txt"] = tabbed.encode("utf-8")
    files["utf16_tab_test.txt"] = b"\xff\xfe" + tabbed.encode("utf-16-le")
    files["utf16be_tab_test.txt"] = b"\xfe\xff" + tabbed.encode("utf-16-be")

    # Chinese text in UTF-16LE / UTF-16BE (wide-character wrapping checks):
    # 272 lines of CJK, like the committed fixture (~46 KB).
    chinese = chinese_lipsum(random.Random(SEED + 1))
    files["Chinese-Lipsum.utf16.txt"] = b"\xff\xfe" + chinese.encode("utf-16-le")
    files["Chinese-Lipsum.utf16be.txt"] = b"\xfe\xff" + chinese.encode("utf-16-be")

    # ANSI SGR showcase used by the manual `klogg_grep` / rendering checks.
    files["ansi_colors_example.txt"] = ansi_matrix().encode("ascii")

    # Log used by the Electron session / recorder smoke tests (never committed).
    rec_rng = random.Random(SEED + 2)
    files["recorder_demo.log"] = (
        "\n".join(log_line(rec_rng, i) for i in range(1, 201)) + "\n"
    ).encode("utf-8")

    if big:
        files["random_block_500Mb.txt"] = random_log_bytes(random.Random(SEED + 3), 500 * 1024 * 1024)

    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        default=str(REPO_ROOT / "out" / "testdata"),
        help="output directory (default: <repo>/out/testdata)",
    )
    parser.add_argument("--force", action="store_true", help="rewrite existing files")
    parser.add_argument("--big", action="store_true", help="also generate the 500 MB log")
    args = parser.parse_args()

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, data in build_fixtures(args.big).items():
        print(write(out / name, data, args.force))
    print(f"fixtures in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
