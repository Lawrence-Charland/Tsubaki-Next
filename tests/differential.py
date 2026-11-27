#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Lawrence Charland
"""Differential tests: the built-in algorithms against independent tools.

Every case hashes the same bytes twice — once with tsubaki, once with a
reference implementation — and compares the hexadecimal digests. Only built-in
algorithms are covered, and only the digest itself is compared: the listing
format, the report block and the option handling are general.py's business.

    ./tests/differential.py
    TSUBAKI=/usr/bin/tsubaki ./tests/differential.py   # name the binary
    ./tests/differential.py --binary /path/to/tsubaki

coreutils (`*sum`, `b2sum`) and `openssl` are the references. A tool that is
not installed turns its algorithms into skips rather than failures, so a bare
machine still runs the rest. Skipping is not silent: the summary says how many
algorithms were left out.

Exit status: 0 = every comparison matched, 1 = at least one mismatch, 2 =
nothing could be compared.
"""

from __future__ import annotations

import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from harness import resolve_binary

# The empty input, one byte, and both sides of the 64- and 128-byte block
# boundaries, then inputs larger than the 1 MiB copy chunk.
SIZES = (0, 1, 3, 55, 56, 63, 64, 65, 127, 128, 129, 1023, 4096, 1 << 20 | 1)

# ASCII, because `-k` uses the bytes of the argument verbatim and so does
# `openssl dgst -hmac`.
TEXT_KEY = "differential-test-key"
# 32 bytes: within BLAKE2b's 64-byte and BLAKE2s's 32-byte key limit.
MAC_KEY = bytes(range(32))

CORESUMS = {
    "md5": "md5sum",
    "sha1": "sha1sum",
    "sha224": "sha224sum",
    "sha256": "sha256sum",
    "sha384": "sha384sum",
    "sha512": "sha512sum",
    "blake2b512": "b2sum",
}

# openssl dgst -r -<name>
DIGESTS = {
    "sha3_256": "sha3-256",
    "sha3_512": "sha3-512",
    "blake2s256": "blake2s256",
}

# openssl dgst -r -<name> -hmac <key>
HMACS = {
    "hmac_md5": "md5",
    "hmac_sha1": "sha1",
    "hmac_sha224": "sha224",
    "hmac_sha256": "sha256",
    "hmac_sha384": "sha384",
    "hmac_sha512": "sha512",
    "hmac_sha3_256": "sha3-256",
    "hmac_sha3_512": "sha3-512",
}

# openssl mac -macopt hexkey:<key> -in <file> <name>
MACS = {
    "blake2b512_mac": "BLAKE2BMAC",
    "blake2s256_mac": "BLAKE2SMAC",
}


def capture(cmd: list[str]) -> str:
    cp = subprocess.run(cmd, capture_output=True, text=True)
    if cp.returncode != 0:
        raise RuntimeError(f"{cmd[0]} exited {cp.returncode}: "
                           f"{cp.stderr.strip() or cp.stdout.strip()}")
    return cp.stdout


def tool_for(algo: str) -> str:
    if algo in CORESUMS:
        return CORESUMS[algo]
    return "openssl"


def reference(algo: str, path: Path) -> str:
    """The hex digest of `path` from an implementation other than tsubaki."""
    if algo in CORESUMS:
        return capture([CORESUMS[algo], str(path)]).split()[0].lower()
    if algo in DIGESTS:
        out = capture(["openssl", "dgst", "-r", "-" + DIGESTS[algo],
                       str(path)])
        return out.split()[0].lower()
    if algo in HMACS:
        out = capture(["openssl", "dgst", "-r", "-" + HMACS[algo],
                       "-hmac", TEXT_KEY, str(path)])
        return out.split()[0].lower()
    if algo in MACS:
        out = capture(["openssl", "mac", "-macopt", f"hexkey:{MAC_KEY.hex()}",
                       "-in", str(path), MACS[algo]])
        return out.strip().splitlines()[-1].lower()
    raise KeyError(algo)


