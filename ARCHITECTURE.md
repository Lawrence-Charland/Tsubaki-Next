# Tsubaki-Next Architecture

[中文](ARCHITECTURE.zh-cn.md)

## Overview

The program is a pull-based pipeline. Producers implement one method that hands
out the next path; a single stream object merges the producers; a pool of
workers pulls paths from that stream and hashes them; one writer turns each
finished result into a line of output.

```
  argv ──> cmdline ──> tsubaki_core ──> dirscanner      ─┐
                                    ├─> listscanner     ─┼─> fanin ─> work_manager ─> out
                                    └─> cached_taskstream ┘                ├─> printer (stderr)
                                                                            └─> work_result ─> report
```

There is no intermediate "file list" object. Nothing is sorted, counted or
filtered after scanning: a path is hashed as soon as a worker gets it.

## Startup sequence

`main()` only wraps `tsubaki::tsubaki_main()` in a try/catch and turns an
escaped exception into an error message plus exit code 1.

`tsubaki_main()` does the following, in order:

1. `def_args()` builds an `argparser::ap_instance` from the entry definitions in
   `include/cmdline.hpp`.
2. `pre_parse(argc, argv)` splits the arguments into styled tokens;
   `evaluate()` matches them against the entries.
3. The program name is taken from `argv[0]`, and `--help` and `--version` are
   checked **before** validation. Both therefore work with no other arguments
   present, and both win over every argument error. Help text comes from
   `help_text()` in `src/help.cpp`; the version string is the CMake project
   version, injected as `TSUBAKI_VERSION`.
4. `fine(false)` runs the validation. Every failed entry is reported with
   `argparser::describe()`, followed by a hint to run `--help`.
5. Values are read out of the entries and checked: the algorithm is mapped
   through `str_to_alg()`, the thread count is parsed (falling back to
   `std::thread::hardware_concurrency()`), and the output stream is opened.
6. Inputs are turned into producers and added to a `fanin_taskstream`: one
   `dirscanner` per directory, one `listscanner` per `-` or list file, and one
   `cached_taskstream` for the plain file arguments. The queue is built in that
   order: directories and `-` markers as they appear on the command line, then
   the cached files, then the `-l` lists. The cached stream is added even when
   it is empty, and an empty input list is legal: `files` has no minimum count,
   so nothing is a valid run that just prints an empty report.
7. `work_manager(tasks, *out, alg, verbose).run(threads)` starts the workers and
   returns a `work_result`.
8. `tsubaki_main` prints `#` and the result to the output stream and turns
   `errors.empty()` into the exit code.

## Command line layer

