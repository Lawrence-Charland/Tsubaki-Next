# Tsubaki-Next

[中文](README.zh-cn.md)

Generate, verify and compare checksum manifests. Input can be a file, a
directory tree, an existing manifest or standard input, combined freely in one
run and hashed in parallel. The result is a manifest: one `<checksum> <path>`
line per file, followed by a `#` report block naming the algorithm, the counts,
the duration and the errors. Manifest lines interoperate with GNU coreutils hash
tools: `sha256sum -c` verifies what it writes, and it reads what `sha256sum`
writes.

Built-in algorithms cover the common digests and keyed hashes (HMAC, keyed
BLAKE2), and `tsubaki -a` lists them all; anything missing comes from a
provider, see [More algorithms](#more-algorithms).

Four subcommands: `sum` generates a manifest, `verify` re-hashes the paths a
manifest names and reports what changed and what it could not decide, `cmp`
compares two manifests, and `algo` describes a single algorithm.
 `verify` answers "did anything change since the record?",
`cmp` answers "what exactly changed between two runs?", and they chain: refresh
with `verify`, then compare the result against the old manifest with `cmp`.

It is the spiritual successor of the original
[Tsubaki](https://github.com/Lawrence-Charland/Tsubaki), but not a feature
superset: what is missing is listed under
[Relationship to the original Tsubaki](#relationship-to-the-original-tsubaki).

## More algorithms

The program itself ships 20 built-in algorithms — 10 digests and 10 keyed
hashes; the list is under [Algorithms](docs/USAGE.md#algorithms). More do not
need a code change: a provider supplies them, one `.so` per provider and one
algorithm per `.so`, and a directory listed in
`TSUBAKI_DYNAMIC_PROVIDER_PATHS` makes its names usable as algorithm names (see
[Dynamic algorithms](docs/USAGE.md#dynamic-algorithms)).

Ready-made providers live in the companion project
[Tsubaki-Sample-Providers](https://github.com/Lawrence-Charland/Tsubaki-Sample-Providers):
one CMake project builds the whole set, including a few constructions the
built-in algorithms do not offer at all.

- non-cryptographic checksums: CRC-32, CRC-32C, CRC-64/XZ, Adler-32, XXH64
- SHA-2 truncated variants: SHA-512/224, SHA-512/256, and their HMACs
- the rest of SHA-3 and original Keccak: SHA3-224, SHA3-384, Keccak-224/256/384/512
- the XOF family: SHAKE, TurboSHAKE, cSHAKE, KT (KangarooTwelve), KMAC
- BLAKE3, keyed mode included, and BLAKE2b/BLAKE2s with `--length`, `--salt`
  and `--personal`
- RIPEMD-160, the Chinese standard SM3, and HMAC-SM3
- the CMAC family: AES, ARIA, Camellia, SEED, SM4

(This list may lag behind; that repository is the authority on what is
supported.)

`tsubaki algo <name>` shows the digest width,
the key format and the switches a module accepts; to write one yourself, see
the [provider contract](docs/PROVIDER-ABI.md).

## Quick start

Prebuilt binaries are on the
[releases page](https://github.com/Lawrence-Charland/Tsubaki-Next/releases):
Linux only so far (x86_64, aarch64, armv7l, riscv64, loongarch64), statically
linked with OpenSSL compiled in, so you unpack and run. Every other platform
builds from source; that needs CMake 3.28+, a C++23 compiler, OpenSSL 3.0+ and
Ninja:

```sh
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
```

The binary lands in `build/linux-gcc-release/tsubaki`; `cmake --install` copies
it to `CMAKE_INSTALL_PREFIX/bin` (`/usr/local` by default). The first configure
fetches LC-ArgParser from GitHub and reuses that copy afterwards; without
network access, point CMake at a local checkout with
`-DFETCHCONTENT_SOURCE_DIR_LC-ARGPARSER=/path/to/LC-ArgParser`.

A first run:

```sh
tsubaki sum sha256 . -o sums.txt     # write a manifest
tsubaki verify sums.txt              # re-hash, printing the result
tsubaki cmp old.txt new.txt          # compare them, printing the differences
```

The output of `sum` is the manifest, and both `verify` and `cmp` consume it.
Results go to stdout unless `-o` names a file; `-v` shows live progress on
stderr. Options go after the inputs: a separated value keeps taking plain
arguments until the next switch, `-e` and `-l` especially.

## Supported platforms

| System | Presets | Notes |
| --- | --- | --- |
| Linux, glibc or musl | `linux-gcc-debug`, `linux-gcc-release` | the only platform with prebuilt binaries; all five architectures |
| macOS 12+ | `macos-debug`, `macos-release` | Xcode 16+, Apple toolchain, Homebrew's `openssl@3` found automatically |
| FreeBSD 13+ | `freebsd-debug`, `freebsd-release` | base clang; `pkg install openssl3` |
| other POSIX (NetBSD, OpenBSD, Solaris, ...) | `posix-debug`, `posix-release` | needs OpenSSL 3.0+; LibreSSL has no `EVP_MAC` |
| native Windows | — | not supported and not planned; use WSL or Cygwin |

`linux-clang-debug` is the single non-GCC Linux preset, kept as a second opinion
on compiler warnings. `linux-gcc-san` adds AddressSanitizer and
UndefinedBehaviorSanitizer to the Debug build for local runs.

## Documentation

- [Usage](docs/USAGE.md): subcommands, options, manifest format, report block,
  exit status, interrupts
- [provider contract](docs/PROVIDER-ABI.md): what a dynamic algorithm module
  has to export
- [Architecture](docs/ARCHITECTURE.md): module layout, threading model, data flow

Where these documents and the program disagree, the documents are what counts,
not the program's actual behavior; the author reserves the right to change them.

## Relationship to the original Tsubaki

The original was written in C++17 against OpenSSL and packed three modes into
one binary — computing checksums, finding duplicates, and comparing two result
files; that repository is now archived. The rewrite turned modes into
subcommands, the manifest into an intermediate that can be fed back, and
algorithms from a compile-time list into loadable `.so` modules.

Some things were dropped: there is no separate deduplication mode — duplicates
surface through `cmp`'s `[D]` groups; size filtering, `--test` dry runs and
`--balance` are gone; a plain path list must be written as `<NONE> <path>` or
passed with `-l`. Most old `sum` command lines still work: the order of
`sum <algorithm> <paths>` is unchanged, and `--exclude`, `-v` and
`--force-rescan` keep their names; `--allow-symlinks` became
`--follow-symlinks`, `--thd-amount` became `--thread`, and the `stdin`
positional became `-`. The manifests are readable by either version; only the
report fields were renamed.

The name predates the rewrite: it comes from Tsubaki Sawabe in *Your Lie in
April* — purely personal preference — and has nothing to do with checksumming.

## Status

A single-author project; there has been no security audit, and no CI or fuzzing
has been added yet. Correctness rests on three black-box suites under `tests/`,
which `tests/check.py` runs in one go: the contract tests in `general.py`, the
provider ABI tests in `abi.py` (they need a C++ compiler) and the differential
tests in `differential.py` against coreutils and OpenSSL. Two invariants stand
behind them: manifest lines match coreutils, and the report keys are frozen. The built-in algorithms all go through OpenSSL; dynamic
providers are third-party code, so read a module's `Source:` line and its script
with `tsubaki algo` before trusting it.

## License

Tsubaki-Next is released under the GNU General Public License, version 2. See
[COPYING](COPYING) for the full text.

The argument parser, LC-ArgParser, is a separate project by the same author,
licensed under MIT; its own `LICENSE` file travels with it, and the copy used at
build time stays under that licence.
