#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Lawrence Charland
"""Black-box provider ABI tests for tsubaki.

Every case corresponds to one promise in docs/PROVIDER-ABI.md. Unlike
`general.py`, this suite needs a C++ compiler: each fixture is a real `.so` built
from `include/dynamic_provider_header.hpp`, because the promise under test is
what the loader and the two sides agree on.

    ./tests/abi.py                            # find the binary under build/
    TSUBAKI=/usr/bin/tsubaki ./tests/abi.py   # name the binary
    CXX=clang++ ./tests/abi.py                # name the compiler

Exit status: 0 = everything passed, 1 = something failed, 2 = no compiler, so
nothing ran. A fixture that fails to compile is a failure, not a skip: it means
this file is wrong, not the machine.

What cannot live here: memory ownership, `noexcept` and cross-allocator free
are contract rules with no observable byte pattern. They are checked by reading
the code, not by running it.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from harness import Harness, digest, field, resolve_binary, section

HEAD = """#include "dynamic_provider_header.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tsubaki
{
extern "C"
{
"""

TAIL = """}
}
"""


class BuildError(Exception):
    pass


def block_name(name: str) -> str:
    return (f'    const char *tsubaki_provider_name() noexcept'
            f' {{ return "{name}"; }}\n\n')


def block_is_keyed(value: bool) -> str:
    word = "true" if value else "false"
    return (f'    bool tsubaki_is_keyed() noexcept {{ return {word}; }}\n\n')


def block_check_key(length: int) -> str:
    return ('    bool tsubaki_check_key(const char *, std::size_t n) noexcept\n'
            f'    {{\n        return n == {length};\n    }}\n\n')


def block_algo_info(text: str) -> str:
    return ('    const char *tsubaki_algo_info() noexcept\n'
            f'    {{\n        return "{text}";\n    }}\n\n')


def block_need_args(value: bool) -> str:
    word = "true" if value else "false"
    return (f'    bool tsubaki_need_more_args() noexcept {{ return {word}; }}\n\n')


def block_arg_def(script: str | None) -> str:
    if script is None:
        return ('    const char *tsubaki_arg_definition() noexcept'
                ' { return nullptr; }\n\n')
    return ('    const char *tsubaki_arg_definition() noexcept\n'
            f'    {{\n        return "{script}";\n    }}\n\n')


def block_take_args(message: str = "") -> str:
    return ('    const char *tsubaki_take_args(tsubaki_pass_content *,'
            ' std::size_t) noexcept\n'
            f'    {{\n        return "{message}";\n    }}\n\n')


def block_digest(digest: str) -> str:
    """A digest that only proves the file could be opened."""
    return f'''    tsubaki_checksum_result tsubaki_checksum_file(const char *path,
                                                  const char *, std::size_t) noexcept
    {{
        tsubaki_checksum_result r{{}};
        std::FILE *f = std::fopen(path, "rb");
        if (!f) {{ r.errcode = errno; return r; }}
        std::fclose(f);
        std::snprintf(r.result, sizeof r.result, "{digest}");
        return r;
    }}
'''


def block_digest_key_length(prefix: str) -> str:
    """The digest carries the key length, so a byte string is visible."""
    return f'''    tsubaki_checksum_result tsubaki_checksum_file(const char *path,
                                                  const char *, std::size_t len) noexcept
    {{
        tsubaki_checksum_result r{{}};
        std::FILE *f = std::fopen(path, "rb");
        if (!f) {{ r.errcode = errno; return r; }}
        std::fclose(f);
        std::snprintf(r.result, sizeof r.result, "{prefix}%02x", (unsigned)len);
        return r;
    }}
'''


ARGS_SCRIPT = ("{ entry_name = lev; entry_type = option; short_name = L; "
               "max_value_count = 1; min_value_count = 1; }")


def module(name: str, *blocks: str) -> str:
    return HEAD + block_name(name) + "".join(blocks) + TAIL


class Builder:
    def __init__(self, compiler: str, outdir: Path, include: Path) -> None:
        self.compiler = compiler
        self.outdir = outdir
        self.include = include
        self.outdir.mkdir(parents=True, exist_ok=True)

    def build(self, filename: str, source: str) -> Path:
        src = self.outdir / f"{filename}.cpp"
        src.write_text(source)
        so = self.outdir / f"{filename}.so"
        cp = subprocess.run(
            [self.compiler, "-std=c++23", "-fPIC", "-shared", "-O0",
             "-I", str(self.include), str(src), "-o", str(so)],
            capture_output=True, text=True)
        if cp.returncode != 0:
            raise BuildError(f"{filename}: {cp.stderr.strip()}")
        return so


def staging(root: Path, label: str, modules: list[Path]) -> Path:
    """One directory holding the modules a case should see."""
    d = root / "load" / label
    d.mkdir(parents=True, exist_ok=True)
    for so in modules:
        shutil.copy2(so, d / so.name)
    return d


def env_for(*dirs: Path) -> dict[str, str]:
    return {"TSUBAKI_DYNAMIC_PROVIDER_PATHS":
            os.pathsep.join(str(d) for d in dirs)}


def provider_row(text: str, name: str) -> list[str] | None:
    """The `-a` row for one provider: [keyed, extra args, source]."""
    for line in text.splitlines():
        cells = line.split()
        if len(cells) >= 4 and cells[0] == name:
            return [cells[1], cells[2], " ".join(cells[3:])]
    return None


def loaded_count(text: str) -> int | None:
    for line in text.splitlines():
        if line.startswith("(") and "dynamic algorithms loaded" in line:
            return int(line[1:].split()[0])
    return None


# ---------------------------------------------------------------- A01-A03

def test_minimal_module(h: Harness, root: Path, plain: Path) -> None:
    section("A01-A03 The smallest module that works")
    d = staging(root, "minimal", [plain])

    r = h.run("-a", env=env_for(d))
    row = provider_row(r.out, "plain")
    if r.rc == 0 and row == ["NO", "NO", str(d / "plain.so")]:
        h.ok("A01", "only a name and a checksum function are required to load")
    else:
        h.bad("A01", "only a name and a checksum function are required to load",
              f"rc={r.rc} row={row}")

    r = h.run("algo", "plain", env=env_for(d))
    want = f"Source:     {d / 'plain.so'}\nNo information provided.\n"
    if r.rc == 0 and r.out == want:
        h.ok("A01b", "a module without algo info still answers `algo`")
    else:
        h.bad("A01b", "a module without algo info still answers `algo`",
              f"rc={r.rc} out={r.out!r}")

    write_file = root / "a01.txt"
    write_file.write_text("hello\n")
    r = h.sum("plain", write_file, env=env_for(d))
    if r.rc == 0 and "0000abc1" in r.out:
        h.ok("A01c", "the module's digest reaches the report")
    else:
        h.bad("A01c", "the module's digest reaches the report",
              f"rc={r.rc} out={r.out!r} err={r.err!r}")


def test_key_switch(h: Harness, root: Path, unkeyed_false: Path,
                    laxness: Path) -> None:
    section("A02-A03 A missing key switch means unkeyed")
    d = staging(root, "unkeyed_false", [unkeyed_false])
    r = h.run("-a", env=env_for(d))
    row = provider_row(r.out, "unkeyed_false")
    if r.rc == 0 and row is not None and row[0] == "NO":
        h.ok("A02", "is_keyed() returning false needs no checker to load")
    else:
        h.bad("A02", "is_keyed() returning false needs no checker to load",
              f"rc={r.rc} row={row} err={r.err!r}")

    f = root / "a03.txt"
    f.write_text("hello\n")
    d = staging(root, "laxness", [laxness])
    r = h.sum("laxness", f, "-k", "abcd", env=env_for(d))
    # The checker would reject every key; acceptance proves it was not called,
    # and the 00 shows the key was not handed over either.
    if r.rc == 0 and "0000abc200" in r.out and "doesn't need key" in r.err:
        h.ok("A03", "an unkeyed module's checker is never consulted")
    else:
        h.bad("A03", "an unkeyed module's checker is never consulted",
              f"rc={r.rc} out={r.out!r} err={r.err!r}")


# ---------------------------------------------------------------- A04-A06

def test_keyed_module(h: Harness, root: Path, keyed: Path,
                      keyed_no_check: Path) -> None:
    section("A04-A05 A keyed module and its checker")
    d = staging(root, "keyed_no_check", [keyed_no_check])
    r = h.run("-a", env=env_for(d))
    refused = [line for line in r.err.splitlines()
               if line.startswith("Error: dlsym failed for")]
    if (r.rc == 0 and refused and "Dynamic providers" not in r.out):
        h.ok("A04", "is_keyed() true without a checker fails the load")
    else:
        h.bad("A04", "is_keyed() true without a checker fails the load",
              f"rc={r.rc} err={r.err!r}")

    r = h.sum("keyed_no_check", root / "a04.txt", env=env_for(d))
    if r.rc == 64 and 'unknown algorithm: "keyed_no_check"' in r.err:
        h.ok("A04b", "the refused module is not reachable by name")
    else:
        h.bad("A04b", "the refused module is not reachable by name",
              f"rc={r.rc} err={r.err!r}")

    f = root / "a05.txt"
    f.write_text("hello\n")
    d = staging(root, "keyed", [keyed])
    r = h.run("-a", env=env_for(d))
    row = provider_row(r.out, "keyed")
    if row is not None and row[0] == "YES":
        h.ok("A05", "a keyed module is listed as KEYED YES")
    else:
        h.bad("A05", "a keyed module is listed as KEYED YES", f"row={row}")

    r = h.sum("keyed", f, env=env_for(d))
    if r.rc == 64 and '"keyed" needs a key.' in r.err:
        h.ok("A05b", "a keyed module asks for a key like a built-in one")
    else:
        h.bad("A05b", "a keyed module asks for a key like a built-in one",
              f"rc={r.rc} err={r.err!r}")

    r = h.sum("keyed", f, "-k", "abcde", env=env_for(d))
    if r.rc == 64 and "not valid for algorithm" in r.err:
        h.ok("A05c", "the module's checker decides whether a key is acceptable")
    else:
        h.bad("A05c", "the module's checker decides whether a key is acceptable",
              f"rc={r.rc} err={r.err!r}")

    r = h.sum("keyed", f, "-k", "abcd", env=env_for(d))
    if r.rc == 0 and "0000abc304" in r.out:
        h.ok("A05d", "an accepted key reaches the checksum function")
    else:
        h.bad("A05d", "an accepted key reaches the checksum function",
              f"rc={r.rc} out={r.out!r} err={r.err!r}")


def test_builtin_shadow(h: Harness, root: Path, shadow: Path) -> None:
    section("A06 A module cannot shadow a built-in name")
    d = staging(root, "shadow", [shadow])
    r = h.run("-a", env=env_for(d))
    if (r.rc == 0 and f'Error: refused to load "{d / "shadow.so"}"' in r.err
            and "Dynamic providers" not in r.out):
        h.ok("A06", "a module claiming sha256 is refused at load")
    else:
        h.bad("A06", "a module claiming sha256 is refused at load",
              f"rc={r.rc} err={r.err!r}")

    r = h.run("algo", "sha256", env=env_for(d))
    if r.rc == 0 and r.out.startswith("Source:     built-in\n"):
        h.ok("A06b", "sha256 still resolves to the built-in algorithm")
    else:
        h.bad("A06b", "sha256 still resolves to the built-in algorithm",
              f"rc={r.rc} out={r.out[:80]!r}")


# ---------------------------------------------------------------- A07-A09

def test_scan(h: Harness, root: Path, plain: Path, dupe_a: Path,
              dupe_b: Path) -> None:
    section("A07-A10 How the scan walks directories")
    f = root / "a07.txt"
    f.write_text("hello\n")
    first = staging(root, "dupe_first", [dupe_a])
    second = staging(root, "dupe_second", [dupe_b])

    r = h.sum("dupe", f, env=env_for(first, second))
    if r.rc == 0 and "0000d00a" in r.out:
        h.ok("A07", "the first directory in the variable wins a duplicate")
    else:
        h.bad("A07", "the first directory in the variable wins a duplicate",
              f"rc={r.rc} out={r.out!r}")

    r = h.sum("dupe", f, env=env_for(second, first))
    if r.rc == 0 and "0000d00b" in r.out:
        h.ok("A07b", "reversing the order reverses the winner")
    else:
        h.bad("A07b", "reversing the order reverses the winner",
              f"rc={r.rc} out={r.out!r}")

    d = root / "load" / "extensions"
    d.mkdir(parents=True, exist_ok=True)
    shutil.copy2(plain, d / "plain.so")
    shutil.copy2(plain, d / "libx.so.1")
    shutil.copy2(plain, d / "x.SO")
    r = h.run("-a", env=env_for(d))
    if r.rc == 0 and loaded_count(r.out) == 1 and "x.SO" not in r.out:
        h.ok("A08", "only an extension of exactly .so is loaded")
    else:
        h.bad("A08", "only an extension of exactly .so is loaded",
              f"rc={r.rc} count={loaded_count(r.out)} out={r.out[-200:]!r}")

    d = root / "load" / "garbage"
    d.mkdir(parents=True, exist_ok=True)
    (d / "garbage.so").write_bytes(b"not an elf file\n")
    shutil.copy2(plain, d / "plain.so")
    r = h.run("-a", env=env_for(d))
    if (r.rc == 0 and f'Error: dlopen failed for "{d / "garbage.so"}"' in r.err
            and loaded_count(r.out) == 1):
        h.ok("A09", "a .so that is not loadable is reported and skipped")
    else:
        h.bad("A09", "a .so that is not loadable is reported and skipped",
              f"rc={r.rc} err={r.err!r} count={loaded_count(r.out)}")

    r = h.run("-a", env=env_for(root / "no-such-dir", root / "load" / "minimal"))
    hits = [line for line in r.err.splitlines()
            if "cannot access directory" in line]
    if r.rc == 0 and hits and loaded_count(r.out) == 1:
        h.ok("A10", "a path that is not a directory is reported, not fatal")
    else:
        h.bad("A10", "a path that is not a directory is reported, not fatal",
              f"rc={r.rc} err={r.err!r} count={loaded_count(r.out)}")


# ---------------------------------------------------------------- A11-A13

def test_argument_trio(h: Harness, root: Path, args_false: Path,
                       args_no_script: Path, args_no_take: Path) -> None:
    section("A11-A13 The three argument symbols stand or fall together")
    d = staging(root, "args_false", [args_false])
    r = h.run("-a", env=env_for(d))
    row = provider_row(r.out, "args_false")
    if r.rc == 0 and row is not None and row[1] == "NO":
        h.ok("A11", "need_more_args() false needs neither other symbol")
    else:
        h.bad("A11", "need_more_args() false needs neither other symbol",
              f"rc={r.rc} row={row} err={r.err!r}")

    d = staging(root, "args_no_script", [args_no_script])
    r = h.run("-a", env=env_for(d))
    if (r.rc == 0 and "Error: dlsym failed for" in r.err
            and "Dynamic providers" not in r.out):
        h.ok("A12", "need_more_args() true without a script fails the load")
    else:
        h.bad("A12", "need_more_args() true without a script fails the load",
              f"rc={r.rc} err={r.err!r}")

    d = staging(root, "args_no_take", [args_no_take])
    r = h.run("-a", env=env_for(d))
    if (r.rc == 0 and "Error: dlsym failed for" in r.err
            and "Dynamic providers" not in r.out):
        h.ok("A13", "need_more_args() true without take_args fails the load")
    else:
        h.bad("A13", "need_more_args() true without take_args fails the load",
              f"rc={r.rc} err={r.err!r}")


# ---------------------------------------------------------------- A14-A17

def test_algo_output(h: Harness, root: Path, info: Path, args_bad: Path,
                     args: Path) -> None:
    section("A14-A17 What `algo` and `-a` show about a module")
    d = staging(root, "info", [info])
    r = h.run("algo", "info", env=env_for(d))
    want = (f"Source:     {d / 'info.so'}\n"
            "Digest:     8 hex\nKey:        none\nOrigin:     https://example.org/x\n")
    if r.rc == 0 and r.out == want:
        h.ok("A14", "algo info is printed verbatim under tsubaki's Source line")
    else:
        h.bad("A14", "algo info is printed verbatim under tsubaki's Source line",
              f"rc={r.rc} out={r.out!r}")

    d = staging(root, "args_bad", [args_bad])
    r = h.run("algo", "args_bad", env=env_for(d))
    if (r.rc == 0 and "ArgParser script:\nthis is not a script {\n" in r.out):
        h.ok("A15", "algo prints a broken script without parsing it")
    else:
        h.bad("A15", "algo prints a broken script without parsing it",
              f"rc={r.rc} out={r.out!r} err={r.err!r}")

    d = staging(root, "args", [args])
    r = h.run("-a", env=env_for(d))
    row = provider_row(r.out, "args")
    if row is not None and row[1] == "YES":
        h.ok("A16", "a module that takes arguments is listed as EXTRA ARGS YES")
    else:
        h.bad("A16", "a module that takes arguments is listed as EXTRA ARGS YES",
              f"row={row}")

    r = h.run("-C", env=env_for(d))
    words = r.out.split()
    if r.rc == 0 and "args" in words and "-L" in words:
        h.ok("A17", "completion offers the provider name and its switch")
    else:
        h.bad("A17", "completion offers the provider name and its switch",
              f"rc={r.rc} words={sorted(words)[:20]}")


# ---------------------------------------------------------------- A18-A22

def test_argument_handling(h: Harness, root: Path, args: Path,
                           args_reject: Path, args_bad: Path,
                           args_null: Path) -> None:
    section("A18-A22 Arguments the script declares")
    f = root / "a18.txt"
    f.write_text("hello\n")
    d = staging(root, "args", [args])

    r = h.sum("args", f, "-L", "128", env=env_for(d))
    if r.rc == 0 and "0000abc60080" in r.out:
        h.ok("A18", "a parsed option value reaches take_args")
    else:
        h.bad("A18", "a parsed option value reaches take_args",
              f"rc={r.rc} out={r.out!r} err={r.err!r}")

    r = h.sum("args", f, "-L", "5", env=env_for(d))
    if r.rc == 0 and "0000abc60005" in r.out:
        h.ok("A18b", "a different value changes what the module computes")
    else:
        h.bad("A18b", "a different value changes what the module computes",
              f"rc={r.rc} out={r.out!r}")

    r = h.sum("args", f, env=env_for(d))
    if (r.rc == 64 and "Not enough values for the option: lev@dynamic" in r.err
            and not r.out):
        h.ok("A19", "a missing required value is tsubaki's own error")
    else:
        h.bad("A19", "a missing required value is tsubaki's own error",
              f"rc={r.rc} out={r.out!r} err={r.err!r}")

    r = h.sum("args", f, "-L", "1", "-L", "2", env=env_for(d))
    if (r.rc == 64 and "Too many values for the option: lev@dynamic" in r.err
            and not r.out):
        h.ok("A20", "the declared maximum is enforced before take_args runs")
    else:
        h.bad("A20", "the declared maximum is enforced before take_args runs",
              f"rc={r.rc} out={r.out!r} err={r.err!r}")

    d = staging(root, "args_reject", [args_reject])
    r = h.sum("args_reject", f, "-L", "5", env=env_for(d))
    if (r.rc == 64 and r.err.startswith("bad combination")
            and not r.out):
        h.ok("A21", "a message from take_args becomes the error, status 64")
    else:
        h.bad("A21", "a message from take_args becomes the error, status 64",
              f"rc={r.rc} out={r.out!r} err={r.err!r}")

    d = staging(root, "args_bad_script", [args_bad])
    r = h.sum("args_bad", f, env=env_for(d))
    if r.rc == 70 and ("failed to check the arguments required by the "
                       "dynamic provider" in r.err):
        h.ok("A22", "a script the library cannot parse is fatal, status 70")
    else:
        h.bad("A22", "a script the library cannot parse is fatal, status 70",
              f"rc={r.rc} err={r.err!r}")

    d = staging(root, "args_null_script", [args_null])
    r = h.sum("args_null_script", f, env=env_for(d))
    r2 = h.run("algo", "args_null_script", env=env_for(d))
    if (r.rc == 70 and r2.rc == 70
            and "didn't define the ArgParser script" in r2.err):
        h.ok("A22b", "a null script is fatal on both the sum and algo paths")
    else:
        h.bad("A22b", "a null script is fatal on both the sum and algo paths",
              f"sum rc={r.rc} algo rc={r2.rc} algo err={r2.err!r}")


# ---------------------------------------------------------------- A23-A26

def test_digest_crossing(h: Harness, root: Path, fails: Path, keyed: Path,
                         wide: Path, args: Path) -> None:
    section("A23-A26 The digest and the key cross as plain data")
    good = root / "a23-good.txt"
    bad = root / "a23-bad.txt"
    good.write_text("hello\n")
    bad.write_text("hello\n")
    d = staging(root, "fails", [fails])
    r = h.sum("fails", good, bad, env=env_for(d))
    if (r.rc == 1 and f"<NONE>  {bad}" in r.out
            and field(r.out, "Failed") == "1"
            and field(r.out, "Hashed") == "1"
            and "Permission denied" in r.out):
        h.ok("A23", "a non-zero errcode fails that file and only that file")
    else:
        h.bad("A23", "a non-zero errcode fails that file and only that file",
              f"rc={r.rc} out={r.out!r}")

    f = root / "a24.txt"
    f.write_text("hello\n")
    key = root / "a24.key"
    key.write_bytes(b"a\0bc")
    d = staging(root, "keyed", [keyed])
    r = h.sum("keyed", f, "-K", key, env=env_for(d))
    if r.rc == 0 and "0000abc304" in r.out:
        h.ok("A24", "a key is a byte string: a NUL inside does not truncate")
    else:
        h.bad("A24", "a key is a byte string: a NUL inside does not truncate",
              f"rc={r.rc} out={r.out!r} err={r.err!r}")

    d = staging(root, "wide", [wide])
    r = h.sum("wide", f, env=env_for(d))
    line = r.out.splitlines()[1] if len(r.out.splitlines()) > 1 else ""
    if r.rc == 0 and line.startswith("0123456789abcdef" * 16):
        h.ok("A25", "a digest of 256 characters fills the buffer and prints")
    else:
        h.bad("A25", "a digest of 256 characters fills the buffer and prints",
              f"rc={r.rc} len={len(line)}")

    d = staging(root, "args", [args])
    manifest = root / "a26.sum"
    manifest.write_text(f"0000abc60080  {f}\n")
    r = h.verify("-u", "args", manifest, "-L", "128", env=env_for(d))
    r2 = h.verify("-u", "args", manifest, "-L", "5", env=env_for(d))
    if (r.rc == 0 and field(r.out, "Changed") == "0"
            and field(r2.out, "Changed") == "1"):
        h.ok("A26", "verify forwards provider switches and hashes again")
    else:
        h.bad("A26", "verify forwards provider switches and hashes again",
              f"rc={r.rc} changed={field(r.out, 'Changed')} "
              f"rc2={r2.rc} changed2={field(r2.out, 'Changed')}")


# ---------------------------------------------------------------- A27-A30

def test_one_scan(h: Harness, root: Path, shadow: Path,
                  keyed_no_check: Path) -> None:
    """One scan per process, started by the work, not before `main`.

    `dpf_instance::ins()` is a function-local static, so the scan begins on the
    first use and every later caller reuses it. That is what keeps a loader
    failure to one `Error:` line no matter how many objects ask for the map, and
    what keeps `--help` and `--version` from touching the provider directories
    at all.
    """
    section("A27-A30 One scan per process")

    d = root / "load" / "two_failures"
    d.mkdir(parents=True, exist_ok=True)
    (d / "garbage.so").write_bytes(b"not an elf file\n")
    shutil.copy2(keyed_no_check, d / "keyed_no_check.so")
    counts = {}
    for command in (["-a"], ["-C"], ["algo", "garbage"]):
        r = h.run(*command, env=env_for(d))
        counts[" ".join(command)] = {
            name: sum(1 for line in r.err.splitlines()
                      if line.endswith(f'"{d / name}"'))
            for name in ("garbage.so", "keyed_no_check.so")}
    if all(n == 1 for seen in counts.values() for n in seen.values()):
        h.ok("A27", "each failing module prints exactly one Error line")
    else:
        h.bad("A27", "each failing module prints exactly one Error line",
              f"counts={counts}")

    r1 = h.run("--version", env=env_for(d))
    r2 = h.run("--help", env=env_for(d))
    if not r1.err and not r2.err:
        h.ok("A28", "help and version never scan the provider directories")
    else:
        h.bad("A28", "help and version never scan the provider directories",
              f"version={r1.err!r} help={r2.err!r}")

    f = root / "a29.txt"
    f.write_text("hello\n")
    d = staging(root, "shadow_for_sum", [shadow])
    r = h.sum("sha256", f, env=env_for(d))
    refused = "refused to load" in r.err
    digest_ok = f" {f}" in r.out and r.out.count("\n") > 1
    if refused and digest_ok and not r.err.replace(
            f'Error: refused to load "{d / "shadow.so"}"\n', ""):
        h.ok("A29", "a module claiming a built-in name is refused by sum")
    else:
        h.bad("A29", "a module claiming a built-in name is refused by sum",
              f"refused={refused} digest_ok={digest_ok} err={r.err!r}")

    d = root / "load" / "verify_scan"
    d.mkdir(parents=True, exist_ok=True)
    (d / "garbage.so").write_bytes(b"not an elf file\n")
    manifest = root / "a30.sum"
    manifest.write_text(f"# Algorithm:         sha256\n{digest(f)}  {f}\n")
    r = h.verify("-u", "sha256", manifest, env=env_for(d))
    hits = [line for line in r.err.splitlines() if 'garbage.so"' in line]
    if r.rc == 0 and len(hits) == 1 and field(r.out, "Changed") == "0":
        h.ok("A30", "verify's two checksum_api objects share one scan")
    else:
        h.bad("A30", "verify's two checksum_api objects share one scan",
              f"rc={r.rc} hits={len(hits)} changed={field(r.out, 'Changed')!r}")


# ---------------------------------------------------------------- runner

def find_compiler(explicit: str | None) -> str | None:
    if explicit:
        return shutil.which(explicit)
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    return None


def build_all(b: Builder) -> dict[str, Path]:
    mods: dict[str, Path] = {}
    mods["plain"] = b.build("plain", module("plain",
                                            block_digest("0000abc1")))

    mods["unkeyed_false"] = b.build(
        "unkeyed_false",
        module("unkeyed_false", block_is_keyed(False),
               block_digest("0000abc2")))

    mods["laxness"] = b.build("laxness", module(
        "laxness", block_is_keyed(False), block_check_key(0),
        block_digest_key_length("0000abc2")))

    mods["keyed"] = b.build("keyed", module(
        "keyed", block_is_keyed(True), block_check_key(4),
        block_digest_key_length("0000abc3")))

    mods["keyed_no_check"] = b.build("keyed_no_check", module(
        "keyed_no_check", block_is_keyed(True), block_digest("0000abc4")))

    mods["shadow"] = b.build("shadow", module(
        "sha256", block_digest("0000abc5")))

    for tag, dig in (("a", "0000d00a"), ("b", "0000d00b")):
        mods[f"dupe_{tag}"] = b.build(
            f"dupe_{tag}", module("dupe", block_digest(dig)))

    mods["info"] = b.build("info", module(
        "info",
        block_algo_info("Digest:     8 hex\\nKey:        none\\n"
                        "Origin:     https://example.org/x"),
        block_digest("0000abce")))

    mods["args_false"] = b.build("args_false", module(
        "args_false", block_need_args(False), block_digest("0000abcb")))

    mods["args_no_script"] = b.build("args_no_script", module(
        "args_no_script", block_need_args(True), block_digest("0000abca")))

    mods["args_no_take"] = b.build("args_no_take", module(
        "args_no_take", block_need_args(True), block_arg_def(ARGS_SCRIPT),
        block_digest("0000abc9")))

    mods["args_bad"] = b.build("args_bad", module(
        "args_bad", block_need_args(True),
        block_arg_def("this is not a script {"), block_take_args(),
        block_digest("0000abc8")))

    mods["args_null"] = b.build("args_null_script", module(
        "args_null_script", block_need_args(True), block_arg_def(None),
        block_take_args(), block_digest("0000abc7")))

    mods["args_reject"] = b.build("args_reject", module(
        "args_reject", block_need_args(True), block_arg_def(ARGS_SCRIPT),
        block_take_args("bad combination"), block_digest("0000abc6")))

    mods["args"] = b.build("args", module(
        "args",
        block_algo_info("Digest:     8 hex\\nKey:        none"),
        block_need_args(True), block_arg_def(ARGS_SCRIPT),
        """    static unsigned g_lev = 0;

    const char *tsubaki_take_args(tsubaki_pass_content *c,
                                  std::size_t n) noexcept
    {
        if (n == 0 || c[0].contentc == 0)
            return "level missing";
        g_lev = static_cast<unsigned>(
            std::strtoul(c[0].contentv[0], nullptr, 10));
        return "";
    }

    tsubaki_checksum_result tsubaki_checksum_file(const char *path,
                                                  const char *,
                                                  std::size_t) noexcept
    {
        tsubaki_checksum_result r{};
        std::FILE *f = std::fopen(path, "rb");
        if (!f) { r.errcode = errno; return r; }
        std::fclose(f);
        std::snprintf(r.result, sizeof r.result, "0000abc6%04x",
                      g_lev & 0xffffu);
        return r;
    }
