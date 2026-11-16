# Tsubaki-Next

[中文](README.zh-cn.md)

A reimplementation of the Tsubaki checksum tool. It keeps the name and the
goal, not the old architecture: files are no longer collected into a list
before anything happens. Scanners feed a shared task stream and a worker pool
consumes it as paths arrive. Hashing is done by OpenSSL: plain digests through
`EVP_MD`, keyed hashes (HMAC and keyed BLAKE2) through `EVP_MAC`.

## Contents

- [Build](#build)
- [Usage](#usage)
  - [Algorithms](#algorithms)
  - [Options](#options)
  - [Option values](#option-values)
  - [Keyed hashes](#keyed-hashes)
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
`CMakeLists.txt` itself accepts 3.10), a C++23 compiler, OpenSSL (only
`libcrypto` is linked; the keyed algorithms need OpenSSL 3.0 or newer for
`EVP_MAC`), Ninja for the bundled presets.

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
preset exists with the same layout.

## Usage

```
tsubaki sum [options] <algorithm> <file|dir>...
tsubaki --help
tsubaki --version
```

The `sum` subcommand is required. `--help` and `--version` are the only
invocations that do not need it; any other word in the subcommand position is
rejected as an unknown subcommand.

`algorithm` is a positional, and so is every input that follows it. An input is
a directory, a file, or `-` for stdin, and the kinds can be mixed in one
command. A `\` on its own ends the input list; a plain argument after it is not
an input any more.

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

A keyed algorithm without a key is rejected with exit status 2 and a
`needs a key` message. A key given to a plain digest is ignored with a warning.

The digests agree with `openssl dgst -<name>`, the HMACs with
`openssl dgst -<digest> -hmac <key>`, and the BLAKE2 MACs with
`openssl mac -macopt key:<key> BLAKE2BMAC` (or `BLAKE2SMAC`). The output is
lowercase hexadecimal, and its length is set by the algorithm.

### Options

| Option                         | Meaning                                                              |
| ------------------------------ | -------------------------------------------------------------------- |
| `-h`, `--help`                 | print help and exit                                                  |
| `-V`, `--version`              | print the version and exit                                           |
| `-v`, `--verbose`              | live progress indicator on stderr                                    |
| `-t<n>`, `--thread=<n>`        | worker threads, a positive integer; defaults to hardware concurrency |
| `-k<key>`, `--key=<key>`       | secret key for the keyed algorithms; ignored with a warning by the others |
| `-K<file>`, `--key-file=<file>` | read the key from `<file>` (`-` for stdin); trailing CR/LF dropped; ignored by plain digests |
| `-o<file>`, `--output=<file>`  | write results to `<file>`, created or truncated, instead of stdout   |
| `-e<path>`, `--exclude=<path>` | skip `<path>` and everything below it when walking a directory; repeatable |
| `-l<file>`, `--list=<file>`    | read paths from `<file>`; repeatable                                 |
| `-s`, `--follow-symlinks`      | follow symbolic links to directories                                 |
| `-f`, `--force-rescan`         | recompute checksums already listed in the input                      |

`--threads`, `--excludes` and `--lists` are accepted as aliases.

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
of them are taken depends on the option: `-t`, `-k`, `-K` and `-o` take exactly
one, `-e` and `-l` take as many as you give, up to the next switch that is still
in the queue.

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

The entries are evaluated in a fixed order, and a switch that has been consumed
is gone from the queue. A later option can therefore walk past a switch that an
earlier option already took:

```sh
# threads runs first and takes "-t 4";
# excludes then sees "c.txt ." and takes both
tsubaki sum sha256 -e c.txt -t 4 .        # no input left: empty run, exit 0
```

That is why the safe shape puts the inputs before every option:

```sh
tsubaki sum sha256 dir1 file2 - -e build -l old.txt   # -e takes build, -l takes old.txt
tsubaki sum sha256 -e build dir1                      # -e takes dir1 too, nothing left
```

A `\` on its own ends the input list; a plain argument after it is no longer an
input, so it is rejected unless another option takes it. In a shell, quote the
backslash as `'\'`: an unquoted `\ ` escapes the space and never reaches the
program.

### Keyed hashes

A key comes from one of two places, never both:

- `--key` takes it as a literal string: the bytes of the argument are used as
  they are, with no hex or escape decoding, so `-k 00ff` is the four-byte key
  `0`, `0`, `f`, `f`.
- `--key-file` reads it from a file, or from stdin when the path is `-`. The
  whole stream is read and any run of trailing CR/LF is dropped, so
  `echo secret > key` yields `secret`, while interior newlines, NUL bytes and
  trailing spaces stay part of the key. A missing, unreadable or empty file
  exits 114.

`-k` and `-K` together are rejected with exit status 2, and so is `-K -`
together with a `-` input: stdin may be named only once. With a plain digest,
either form is ignored with a warning and the key file is not even opened.

One run uses one key for every file; to check the same tree under two keys, run
the command twice.

Keyed BLAKE2 caps the key length: `blake2b512_mac` takes at most 64 bytes and
`blake2s256_mac` at most 32. A longer key is rejected with exit status 2 before
any file is opened; HMAC takes any length.

`--key` puts the key on the command line: while the process runs, any user on
the machine can read it from `ps` or `/proc/<pid>/cmdline`, and the shell keeps
a copy in its history. `--key-file` keeps it out of both, but the file itself is
only as private as its permissions (use `chmod 600`). Either way the key stays
in process memory while the run lasts. Do not pass a real key on a shared host.

Keyed results are printed like any other: `<checksum> <path>`, with the keyed
algorithm named in the report block. Nothing marks a line as keyed, so a file
of HMAC lines can only be told apart from a digest run by the algorithm name in
the report block it travels with.

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

A path whose first space-separated field is all hex digits would be read as a
checksum followed by a path, so write such entries as `./name` or as absolute
paths. A hex-only name with no space in it is unaffected. Paths with spaces
need no quoting: an unrecognised line is taken whole.

Since `<NONE> <path>` is accepted as input as well, the output of one run can be
fed back with `-l` to redo only the files that were not hashed.

Excludes are compared with `equivalent()` against every entry met during a
directory walk, so they are neither patterns nor string prefixes. They only
affect directory scans: a file named on the command line, or read from a list,
is hashed even when it is excluded. An exclude that is the scanned directory
itself, or a parent of it, matches nothing and is silently ignored — exclude a
subdirectory instead. Do not use `-e` to drop a directory that you also pass as
an input; just leave that argument out.

Inside a directory walk only regular files are hashed; a device or a FIFO met
there is skipped silently. An explicitly named input is different: a device or
a FIFO given on the command line, or written in a list, is opened and read as a
stream. `/dev/null` therefore hashes as empty content, `/dev/stdin` hashes
whatever arrives on stdin, and `/dev/fd/N` from process substitution works as
well. Such a stream can block or never end; see Output and Interrupt.

Duplicate entries are not merged: the same path named twice is processed twice.

Sparse files are hashed over their logical length: holes read as zeros, so a 1 TB
sparse image still costs 1 TB of reads even if it occupies a few megabytes on
disk.

## Output

Results go to stdout unless `--output` is given. One line per file:

```
<checksum> <path>
```

Lines are written in completion order, not input order, so with more than one
worker the order changes between runs. A path is printed exactly as its scanner
produced it: `./name` for entries found under `.`, as written for a list entry.

Files that were not hashed — interrupted, or the `none` algorithm — are written
as `<NONE> <path>`. The same happens to a file that passes the scan but fails to
open, read or hash; that one is counted as failed.

A path that is missing is filtered out before any worker sees it: it produces
no output line and none of the counters below. It only adds an entry to the
error block, which still makes the exit status 1. A directory written in a list
file is rejected the same way.

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

An `# Errors:` count is always written, even when it is 0. When at least one
error was recorded, the count is followed by a `# Error messages:` block with one
`# Value: / Message: / Operation:` line per entry, and an interrupted run gets a
`# The process was interrupted halfway through.` line under the count:

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

Write errors are not checked. Every worker line goes out through
`std::osyncstream`, which flushes the complete insertion, and the report is
written after the workers are joined; a flush that fails — a full disk, a
broken descriptor, a file on a removed mount — only leaves the stream in a
failed state, which the program never inspects. The run keeps its exit status:
`0` still means "every file was hashed", not "every byte reached the
destination". The `--output` file is closed by the `std::ofstream` destructor,
and that error is ignored too.

A pipe with no reader is the one case that is visible: the next write raises
`SIGPIPE`, whose default action kills the process, and a shell reports `141`.
Nothing catches or retries it. If the destination can fill up, check the file
after the run or hash it again; the summary on stderr does not prove that the
lines were written.

## Exit status

| Code | Meaning |
| --- | --- |
| `0` | no error was recorded |
| `1` | at least one error was recorded: a checksum failure, a missing path, an unopenable list file, or a missing exclude |
| `2` | invalid command line: unknown subcommand, unknown algorithm, a keyed algorithm without a key or with a key too long for it, `-k` together with `-K`, `-K -` together with a `-` input, bad thread count, or leftover arguments |
| `70` | fatal start-up failure: `argc` is 0, so the process refuses to run |
| `74` | the `--output` file cannot be opened for writing |
| `114` | the `--key-file` cannot be read, or holds no key |
| `130` | the run was interrupted with `Ctrl-C` |
| `250` | `Ctrl-C` was pressed five times; the process aborted immediately, so the output can end without its report block |
| `-1` (`255` in a shell) | an unexpected exception escaped `main()` |

The status reports argument, scanning and hashing problems only. A silent write
failure does not change it, and a process killed by `SIGPIPE` shows `141` in the
shell, which is not one of the codes above.

`--help` and `--version` exit with `0`. `70` and `74` are the sysexits values
`EX_SOFTWARE` and `EX_IOERR`; `130` is the shell convention `128 + SIGINT`;
`250` is just below the `255` that a shell reports for `-1`, so the two cannot
be confused. `114` is not a sysexits value; it is reserved here for a key file
that cannot be read.

The codes live in `include/exit_codes.hpp` as `TSUBAKI_SUCCESS`,
`TSUBAKI_HASH_ERROR`, `TSUBAKI_INVALID_ARGUMENT`, `TSUBAKI_FILE_WRITE_FAILED`,
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
`sum` is the first one and the only one that exists today.

Everything that needs every checksum in memory before it can decide anything —
comparing several sets of results — is deliberately left out for now.

## Roadmap

The pipeline stays one-way: a path is hashed and written out, never collected.
A comparison is the opposite shape — it can only decide once every set involved
is in memory — so it will become another subcommand of the same binary, next to
`sum`. Nothing beyond that is designed yet; what is fixed is the shape it would
take:

- It is not a new program. A subcommand gets a `subcommands::` constant, its own
  entry group in `include/cmdline.hpp`, a branch in `main()`'s dispatch, and a
  driver like `sum_main()`. The parser, the scanners and the checksum layer are
  shared; only the result format and the exit code mapping differ.
- `timeline_fs` is the structure sketched for keeping results comparable: an
  in-memory tree in which every node carries a *per-snapshot* child list and a
  per-snapshot checksum map. A time id is added by name, then files and
  directories are created, updated or removed under that id. Loading any number
  of ids and walking the tree shows what changed between runs, and the same map
  shows which files hold identical content.

Development of `timeline_fs` is paused, so its sources are kept as an
experiment under [experiments/timeline_fs/](experiments/timeline_fs/) instead
of the source tree: no target references them, and no comparison entry or
driver exists. The module layout itself is described in
[ARCHITECTURE.md](ARCHITECTURE.md).

## Architecture

The module layout, the task-stream interface, the threading model and the data
flow are described in [ARCHITECTURE.md](ARCHITECTURE.md).

## License

Tsubaki-Next is released under the GNU General Public License, version 2. See
[COPYING](COPYING) for the full text.

The argument parser is a separate project by the same author, licensed under
MIT; its own `LICENSE` file travels with it and the copy used at build time
stays under that licence.
