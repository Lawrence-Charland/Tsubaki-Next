# Tsubaki-Next

[中文](README.zh-cn.md)

A reimplementation of the Tsubaki checksum tool. It keeps the name and the
goal, not the old architecture: files are no longer collected into a list
before anything happens. Scanners feed a shared task stream and a worker pool
consumes it as paths arrive.

## Origins

Tsubaki-Next is the spiritual successor of an earlier Tsubaki. The archived
repository
[Lawrence-Charland/Tsubaki](https://github.com/Lawrence-Charland/Tsubaki) holds
two independent attempts side by side: `old` is the author's own version,
`main` is GDJT-DSR's. That repository is archived;
[GDJT-DSR/Tsubaki](https://github.com/GDJT-DSR/Tsubaki) is still maintained.

## Scope

The current build produces a single target, `tsubaki`, which computes
checksums.

Everything that needs every checksum in memory before it can decide anything —
the `cmp` and `dup` style comparisons — is deliberately left out. If such
functionality comes back, it will most likely come back as its own build
target, not as a mode of `tsubaki`.

## Build

Requirements: CMake 3.28+, a C++23 compiler, OpenSSL (only `libcrypto` is
linked), Ninja for the bundled presets.

```sh
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
```

The first configure fetches
[LC-ArgParser](https://github.com/Lawrence-Charland/LC-ArgParser) from GitHub,
so network access is needed once; the copy stays under `build/_deps` and is
reused afterwards. Without network access, point CMake at a local checkout:

```sh
cmake --preset linux-gcc-release \
      -DFETCHCONTENT_SOURCE_DIR_LC-ARGPARSER=/path/to/LC-ArgParser
```

The binary lands in `build/linux-gcc-release/tsubaki`. A `linux-gcc-debug`
preset exists with the same layout.

## Usage

```
tsubaki [options] <algorithm> <file|dir>...
```

`algorithm` is a positional, and so is every input that follows it. An input is
a directory, a file, or `-` for stdin, and the kinds can be mixed in one
command. A `\` on its own ends the input list; a plain argument after it is not
an input any more.

The safe shape is: algorithm first, then every input in one go, then the
options.

```sh
tsubaki sha256 dir1 file2 - -e build -l old.txt
```

Options come last because `-e` and `-l` take every plain argument that follows
them, and by then all the inputs are already collected.

### Options

| Option                     | Meaning                                              |
| -------------------------- | ---------------------------------------------------- |
| `-h`, `--help`             | print help and exit                                  |
| `-v`, `--verbose`          | live progress indicator on stderr                    |
| `-t<n>`, `--thread=<n>`    | worker threads, defaults to hardware concurrency     |
| `-o<file>`, `--output=<file>` | write results to `<file>` instead of stdout        |
| `-e<path>`, `--exclude=<path>` | skip `<path>` and everything below it when scanning directories; repeatable |
| `-l<file>`, `--list=<file>` | read paths from `<file>`; repeatable                |
| `-s`, `--follow-symlinks`  | follow symbolic links to directories                 |
| `-f`, `--force-rescan`     | recompute checksums already listed in the input      |

`--threads`, `--excludes` and `--lists` are accepted as aliases.

Parsing itself is done by
[LC-ArgParser](https://github.com/Lawrence-Charland/LC-ArgParser), a small
standalone library; `include/cmdline.hpp` only describes this program's command
line.

### Option values

A long option takes its value with `=` and a short option takes it sticky:

```sh
tsubaki sha256 . --exclude=build --list=old.txt
tsubaki sha256 . -ebuild -lold.txt
```

A separated value is the run of plain arguments right after the switch. How many
of them are taken depends on the option: `-t` and `-o` take one, `-e` and `-l`
take as many as you give, up to the next switch that is still in the queue.

```sh
tsubaki sha256 . -e a b c -l list1 list2   # three excludes, two lists
```

`-e` stops at `-l` here, because `lists` is evaluated after `excludes`; each
entry takes its own run of values.

The entries are evaluated in a fixed order, and a switch that has been consumed
is gone from the queue. A later option can therefore walk past a switch that an
earlier option already took:

```sh
# threads runs first and takes "-t 4";
# excludes then sees "c.txt ." and takes both
tsubaki sha256 -e c.txt -t 4 .        # error: no input left
```

That is why the safe shape puts the inputs before every option:

```sh
tsubaki sha256 dir1 file2 - -e build -l old.txt   # -e takes build, -l takes old.txt
tsubaki sha256 -e build dir1                      # -e takes dir1 too, nothing left
```

A `\` on its own ends the input list; a plain argument after it is no longer an
input, so it is rejected unless another option takes it. In a shell, quote the
backslash as `'\'`: an unquoted `\ ` escapes the space and never reaches the
program.

### Algorithms

```
none  md5  sha1  sha224  sha256  sha384  sha512
sha3_256  sha3_512  blake2b512  blake2s256
```

`none` only lists the files: no hashing is done, and every file is reported as
unprocessed.

## Inputs

Files and directories on the command line are scanned directly. A list file
(`-l`, or stdin via `-`) is read line by line:

- an empty line or a line starting with `#` is ignored;
- a line starting with `<NONE> ` names the path after the marker;
- a line in `<checksum> <path>` form keeps the checksum as it is, writes
  `# Kept` and the original line to the output, and only rescans the path when
  `--force-rescan` is given;
- any other line is a path, spaces included.

A kept line is decoupled from the hashing run: it is not hashed and does not
show up in the report counts.

A path made only of hex digits would be read as a checksum, so write entries as
`./name` or as absolute paths. Paths with spaces need no quoting: an
unrecognised line is taken whole.

Since `<NONE> <path>` is accepted as input as well, the output of one run can be
fed back with `-l` to redo only the files that were left unprocessed.

Duplicate entries are not merged: the same path named twice is processed twice.

## Output

Results go to stdout unless `--output` is given. One line per file:

```
<checksum> <path>
```

Files that were not hashed — interrupted, or the `none` algorithm — are written
as `<NONE> <path>`. A report block follows, every line of which starts with
`#`:

```
#
# ----------General Report----------
# Algorithm: sha256
# Total Processed: 3
# Succeeded: 3
# Failed: 0
# Unprocessed: 0
# Duration: 1.23456 seconds
```

`Algorithm` is the one chosen for the run, so a `Ctrl-C` does not turn it into
`none` in the report. `Duration` measures the hashing run itself, not argument
parsing or scanning setup.

`Total Processed` is `Succeeded + Failed`; files that were left unprocessed are
not counted, and neither are kept lines or skipped paths.

When something went wrong, an `# Error messages:` block follows with one
`# Value= / Message= / Operation=` line per entry. The progress indicator, when
enabled, is written to stderr and never to the result stream.

## Exit status

`0` when every processed file succeeded and no error was recorded, `1`
otherwise. Argument errors exit with `1` as well.

## Interrupt

`Ctrl-C` switches the run to the `none` algorithm: files that have not started
yet are still popped, written as `<NONE>` and counted as unprocessed, so the
report accounts for every file the scanners produced. A file that has already
entered the hashing call finishes normally.

This makes an interrupted run resumable. Feed the output file back as a `-l`
input: lines that already carry a checksum are kept unchanged, and the `<NONE>`
lines are hashed again. The report block starts with `#`, so it is ignored and
the file can be used as it is.

## Architecture

The module layout, the task-stream interface, the threading model and the data
flow are described in [ARCHITECTURE.md](ARCHITECTURE.md).

## License

Tsubaki-Next is released under the GNU General Public License, version 2. See
[COPYING](COPYING) for the full text.

The argument parser is a separate project by the same author, licensed under
MIT; its own `LICENSE` file travels with it and the copy used at build time
stays under that licence.