"""))

    mods["fails"] = b.build("fails", module(
        "fails",
        """    tsubaki_checksum_result tsubaki_checksum_file(const char *path,
                                                  const char *,
                                                  std::size_t) noexcept
    {
        tsubaki_checksum_result r{};
        std::FILE *f = std::fopen(path, "rb");
        if (!f) { r.errcode = errno; return r; }
        std::fclose(f);
        if (std::strstr(path, "bad"))
        {
            r.errcode = EACCES;
            return r;
        }
        std::snprintf(r.result, sizeof r.result, "0000abcf");
        return r;
    }
"""))

    mods["wide"] = b.build("wide", module(
        "wide",
        """    tsubaki_checksum_result tsubaki_checksum_file(const char *,
                                                  const char *,
                                                  std::size_t) noexcept
    {
        tsubaki_checksum_result r{};
        for (int i = 0; i < 256; ++i)
            r.result[i] = "0123456789abcdef"[i % 16];
        r.result[256] = 0;
        return r;
    }
"""))
    return mods


def main(argv: list[str]) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Black-box provider ABI tests for tsubaki",
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
        help="compiler used to build the fixture modules",
    )
    ns = parser.parse_args(argv)

    binary = resolve_binary(ns.binary)
    if binary is None:
        print("tsubaki not found. Pass --binary or set "
              "TSUBAKI=/path/to/tsubaki.",
              file=sys.stderr)
        return 1
    compiler = find_compiler(ns.cxx)
    if compiler is None:
        what = (f'"{ns.cxx}" is not executable' if ns.cxx
                else "no C++ compiler found")
        print(f"{what}; the ABI suite needs one. "
              "Pass --cxx or set CXX=/path/to/c++.",
              file=sys.stderr)
        return 2

    h = Harness(binary)
    print(f"tsubaki: {binary}")
    print(f"compiler: {compiler}")
    with tempfile.TemporaryDirectory(prefix="tsubaki-abi-") as tmp:
        root = Path(tmp)
        try:
            mods = build_all(Builder(compiler, root / "mods",
                                      Path(__file__).resolve().parent.parent
                                      / "include"))
        except BuildError as e:
            print(f"Error: a fixture failed to compile: {e}", file=sys.stderr)
            return 1
        test_minimal_module(h, root, mods["plain"])
        test_key_switch(h, root, mods["unkeyed_false"], mods["laxness"])
        test_keyed_module(h, root, mods["keyed"], mods["keyed_no_check"])
        test_builtin_shadow(h, root, mods["shadow"])
        test_scan(h, root, mods["plain"], mods["dupe_a"], mods["dupe_b"])
        test_argument_trio(h, root, mods["args_false"],
                           mods["args_no_script"], mods["args_no_take"])
        test_algo_output(h, root, mods["info"], mods["args_bad"],
                         mods["args"])
        test_argument_handling(h, root, mods["args"], mods["args_reject"],
                               mods["args_bad"], mods["args_null"])
        test_digest_crossing(h, root, mods["fails"], mods["keyed"],
                             mods["wide"], mods["args"])
        test_one_scan(h, root, mods["shadow"], mods["keyed_no_check"])

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