def tsubaki_digest(binary: Path, algo: str, source: str,
                   key_file: Path | None, text_key: str | None,
                   stdin_data: bytes | None) -> str:
    """The hex digest tsubaki prints for one file, or for /dev/stdin.

    Only the first field of the single result line is read; `#` lines are the
    header and the report block.
    """
    cmd = [str(binary), "sum", algo]
    if key_file is not None:
        cmd += ["-K", str(key_file)]
    if text_key is not None:
        cmd += ["-k", text_key]
    cmd.append(source)
    kwargs: dict[str, object] = {"capture_output": True}
    if stdin_data is None:
        kwargs["stdin"] = subprocess.DEVNULL
    else:
        kwargs["input"] = stdin_data
    cp = subprocess.run(cmd, **kwargs)
    lines = [line for line in cp.stdout.decode("utf-8", "replace").splitlines()
             if line and not line.startswith("#")]
    if cp.returncode != 0 or len(lines) != 1:
        raise RuntimeError(f"rc={cp.returncode} results={len(lines)}: "
                           f"{cp.stderr.decode('utf-8', 'replace').strip()}")
    return lines[0].split(" ", 1)[0]


def version_of(tool: str) -> str:
    try:
        out = capture([tool, "--version"] if tool != "openssl"
                      else ["openssl", "version"])
    except (OSError, RuntimeError):
        return tool
    return out.splitlines()[0] if out else tool


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Differential tests for tsubaki's built-in algorithms",
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
        return 1

    algorithms = [*CORESUMS, *DIGESTS, *HMACS, *MACS]
    tools = sorted({tool_for(algo) for algo in algorithms
                    if shutil.which(tool_for(algo))})
    print(f"tsubaki: {binary}")
    seen: set[str] = set()
    for tool in tools:
        line = version_of(tool)
        if "GNU coreutils" in line:
            line = "coreutils " + line.split(") ", 1)[-1]
        else:
            line = line.split(" (", 1)[0]
        if line not in seen:
            seen.add(line)
            print(f"reference: {line}")

    matched = 0
    mismatched = 0
    skipped = 0
    with tempfile.TemporaryDirectory(prefix="tsubaki-diff-") as tmp:
        root = Path(tmp)
        rng = random.Random(0x74737562)
        samples = {}
        for size in SIZES:
            path = root / f"input-{size}.bin"
            path.write_bytes(rng.randbytes(size))
            samples[size] = path
        key_file = root / "mac.key"
        key_file.write_bytes(MAC_KEY)

        for algo in algorithms:
            tool = tool_for(algo)
            if shutil.which(tool) is None:
                print(f"  \033[33mSKIP\033[0m  {algo:<16} {tool} not found")
                skipped += 1
                continue
            try:
                reference(algo, samples[SIZES[0]])
            except RuntimeError as exc:
                print(f"  \033[33mSKIP\033[0m  {algo:<16} {exc}")
                skipped += 1
                continue

            key_file_arg = key_file if algo in MACS else None
            text_key_arg = TEXT_KEY if algo in HMACS else None
            items = [(f"size {size}", path, str(path), None)
                     for size, path in samples.items()]
            # A device is read as a stream, so /dev/stdin hashes what arrives
            # on stdin; `-` would be a listing instead.
            stream = samples[4096]
            items.append(("stream", stream, "/dev/stdin",
                          stream.read_bytes()))

            cases = len(items)
            bad: list[str] = []
            for label, sample, source, data in items:
                try:
                    want = reference(algo, sample)
                    got = tsubaki_digest(binary, algo, source,
                                         key_file=key_file_arg,
                                         text_key=text_key_arg,
                                         stdin_data=data)
                except RuntimeError as exc:
                    bad.append(f"{label}: {exc}")
                    continue
                if got != want:
                    bad.append(f"{label}:\n"
                               f"            tsubaki   {got}\n"
                               f"            reference {want}")

            matched += cases - len(bad)
            mismatched += len(bad)
            verdict = "\033[32mOK  \033[0m" if not bad else "\033[31mFAIL\033[0m"
            print(f"  {verdict}  {algo:<16} {cases} cases, {tool}")
            for detail in bad:
                print(f"        {detail}")

    print("\n--------------------------------")
    print(f"{matched} matched, {mismatched} mismatched, "
          f"{skipped} algorithms skipped")
    if mismatched:
        return 1
    return 0 if matched else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

# This test script was assisted by DeepSeek.
