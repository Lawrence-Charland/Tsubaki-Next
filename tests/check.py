#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Lawrence Charland
"""Black-box contract tests for tsubaki.

Every case corresponds to one promise in ARCHITECTURE.md / README.md. Nothing
here calls internal functions: the test feeds bytes in and checks the bytes
that come back out.

    ./tests/check.py                          # find the binary under build/
    TSUBAKI=/usr/bin/tsubaki ./tests/check.py # name the binary
    ./tests/check.py --binary /path/to/tsubaki

Exit status: 0 = everything passed, 1 = something failed.

Cases 06, 07 and 09 guard two defects that were fixed: A, where fan-in dropped
an error once a child stream ran dry, and B, where listscanner never checked
the stream state, so a list source that failed to read looked exhausted. They
pass today; if one turns back into XFAIL, the defect has returned.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHA256_HEADER = "# Algorithm:         sha256\n"


class Run:
    __slots__ = ("rc", "out", "err")

    def __init__(self, rc: int, out: str = "", err: str = "") -> None:
        self.rc = rc
        self.out = out
        self.err = err


class Harness:
    def __init__(self, binary: Path) -> None:
        self.bin = str(binary)
        self.passed = 0
        self.failed = 0
        self.known = 0
        self.failed_names: list[str] = []
        self.known_names: list[str] = []

    def run(
        self,
        *args: object,
        stdin_data: str | None = None,
        timeout: float | None = None,
    ) -> Run:
        cmd = [self.bin, *(str(a) for a in args)]
        kwargs: dict[str, object] = {
            "capture_output": True,
            "text": True,
            "timeout": timeout,
        }
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


# ---------------------------------------------------------------- 01-02

def test_basic(h: Harness, root: Path) -> None:
    section("01-02 Basic checksums")
    fx = root / "t1"
    (fx / "sub" / "deep").mkdir(parents=True)
    write(fx / "a.txt", b"hello\n")
    write(fx / "empty", b"")
    write(fx / "sub" / "with space.txt", b"x\n")
    write(fx / "sub" / "deep" / "a=b.txt", b"y\n")
    write(fx / "sub" / "deep" / "big.bin", os.urandom(100000))

    name = ("output matches sha256sum line by line "
            "(subdirectory/empty file/space name/= name)")
    r = h.sum("sha256", fx)
    got = [tuple(line.split(" ", 1)) for line in result_lines(r.out)]
    want = [(digest(p), str(p)) for p in sorted(fx.rglob("*")) if p.is_file()]
    if r.rc == 0 and sorted(got) == sorted(want):
        h.ok("01", name)
    else:
        h.bad("01", name, f"rc={r.rc} got={got} want={want}")

    name = "empty input: all four counters 0, exit 0"
    r = h.sum("sha256")
    if (r.rc == 0 and field(r.out, "Processed") == "0"
            and field(r.out, "Hashed") == "0"
            and field(r.out, "Failed") == "0"
            and field(r.out, "Not hashed") == "0"
            and field(r.out, "Errors") == "0"):
        h.ok("02", name)
    else:
        h.bad("02", name, f"rc={r.rc} processed={field(r.out, 'Processed')} "
                          f"errors={field(r.out, 'Errors')}")


# ---------------------------------------------------------------- 03

def test_excludes(h: Harness, root: Path) -> None:
    section("03 Exclude semantics")
    fx = root / "t3"
    write(fx / "b" / "in_b.txt", b"1\n")
    write(fx / "bc" / "in_bc.txt", b"2\n")
    name = "-e /a/b does not also exclude /a/bc"
    r = h.sum("sha256", fx, "-e", fx / "b")
    if (r.rc == 0 and count_results(r.out) == 1
            and str(fx / "bc" / "in_bc.txt") in r.out):
        h.ok("03", name)
    else:
        h.bad("03", name, f"rc={r.rc} lines={count_results(r.out)}")


# ---------------------------------------------------------------- 04-05

def test_lists(h: Harness, root: Path) -> None:
    section("04-05 List round-trips")
    fx = root / "t4"
    a = write(fx / "a.txt", b"hello\n")
    keep = root / "keep.list"
    keep.write_text(f"{digest(a)} {a}\n", encoding="utf-8")
    name = "<checksum> <path> line only prints # Kept: and stays out of the counts"
    r = h.sum("sha256", "-l", keep)
    if (r.rc == 0 and field(r.out, "Processed") == "0"
            and f"# Kept: {a}" in r.out):
        h.ok("04", name)
    else:
        h.bad("04", name, f"rc={r.rc} processed={field(r.out, 'Processed')}")

    fx = root / "t5"
    write(fx / "one.txt", b"a\n")
    write(fx / "sub" / "two.txt", b"b\n")
    name = "a <NONE> line is rehashed after being fed back, matching a direct run"
    r_none = h.sum("none", fx)
    manifest = root / "none.txt"
    manifest.write_text(r_none.out, encoding="utf-8")
    r_refeed = h.sum("sha256", "-l", manifest)
    r_direct = h.sum("sha256", fx)
    if (sorted(result_lines(r_refeed.out)) == sorted(result_lines(r_direct.out))
            and field(r_refeed.out, "Hashed") == "2"):
        h.ok("05", name)
    else:
        h.bad("05", name, "the fed-back result differs from a direct run")


# ---------------------------------------------------------------- 06-07

def test_error_paths(h: Harness, root: Path) -> None:
    section("06-07 Error paths")
    fx = root / "t6"
    d = fx / "d"
    write(d / "f.txt", b"x\n")
    dir_list = root / "dir.list"
    dir_list.write_text(f"{d}\n", encoding="utf-8")

    name = "a directory in a list: no output line, Errors always 1, exit 1 (20 runs)"
    broken = 0
    for _ in range(20):
        r = h.sum("sha256", "-l", dir_list)
        if (r.rc != 1 or field(r.out, "Errors") != "1"
                or count_results(r.out) != 0):
            broken += 1
    h.regression(
        "06", name, broken,
        f"regression (defect A): {broken} of 20 runs lost the error or exited 0. "
        "Draining a child stream in place and then dropping it races the errors "
        "other threads are still writing into it")

    name = "a missing path: Errors always 1, Failed 0, exit 1 (20 runs)"
    broken = 0
    for i in range(20):
        r = h.sum("sha256", root / f"does-not-exist-{i}")
        if (r.rc != 1 or field(r.out, "Errors") != "1"
                or field(r.out, "Failed") != "0"):
            broken += 1
    h.regression(
        "07", name, broken,
        f"regression (defect A): {broken} of 20 runs lost the error or exited 0 "
        "(same source as 06)")


# ---------------------------------------------------------------- 08-09

def test_algorithms_and_streams(h: Harness, root: Path) -> None:
    section("08-09 Algorithms and streams")
    fx = root / "t8"
    write(fx / "1.txt", b"a\n")
    write(fx / "2.txt", b"b\n")
    write(fx / "3.txt", b"c\n")
    name = "none: every line is <NONE>, Hashed 0, Not hashed 3"
    r = h.sum("none", fx)
    none_lines = [line for line in r.out.splitlines()
                  if line.startswith("<NONE> ")]
    if (r.rc == 0 and len(none_lines) == 3
            and field(r.out, "Hashed") == "0"
            and field(r.out, "Not hashed") == "3"):
        h.ok("08", name)
    else:
        h.bad("08", name, f"rc={r.rc} none_lines={len(none_lines)} "
                          f"not_hashed={field(r.out, 'Not hashed')}")

    fx = root / "t9"
    d = fx / "d"
    write(d / "f.txt", b"x\n")
    name = "a list source that fails to read must exit non-zero (regression guard)"
    r = h.sum("sha256", "-l", d)
    if r.rc != 0:
        h.xpass("09", name)
    else:
        h.xfail(
            "09", name,
            f"regression (defect B): rc=0 with Processed="
            f"{field(r.out, 'Processed')} and Errors={field(r.out, 'Errors')}. "
            "listscanner treated the I/O error as EOF")


# ---------------------------------------------------------------- 10-11

def test_paths_and_report(h: Harness, root: Path) -> None:
    section("10-11 Paths and report")
    fx = root / "t10" / "w"
    write(fx / "#hash.txt", b"a\n")
    write(fx / "a b.txt", b"b\n")
    write(fx / "a=b.txt", b"c\n")
    name = "paths containing #, a space and = are all handled"
    r = h.sum("sha256", fx)
    if (r.rc == 0 and count_results(r.out) == 3
            and "/#hash.txt" in r.out
            and "/a b.txt" in r.out
            and "/a=b.txt" in r.out):
        h.ok("10", name)
    else:
        h.bad("10", name, f"rc={r.rc} lines={count_results(r.out)}")

    fx = root / "t11" / "d"
    write(fx / "f.txt", b"x\n")
    name = "the report block starts with # and carries the right Algorithm"
    r = h.sum("sha256", fx)
    stray = [line for line in r.out.splitlines()
             if line and not line.startswith("#")
             and not re.match(r"^[0-9a-f]{64} ", line)
             and not line.startswith("<NONE> ")]
    if r.rc == 0 and field(r.out, "Algorithm") == "sha256" and not stray:
        h.ok("11", name)
    else:
        h.bad("11", name, f"unexpected lines: {stray[:3]}")


# ---------------------------------------------------------------- 12-15

def test_subcommands_and_exit_codes(h: Harness, root: Path) -> None:
    section("12-15 Subcommands and exit codes")
    name = "a missing sum subcommand is reported as unknown, exit 64"
    r = h.run("sha256", ".")
    if r.rc == 64 and "unknown subcommand" in r.err:
        h.ok("12", name)
    else:
        h.bad("12", name, f"rc={r.rc} {r.err.splitlines()[:1]}")

    name = "--help and --version need no subcommand"
    r_help = h.run("--help")
    r_ver = h.run("--version")
    if r_help.rc == 0 and r_ver.rc == 0:
        h.ok("13", name)
    else:
        h.bad("13", name, f"help={r_help.rc} version={r_ver.rc}")

    name = "invalid argument: the thread count is not an integer, exit 64"
    r = h.sum("sha256", "--thread=x")
    if r.rc == 64:
        h.ok("14", name)
    else:
        h.bad("14", name, f"rc={r.rc}")

    name = "the output file cannot be opened, exit 73"
    r = h.sum("sha256", ".", "-o", root / "no-such-dir" / "out.txt")
    if r.rc == 73:
        h.ok("15", name)
    else:
        h.bad("15", name, f"rc={r.rc}")


# ---------------------------------------------------------------- 16

def test_key_lengths(h: Harness, root: Path) -> None:
    section("16 Key lengths")
    fx = root / "t16"
    x = write(fx / "x.bin", b"x\n")
    name = "BLAKE2 key limits: 32/64 bytes pass, 33/65 bytes exit 64"
    r32s = h.sum("blake2s256_mac", "-k", "a" * 32, x)
    r33s = h.sum("blake2s256_mac", "-k", "a" * 33, x)
    r64b = h.sum("blake2b512_mac", "-k", "a" * 64, x)
    r65b = h.sum("blake2b512_mac", "-k", "a" * 65, x)
    if (r32s.rc == 0 and count_results(r32s.out) == 1
            and r33s.rc == 64 and count_results(r33s.out) == 0
            and r64b.rc == 0 and r65b.rc == 64
            and 'the key is not valid for algorithm "blake2s256_mac"'
            in r33s.err):
        h.ok("16", name)
    else:
        h.bad("16", name, f"rc: s32={r32s.rc} s33={r33s.rc} "
                          f"b64={r64b.rc} b65={r65b.rc}")


# ---------------------------------------------------------------- 17-19

def test_key_files(h: Harness, root: Path) -> None:
    section("17-19 Key files")
    fx = root / "t17"
    a = write(fx / "a.txt", b"hello\n")
    key_lf = root / "key-lf"
    key_lf.write_text("secret\n", encoding="utf-8")
    key_crlf = root / "key-crlf"
    key_crlf.write_text("secret\r\n", encoding="utf-8")
    key_empty = write(root / "key-empty", b"")
    keydir = root / "keydir"
    keydir.mkdir()
    key_too_long = root / "key-too-long"
    key_too_long.write_text("a" * 33 + "\n", encoding="utf-8")

    name = ("a key file matches -k: LF/CRLF/stdin/sticky all work "
            "and do not eat input files")
    r_want = h.sum("hmac_sha256", a, "-k", "secret")
    r_file = h.sum("hmac_sha256", "-K", key_lf, a)
    r_crlf = h.sum("hmac_sha256", a, f"--key-file={key_crlf}")
    r_sticky = h.sum("hmac_sha256", a, f"-K{key_lf}")
    r_stdin = h.sum("hmac_sha256", a, "-K", "-", stdin_data="secret\n")
    want = result_lines(r_want.out)
    if (want and result_lines(r_file.out) == want
            and result_lines(r_crlf.out) == want
            and result_lines(r_sticky.out) == want
            and result_lines(r_stdin.out) == want
            and r_stdin.rc == 0):
        h.ok("17", name)
    else:
        h.bad("17", name, "the forms disagree")

    name = ("key file error paths: missing/empty/directory 66, -k with -K 64, "
            "too long 64, a plain digest ignores it")
    r_missing = h.sum("hmac_sha256", a, "-K", root / "no-such-key")
    r_empty = h.sum("hmac_sha256", a, "-K", key_empty)
    r_dir = h.sum("hmac_sha256", a, "-K", keydir)
    r_both = h.sum("hmac_sha256", a, "-k", "x", "-K", key_lf)
    r_long = h.sum("blake2s256_mac", a, "-K", key_too_long)
    r_plain = h.sum("sha256", a, "-K", root / "no-such-key")
    if (r_missing.rc == 66 and r_empty.rc == 66 and r_dir.rc == 66
            and r_both.rc == 64 and r_long.rc == 64
            and 'the key is not valid for algorithm "blake2s256_mac"'
            in r_long.err
            and r_plain.rc == 0 and "doesn't need key" in r_plain.err):
        h.ok("18", name)
    else:
        h.bad("18", name, f"rc: missing={r_missing.rc} empty={r_empty.rc} "
                          f"dir={r_dir.rc} both={r_both.rc} "
                          f"long={r_long.rc} plain={r_plain.rc}")

    name = "stdin may be named once only: -K - with a - input exits 64, no result lines"
    r = h.sum("hmac_sha256", a, "-K", "-", "-", stdin_data="secret\n")
    if (r.rc == 64 and not result_lines(r.out)
            and "stdin was used for the key" in r.err):
        h.ok("19", name)
    else:
        h.bad("19", name, f"rc={r.rc}")


# ---------------------------------------------------------------- 20-21

def test_special_files(h: Harness, root: Path) -> None:
    section("20-21 Special files")
    fx = root / "t20"
    write(fx / "a.txt", b"a\n")
    write(fx / "sub" / "b.txt", b"b\n")
    os.mkfifo(fx / "pipe")
    os.symlink("../pipe", fx / "sub" / "link-pipe")
    name = ("a walk yields regular files only: a FIFO and a link to it are "
            "skipped without blocking")
    r = h.sum("sha256", fx, timeout=10)
    if (r.rc == 0 and count_results(r.out) == 2
            and field(r.out, "Errors") == "0" and "pipe" not in r.out):
        h.ok("20", name)
    else:
        h.bad("20", name, f"rc={r.rc} (124 means it hung) "
                          f"lines={count_results(r.out)}")

    fx = root / "t21"
    fifo = fx / "pipe"
    fx.mkdir(parents=True)
    os.mkfifo(fifo)

    def writer() -> None:
        try:
            with open(fifo, "wb") as f:
                f.write(b"hello\n")
        except OSError:
            pass

    name = ("an explicitly named FIFO is still read as a stream: the result "
            "matches what was written")
    th = threading.Thread(target=writer, daemon=True)
    th.start()
    r = h.sum("sha256", fifo, timeout=10)
    th.join(timeout=5)
    want = f"{hashlib.sha256(b'hello\n').hexdigest()} {fifo}"
    if r.rc == 0 and result_lines(r.out) == [want]:
        h.ok("21", name)
    else:
        h.bad("21", name, f"rc={r.rc} result={result_lines(r.out)} want={want}")


# ---------------------------------------------------------------- 22-29

def test_verify(h: Harness, root: Path) -> None:
    section("22-30 Verify")
    fx = root / "v"
    a = write(fx / "a.txt", b"hello\n")
    b = write(fx / "b.txt", b"world\n")
    manifest = root / "sums.txt"
    h.sum("sha256", a, b, "-o", manifest)

    name = "unchanged: Changed/Unsure/Errors all 0, exit 0, .verify written by default"
    r = h.verify(manifest, "-o", "-")
    r_default = h.verify(manifest)
    produced = Path(str(manifest) + ".verify")
    default_report = produced.read_text(encoding="utf-8") if produced.exists() else ""
    if (r.rc == 0 and field(r.out, "Changed") == "0"
            and field(r.out, "Unsure") == "0"
            and field(r.out, "Errors") == "0"
            and r_default.rc == 0 and produced.exists()
            and field(default_report, "Changed") == "0"):
        h.ok("22", name)
    else:
        h.bad("22", name, f"rc={r.rc} changed={field(r.out, 'Changed')} "
                          f"default_created={produced.exists()}")

    name = ("a modified file: Changed 1, exit still 0, the output holds the "
            "new checksums")
    a.write_bytes(b"changed\n")
    out = root / "checked.txt"
    r = h.verify(manifest, "-o", out)
    report = out.read_text(encoding="utf-8") if out.exists() else ""
    new_digest = digest(a)
    r_again = h.verify(out, "-o", "-")
    if (r.rc == 0 and field(report, "Changed") == "1"
            and field(report, "Unsure") == "0"
            and f"# {a}" in report
            and f"{new_digest} {a}" in report
            and field(r_again.out, "Changed") == "0"):
        h.ok("23", name)
    else:
        h.bad("23", name, f"rc={r.rc} changed={field(report, 'Changed')} "
                          f"on_second_run={field(r_again.out, 'Changed')}")

    name = "bare paths and <NONE> lines count as unsure and still get hashed"
    unsure_manifest = root / "unsure.txt"
    unsure_manifest.write_text(
        SHA256_HEADER + f"{a}\n<NONE> {b}\n", encoding="utf-8")
    r = h.verify(unsure_manifest, "-o", "-")
    if (r.rc == 0 and field(r.out, "Changed") == "0"
            and field(r.out, "Unsure") == "2"
            and count_results(r.out) == 2
            and f"{digest(a)} {a}" in r.out
            and f"{digest(b)} {b}" in r.out):
        h.ok("24", name)
    else:
        h.bad("24", name, f"rc={r.rc} unsure={field(r.out, 'Unsure')} "
                          f"lines={count_results(r.out)}")

    name = "uppercase hexadecimal checksums are not changes"
    upper_manifest = root / "upper.txt"
    upper_manifest.write_text(
        SHA256_HEADER + f"{digest(b).upper()} {b}\n", encoding="utf-8")
    r = h.verify(upper_manifest, "-o", "-")
    if r.rc == 0 and field(r.out, "Changed") == "0":
        h.ok("25", name)
    else:
        h.bad("25", name, f"rc={r.rc} changed={field(r.out, 'Changed')}")

    name = "-o naming the input manifest: refused, and the original file is untouched"
    before = manifest.read_bytes()
    r = h.verify(manifest, "-o", manifest)
    if (r.rc == 64 and "must not be the input file" in r.err
            and manifest.read_bytes() == before):
        h.ok("26", name)
    else:
        h.bad("26", name, f"rc={r.rc} "
                          f"input_changed={manifest.read_bytes() != before}")

    name = ("input errors: missing 66, no Algorithm header 64, none manifest 64, "
            "--verify unavailable")
    r_missing = h.verify(root / "no-such-manifest")
    plain = root / "plain.list"
    plain.write_text(f"{digest(b)} {b}\n", encoding="utf-8")
    r_plain = h.verify(plain)
    none_manifest = root / "none-manifest.txt"
    h.sum("none", b, "-o", none_manifest)
    r_none = h.verify(none_manifest)
    r_hidden = h.sum("sha256", b, "--verify")
    if (r_missing.rc == 66
            and "cannot read the input file" in r_missing.err
            and r_plain.rc == 64
            and 'no "Algorithm:" header' in r_plain.err
            and r_none.rc == 64
            and "no checksums to verify" in r_none.err
            and r_hidden.rc == 64):
        h.ok("27", name)
    else:
        h.bad("27", name, f"rc: missing={r_missing.rc} plain={r_plain.rc} "
                          f"none={r_none.rc} hidden={r_hidden.rc}")

    name = "keyed algorithms: right key Changed 0, wrong key Changed 1, no key exit 64"
    keyed = root / "keyed.txt"
    h.sum("hmac_sha256", b, "-k", "secret", "-o", keyed)
    r_good = h.verify(keyed, "-k", "secret", "-o", "-")
    r_bad = h.verify(keyed, "-k", "wrong", "-o", "-")
    r_nokey = h.verify(keyed, "-o", "-")
    if (r_good.rc == 0 and field(r_good.out, "Changed") == "0"
            and r_bad.rc == 0 and field(r_bad.out, "Changed") == "1"
            and r_nokey.rc == 64 and "needs a key" in r_nokey.err):
        h.ok("28", name)
    else:
        h.bad("28", name, f"rc: good={r_good.rc} bad={r_bad.rc} "
                          f"nokey={r_nokey.rc} bad_changed="
                          f"{field(r_bad.out, 'Changed')}")

    name = "paths containing #, a space and = can be verified"
    fx = root / "v29"
    write(fx / "#hash.txt", b"a\n")
    write(fx / "a b.txt", b"b\n")
    write(fx / "a=b.txt", b"c\n")
    manifest = root / "v29.txt"
    h.sum("sha256", fx, "-o", manifest)
    r = h.verify(manifest, "-o", "-")
    if (r.rc == 0 and field(r.out, "Changed") == "0"
            and field(r.out, "Unsure") == "0"
            and count_results(r.out) == 3):
        h.ok("29", name)
    else:
        h.bad("29", name, f"rc={r.rc} changed={field(r.out, 'Changed')} "
                          f"lines={count_results(r.out)}")

    name = ("a path that cannot be hashed: Failed 1, unsure 1, exit 1, and "
            "<NONE> in the refreshed listing")
    fx30 = root / "v30"
    locked = write(fx30 / "locked.txt", b"secret\n")
    os.chmod(locked, 0o000)
    target = locked
    if os.access(locked, os.R_OK):  # root ignores the mode bits
        target = Path("/proc/self/mem")  # a regular file whose reads always fail
    manifest30 = root / "v30.txt"
    manifest30.write_text(SHA256_HEADER + f"{'0' * 64} {target}\n",
                          encoding="utf-8")
    r = h.verify(manifest30, "-o", "-")
    os.chmod(locked, 0o644)
    if (r.rc == 1 and field(r.out, "Failed") == "1"
            and field(r.out, "Unsure") == "1"
            and f"<NONE> {target}" in r.out
            and f"# Unsure files:\n# {target}\n" in r.out):
        h.ok("30", name)
    else:
        h.bad("30", name, f"rc={r.rc} failed={field(r.out, 'Failed')} "
                          f"unsure={field(r.out, 'Unsure')}")


# ---------------------------------------------------------------- 31-36

def test_cmp(h: Harness, root: Path) -> None:
    section("31-36 Compare")
    fx = root / "c"
    fx.mkdir(parents=True)
    same, changed = "a" * 64, "b" * 64
    old, moved = "c" * 64, "d" * 64
    new, added = "e" * 64, "f" * 64
    left = fx / "left.txt"
    right = fx / "right.txt"
    left.write_text(f"{same} /p/same\n{changed} /p/changed\n"
                    f"{old} /p/deleted\n{moved} /p/moved\n",
                    encoding="utf-8")
    right.write_text(f"{same} /p/same\n{new} /p/changed\n"
                     f"{added} /p/added\n{moved} /p/moved-here\n",
                     encoding="utf-8")

    name = "same path: equal checksums are matched, different ones modified"
    r = h.cmp(left, right, "-o", "-")
    if (r.rc == 0 and "[=] /p/same" in r.out
            and "[!] /p/changed" in r.out
            and "[!] /p/same" not in r.out
            and "[=] /p/changed" not in r.out):
        h.ok("31", name)
    else:
        h.bad("31", name, f"rc={r.rc} out={r.out!r}")

    name = "same checksum elsewhere: one group of L and R paths"
    r = h.cmp(left, right, "-o", "-")
    if (r.rc == 0 and "[D][1][L]/p/moved\n" in r.out
            and "[D][1][R]/p/moved-here\n" in r.out
            and "[U][L]/p/deleted\n" in r.out
            and "[U][R]/p/added\n" in r.out
            and "[D][2]" not in r.out):
        h.ok("32", name)
    else:
        h.bad("32", name, f"rc={r.rc} out={r.out!r}")

    name = "uppercase hexadecimal checksums are not changes"
    h_left = fx / "upper-left.txt"
    h_right = fx / "upper-right.txt"
    h_left.write_text(f"{same} /u/one\n", encoding="utf-8")
    h_right.write_text(f"{same.upper()} /u/one\n", encoding="utf-8")
    r = h.cmp(h_left, h_right, "-o", "-")
    if r.rc == 0 and "[=] /u/one" in r.out and "[!] /u/one" not in r.out:
        h.ok("33", name)
    else:
        h.bad("33", name, f"rc={r.rc} out={r.out!r}")

    name = "an entry without a checksum is an error and is left out"
    bad = fx / "bad.txt"
    empty = fx / "empty.txt"
    bad.write_text("<NONE> /u/none\n/u/bare\n", encoding="utf-8")
    empty.write_text("", encoding="utf-8")
    r = h.cmp(bad, empty, "-o", "-")
    if (r.rc == 1 and r.out.count("[E] Value:") == 2
            and "[E] Error messages:" in r.out
            and "[=] Matched:" in r.out
            and "/u/none" not in r.out.split("[E] Error messages:")[0]):
        h.ok("34", name)
    else:
        h.bad("34", name, f"rc={r.rc} out={r.out!r}")

    name = "a repeated line is compared once and is not reported twice"
    dup_left = fx / "dup-left.txt"
    dup_right = fx / "dup-right.txt"
    dup_left.write_text(f"{same} /u/same\n{same} /u/same\n", encoding="utf-8")
    dup_right.write_text(f"{same} /u/same\n", encoding="utf-8")
    r = h.cmp(dup_left, dup_right, "-o", "-")
    if (r.rc == 0 and r.out.count("[=] /u/same") == 1
            and "[U][L]/u/same" not in r.out):
        h.ok("35", name)
    else:
        h.bad("35", name, f"rc={r.rc} out={r.out!r}")

    name = "one path with two checksums on the same side is refused"
    clash_left = fx / "clash-left.txt"
    clash_right = fx / "clash-right.txt"
    clash_left.write_text(f"{same} /u/clash\n{changed} /u/clash\n",
                          encoding="utf-8")
    clash_right.write_text("", encoding="utf-8")
    r = h.cmp(clash_left, clash_right, "-o", "-")
    if r.rc == 64 and "more than once" in r.err:
        h.ok("36", name)
    else:
        h.bad("36", name, f"rc={r.rc} err={r.err!r}")


# ---------------------------------------------------------------- runner

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


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Black-box contract tests for tsubaki",
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
        return 1

    h = Harness(binary)
    print(f"tsubaki: {binary}")
    with tempfile.TemporaryDirectory(prefix="tsubaki-check-") as tmp:
        root = Path(tmp)
        test_basic(h, root)
        test_excludes(h, root)
        test_lists(h, root)
        test_error_paths(h, root)
        test_algorithms_and_streams(h, root)
        test_paths_and_report(h, root)
        test_subcommands_and_exit_codes(h, root)
        test_key_lengths(h, root)
        test_key_files(h, root)
        test_special_files(h, root)
        test_verify(h, root)
        test_cmp(h, root)

    print("\n--------------------------------")
    print(f"{h.passed} passed, {h.failed} failed, {h.known} known issues")
    if h.known_names:
        print("known issues (do not affect the exit status):")
        for item in h.known_names:
            print(f"  - {item}")
    if h.failed_names:
        print("failed cases:")
        for item in h.failed_names:
            print(f"  - {item}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

# This test script was assisted by DeepSeek
