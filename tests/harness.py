#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Lawrence Charland
"""Shared plumbing for the black-box test suites.

`general.py` covers the program's own promises and needs nothing but the binary;
`abi.py` compiles provider modules and covers docs/PROVIDER-ABI.md. Both print
the same PASS/FAIL lines and count the same way, so this module holds the run
wrapper, the counters and the small text helpers.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class Run:
    __slots__ = ("rc", "out", "err")

    def __init__(self, rc: int, out: str = "", err: str = "") -> None:
        self.rc = rc
        self.out = out
        self.err = err


class Harness:
    def __init__(self, binary: Path) -> None:
        self.bin = str(Path(binary).resolve())
        self.passed = 0
        self.failed = 0
        self.known = 0
        self.skipped = 0
        self.failed_names: list[str] = []
        self.known_names: list[str] = []
        self.skipped_names: list[str] = []

    def run(
        self,
        *args: object,
        stdin_data: str | None = None,
        timeout: float | None = None,
        cwd: object | None = None,
        env: dict[str, str] | None = None,
    ) -> Run:
        cmd = [self.bin, *(str(a) for a in args)]
        kwargs: dict[str, object] = {
            "capture_output": True,
            "text": True,
            "timeout": timeout,
        }
        if cwd is not None:
            kwargs["cwd"] = cwd
        if env is not None:
            kwargs["env"] = {**os.environ, **env}
        if stdin_data is None:
            kwargs["stdin"] = subprocess.DEVNULL
        else:
            kwargs["input"] = stdin_data
        try:
            cp = subprocess.run(cmd, **kwargs)
        except subprocess.TimeoutExpired:
            return Run(124, "", f"timeout after {timeout}s")
        return Run(cp.returncode, cp.stdout, cp.stderr)

    def sum(self, *args: object, **kwargs: object) -> Run:
        return self.run("sum", *args, **kwargs)

    def verify(self, *args: object, **kwargs: object) -> Run:
        return self.run("verify", *args, **kwargs)

    def cmp(self, *args: object, **kwargs: object) -> Run:
        return self.run("cmp", *args, **kwargs)

    def ok(self, ident: str, name: str) -> None:
        self.passed += 1
        print(f"  \033[32mPASS\033[0m  {ident}  {name}")

    def bad(self, ident: str, name: str, detail: str = "") -> None:
        self.failed += 1
        self.failed_names.append(f"{ident} {name}")
        print(f"  \033[31mFAIL\033[0m  {ident}  {name}")
        if detail:
            print(f"        {detail}")

    def skip(self, ident: str, name: str, reason: str) -> None:
        self.skipped += 1
        self.skipped_names.append(f"{ident} {name} ({reason})")
        print(f"  \033[36mSKIP\033[0m  {ident}  {name}  ({reason})")

    def xpass(self, ident: str, name: str) -> None:
        self.passed += 1
        print(f"  \033[32mXPASS\033[0m {ident}  {name}  "
              f"<- fixed; turn this into a regular case")

    def xfail(self, ident: str, name: str, detail: str = "") -> None:
        self.known += 1
        self.known_names.append(f"{ident} {name}")
        print(f"  \033[33mXFAIL\033[0m {ident}  {name}")
        if detail:
            print(f"        {detail}")

    def regression(self, ident: str, name: str, broken: int,
                   detail: str) -> None:
        if broken == 0:
            self.xpass(ident, name)
        else:
            self.xfail(ident, name, detail)


def section(title: str) -> None:
    print(f"\n{title}")


def field(text: str, name: str) -> str:
    m = re.search(rf"^# {re.escape(name)}:\s*(.*)$", text, re.MULTILINE)
    return m.group(1).strip() if m else ""


def result_lines(text: str, width: int = 64) -> list[str]:
    return [line for line in text.splitlines()
            if re.match(rf"^[0-9a-f]{{{width}}} ", line)]


def count_results(text: str, width: int = 64) -> int:
    return len(result_lines(text, width))


def digest(path: Path, name: str = "sha256") -> str:
    h = hashlib.new(name)
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def write(path: Path, data: bytes) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def resolve_binary(explicit: str | None) -> Path | None:
    if explicit:
        p = Path(explicit)
        return p if p.is_file() and os.access(p, os.X_OK) else None
    for candidate in (
        ROOT / "build" / "linux-gcc-release" / "tsubaki",
        ROOT / "build" / "linux-gcc-debug" / "tsubaki",
    ):
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    found = shutil.which("tsubaki")
    return Path(found) if found else None


def require_binary(argv: list[str], description: str) -> tuple[Path | None, int]:
    """Parse -b/--binary, locate the executable and print a complaint if not."""
    parser = argparse.ArgumentParser(
        description=description,
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "-b", "--binary",
        default=os.environ.get("TSUBAKI"),
        help="path to the tsubaki executable",
    )
    ns = parser.parse_args(argv)

    binary = resolve_binary(ns.binary)
    if binary is None:
        print("tsubaki not found. Pass --binary or set "
              "TSUBAKI=/path/to/tsubaki.",
              file=sys.stderr)
        return None, 1
    return binary, 0

# This test script was assisted by DeepSeek.