Parsing is done by `LC-ArgParser`
(<https://github.com/Lawrence-Charland/LC-ArgParser>), a separate C++17 library
fetched by CMake at configure time and compiled into the binary as a source
file, not linked as a shared library. Tsubaki-Next only touches its public interface
(`ap_instance`, `ap_entry`, `ap_defkey`); the library knows nothing about
checksums, and it can be reused as is by another program.

The parts of its behaviour that surface here:

- `pre_parse` tags every token as simple, single-dash, double-dash or
  `argv_zero`; everything after `--` becomes simple.
- Long names take `--name` or `--name=value`, short names take `-v`, bundled
  `-vs`, or a sticky value `-ovalue`. A separated value (`--output out.txt`) is
  supported by the library itself.
- An option collects the simple tokens that follow it until the next non-simple
  token or its value limit; flags only count occurrences.
- A positional collects simple tokens within its min/max range, and a barrier
  token ends the collection early and is consumed with it.
- `evaluate()` walks the entries in declaration order and erases what each one
  consumed, so the declaration order decides who sees a separated value.
- `fine()` and `errors()` summarise the per-entry results, optionally reporting
  whatever was left unconsumed as `invalid_arguments`.
- The library makes no compatibility promise; the vendored copy is pinned to a
  single commit.

Entries are defined once, in `include/cmdline.hpp`; their declaration order is
the evaluation order: threads, output, the flags, algorithm, excludes, lists,
files, version, help.

- `excludes` and `lists` come before `files`, so their values are taken before
  the positional looks at anything, and a separated value (`-e build`) works as
  long as no file argument follows it.
- Neither entry has an upper bound on its value count, so `-e build .` takes `.`
  as a second exclude and leaves `files` empty.
- `files` is the last positional. It collects simple arguments until the `\`
  barrier; a plain argument after the barrier is left unconsumed, and
  `fine(false)` reports it as `invalid_arguments`.
- `-h`/`--help` is a flag, so `--help=something` is a validation error rather
  than a help request. The same applies to `-V`/`--version`.

## Task streams

### The interface

`basic_taskstream` (`include/basic_taskstream.hpp`) is the only interface
producers implement:

```cpp
virtual std::optional<std::filesystem::path> pop();      // public entry
virtual std::optional<std::filesystem::path> pop_raw() = 0;  // protected
void report_error(const error_entry &);
std::vector<error_entry> take_errors();
```

`pop()` is the only method workers call. It takes the mutex, calls `pop_raw()`,
releases the mutex, and then inspects the candidate outside the lock:

- a regular file is returned;
- `status()` setting an error code — a missing path, a dangling symlink —
  records that error and the loop continues;
- a directory is recorded as `operation_not_permitted` and the loop continues;
- anything else that exists — a character or block device, a FIFO, a socket —
  is returned as it is, and opening or reading it is left to the worker.

This filter lives in the base class, so producers only have to answer "what is
the next path". `dirscanner` overrides `pop()` as well, for one reason: it walks
directory entries, so it silently skips every non-file instead of recording an
error for each subdirectory it passes. That is also what keeps a directory walk
from opening a FIFO or a device it happens to meet.

A path rejected here (missing, unstatable, or a directory in a list) never
reaches a worker, so it is not printed and not counted in any of the three
counters; it only leaves an `error_entry`, which is enough to make the exit code
1. That is why `Failed` can be 0 while the run still fails: `Failed` only counts
files whose checksum attempt returned an error.

`error_entry` is a pair of `std::error_code` and a free-form operation string;
the string is what ends up in the report, so producers are expected to describe
what they were doing, not just what failed.

### dirscanner

Constructed with a directory, the exclude list and the symlink flag. It holds a
`std::filesystem::recursive_directory_iterator` opened with
`skip_permission_denied`, plus `follow_directory_symlink` when requested. The
constructor rejects a nonexistent path and a non-directory with
`std::invalid_argument`, and drops excludes that do not exist, recording one
error per missing exclude.

`pop_raw()` calls `skip_if_possible()` first: while the current entry is
`equivalent()` to one of the excludes, it disables recursion for that entry (if
it is a directory), steps over it, and checks the next entry. Paths are compared
with `equivalent()`, not by string prefix, so an exclude matches the same
inode wherever it is reached from.

The check only sees entries the iterator yields, and the iterator never yields
the root it was opened on. An exclude that is the scanned root, or a parent of
it, therefore matches nothing and is silently ignored; excluding a subdirectory
is the supported shape. Excludes also never apply to files that are named on the
command line or read from a list, because those do not go through this scanner.

A nonexistent exclude is dropped after an `error_entry` is recorded, so it still
makes the run exit 1 even though scanning continues. A missing directory or a
path that is not a directory makes the constructor throw `std::invalid_argument`;
that exception is not converted into an error entry — it escapes `tsubaki_main`
and is caught by the last-resort handler in `main()`.

### listscanner

Reads lines from any `std::istream` — a file opened by `tsubaki_core`, or
`std::cin` when `-` is used — and writes to the result stream when it decides to
keep a line. Each non-empty line is trimmed; lines starting with `#` are
ignored.

The line is split at the first space by `get_equation()`. If the first part is
the literal `<NONE>` — the marker this program writes for a file it did not hash
— the part after the space is the path. Otherwise, if the first part is not
entirely hexadecimal (`is_checksum()`), the whole line is a path, spaces
included. If the first part *is* hexadecimal, the line is a `<checksum> <path>`
pair:

- with `--force-rescan`, the path is handed out for hashing;
- otherwise the line is decoupled from the run: `# Kept: <path>` and the
  trimmed original line are written to the output, and the line contributes
  nothing to the run or to the report counts.

Taking an unrecognised line whole is what makes a space in a name harmless, and
it is what lets an `<NONE> <path>` output be fed back as input. The one
ambiguity left is a name made only of hex digits: it is lexically identical to a
checksum, and the only way to separate them is to make the first field
non-hexadecimal (`./name`, an absolute path). The `<NONE> ` prefix is reserved
and is always read as a marker.

A list line always names a file. A directory written on a list line is handed
out like any other path and then rejected by `pop()`; it is never walked. Only
directory arguments on the command line become a `dirscanner`.

### cached_taskstream

Holds a vector of paths and hands them out from the back, so a group of plain
file arguments is processed in reverse order. It performs no existence check of
its own; invalid paths are filtered by `basic_taskstream::pop()`.

### fanin_taskstream

This is itself a taskstream, which is what lets the worker pool see a single
stream. `pop_raw()` looks at the front child: if the child yields a path, that
path is returned; if the child is exhausted, its errors are drained with
`take_errors()` and appended to the fan-in's own error list, the child is
dropped, and the next child is consulted.

Because `pop_raw()` runs inside `basic_taskstream::pop()`'s mutex, the whole
pull from the children is serialised. Only hashing runs in parallel; the moment
a worker has to ask for the next path it waits behind every other asking
worker. This also means a `listscanner` blocked on `std::cin` blocks the stream,
not just one thread.

## Thread-safety rules

`pop_raw()` always runs with `mutex_` held, and `errors` has no lock of its own.

- Accessing your own state inside `pop_raw()` needs no lock, but calling
  `report_error()`, `take_errors()`, `pop()` or `add_task()` from there will
  self-deadlock: the mutex is not recursive. Write to `errors` directly instead.
- `errors` may only be touched while the mutex is held (inside `pop_raw()`) or
  from a single thread (construction, or after `take_errors()`).
- Lock order is parent then child: `fanin` locks itself, then a child. A child
  must never call back into a parent, or two threads can deadlock.
- Keep `pop_raw()` short and non-blocking. `listscanner`'s `getline` already
  holds the lock while waiting on stdin, and that blocks the whole stream.
- Return errors, do not throw. An exception escaping `pop_raw()` reaches a
  worker that has no handler, which ends in `std::terminate`.

## Hashing

`checksum_file()` in `src/checksum.cpp` maps the algorithm to an `EVP_MD`
(`to_evp()`), opens the file with `O_RDONLY | O_CLOEXEC`, and streams it through
`EVP_DigestInit_ex` / `EVP_DigestUpdate` / `EVP_DigestFinal_ex`. The read buffer
is a 64 KiB `thread_local` vector, so each worker allocates it once and reuses
it. `EINTR` is retried; every other failure is returned as a `std::error_code`
wrapped in `std::unexpected`, never as an exception. `algorithm::none` short-
circuits to `<NONE>` without touching the file.

Reading is done with plain `read()` calls, not `mmap` or `SEEK_DATA`, so a sparse
file is hashed over its logical length: holes are fed to the digest as zeros.
The cost therefore follows `st_size`, not the allocated block count, and memory
use stays at the 64 KiB buffer no matter how large the file claims to be.

The same `open()` is used for an explicitly named non-regular path, and it is
blocking: a FIFO with no writer blocks before the first read, and a device that
never reaches EOF keeps the worker inside the loop. Neither path observes the
algorithm switch, which is only looked at between files.

The digest is rendered with `to_hex()` in lowercase.

## Workers, progress and report

`work_manager` owns the worker vector, the progress thread, the atomic
algorithm, and three atomic counters. `run()` returns a `work_result` instead of
writing the report itself, so the caller decides where and how it is printed.

- `run(thread_count)` remembers the algorithm it was started with, installs the
  SIGINT handler, starts `thread_count` workers and the printer, and waits.
- Each worker loops: `tasks.pop()`, `checksum_file(path, alg.load())`, write one
  line through an `osyncstream`. A failed checksum prints `<NONE> <path>` and
  increments `failed`; a success increments `success`, or `unprocessed` when the
  current algorithm is `none`. A path that `pop()` rejected never gets here, so
  it has no line and no counter — only an error entry.
- The printer, unless quiet or running with `none`, repaints a single line every
  100 ms on stderr. It uses the terminal width of stderr and degenerates to a
  plain `[N files]` when the width is unknown.
- `wait()` joins the workers, sets the algorithm to `none` (which stops the
  printer loop), and joins the printer.
- When everything is joined, `run()` returns a snapshot: the algorithm it
  started with, the collected errors, the three counters, and the wall-clock
  time spent between starting and joining.

The constructor takes a `catch_sigint` flag, on by default. With it off, `run()`
installs no SIGINT handler and leaves `Ctrl-C` to the caller, which is what an
embedding driver wants.

`work_result` is a plain value with `alg`, `errors`, `success`, `failed`,
`unprocessed` and `duration`. `operator+` merges two results of the same
algorithm (errors appended, counters and durations added), which is there for a
future setup with more than one manager. `operator<<` writes the report block:
`Algorithm`, `Total Processed` (= succeeded + failed), the three counts,
`Duration`, and, when `errors` is not empty, an `Errors` count followed by the
`# Error messages:` list.

`tsubaki_main` prints `#` and the result (`*out << "#\n" << result`) and returns
0 when `errors` is empty. `failed` itself does not decide the exit code; every
checksum failure also leaves an error entry, so in practice the two agree.

`SIGINT` is implemented through the algorithm value, not through a separate
flag: the handler stores `algorithm::none` into the worker's atomic, and every
later `checksum_file()` call returns `<NONE>` immediately. Remaining paths are
still popped, printed and counted as unprocessed, so the report accounts for
everything the scanners produced. A worker already inside a blocking `open()` or
an endless read never returns to that check, so one `Ctrl-C` does not free it;
the handler also bumps an atomic counter, and on the fifth `Ctrl-C` it writes a
fixed message with `write(2, ...)` and calls `std::terminate()`.

## Output streams

`tsubaki_core` picks the sink once: `std::cout`, or a file opened by
`--output`. That single `std::ostream` is shared by every worker, the list
scanners (for `# Kept: <path>` markers) and the final report. Writes go through
`std::osyncstream`, which flushes complete insertions, so lines never interleave.

The progress indicator is the only thing written to stderr. Errors detected
while building the argument set (`cmdline` failures, an unopenable output file,
a missing list file) are written to stderr as well; errors found during the run
end up in the report block of the result stream.

## Concurrency model

| Thread | Count | Work |
| --- | --- | --- |
| main | 1 | builds the streams, then calls `run()`, which joins the workers |
| worker | `-t` | pop one path, hash it, write one line |
| printer | 1 | repaint the progress line every 100 ms |

- One mutex per taskstream. The mutex covers `pop_raw()` and the error vector;
  it is held only while a path is being taken out.
- Atomics: the algorithm, the three counters, the interrupt counter, and the
  signal target pointer. The pointer is `volatile` because the handler writes
  it; the algorithm and the interrupt counter are `std::atomic` because the
  handler stores into them.
- No lock is held while hashing, so file I/O is fully parallel.
- Nothing in the pipeline reorders results: output order is completion order,
  not scan order.

## Data flow example

```
tsubaki sha256 . -e build -l old.txt
```

1. `files` collects `.`; `-e build` is a separated value, read by `excludes`
   before the positional looks at anything, and `-l old.txt` becomes a list.
2. `.` becomes a `dirscanner` with `build` excluded; `old.txt` is opened and
   becomes a `listscanner`.
3. Both are added to the fan-in, plus an empty `cached_taskstream`.
4. Workers pull paths: first from the `dirscanner` (walking the tree), then from
   the `listscanner` once the directory is exhausted.
5. Kept lines from `old.txt` are written immediately as `# Kept: <path>` plus
   the original line; they never enter the hashing path.
6. When all children are exhausted, the fan-in returns nothing, the workers
   stop, the printer stops, and `run()` returns the `work_result` that
   `tsubaki_main` prints.

## One manager at a time

`work_manager` only needs a `basic_taskstream`, and most of the shared pieces
are already handled by the interface: `run()` returns a `work_result` that
`operator+` can merge, the caller prints it, and `catch_sigint = false` lets an
outer driver own the interrupt. Splitting a run across several managers is
therefore mostly bookkeeping:

- `-t` has to be divided between the managers;
- each manager paints its own progress line, so they would fight over the same
  terminal row, or need a shared printer;
- the inputs have to be partitioned, which is the real work: several managers
  over one shared fan-in would still serialise on its mutex;
- errors and the exit code come from the merged `work_result`.

None of that is hard, but none of it buys much either: the bottleneck is I/O and
hashing, and pulling a path is the only serialised step. One manager keeps the
program small, and `work_result::operator+` plus the signal switch leave the
door open for a caller that wants more.

## Extension points

- **New input source**: derive from `basic_taskstream`, implement `pop_raw()`,
  and add an entry to `def_args()` if it needs syntax of its own. The fan-in does
  not care how many producers exist.
- **New output sink**: `work_manager` is the only consumer; give it a different
  `std::ostream` or replace it, keeping `basic_taskstream` untouched.
- **New algorithm**: add one line to `ALGORITHM_LIST` in
  `include/checksum.hpp` (`algorithm`, `str_to_alg()` and `alg_to_str()` are
  generated from it) and a case in `to_evp()`.
- **New build target**: the API is deliberately narrow, so another executable
  (for example one that needs every checksum at once, like a comparison tool)
  can reuse `checksum_file()`, the scanners and the argument parser without
  sharing `work_manager` or the result format. That is a separate program, not
  a mode of `tsubaki`.

## Source map

### Classes

| Class | Base | Responsibility |
| --- | --- | --- |
| `tsubaki::basic_taskstream` | — | virtual base of every task stream: `pop()` filters the candidate, `pop_raw()` is the hook each subclass implements, `errors` + `report_error()` + `take_errors()` carry failures |
| `tsubaki::dirscanner` | `basic_taskstream` | recursive directory walk with excludes and symlink control |
| `tsubaki::listscanner` | `basic_taskstream` | parses a list file or stdin and writes `# Kept: <path>` lines |
| `tsubaki::cached_taskstream` | `basic_taskstream` | replays a fixed vector of paths |
| `tsubaki::fanin_taskstream` | `basic_taskstream` | merges child streams; owns them through `unique_ptr` |
| `tsubaki::work_manager` | — | worker pool, progress line, SIGINT switch |
| `tsubaki::work_result` | — | value snapshot returned by `run()`, with `operator+` and `operator<<` |
| `tsubaki::error_entry` | — | `std::error_code` plus the operation that failed |

### Files

| Path | Contents |
| --- | --- |
| `src/tsubaki_main.cpp` | entry point and the last-resort exception handler |
| `src/tsubaki_core.cpp` | startup sequence, argument checks, stream construction |
| `include/basic_taskstream.hpp`, `src/basic_taskstream.cpp` | the base stream and its `pop()` filter |
| `include/dirscanner.hpp`, `src/dirscanner.cpp` | directory walk |
| `include/listscanner.hpp`, `src/listscanner.cpp` | list-file and stdin parsing |
| `include/cached_taskstream.hpp`, `src/cached_taskstream.cpp` | fixed path replay |
| `include/fanin_taskstream.hpp`, `src/fanin_taskstream.cpp` | multi-source merge |
| `include/checksum.hpp`, `src/checksum.cpp` | `ALGORITHM_LIST`, `str_to_alg()` / `alg_to_str()`, EVP hashing |
| `include/worker.hpp`, `src/worker.cpp` | `work_result` and `work_manager` |
| `include/utilities.hpp`, `src/utilities.cpp` | string helpers, terminal width, fd guard |
| `include/cmdline.hpp` | argument entry definitions |
| `include/help.hpp`, `src/help.cpp` | help text |
| `LC-ArgParser` (fetched) | standalone argument parser, compiled in as a source file |

`src/kousei_main.cpp`, `src/timeline_fs.cpp` and `include/timeline_fs.hpp` are an
unfinished experiment (a later build target that is not enabled in
`CMakeLists.txt`). They are not part of the `tsubaki` source map.
