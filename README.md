# Tsubaki-Next

[中文](README.zh-cn.md)

A reimplementation of the Tsubaki checksum tool, with checksum computation at
its core. It covers most plain digests and keyed hashes, and hashes a single
file, a whole directory tree, or every path named in an existing list — pulling
from all of them at once and hashing in parallel. A directory scan takes its
own exclude list. Algorithms outside the built-in list can come from shared
objects named by an environment variable; see
[Dynamic algorithms](#dynamic-algorithms).

The result is a *manifest* — the checksum file `sum` writes: `<checksum>
<path>` lines plus a `#` report block. Because a manifest is a plain file, the
same tool can do two further things with it:

- `verify` re-hashes the paths a manifest names and reports `changed` and
  `unsure`. It is the convenient answer to "did anything change?", and its
  output is itself a refreshed manifest, so a verification can be archived or
  run again. Its granularity is coarse: one verdict per path, and `unsure`
  covers both "the manifest carried no checksum" and "the file could not be
  hashed".
- `cmp` compares two manifests without touching the files at all. It is the
  detailed answer to "what changed?": `[!]` modified, `[D]` moved, copied,
  merged or renamed (grouped by checksum), `[U]` deleted or added, and `[=]`
  matched. The extra detail costs convenience — you need two manifests to start
  with, and it cannot tell you how the current files differ from either of
  them.

The two compose naturally: `verify` produces an up-to-date manifest, and `cmp`
turns it into a fine-grained comparison against the old one.

## Contents

- [Build](#build)
- [Usage](#usage)
  - [Algorithms](#algorithms)
  - [Dynamic algorithms](#dynamic-algorithms)
  - [Options](#options)
  - [Option values](#option-values)
  - [Keyed hashes](#keyed-hashes)
  - [Verifying](#verifying)
  - [Comparing](#comparing)
- [Inputs](#inputs)
- [Output](#output)
- [Exit status](#exit-status)
- [Interrupt](#interrupt)
- [Origins and scope](#origins-and-scope)
- [Roadmap](#roadmap)
- [Architecture](#architecture)
- [License](#license)

## Build

Requirements: CMake 3.28+ (that is what `CMakePresets.json` asks for;
`CMakeLists.txt` itself accepts 3.10), a C++23 compiler, OpenSSL 3.0 or newer
(only `libcrypto` is linked; `EVP_MAC` and `<openssl/core_names.h>` set the
floor), Ninja for the bundled presets.

```sh
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
```

The first configure fetches
[LC-ArgParser](https://github.com/Lawrence-Charland/LC-ArgParser) from GitHub,
so network access is needed once; the copy stays under `build/<preset>/_deps`
and is reused afterwards. Without network access, point CMake at a local
checkout:

```sh
cmake --preset linux-gcc-release \
      -DFETCHCONTENT_SOURCE_DIR_LC-ARGPARSER=/path/to/LC-ArgParser
```

The binary lands in `build/linux-gcc-release/tsubaki`. A `linux-gcc-debug`
preset exists with the same layout. `cmake --install build/linux-gcc-release`
copies it to `CMAKE_INSTALL_PREFIX/bin` (`/usr/local` by default).

### Supported platforms

Tsubaki-Next is a POSIX program: dynamic providers need `dlfcn.h`, file reads
use `unistd.h` and `fcntl.h`, the worker pool uses `pthread.h`, and the
progress line asks `sys/ioctl.h` for the terminal width. Everything that
provides those builds without source changes:

| System | Presets | Notes |
| --- | --- | --- |
| Linux, glibc or musl | `linux-gcc-debug`, `linux-gcc-release`, `linux-gcc-dist-static` | GCC is the default toolchain. x86_64, aarch64, armv7, riscv64, loongarch64 and ppc64le are all supported. |
| macOS 12+ | `macos-debug`, `macos-release`, `macos-dist-static` | Xcode 16+ (libc++ needs it for `std::expected`). Apple toolchain only — GCC cannot target Darwin. Homebrew's `openssl@3` is found automatically. |
| FreeBSD 13+ | `freebsd-debug`, `freebsd-release`, `freebsd-dist-static` | Base clang; `pkg install openssl3`. |
| NetBSD, OpenBSD, DragonFly, illumos, Solaris, Cygwin | `posix-debug`, `posix-release` | Need OpenSSL 3.0+ from ports or pkgsrc: LibreSSL has no `EVP_MAC`, and CMake stops with an explicit message instead of failing at the first include. On Cygwin the providers must end in `.so`; the providers project sets that suffix. |
| **Native Windows (MSVC, MinGW)** | — | **Not supported and not planned.** The sources use `dlfcn.h`, `unistd.h`, `pthread.h`, `sys/ioctl.h`, `::open`/`::read` and `O_CLOEXEC`, none of which native Windows has. Use WSL or Cygwin. |

`linux-clang-debug` is the single non-GCC preset, kept as a second opinion on
compiler warnings. Every Linux preset pins its compiler by absolute path
(`/usr/bin/g++`, or `/usr/bin/clang++` for that one), so pass
`-DCMAKE_CXX_COMPILER=/path/to/c++` on the configure line if yours lives
elsewhere; the macOS, FreeBSD and other-POSIX presets leave the compiler to
the platform default.

`linux-gcc-dist-static`, `freebsd-dist-static` and `macos-dist-static` are
maintainer presets: they link the static `libcrypto` from
`res/openssl-static-for-tsubaki`. End users should build the plain release
preset instead.

### Distributions

`scripts/build-release-dist-all.sh` builds the statically linked binary for
every supported target and packs it into
`res/distributions/tsubaki-<version>-<target>.tar.gz` (the binary plus
`COPYING`). It uses one compiler, GCC, and cross-compiles wherever a GCC cross
toolchain exists:

```sh
scripts/build-release-dist-all.sh --list            # matrix, and what is missing
scripts/build-release-dist-all.sh                   # everything this host can build
scripts/build-release-dist-all.sh linux-aarch64     # just the named targets
```

Every target needs its GCC (`<arch>-linux-gnu-g++`, or the FreeBSD
equivalents) and a static `libcrypto` built for that target in
`res/openssl-static-for-tsubaki-<target>/`. That is what
`scripts/build-static-openssl.sh` does — it takes the same target names and
cross-compiles OpenSSL with `<prefix>gcc`:

```sh
scripts/build-static-openssl.sh --list              # OpenSSL target per platform
scripts/build-static-openssl.sh 3.5.9               # native libcrypto only
scripts/build-static-openssl.sh 3.5.9 --all         # every target with a toolchain
scripts/build-static-openssl.sh 3.5.9 linux-aarch64 # just the named targets
```

Native output stays in `res/openssl-static-for-tsubaki` (what the presets point
at); a cross target goes to `res/openssl-static-for-tsubaki-<target>`, which is
where the distribution script looks for it. Full build logs land in
`build/openssl-logs/<target>.log`.

Targets whose toolchain is missing are skipped and listed in the summary, and
the script exits non-zero only when a target it started fails. macOS targets
build on macOS alone, and `posix-native` covers the remaining systems when the
script runs there.

## Usage

```
tsubaki sum [options] <algorithm> <file|dir>...
tsubaki verify [options] <checksum-file|->   (- for stdin, needs -u)
tsubaki cmp [options] <left-list> <right-list>
tsubaki --help
tsubaki --version
tsubaki --algorithm
```

A subcommand is required: `sum` calculates checksums, `verify` re-checks a
file written by `sum`, and `cmp` compares two such files. `--help`, `--version`
and `--algorithm` (or `-a`) are the only invocations that do not need one; any
other word in the subcommand position is rejected as an unknown subcommand.
Those three are checked before validation, so they also win over every argument
error: running `tsubaki -a junk` prints the algorithm table and exits 0. When
several of them are given, `--help` wins over `--version`, and both win over
`--algorithm`.

Everything up to [Verifying](#verifying) describes `sum`; `verify` reuses the
checksum-file format and the `-v`, `-t`, `-k`, `-K`, `-o` and `-u` options. `cmp`
reuses the same format, but takes two files and only the `-o` and `-c`
options.

`algorithm` is a positional, and so is every input that follows it. An input is
a directory, a file, or `-` for stdin, and the kinds can be mixed in one
command.

The safe shape is: algorithm first, then every input in one go, then the
options.

```sh
tsubaki sum sha256 dir1 file2 - -e build -l old.txt
```

Options come last because a separated value is taken from the plain arguments
that physically follow the switch, up to the next one still in the queue: an
`-e` placed before the inputs would swallow them. `files` is evaluated after
`excludes` and `lists`, so whatever those two leave behind still reaches it.

No input is required at all: `tsubaki sum sha256` runs with an empty stream and
prints the report block alone, with every counter at 0.

### Algorithms

`none` only lists the files: no hashing is done, and every file is counted as
`Not hashed`. Every other name is either a plain digest or a keyed hash.

Plain digests:

```
md5  sha1  sha224  sha256  sha384  sha512  sha3_256  sha3_512
blake2b512  blake2s256
```

Keyed hashes:

```
hmac_md5  hmac_sha1  hmac_sha224  hmac_sha256  hmac_sha384  hmac_sha512
hmac_sha3_256  hmac_sha3_512  blake2b512_mac  blake2s256_mac
```

An `hmac_*` name is HMAC with the digest that follows the prefix, so
`hmac_sha3_512` is HMAC-SHA3-512. `blake2b512_mac` and `blake2s256_mac` are
keyed BLAKE2b-512 and BLAKE2s-256; a keyed BLAKE2 value differs from the
unkeyed one because the key seeds the compression function.

A keyed algorithm without a key is rejected with exit status 64 and a
`needs a key` message. A key given to a plain digest is ignored with a warning.

The digests agree with `openssl dgst -<name>`, the HMACs with
`openssl dgst -<digest> -hmac <key>`, and the BLAKE2 MACs with
`openssl mac -macopt key:<key> BLAKE2BMAC` (or `BLAKE2SMAC`). The output is
lowercase hexadecimal, and its length is set by the algorithm.

`tsubaki -a` (or `--algorithm`) prints the same names as a two-column table
(`ALGORITHM`, `KEYED`): the names are walked from the literal list the parser
itself uses and `KEYED` comes from the key policy, so the table tracks the
implementation instead of repeating the list above by hand.
A second `ALGORITHM`, `KEYED`, `SOURCE` table follows when provider modules were
loaded; see below.

### Dynamic algorithms

An algorithm that is not built in can come from a shared object. List the
directories in `TSUBAKI_DYNAMIC_PROVIDER_PATHS`, separated by `:`; each one is
walked recursively and every file whose extension is exactly `.so` is loaded
(`libx.so.1` is skipped). Empty segments are ignored.

Ready-made providers live in
[Tsubaki-Sample-Providers](https://github.com/Lawrence-Charland/Tsubaki-Sample-Providers)
as one CMake project; that repository's README lists what it currently ships.
It is the shortest way to see the contract compiled and is a fair template for
a provider of your own.

- **What a provider exports.** Four `extern "C"` symbols, declared for it in
  `include/dynamic_provider_header.hpp`, which also states the contract in full:
  `tsubaki_provider_name()` returns the name to use on the command line,
  `tsubaki_is_keyed()` says whether a key is required, `tsubaki_check_key()`
  accepts or rejects a key, and `tsubaki_checksum_file()` computes the digest
  into a `tsubaki_checksum_result` returned by value: a caller-owned buffer for
  the digest plus an errno-style `errcode`, both filled on success and on
  failure. Only plain data crosses — no `std::string`, no
  `std::filesystem::path`, no `std::expected`, no `std::error_code` — so
  nothing is ever allocated on one side and freed on the other. That is what
  lets a statically linked `tsubaki` load modules at all.
- **Where the name works.** Everywhere an algorithm name is accepted, `verify`
  `-u` included. A built-in name always wins: a module that reports one is
  refused while it loads, with an error naming the file, and never reaches the
  table, so a `.so` cannot shadow `sha256`. Between two providers claiming one
  name, the first loaded wins — the order in the environment variable, then the
  walk order inside each directory.
- **Keys.** A keyed provider is asked for a key like any built-in keyed
  algorithm (`needs a key`, exit status 64) and validates it through
  `tsubaki_check_key()`; there is no length limit to declare.
- **What the digest must look like.** At least 32 hexadecimal characters —
  MD5's length — lowercase, NUL terminated, and shorter than
  `TSUBAKI_MAX_RESULT_LENGTH`. `listscanner` only recognises a checksum from
  that length up, so a shorter digest is read back as a path and `verify`
  reports the line as unsure. Anything weaker than MD5 has no business behind
  this ABI anyway.
- **How it must be built.** A C++23 `-fPIC` shared object for the same
  architecture and calling convention as `tsubaki`. The boundary carries plain
  data only, so the two sides no longer have to share one standard library, but
  a module is self-contained: `tsubaki` exports no symbols to it and hands it no
  runtime to borrow, and a statically linked `tsubaki` still loads modules
  through the system C library, so the module has to match the glibc generation
  `tsubaki` was linked with. One project may build several algorithms — try
  this first: they share the contract header and the file loop — emitting one
  `.so` per name, and the directory walk picks up every one of them.
- **What goes wrong.** A path that is not a readable directory, a `.so` that
  `dlopen` refuses, a missing symbol: each prints one `Error:` line on stderr
  and is skipped, and none of them changes the exit status. Modules stay loaded
  for the life of the process; nothing unloads them.

`--algorithm` lists what it found in its own table:

```
---------- Dynamic providers ----------
ALGORITHM  KEYED  SOURCE
blake3     No     /home/user/lib/tsubaki/blake3.so
```

`SOURCE` is the module path, which makes a stale duplicate easy to spot when two
directories both hold a build of the same algorithm.

### Options

| Option                         | Meaning                                                              |
| ------------------------------ | -------------------------------------------------------------------- |
| `-h`, `--help`                 | print help and exit                                                  |
| `-V`, `--version`              | print the version and exit                                           |
| `-a`, `--algorithm`            | print the algorithms, whether each one is keyed and, for a provider, the module it came from, then exit; needs no subcommand |
| `-v`, `--verbose`              | live progress indicator on stderr; not shown for `none`              |
| `-t<n>`, `--thread=<n>`        | worker threads, a positive integer; defaults to hardware concurrency (1 if unknown) |
| `-k<key>`, `--key=<key>`       | secret key for the keyed algorithms; ignored with a warning by the others |
| `-K<file>`, `--key-file=<file>` | read the key from `<file>` (`-` for stdin); trailing CR/LF dropped; ignored by plain digests |
| `--strip-crlf`                 | read the key file (or `-K -` stdin) with LF-only line endings, so a CRLF file yields the same key as an LF one |
| `-o<file>`, `--output=<file>`  | write results to `<file>`, created or truncated, instead of stdout (sum), `<checksum-file>.verify` (verify) or `<left>_to_<right>.cmp` (cmp); `-` means stdout |
| `-u<algo>`, `--use-algo=<algo>` | verify with `<algo>` instead of the listing's `Algorithm:` header (verify only) |
| `-c`, `--canonical`            | make both paths absolute, resolve symlinks and normalize separators before comparing (cmp only) |
| `-e<path>`, `--exclude=<path>` | skip `<path>` and everything below it when walking a directory; repeatable |
| `-l<file>`, `--list=<file>`    | read paths from `<file>`; repeatable                                 |
| `-s`, `--follow-symlinks`      | follow symbolic links to directories                                 |
| `-f`, `--force-rescan`         | recompute checksums already listed in the input                      |

`--threads`, `--excludes` and `--lists` are accepted as aliases.

`verify` accepts `-v`, `-t`, `-k`, `-K`, `--strip-crlf` and `-o` with the same
meanings, plus `-u` to name the algorithm. It takes no algorithm or file
positional, and rejects `-e`, `-l`, `-s` and `-f`: the paths, the algorithm when
`-u` is absent and the unconditional rescan all come from the checksum file.
That file may be `-` for stdin, but the `Algorithm:` header cannot then be
prefetched, so a `-` input requires `-u`.

`cmp` accepts only `-o` and `-c`. It never hashes anything, so it takes no
algorithm, key, thread or scan option; `-v`, `-t`, `-k`, `-K`, `--strip-crlf`,
`-e`, `-l`, `-s` and `-f` are rejected with exit status 64. Its two positionals
are the result files to compare, not paths to hash.

Parsing itself is done by
[LC-ArgParser](https://github.com/Lawrence-Charland/LC-ArgParser), a small
standalone library; `include/cmdline.hpp` only describes this program's command
line.

### Option values

A long option takes its value with `=` and a short option takes it sticky:

```sh
tsubaki sum sha256 . --exclude=build --list=old.txt
tsubaki sum sha256 . -ebuild -lold.txt
tsubaki sum hmac_sha256 . --key=secret
```

A separated value is the run of plain arguments right after the switch. How many
of them are taken depends on the option: `-t`, `-k`, `-K`, `-o` and `-u` take
exactly one, `-e` and `-l` take as many as you give, up to the next switch that
is still in the queue.

```sh
tsubaki sum sha256 . -e a b c -l list1 list2   # three excludes, two lists
tsubaki sum hmac_sha256 . -k secret            # one key, then no more values
```

`-e` stops at `-l` here, because `lists` is evaluated after `excludes`; each
entry takes its own run of values.

A switch that matches no entry is not rejected as an option: it stays a plain
argument, so a positional collects it and it is reported as a missing file. A
typo such as `--exlude=build` therefore shows up in the error block, not as an
option error.

### Keyed hashes

A key comes from one of two places, never both:

- `--key` takes it as a literal string: the bytes of the argument are used as
  they are, with no hex or escape decoding, so `-k 00ff` is the four-byte key
  `0`, `0`, `f`, `f`.
- `--key-file` reads it from a file, or from stdin when the path is `-`. The
  whole stream is read and any run of trailing CR/LF is dropped, so
  `echo secret > key` yields `secret`, while interior newlines, NUL bytes and
  trailing spaces stay part of the key. A missing, unreadable or empty file
  exits 66. Add `--strip-crlf` when the file may have been written on Windows:
  it drops the CR of every CRLF line ending, so `a\r\nb\r\n` becomes `a\nb`
  instead of `a\r\nb`. It never rewrites a `--key` literal.

`-k` and `-K` together are rejected with exit status 64, and so is `-K -`
together with a `-` input: stdin may be named only once. With a plain digest,
`-k`, `-K` and `--strip-crlf` are ignored with a warning and the key file is
not even opened.

One run uses one key for every file; to check the same tree under two keys, run
the command twice.

Keyed BLAKE2 caps the key length: `blake2b512_mac` takes at most 64 bytes and
`blake2s256_mac` at most 32. A longer key is rejected with exit status 64 before
any file is opened; HMAC takes any length.

`--key` puts the key on the command line: while the process runs, any user on
the machine can read it from `ps` or `/proc/<pid>/cmdline`, and the shell keeps
a copy in its history. `--key-file` keeps it out of both, but the file itself is
only as private as its permissions (use `chmod 600`). Either way the key stays
in process memory while the run lasts and never appears anywhere in the
results. Do not use a real key on a shared host.

Keyed results are printed like any other: `<checksum> <path>`, with the keyed
algorithm named in the report block. Nothing marks a line as keyed, so a file
of HMAC lines can only be told apart from a digest run by the algorithm name in
the report block it travels with.

### Verifying

`verify` re-reads a checksum file written by `sum` and recomputes every path it
names:

```sh
tsubaki verify sums.txt                 # writes sums.txt.verify
tsubaki verify sums.txt -o -            # prints the refreshed checksums
tsubaki verify sums.txt -o new.txt      # writes new.txt
tsubaki verify sums.txt -u sha256       # uses sha256 instead of the header
cat sums.txt | tsubaki verify - -u sha256   # reads the listing from stdin
```

The listing may be `-` for stdin. The `Algorithm:` header cannot be read without
consuming the stream that the paths still have to come from, so a `-` listing
requires `-u` and exits 64 without it; stdin may also be named only once, so
`-K -` is refused alongside a `-` listing. Without `-o` the output is named
after the input, which for stdin is `stdin.verify`.

The algorithm comes from the file's `# Algorithm:` header unless `-u` (or
`--use-algo`) names one. A forced name is announced on stderr and replaces the
header entirely, so `-u` also accepts a listing that has no header at all or
one whose header is stale; an unknown name exits 64. Forcing `none` is allowed,
but nothing is then hashed: every path stays `Not hashed` and counts as unsure.
Paths, `<NONE>` markers and comment lines are read exactly as `-l` would read
them, except that every checksum is recomputed: `verify` implies
`--force-rescan`.

The output has the same shape as a `sum` run: `<checksum> <path>` lines for
every processed file, then the report block. The report gains two fields:

```
# Changed:           1
# Unsure:            0
```

A *changed* file is one whose stored checksum differs from the recomputed one.
An *unsure* file is one the run could not decide about: the listing gave no
checksum for it (a bare path, or `<NONE> <path>`), or it could not be hashed at
all — the run was interrupted, or the hash failed, or the algorithm was `none`.
The affected paths are listed below each count in the result stream; the copy of
the report on stderr omits them. A changed or unsure file does not change the
exit status; only read, write and scan errors do, exactly as in `sum` — a
hashing failure is one of those, so it still exits 1. The output is a refreshed
listing, so a second `verify` of it reports `Changed: 0` as long as the files
did not change in between; paths left as `<NONE>` in it come back as unsure.

`-v`, `-t`, `-k`, `-K` and `-o` work as they do for `sum`, and `-u` is described
above. The key must be supplied again for a keyed algorithm, because a checksum
file records the algorithm name but never the key. A different key produces
different checksums, so a wrong key reads as every file changed, not as an
error. A listing written with `none` is rejected (exit 64), because it holds no
checksums to compare; `-u` with any other algorithm proceeds, while `-u none`
runs and leaves every path unsure. `-o` must not name the input file (exit 64),
because the input would be truncated before it is read.

### Comparing

`cmp` reads two result files written by `sum` and reports how they differ:

```sh
tsubaki cmp old.txt new.txt               # writes old_to_new.cmp
tsubaki cmp old.txt new.txt -o -          # prints the comparison
tsubaki cmp old.txt new.txt -o diff.cmp   # writes diff.cmp
```

Both inputs use the checksum-file format: `#` lines, including a whole report
block, are ignored, and every other line must be a `<checksum> <path>` pair.
The separator is either two spaces or a space and the `*` binary marker, which
is not part of the path. A bare path or `<NONE> <path>` is recorded as an
error and left out, because there is no checksum to compare. One of the two
files may be `-` for stdin, but not both. Checksums are compared as lowercase
hexadecimal, so an uppercase spelling is not a change. Only the checksum
strings are compared: two listings written with different algorithms, or with
different keys, share no checksums at all, so every path reads as modified and
nothing as matched, moved or deleted.

The output has one section per kind of difference:

```
# tsubaki comparison result
# [!] Modified:
[!] ./chg.txt

# L = old.txt, R = new.txt
# [D] Moved,copied,merged or renamed:
# [D][1]
# [D][1][L]
[D][1][L]./mv.txt
# [D][1][R]
[D][1][R]./moved.txt

# L = old.txt, R = new.txt
# [U] Deleted or added:
# [U][L]
[U][L]./del.txt
# [U][R]
[U][R]./add.txt

# [=] Matched:
[=] ./same.txt
```

- `[!]` *Modified*: the same path in both listings with a different checksum.
  The left spelling is printed.
- `[D][n]` *Moved, copied, merged or renamed*: one group per checksum held by
  both sides at different paths; `[D][n][L]` lists the left paths and
  `[D][n][R]` the right ones. A checksum that only one side holds produces no
  group; its paths fall under deleted or added.
- `[U]` *Deleted or added*: paths that only the left (`[U][L]`) or only the
  right (`[U][R]`) listing names, with no matching checksum on the other side.
- `[=]` *Matched*: the same path in both listings with the same checksum.
- `[E]` *Error messages*: only when something was recorded — a line without a
  checksum, a path that `-c` could not canonicalize, or a list that failed
  while being read. The comparison continues, and the exit status is then 1.

A path repeated in one listing with the same checksum is compared once. The
same path repeated with different checksums in one listing is refused with exit
status 64, because there is no single value to compare.

Without `-c`, paths are compared exactly as written, so `./a.txt` and
`b/../a.txt` are different paths. With `-c` (or `--canonical`) both sides are
passed through `std::filesystem::canonical()` first, which makes each path
absolute, resolves symlinks and normalizes separators, so differently spelled
routes to the same file match. Canonicalization needs the path to exist: a
deleted file cannot be canonicalized, so it will be compared with an uncanonicalized 
path, with an error reported.

Without `-o`, the result goes to `<left-stem>_to_<right-stem>.cmp` in the
current directory (`old.txt` and `new.txt` give `old_to_new.cmp`), and a notice
on stderr names that file. `-o -` writes to stdout. `-o` must not name either
input (exit 64), because both lists are read after the output is opened. The
`# L = ..., R = ...` lines repeated between sections name the two inputs; an
initial copy goes to stderr so it stays visible when the result goes to a file.

`cmp` never hashes, walks or scans anything: it reads two files, so the
interrupt handling described below does not apply to it and a `Ctrl-C` simply
kills the process.

## Inputs

Files and directories on the command line are scanned directly. A list file
(`-l`, or stdin via `-`) is read line by line. Every line is trimmed before it
is looked at, so leading and trailing whitespace never becomes part of a path.

- an empty line or a line starting with `#` is ignored;
- a line starting with `<NONE> ` (the marker and one space) names the path after
  the marker;
- a line in `<checksum> <path>` form keeps the checksum as it is, writes
  `# Kept: <path>` and the (trimmed) original line to the output, and only
  rescans the path when `--force-rescan` is given;
- any other line is a path, spaces included.

A kept line is decoupled from the hashing run: it is not hashed and does not
show up in the report counts. It is written to the result stream while the list
is being read, so it can appear between computed lines.

Every list entry is treated as a file. A directory on a list line is rejected,
not expanded; only directory arguments on the command line are walked. A line
beginning with `#` is a comment even if it was meant as a path, so a file named
`#name` has to be written as `./#name` or by an absolute path.

A line without a checksum is taken whole, spaces included, so a path may
contain spaces. But a line whose first space-separated field is at least 32
characters of pure hexadecimal — a field that therefore cannot contain a `/` —
cannot be told apart from `<checksum> <path>`, and is always read that way.
That is the price of accepting two formats in one file, so write such a path
as `./name` or as an absolute path. A shorter hex field, or a hex-only name
with no space in it, is unaffected.

coreutils separates a checksum from its path with two spaces, or with a space
and a `*` for a binary read (`sha256sum --binary`); the `*` is dropped and is
never part of the path, so a listing written either way reads the same. Because
the output of `sum` uses a single space, a bare path that really starts with
`*` would read back as that marker: write it as `./*name` or as an absolute
path.

Since `<NONE> <path>` is accepted as input as well, the output of one run can be
fed back with `-l` to redo only the files that were not hashed.

Excludes are compared with `equivalent()` against every entry met during a
directory walk, so they are neither patterns nor string prefixes. They only
affect directory scans: a file named on the command line, or read from a list,
is hashed even when it is excluded. An exclude that is the scanned directory
itself, or a parent of it, matches nothing and is silently ignored — exclude a
subdirectory instead. Do not use `-e` to drop a directory that you also pass as
an input; just leave that argument out.

A device or a FIFO named on the command line or in a list is opened and read as
a stream. `/dev/null` therefore hashes as empty content, `/dev/stdin` hashes
whatever arrives on stdin, and `/dev/fd/N` from process substitution works as
well. Such a stream can block or never end; see Interrupt. A socket cannot be
read, so naming one is an error. A directory walk, by contrast, yields regular
files only: a device, a FIFO or a socket met while walking is skipped without
so much as an error entry, and `sum sha256 ~` therefore cannot stall on a pipe
somebody left behind.

Duplicate entries are not merged: the same path named twice is processed twice.

Sparse files are hashed over their logical length: holes read as zeros, so a 1 TB
sparse image still costs 1 TB of reads even if it occupies a few megabytes on
disk.

## Output

Results go to stdout unless `--output` is given. The result stream starts with
a `# tsubaki checksum result` header, then one line per file:

```
<checksum> <path>
```

(`cmp` writes `# tsubaki comparison result` and its own sections instead; see
[Comparing](#comparing).)

Lines are written in completion order, not input order, so with more than one
worker the order changes between runs. A path is printed exactly as its scanner
produced it: `./name` for entries found under `.`, as written for a list entry.

Files that were not hashed — interrupted, or the `none` algorithm — are written
as `<NONE> <path>`. The same happens to a file that passes the scan but fails to
open, read or hash; that one is counted as failed.

A path that is missing is filtered out when it is taken from the task stream:
it produces no output line and none of the counters below. It only adds an
entry to the error block, which still makes the exit status 1. A directory
written in a list file is rejected the same way.

A report block follows, every line of which starts with `#`:

```
#
# ---------- Summary ----------
# Algorithm:         sha256
# Processed:         3
# Hashed:            3
# Failed:            0
# Not hashed:        0
# Duration:          1.23456 seconds
# Errors:            0
```

`Algorithm` is the one chosen for the run. It never changes, so an interrupted
run still names the algorithm it was hashing with. `Duration` is measured from
the moment the workers are started to the moment they are joined; argument
parsing and scanner setup are not included.

`Processed` is `Hashed + Failed`; files that were not hashed are not counted,
and neither are kept lines or skipped paths.

An `# Errors:` count is always written, even when it is 0. An interrupted run
adds a `#` separator line and a `# Warning: The process was interrupted halfway
through.` line under the count; when at least one error was recorded, the count
is followed by a `# Error messages:` block with one
`# Value: / Message: / Operation:` line per entry:

```
# Errors:            2
#
# Error messages:
# Value: 2, Message: No such file or directory, Operation: determining the file type of plain.txt
```

An error does not have to come from a checksum failure. A missing input path, an
unopenable list file, or an exclude that does not exist is recorded here as
well, so a run can have `Failed: 0` and still exit 1 (and a missing exclude is
reported even though the scan itself continues normally).

With `--output`, the report block is additionally written to stderr, without
the per-entry error messages, so the summary stays visible on the terminal while
the results go to the file.

The progress indicator, when enabled, is written to stderr and never to the
result stream.

Write errors are checked at the end through `std::ostream::fail()`. Every worker
line is flushed out whole through `std::osyncstream`, and the report is written
after the workers are joined; a flush that fails — a full disk, a broken
descriptor, a file on a removed mount — leaves the stream in a failed state, and
the exit status is `73`. If the results go to a pipe whose reader has already
closed it, the write raises `SIGPIPE` and the default action kills the process
(a shell reports 141), so the `73` path is not reached.

## Exit status

| Code | Meaning |
| --- | --- |
| `0` | no error was recorded; a `verify` run reports changed and unsure files, and a `cmp` run reports every difference, without treating them as errors |
| `1` | at least one error was recorded: a checksum failure, a missing path, an unopenable list file, or a missing exclude; `cmp` also records a list line without a checksum, a path that `-c` could not canonicalize, or a list read error |
| `64` | invalid command line: unknown subcommand, unknown algorithm, a keyed algorithm without a key or with a key too long for it, `-k` together with `-K`, `-K -` together with a `-` input, bad thread count, or leftover arguments; `verify` also uses 64 for an input without an `Algorithm:` header when `-u` is absent, a `-` input with no `-u`, a header written with `none`, an unknown `-u` name, or an output that is the input file; `cmp` uses 64 when both lists are stdin, when the output names one of the lists, and when one list names the same path more than once with different checksums |
| `66` | the `--key-file` cannot be read, the key file holds no key, the `verify` input is missing or is a directory, or a `cmp` list cannot be read |
| `70` | fatal start-up failure: `argc` is 0, so the process refuses to run |
| `73` | the `--output` file cannot be opened for writing, or the output stream fails |
| `130` | the run was interrupted with `Ctrl-C` |
| `250` | `Ctrl-C` was pressed five times; the process aborted immediately, so the output can end without its report block |
| `255` | an unexpected exception escaped `main()` |

`--help` and `--version` exit with `0`. `64`, `66`, `70` and `73` are the
sysexits values `EX_USAGE`, `EX_NOINPUT`, `EX_SOFTWARE` and `EX_CANTCREAT`;
`130` is the shell convention `128 + SIGINT`; `250` is just below the `255`
used for an unexpected exception, so the two cannot be confused.

The codes live in `include/exit_codes.hpp` as `TSUBAKI_SUCCESS`,
`TSUBAKI_HASH_ERROR`, `TSUBAKI_INVALID_OPERATION`, `TSUBAKI_FILE_WRITE_FAILED`,
`TSUBAKI_FILE_READ_FAILED`, `TSUBAKI_INTERRUPTED`, `TSUBAKI_TERMINATED`,
`TSUBAKI_FATAL` and `TSUBAKI_UNEXPECTED`.

## Interrupt

The first `Ctrl-C` raises the manager's interrupt flag. Workers stop before
hashing the next path: files that have not started yet are still popped,
written as `<NONE>` and counted as `Not hashed`, so the report accounts for
every file the scanners produced. A file that has already entered the hashing
call is not abandoned; for a device or a FIFO that means it keeps reading until
EOF. A worker blocked in `open()` or in an endless read never gets back to the
check, so a single `Ctrl-C` cannot stop it.

Pressing `Ctrl-C` five times gives up altogether: the handler writes a fixed
message to stderr and calls `_exit(250)` on the spot. Nothing is flushed after
that, so the output can end without its report block and the counters are lost.

This makes an interrupted run resumable. Feed the output file back as a `-l`
input: lines that already carry a checksum are kept unchanged, and the `<NONE>`
lines are hashed again. The report block starts with `#`, so it is ignored and
the file can be used as it is. An interrupted run exits `130` even when nothing
actually failed, so a `1` still means a real error.

## Origins and scope

Tsubaki-Next is the spiritual successor of an earlier Tsubaki. The archived
repository
[Lawrence-Charland/Tsubaki](https://github.com/Lawrence-Charland/Tsubaki) holds
two independent attempts side by side: `old` is the author's own version,
`main` is GDJT-DSR's. That repository is archived;
[GDJT-DSR/Tsubaki](https://github.com/GDJT-DSR/Tsubaki) is still maintained.

The project builds exactly one executable, `tsubaki`, and will keep doing so.
Features are added as subcommands of that binary rather than as further targets;
`sum`, `verify` and `cmp` are the three that exist today.

`verify` streams: it compares one file at a time and never needs the whole
listing in memory. `cmp` is the exception: it holds both listings and decides
once they are loaded.

## Roadmap

The hashing pipeline stays one-way: a path is hashed and written out, never
collected. `cmp` is the opposite shape and is not built on that pipeline: it
reads the two listings into memory and compares them only after both are
loaded. Nothing beyond these three subcommands is designed.

## Architecture

The module layout, the task-stream interface, the threading model and the data
flow are described in [ARCHITECTURE.md](ARCHITECTURE.md).

## License

Tsubaki-Next is released under the GNU General Public License, version 2. See
[COPYING](COPYING) for the full text.

The argument parser is a separate project by the same author, licensed under
MIT; its own `LICENSE` file travels with it and the copy used at build time
stays under that licence.
