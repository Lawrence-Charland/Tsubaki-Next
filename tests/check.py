#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Lawrence Charland
"""Run every black-box suite and report one verdict.

The three suites are independent programs and stay runnable on their own; this
file only collects them, so that a release check is one command:

    general.py       the program's own promises: sum, verify, cmp, the formats
    abi.py           docs/PROVIDER-ABI.md; builds modules, so it needs a compiler
    differential.py  the built-in digests against coreutils and OpenSSL

Each suite prints its own PASS/FAIL lines; this runner passes them through
unchanged and then a per-suite verdict. Nothing is shared between the suites
except the binary, so a missing compiler or reference tool hits only its own
suite: `abi.py` exits 2, `differential.py` skips the affected algorithms.

    ./tests/check.py                          # find the binary under build/
    TSUBAKI=/usr/bin/tsubaki ./tests/check.py # name the binary
    ./tests/check.py --binary /path/to/tsubaki
    ./tests/check.py --cxx clang++            # forwarded to abi.py

Exit status: 0 = every suite passed, 1 = at least one failure, 2 = no failure
but a suite could not run at all, so the run is incomplete.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

from harness import resolve_binary

HERE = Path(__file__).resolve().parent
SUITES = ("general.py", "abi.py", "differential.py")
VERDICT = {0: "pass", 1: "fail", 2: "incomplete"}


def tally(text: str) -> str:
    """The suite's own closing count line, if it printed one."""
    for line in reversed(text.splitlines()):
        if re.match(r"^\s*\d+ (?:passed|matched), ", line):
            return line
    return ""


def run_suite(name: str, cmd: list[str]) -> tuple[int, str]:
    print(f"\n== {name} ==", flush=True)
    try:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, text=True,
            env={**os.environ, "PYTHONUNBUFFERED": "1"},
        )
    except OSError as exc:
        print(f"could not start {name}: {exc}", file=sys.stderr)
        return 1, ""

    lines: list[str] = []
    assert proc.stdout is not None
    for line in proc.stdout:
        lines.append(line)
        sys.stdout.write(line)
        sys.stdout.flush()
    return proc.wait(), tally("".join(lines))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Run the black-box suites for tsubaki",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "-b", "--binary",
        default=os.environ.get("TSUBAKI"),
        help="path to the tsubaki executable",
    )
    parser.add_argument(
        "--cxx",
        default=os.environ.get("CXX"),
        help="compiler for abi.py's fixture modules",
    )
    ns = parser.parse_args(argv)

    binary = resolve_binary(ns.binary)
    if binary is None:
        print("tsubaki not found. Pass --binary or set "
              "TSUBAKI=/path/to/tsubaki.",
              file=sys.stderr)
        return 1

    results = []
    for name in SUITES:
        cmd = [sys.executable, str(HERE / name), "--binary", str(binary)]
        if name == "abi.py" and ns.cxx:
            cmd += ["--cxx", ns.cxx]
        rc, line = run_suite(name, cmd)
        results.append((name, rc, line))

    print("\n--------------------------------")
    for name, rc, line in results:
        verdict = VERDICT.get(rc, f"rc={rc}")
        print(f"{name:<16} {verdict:<11} {line}".rstrip())

    if any(rc not in (0, 2) for _, rc, _ in results):
        return 1
    return 2 if any(rc == 2 for _, rc, _ in results) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

# This test script was assisted by DeepSeek.
