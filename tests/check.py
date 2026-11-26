#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Lawrence Charland
"""Black-box contract tests for tsubaki.

Every case corresponds to one promise in docs/USAGE.md, docs/ARCHITECTURE.md or
README.md. Nothing here calls internal functions: the test feeds bytes in and
checks the bytes that come back out.

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
        self.bin = str(Path(binary).resolve())
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
        cwd: object | None = None,
    ) -> Run:
        cmd = [self.bin, *(str(a) for a in args)]
        kwargs: dict[str, object] = {
            "capture_output": True,
            "text": True,
            "timeout": timeout,
        }
        if cwd is not None:
            kwargs["cwd"] = cwd
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
    got = [tuple(line.split(None, 1)) for line in result_lines(r.out)]
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
    dir_list.write_text(f"<NONE> {d}\n", encoding="utf-8")

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

    name = ("a key file is read byte for byte: LF/CRLF/stdin/sticky all "
            "match the same -k literal, and input files are not eaten")
    r_want = h.sum("hmac_sha256", a, "-k", "secret\n")
    r_want_crlf = h.sum("hmac_sha256", a, "-k", "secret\r\n")
    r_file = h.sum("hmac_sha256", "-K", key_lf, a)
    r_crlf = h.sum("hmac_sha256", a, f"--key-file={key_crlf}")
    r_sticky = h.sum("hmac_sha256", a, f"-K{key_lf}")
    r_stdin = h.sum("hmac_sha256", a, "-K", "-", stdin_data="secret\n")
    want = result_lines(r_want.out)
    want_crlf = result_lines(r_want_crlf.out)
    if (want and want_crlf and want != want_crlf
            and result_lines(r_file.out) == want
            and result_lines(r_crlf.out) == want_crlf
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
    want = f"{hashlib.sha256(b'hello\n').hexdigest()}  {fifo}"
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

    name = ("unchanged: Changed/Unsure/Errors all 0, exit 0, and the default "
            "output is stdout")
    r = h.verify(manifest, "-o", "-")
    r_default = h.verify(manifest)
    produced = Path(str(manifest) + ".verify")
    if (r.rc == 0 and field(r.out, "Changed") == "0"
            and field(r.out, "Unsure") == "0"
            and field(r.out, "Errors") == "0"
            and r_default.rc == 0
            and field(r_default.out, "Changed") == "0"
            and not produced.exists()):
        h.ok("22", name)
    else:
        h.bad("22", name, f"rc={r.rc} changed={field(r.out, 'Changed')} "
                          f"default_changed={field(r_default.out, 'Changed')} "
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
            and f"{new_digest}  {a}" in report
            and field(r_again.out, "Changed") == "0"):
        h.ok("23", name)
    else:
        h.bad("23", name, f"rc={r.rc} changed={field(report, 'Changed')} "
                          f"on_second_run={field(r_again.out, 'Changed')}")

    name = ("<NONE> lines count as unsure and still get hashed; a line without "
            "a checksum is an error")
    unsure_manifest = root / "unsure.txt"
    unsure_manifest.write_text(
        SHA256_HEADER + f"<NONE> {b}\n{a}\n", encoding="utf-8")
    r = h.verify(unsure_manifest, "-o", "-")
    if (r.rc == 1 and field(r.out, "Changed") == "0"
            and field(r.out, "Unsure") == "1"
            and count_results(r.out) == 1
            and f"{digest(b)}  {b}" in r.out
            and 'not a "<checksum> <path>" line' in r.out):
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
            and f"<NONE>  {target}" in r.out
            and f"# Unsure files:\n# {target}\n" in r.out):
        h.ok("30", name)
    else:
        h.bad("30", name, f"rc={r.rc} failed={field(r.out, 'Failed')} "
                          f"unsure={field(r.out, 'Unsure')}")


# ---------------------------------------------------------------- 31-37

def test_cmp(h: Harness, root: Path) -> None:
    section("31-37 Compare")
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

    name = ("same path: equal checksums are matched, different ones modified, "
            "with the result on stdout and no file written")
    r = h.cmp(left, right, cwd=fx)
    if (r.rc == 0 and "[=] /p/same" in r.out
            and "[!] /p/changed" in r.out
            and "[!] /p/same" not in r.out
            and "[=] /p/changed" not in r.out
            and not (fx / "left_to_right.cmp").exists()):
        h.ok("31", name)
    else:
        h.bad("31", name, f"rc={r.rc} out={r.out!r} "
                          f"created={(fx / 'left_to_right.cmp').exists()}")

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

    name = "an output naming either input is refused and leaves it intact"
    kept = fx / "kept.txt"
    kept.write_text(f"{same} /u/kept\n", encoding="utf-8")
    r_left = h.cmp(kept, right, "-o", str(kept))
    r_right = h.cmp(left, kept, "-o", str(kept))
    if (r_left.rc == 64 and r_right.rc == 64
            and "input files" in r_left.err
            and kept.read_text(encoding="utf-8") == f"{same} /u/kept\n"):
        h.ok("37", name)
    else:
        h.bad("37", name, f"rc={r_left.rc}/{r_right.rc} "
                          f"text={kept.read_text(encoding='utf-8')!r}")


# ---------------------------------------------------------------- 38-39

def test_manifest_lines(h: Harness, root: Path) -> None:
    section("38-39, 53-60 Manifest line format")
    fx = root / "m"
    fx.mkdir(parents=True)
    target = fx / "a.txt"
    target.write_text("hello\n", encoding="utf-8")
    digest = hashlib.sha256(b"hello\n").hexdigest()
    marked = fx / "marked.txt"
    plain = fx / "plain.txt"
    marked.write_text(f"# Algorithm: sha256\n{digest} *{target}\n",
                      encoding="utf-8")
    plain.write_text(f"# Algorithm: sha256\n{digest}  {target}\n",
                     encoding="utf-8")

    name = "the binary marker of sha256sum --binary is not part of the path"
    r = h.verify(marked, "-o", "-")
    if (r.rc == 0 and field(r.out, "Changed") == "0"
            and field(r.out, "Failed") == "0"
            and f"{digest}  {target}\n" in r.out):
        h.ok("38", name)
    else:
        h.bad("38", name, f"rc={r.rc} out={r.out!r} err={r.err!r}")

    name = "a marked line and a two-space line name the same file"
    r = h.cmp(marked, plain, "-o", "-")
    if r.rc == 0 and f"[=] {target}\n" in r.out:
        h.ok("39", name)
    else:
        h.bad("39", name, f"rc={r.rc} out={r.out!r}")

    name = ("sum writes two spaces after the checksum, and the line reads back "
            "unchanged")
    manifest = fx / "sep.txt"
    h.sum("sha256", target, "-o", manifest)
    line = f"{digest}  {target}"
    r_refeed = h.sum("sha256", "-l", manifest, "-f", "-o", "-")
    if (line in manifest.read_text(encoding="utf-8")
            and r_refeed.rc == 0 and result_lines(r_refeed.out) == [line]):
        h.ok("53", name)
    else:
        h.bad("53", name, f"manifest={manifest.read_text(encoding='utf-8')!r} "
                          f"refeed={r_refeed.out!r}")

    name = ("a path that starts with * keeps its star, because the two-space "
            "separation is unambiguous")
    star = fx / "*star"
    star.write_text("x\n", encoding="utf-8")
    star_manifest = fx / "star.txt"
    r_star = h.sum("sha256", "*star", "-o", star_manifest, cwd=fx)
    r_star_refeed = h.sum("sha256", "-l", star_manifest, "-f", "-o", "-",
                          cwd=fx)
    want_star = f"{hashlib.sha256(b'x\n').hexdigest()}  *star"
    if (result_lines(star_manifest.read_text(encoding="utf-8"))
            == [want_star] and r_star_refeed.rc == 0
            and result_lines(r_star_refeed.out) == [want_star]):
        h.ok("54", name)
    else:
        h.bad("54", name, f"manifest="
                          f"{star_manifest.read_text(encoding='utf-8')!r} "
                          f"refeed={r_star_refeed.out!r}")

    def body_lines(text: str) -> list[str]:
        return [ln for ln in text.splitlines()
                if ln and not ln.startswith("#")]

    name = ("a name coreutils would escape is written escaped, and reads back "
            "unchanged")
    odd = fx / "odd"
    odd.mkdir()
    (odd / "back\\slash.txt").write_text("b\n", encoding="utf-8")
    (odd / "new\nline.txt").write_text("n\n", encoding="utf-8")
    odd_manifest = fx / "odd.txt"
    r_odd = h.sum("sha256", "back\\slash.txt", "new\nline.txt",
                  "-o", odd_manifest, cwd=odd)
    r_odd_refeed = h.sum("sha256", "-l", odd_manifest, "-f", "-o", "-", cwd=odd)
    want_odd = sorted([
        f"\\{hashlib.sha256(b'b\n').hexdigest()}  back\\\\slash.txt",
        f"\\{hashlib.sha256(b'n\n').hexdigest()}  new\\nline.txt",
    ])
    got_odd = sorted(body_lines(odd_manifest.read_text(encoding="utf-8")))
    got_refeed = sorted(body_lines(r_odd_refeed.out))
    if (r_odd.rc == 0 and got_odd == want_odd and r_odd_refeed.rc == 0
            and got_refeed == want_odd
            and field(r_odd_refeed.out, "Failed") == "0"):
        h.ok("55", name)
    else:
        h.bad("55", name, f"manifest={got_odd!r} refeed={got_refeed!r} "
                          f"rc={r_odd.rc} err={r_odd.err!r}")

    name = ("one space, two spaces and a tab separate the fields; one space "
            "before * is still the binary marker")
    forms = {
        "one": f"{digest} {target}",
        "two": f"{digest}  {target}",
        "tab": f"{digest}\t{target}",
        "tab-space": f"{digest}\t {target}",
        "marker": f"{digest} *{target}",
    }
    broken = []
    for label, text in forms.items():
        spaced = fx / f"space-{label}.txt"
        spaced.write_text(f"# Algorithm: sha256\n{text}\n", encoding="utf-8")
        r = h.verify(spaced, "-o", "-")
        if not (r.rc == 0 and field(r.out, "Failed") == "0"
                and field(r.out, "Changed") == "0"):
            broken.append(f"{label}: rc={r.rc} err={r.err!r}")
    if not broken:
        h.ok("56", name)
    else:
        h.bad("56", name, "; ".join(broken))

    name = ("leading and trailing spaces in a name are part of the path, and a "
            "carriage return is escaped")
    spaced_dir = fx / "spaced"
    spaced_dir.mkdir()
    (spaced_dir / " lead.txt").write_text("l\n", encoding="utf-8")
    (spaced_dir / "trail.txt ").write_text("t\n", encoding="utf-8")
    (spaced_dir / "car\rret").write_text("c\n", encoding="utf-8")
    spaced_manifest = fx / "spaced.txt"
    r_spaced = h.sum("sha256", " lead.txt", "trail.txt ", "car\rret",
                     "-o", spaced_manifest, cwd=spaced_dir)
    want_spaced = sorted([
        f"{hashlib.sha256(b'l\n').hexdigest()}   lead.txt",
        f"{hashlib.sha256(b't\n').hexdigest()}  trail.txt ",
        f"\\{hashlib.sha256(b'c\n').hexdigest()}  car\\rret",
    ])
    got_spaced = sorted(body_lines(
        spaced_manifest.read_text(encoding="utf-8")))
    r_spaced_refeed = h.sum("sha256", "-l", spaced_manifest, "-f", "-o", "-",
                            cwd=spaced_dir)
    got_spaced_refeed = sorted(body_lines(r_spaced_refeed.out))
    r_spaced_verify = h.verify(spaced_manifest, "-o", "-", cwd=spaced_dir)
    if (r_spaced.rc == 0 and got_spaced == want_spaced
            and r_spaced_refeed.rc == 0 and got_spaced_refeed == want_spaced
            and r_spaced_verify.rc == 0
            and field(r_spaced_verify.out, "Failed") == "0"
            and field(r_spaced_verify.out, "Changed") == "0"):
        h.ok("58", name)
    else:
        h.bad("58", name, f"manifest={got_spaced!r} "
                          f"refeed={got_spaced_refeed!r} "
                          f"verify={r_spaced_verify.out!r}")

    name = ("an extra space stays in the name, and a lone * after one space is "
            "a name")
    lead_three = fx / "lead-three.txt"
    lead_three.write_text(
        f"# Algorithm: sha256\n{hashlib.sha256(b'l\n').hexdigest()}   "
        f"lead.txt\n", encoding="utf-8")
    (fx / "*").write_text("x\n", encoding="utf-8")
    star_only = fx / "star-only.txt"
    star_only.write_text(
        f"# Algorithm: sha256\n{hashlib.sha256(b'x\n').hexdigest()} *\n",
        encoding="utf-8")
    r_lead = h.verify(lead_three, "-o", "-", cwd=spaced_dir)
    r_star = h.verify(star_only, "-o", "-", cwd=fx)
    if (r_lead.rc == 0 and field(r_lead.out, "Hashed") == "1"
            and field(r_lead.out, "Failed") == "0"
            and r_star.rc == 0 and field(r_star.out, "Hashed") == "1"
            and field(r_star.out, "Failed") == "0"):
        h.ok("59", name)
    else:
        h.bad("59", name, f"lead={r_lead.out!r} star={r_star.out!r}")

    name = "an escape sequence coreutils does not define makes the line invalid"
    bad_escape = fx / "bad-escape.txt"
    bad_escape.write_text(f"# Algorithm: sha256\n\\{digest}  a\\q.txt\n",
                          encoding="utf-8")
    r_bad = h.verify(bad_escape, "-o", "-")
    if r_bad.rc == 1 and field(r_bad.out, "Errors") == "1":
        h.ok("60", name)
    else:
        h.bad("60", name, f"rc={r_bad.rc} out={r_bad.out!r}")

    name = ("a changed name with a newline stays on one comment line in the "
            "verify report")
    (odd / "new\nline.txt").write_text("n2\n", encoding="utf-8")
    r_report = h.verify(odd_manifest, "-o", "-", cwd=odd)
    report = r_report.out
    name_ok = "# Changed files:\n# new\\nline.txt\n" in report
    stray = "\nline.txt\n" in report
    if r_report.rc == 0 and name_ok and not stray:
        h.ok("57", name)
    else:
        h.bad("57", name, f"rc={r_report.rc} stray={stray!r} "
                          f"report={report!r}")

# ---------------------------------------------------------------- 40

def test_stdout_output(h: Harness, root: Path) -> None:
    section("40 Output to stdout")
    fx = root / "o"
    fx.mkdir(parents=True)
    a = fx / "a.txt"
    a.write_text("hello\n", encoding="utf-8")
    digest = hashlib.sha256(b"hello\n").hexdigest()

    name = "-o - writes to stdout instead of a file named -"
    r = h.sum("sha256", a, "-o", "-", cwd=fx)
    if (r.rc == 0 and f"{digest}  {a}\n" in r.out
            and not (fx / "-").exists()):
        h.ok("40", name)
    else:
        h.bad("40", name, f"rc={r.rc} out={r.out!r} "
                          f"dash={(fx / '-').exists()}")


# ---------------------------------------------------------------- 41-44

def test_use_algo(h: Harness, root: Path) -> None:
    section("41-44 Verify -u")
    fx = root / "u"
    a = write(fx / "a.txt", b"hello\n")
    b = write(fx / "b.txt", b"world\n")

    name = "-u overrides a stale Algorithm header, and says so on stderr"
    stale = root / "stale.txt"
    stale.write_text("# Algorithm:         sha512\n"
                     f"{digest(a)} {a}\n", encoding="utf-8")
    r_header = h.verify(stale, "-o", "-")
    r_forced = h.verify(stale, "-u", "sha256", "-o", "-")
    if (r_header.rc == 0 and field(r_header.out, "Changed") == "1"
            and r_forced.rc == 0
            and field(r_forced.out, "Changed") == "0"
            and field(r_forced.out, "Unsure") == "0"
            and field(r_forced.out, "Algorithm") == "sha256"
            and "Forced to use algorithm sha256" in r_forced.err):
        h.ok("41", name)
    else:
        h.bad("41", name, f"rc: header={r_header.rc} forced={r_forced.rc} "
                          f"changed: header={field(r_header.out, 'Changed')} "
                          f"forced={field(r_forced.out, 'Changed')}")

    name = ("a listing without an Algorithm header needs -u, which then "
            "verifies it")
    headerless = root / "headerless.txt"
    headerless.write_text(f"{digest(a)} {a}\n{digest(b)} {b}\n",
                          encoding="utf-8")
    r_plain = h.verify(headerless, "-o", "-")
    r_forced = h.verify(headerless, "-u", "sha256", "-o", "-")
    if (r_plain.rc == 64 and 'no "Algorithm:" header' in r_plain.err
            and r_forced.rc == 0
            and field(r_forced.out, "Changed") == "0"
            and field(r_forced.out, "Unsure") == "0"
            and count_results(r_forced.out) == 2):
        h.ok("42", name)
    else:
        h.bad("42", name, f"rc: plain={r_plain.rc} forced={r_forced.rc} "
                          f"changed={field(r_forced.out, 'Changed')}")

    name = "an unknown -u name is a command-line error"
    manifest = root / "u.txt"
    h.sum("sha256", a, b, "-o", manifest)
    r = h.verify(manifest, "-u", "bogus", "-o", "-")
    if r.rc == 64 and 'invalid algorithm: "bogus"' in r.err:
        h.ok("43", name)
    else:
        h.bad("43", name, f"rc={r.rc} err={r.err!r}")

    name = "-u none is accepted, hashes nothing, and leaves every path unsure"
    r = h.verify(manifest, "-u", "none", "-o", "-")
    none_lines = [line for line in r.out.splitlines()
                  if line.startswith("<NONE> ")]
    if (r.rc == 0 and len(none_lines) == 2
            and field(r.out, "Algorithm") == "none"
            and field(r.out, "Not hashed") == "2"
            and field(r.out, "Hashed") == "0"
            and field(r.out, "Changed") == "0"
            and field(r.out, "Unsure") == "2"
            and field(r.out, "Errors") == "0"):
        h.ok("44", name)
    else:
        h.bad("44", name, f"rc={r.rc} none_lines={len(none_lines)} "
                          f"not_hashed={field(r.out, 'Not hashed')} "
                          f"unsure={field(r.out, 'Unsure')}")


# ---------------------------------------------------------------- 45-48

def test_verify_streams(h: Harness, root: Path) -> None:
    section("45-48 Verify from a stream")
    fx = root / "v45"
    a = write(fx / "a.txt", b"hello\n")
    manifest_text = SHA256_HEADER + f"{digest(a)} {a}\n"
    want = f"{digest(a)}  {a}"

    name = 'a "-" manifest needs -u, because its header cannot be prefetched'
    r = h.verify("-", stdin_data=manifest_text)
    if r.rc == 64 and "cannot prefetch" in r.err and "-u" in r.err:
        h.ok("45", name)
    else:
        h.bad("45", name, f"rc={r.rc} err={r.err!r}")

    name = 'a "-" manifest with -u is verified to stdout, writing no file'
    cwd = fx / "cwd"
    cwd.mkdir()
    r = h.verify("-", "-u", "sha256", stdin_data=manifest_text, cwd=cwd)
    if (r.rc == 0 and field(r.out, "Changed") == "0"
            and field(r.out, "Unsure") == "0"
            and want in r.out and not any(cwd.iterdir())):
        h.ok("46", name)
    else:
        h.bad("46", name, f"rc={r.rc} files={[p.name for p in cwd.iterdir()]} "
                          f"changed={field(r.out, 'Changed')}")

    name = "a directory is refused as a manifest, not read as one"
    r = h.verify(fx)
    if r.rc == 66 and "cannot read the input file" in r.err:
        h.ok("47", name)
    else:
        h.bad("47", name, f"rc={r.rc} err={r.err!r}")

    fx = root / "v48"
    fifo = fx / "pipe"
    fx.mkdir(parents=True)
    os.mkfifo(fifo)

    def writer() -> None:
        try:
            with open(fifo, "wb") as f:
                f.write(manifest_text.encode())
        except OSError:
            pass

    name = "a named pipe is read as a manifest instead of being refused"
    th = threading.Thread(target=writer, daemon=True)
    th.start()
    r = h.verify(fifo, "-u", "sha256", "-o", "-", timeout=10)
    th.join(timeout=5)
    if (r.rc == 0 and field(r.out, "Changed") == "0"
            and result_lines(r.out) == [want]):
        h.ok("48", name)
    else:
        h.bad("48", name, f"rc={r.rc} (124 means it hung) "
                          f"result={result_lines(r.out)}")


# ---------------------------------------------------------------- 49-52

def test_text_key(h: Harness, root: Path) -> None:
    section("49-52 Read a key file as text")
    fx = root / "t49"
    a = write(fx / "a.txt", b"hello\n")
    key_crlf = root / "key-text-crlf"
    key_crlf.write_text("a\r\nb\r\n", encoding="utf-8")
    key_lf = root / "key-text-lf"
    key_lf.write_text("a\nb\n", encoding="utf-8")
    want = result_lines(h.sum("hmac_sha256", a, "-k", "a\nb").out)

    name = ("--text reads a key file the way $(cat key) would: CRLF folds to "
            "LF and the trailing line ending goes")
    one_line = write(root / "key-text-one", b"secret\r\n")
    lone_cr = write(root / "key-text-lone", b"a\rb")
    tail_mix = write(root / "key-text-tail", b"a\r\n\n\r")
    r_plain = h.sum("hmac_sha256", a, "-K", key_crlf)
    r_text = h.sum("hmac_sha256", a, "-K", key_crlf, "--text")
    r_lf = h.sum("hmac_sha256", a, "-K", key_lf, "--text")
    r_one = h.sum("hmac_sha256", a, "-K", one_line, "--text")
    r_one_ref = h.sum("hmac_sha256", a, "-k", "secret")
    r_lone = h.sum("hmac_sha256", a, "-K", lone_cr, "--text")
    r_lone_ref = h.sum("hmac_sha256", a, "-k", "a\rb")
    r_tail = h.sum("hmac_sha256", a, "-K", tail_mix, "--text")
    r_tail_ref = h.sum("hmac_sha256", a, "-k", "a")
    if (want and result_lines(r_plain.out) != want
            and result_lines(r_text.out) == want
            and result_lines(r_lf.out) == want
            and result_lines(r_one.out) == result_lines(r_one_ref.out)
            and result_lines(r_lone.out) == result_lines(r_lone_ref.out)
            and result_lines(r_tail.out) == result_lines(r_tail_ref.out)):
        h.ok("49", name)
    else:
        h.bad("49", name, f"plain={result_lines(r_plain.out)} "
                          f"text={result_lines(r_text.out)} "
                          f"lf={result_lines(r_lf.out)} "
                          f"one={result_lines(r_one.out)} "
                          f"lone={result_lines(r_lone.out)} "
                          f"tail={result_lines(r_tail.out)}")

    name = ("the flag also reaches -K -, leaves a -k literal alone, "
            "and only warns on a plain digest")
    r_stdin = h.sum("hmac_sha256", a, "-K", "-", "--text",
                    stdin_data="a\r\nb\r\n")
    r_literal = h.sum("hmac_sha256", a, "-k", "a\r\nb", "--text")
    r_plain_digest = h.sum("sha256", a, "--text")
    if (result_lines(r_stdin.out) == want and r_stdin.rc == 0
            and r_literal.rc == 0
            and result_lines(r_literal.out) != want
            and r_plain_digest.rc == 0
            and result_lines(r_plain_digest.out) == [f"{digest(a)}  {a}"]
            and "doesn't need key" in r_plain_digest.err):
        h.ok("50", name)
    else:
        h.bad("50", name, f"stdin={result_lines(r_stdin.out)} "
                          f"literal={result_lines(r_literal.out)} "
                          f"plain={result_lines(r_plain_digest.out)} "
                          f"plain_err={r_plain_digest.err.strip()!r}")

    name = "verify --text re-hashes with the folded key; without it the file reads as changed"
    manifest = root / "text.txt"
    h.sum("hmac_sha256", a, "-K", key_crlf, "--text", "-o", manifest)
    r_flag = h.verify(manifest, "-K", key_crlf, "--text", "-o", "-")
    r_no_flag = h.verify(manifest, "-K", key_crlf, "-o", "-")
    if (r_flag.rc == 0 and field(r_flag.out, "Changed") == "0"
            and field(r_flag.out, "Errors") == "0"
            and r_no_flag.rc == 0 and field(r_no_flag.out, "Changed") == "1"):
        h.ok("51", name)
    else:
        h.bad("51", name, f"rc: flag={r_flag.rc} plain={r_no_flag.rc} "
                          f"changed: flag={field(r_flag.out, 'Changed')} "
                          f"plain={field(r_no_flag.out, 'Changed')}")

    name = ("a repeated --text exits 64, -T is the same flag, it never "
            "supplies a key itself, and --help lists it")
    r_twice = h.sum("hmac_sha256", a, "-k", "secret",
                    "--text", "--text")
    r_short = h.sum("hmac_sha256", a, "-K", key_crlf, "-T")
    r_no_key = h.sum("hmac_sha256", a, "--text")
    r_help = h.run("--help")
    if (r_twice.rc == 64 and "Too many repetitions" in r_twice.err
            and result_lines(r_short.out) == want
            and r_no_key.rc == 64 and "needs a key" in r_no_key.err
            and not result_lines(r_no_key.out)
            and r_help.rc == 0 and "--text" in r_help.out
            and "-T" in r_help.out
            and "--strip-crlf" not in r_help.out):
        h.ok("52", name)
    else:
        h.bad("52", name, f"rc: twice={r_twice.rc} nokey={r_no_key.rc} "
                          f"short={result_lines(r_short.out)} "
                          f"help={r_help.rc} listed={'--text' in r_help.out}")


# ---------------------------------------------------------------- 61-62

def test_completion(h: Harness, root: Path) -> None:
    name = "-C needs no subcommand, wins over argument errors, and every --option in --help is in its list"
    r = h.run("-C")
    r_junk = h.run("-C", "junk")
    r_help = h.run("--help")
    words = r.out.split()
    advertised = set(re.findall(r"--[a-z][a-z-]*", r_help.out))
    missing = sorted(advertised - set(words))
    if (r.rc == 0 and not r.err and r_junk.rc == 0
            and sorted(r_junk.out.split()) == sorted(words)
            and "--help" in words and "-a" in words and "--completions" in words
            and "sha256" in words and "none" in words
            and "--verify" not in words
            and len(words) == len(set(words))
            and not missing):
        h.ok("61", name)
    else:
        h.bad("61", name, f"rc={r.rc}/{r_junk.rc} words={len(words)} "
                          f"err={r.err!r} missing={missing}")

    name = "--help announces -C and --completions"
    if (r_help.rc == 0 and "-C, --completions" in r_help.out):
        h.ok("62", name)
    else:
        h.bad("62", name, f"rc={r_help.rc} "
                          f"listed={'-C, --completions' in r_help.out}")

    name = "-t/--thread, -e/--exclude and -l/--list take no plural alias"
    fx = root / "t63"
    write(fx / "a.txt", b"x\n")
    empty = root / "empty.list"
    empty.write_text("", encoding="utf-8")
    r_singular = h.sum("sha256", fx, "--thread=1", "--list", empty)
    r_plural = [h.sum("sha256", fx, o) for o in
                ("--threads=1", "--excludes=none", "--lists=" + str(empty))]
    gone = all(w not in words for w in
               ("--threads", "--excludes", "--lists"))
    kept = all(w in words for w in ("--thread", "--exclude", "--list"))
    if (gone and kept and r_singular.rc == 0
            and all(r.rc != 0 for r in r_plural)):
        h.ok("63", name)
    else:
        h.bad("63", name, f"gone={gone} kept={kept} "
                          f"singular={r_singular.rc} "
                          f"plural={[r.rc for r in r_plural]}")

    name = "the four subcommands are the contract: sum, verify, cmp and algo work and are advertised"
    stable = ("sum", "verify", "cmp", "algo")
    r_algo = h.run("algo", "sha256")
    if (all(n in words for n in stable)
            and all(n in r_help.out for n in stable)
            and r_algo.rc == 0 and "Source:" in r_algo.out):
        h.ok("64", name)
    else:
        h.bad("64", name,
              f"missing={[n for n in stable if n not in words]} "
              f"rc={r_algo.rc} out={r_algo.out[:80]!r}")


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
        test_manifest_lines(h, root)
        test_stdout_output(h, root)
        test_use_algo(h, root)
        test_verify_streams(h, root)
        test_text_key(h, root)
        test_completion(h, root)

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

# This test script was assisted by DeepSeek.
