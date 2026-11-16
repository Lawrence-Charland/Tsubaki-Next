# Tsubaki-Next Architecture

[中文](ARCHITECTURE.zh-cn.md)

## Overview

The program is a pull-based pipeline. Producers implement one method that hands
out the next path; a single stream object merges the producers; a pool of
workers pulls paths from that stream and hashes them; one writer turns each
finished result into a line of output.

```
  argv ──> main ──> cmdline ──> sum_core ──> dirscanner      ─┐
                                         ├─> listscanner     ─┼─> fanin ─> work_manager ─> out
                                         └─> cached_taskstream ┘                ├─> printer (stderr)
                                                                                 └─> work_result ─> report
```

There is no intermediate "file list" object. Nothing is sorted, counted or
filtered after scanning: a path is hashed as soon as a worker gets it.

## Contents

- [Startup sequence](#startup-sequence)
- [Command line layer](#command-line-layer)
- [Task streams](#task-streams)
- [Thread-safety rules](#thread-safety-rules)
- [Hashing](#hashing)
- [Workers, progress and report](#workers-progress-and-report)
- [Output streams](#output-streams)
- [Concurrency model](#concurrency-model)
- [Exit codes](#exit-codes)
- [Data flow example](#data-flow-example)
- [One manager at a time](#one-manager-at-a-time)
- [Extension points](#extension-points)
- [Source map](#source-map)

## Startup sequence

`main()` (in `src/main.cpp`) owns the subcommand dispatch and everything that
is not specific to a subcommand:

1. `def_args()` builds an `argparser::ap_instance` from the entry definitions in
   `include/cmdline.hpp`; `pre_parse()` splits the arguments into styled tokens.
2. `evaluate(cmdline_group::general)` matches the `general` group: the `subcmd`
   positional, which takes exactly one word, plus the `--help` and `--version`
   flags. The program name is taken from `argv[0]`.
3. `--help` and `--version` are checked **before** validation. Both therefore
   work without a subcommand, and both win over every argument error. Help text
   comes from `help_text()` in `src/help.cpp`; the version string is the CMake
   project version, injected as `TSUBAKI_VERSION`.
4. `fine()` validates the `general` entries. A missing `subcmd` is reported
   here, followed by a hint to run `--help`.
5. The value of `subcmd` is compared with `subcommands::sum`. `sum` hands the
   preparsed instance to `tsubaki::sum_main()` and returns its exit code;
   anything else is rejected as an unknown subcommand with
   `TSUBAKI_INVALID_ARGUMENT`. The call sits in a try/catch that turns an
   escaped exception into an error message plus `TSUBAKI_UNEXPECTED`.

`sum_main(cmdline)` does the following, in order:

1. `evaluate(cmdline_group::sum)` matches the remaining arguments against the
   entries of the `sum` group; entries of the `general` group keep their
   results.
2. `fine(false)` runs the validation. Every failed entry is reported with
   `argparser::describe()`, followed by a hint to run `--help`.
3. Values are read out of the entries and checked: the algorithm is mapped
   through `str_to_alg()`; a keyed algorithm takes its key from `-k` or from
   `-K` (`get_key()` reads the file, or stdin for `-`, and drops trailing
   CR/LF), while a plain digest warns and ignores either option without opening
   the file; the thread count is parsed (falling back to
   `std::thread::hardware_concurrency()`), and the output stream is opened.
   `check_key_validity()` rejects a key that is empty or too long for the
   algorithm, and `-K -` together with a `-` input is rejected: stdin may be
   named only once.
   Algorithm and key end up in one `checksum_api`, which is passed to the
   workers instead of a bare `algorithm`.
4. Inputs are turned into producers and added to a `fanin_taskstream`: one
   `dirscanner` per directory, one `listscanner` per `-` or list file, and one
   `cached_taskstream` for the plain file arguments. The queue is built in that
   order: directories and `-` markers as they appear on the command line, then
   the cached files, then the `-l` lists. The cached stream is added even when
   it is empty, and an empty input list is legal: `files` has no minimum count,
   so nothing is a valid run that just prints an empty report.
5. `work_manager(tasks, *out, api, verbose).run(threads)` starts the workers and
   returns a `work_result`.
6. `sum_main` writes `#` and then calls `result.print(*out, true)` on the output
   stream, and maps the run onto an exit code: `TSUBAKI_INTERRUPTED` when the
   run was interrupted, `TSUBAKI_HASH_ERROR` when an error was recorded, and
   `TSUBAKI_SUCCESS` otherwise. Argument problems returned
   `TSUBAKI_INVALID_ARGUMENT` earlier, and an `--output` file that cannot be
   opened returns `TSUBAKI_FILE_WRITE_FAILED`.

## Command line layer

Parsing is done by `LC-ArgParser`
(<https://github.com/Lawrence-Charland/LC-ArgParser>), a separate C++17 library
fetched by CMake at configure time and compiled into the binary as a source
file, not linked as a shared library. Tsubaki-Next only touches its public interface
(`ap_instance`, `ap_entry`, `ap_defkey`); the library knows nothing about
checksums, and it can be reused as is by another program.

The parts of its behaviour that surface here:

- `pre_parse` tags every token as simple, single-dash, double-dash or
  `argv_zero`; everything after `--` becomes simple. A `-x` or `--foo` that
  matches no declared entry also stays simple, so a misspelled option is left to
  a positional — the subcommand slot, the algorithm, or the file list — and
  surfaces as an unknown subcommand, an unknown algorithm, or a missing file,
  not as an option error.
- Long names take `--name` or `--name=value`, short names take `-v`, bundled
  `-vs`, or a sticky value `-ovalue`. A separated value (`--output out.txt`) is
  supported by the library itself.
- An option collects the simple tokens that follow it until the next non-simple
  token or its value limit; flags only count occurrences. A `max_value_count`
  of `-1` means no limit at all, which is why every single-value option has to
  declare `1` explicitly.
- A positional collects simple tokens within its min/max range, and a barrier
  token ends the collection early and is consumed with it.
- `evaluate()` walks the entries in declaration order and erases what each one
  consumed, so the declaration order decides who sees a separated value.
  `evaluate(group)` does the same for one group only and leaves the entries of
  the other groups untouched.
- `fine()` and `errors()` summarise the per-entry results, optionally reporting
  whatever was left unconsumed as `invalid_arguments`.
- The library makes no compatibility promise; the vendored copy is pinned to a
  single commit.

Entries are defined once, in `include/cmdline.hpp`, and carry a group:
`general` for `subcmd`/`--help`/`--version`, `sum` for everything else. `main()`
evaluates `general`; `sum_main()` evaluates `sum`. Inside a group the declaration
order is the evaluation order: key-file, key, threads, output, the flags,
algorithm, excludes, lists, files for `sum`, and subcmd, version, help for
`general`. The
names are `inline constexpr` constants in `cmdline_entry`, so the definitions and
the `entry()` lookups share one spelling; a hyphen in the name becomes an
underscore in the identifier.

- `subcmd` is the first entry of the `general` group and a positional with a
  count of exactly one, so it takes the first plain argument whatever it is.
  That is why `tsubaki sha256 .` is rejected as an unknown subcommand instead of
  being read as an algorithm.
- `key-file`, `key`, `threads` and `output` each declare `max_value_count` `1`,
  so a separated value takes exactly one plain argument and leaves the rest to
  the later entries. Without that cap the library would hand them every plain
  token up to the next switch, and `-k secret file.txt` would take `file.txt`
  as part of the key.
- `excludes` and `lists` come before `files`, so their values are taken before
  the positional looks at anything, and a separated value (`-e build`) works as
  long as no file argument follows it.
- Neither entry has an upper bound on its value count, so `-e build .` takes `.`
  as a second exclude and leaves `files` empty.
- `files` is the last positional. It collects simple arguments until the `\`
  barrier; a plain argument after the barrier is left unconsumed, and
  `fine(false)` reports it as `invalid_arguments`.
- `-h`/`--help` is a flag, and the help check returns before validation, so
  `--help=something` still prints the help and exits 0: the parser records
  `flag_unexpected_values`, but `main` never asks. The same applies to
  `-V`/`--version`. A short name only compares its first character, so `-h1` is
  read as `-h`.

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
the next path". Two classes override `pop()`: `dirscanner`, which walks
directory entries and therefore skips every non-file silently instead of
recording an error for each subdirectory it passes — that is also what keeps a
directory walk from opening a FIFO or a device it happens to meet — and
`fanin_taskstream`, whose cursor logic is described below.

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
path that is not a directory makes the constructor throw
`std::invalid_argument`; that exception is not converted into an error entry —
it escapes `sum_main` and is caught by the last-resort handler in `main()`.

### listscanner

Reads lines from any `std::istream` — a file opened by `sum_core`, or
`std::cin` when `-` is used — and writes to the result stream when it decides to
keep a line. Each non-empty line is trimmed; lines starting with `#` are
ignored.

The line is split at the first space by `get_equation()`. If the first part is
the literal `<NONE>` — the marker this program writes for a file it did not hash
— the part after the space is the path. Otherwise, if the first part is not
entirely hexadecimal (`is_checksum()`), the whole line is a path, spaces
included. A line with no space in it never reaches that test, so a hex-only
name listed on its own is just a path. If the first part *is* hexadecimal, the
line is a `<checksum> <path>` pair:

- with `--force-rescan`, the path is handed out for hashing;
- otherwise the line is decoupled from the run: `# Kept: <path>` and the
  trimmed original line are written to the output, and the line contributes
  nothing to the run or to the report counts.

Taking an unrecognised line whole is what makes a space in a name harmless, and
it is what lets an `<NONE> <path>` output be fed back as input. The one
ambiguity left is a name that contains a space and whose first field is entirely
hexadecimal: it splits into a checksum and a path, and the only way to keep it
whole is to make the first field non-hexadecimal (`./name`, an absolute path).
The `<NONE> ` prefix is reserved and is always read as a marker.

A list line always names a file. A directory written on a list line is handed
out like any other path and then rejected by `pop()`; it is never walked. Only
directory arguments on the command line become a `dirscanner`.

### cached_taskstream

Holds a `std::queue` of paths and hands them out from the front, so plain file
arguments keep the order they had on the command line. It performs no existence
check of its own; invalid paths are filtered by `basic_taskstream::pop()`.

### fanin_taskstream

This is itself a taskstream, which is what lets the worker pool see a single
stream. `pop()` overrides the base version and looks at the child named by an
atomic cursor: if the child yields a path, that path is returned; if the child
is exhausted, the cursor is bumped with a compare-and-swap and the next child is
consulted. Children are never dropped, and their errors are not collected here:
`take_errors()` merges the fan-in's own list with every child's list, and
`work_manager::run()` calls it once the workers are joined.

`pop()` holds no lock of its own; the serialisation comes from the children,
whose `pop()` locks their own mutex. So while a child still yields, every worker
asking for a path queues on that one mutex. Only hashing runs in parallel. This
also means a `listscanner` blocked on `std::cin` blocks the stream, not just one
thread.

## Thread-safety rules

`pop_raw()` runs with `mutex_` held whenever the base `pop()` calls it
(`dirscanner` overrides `pop()` and takes the same lock itself), and `errors`
has no lock of its own.

- Accessing your own state inside `pop_raw()` needs no lock, but calling
  `report_error()`, `take_errors()`, `pop()` or `add_task()` from there will
  self-deadlock: the mutex is not recursive. Write to `errors` directly instead.
- `errors` may only be touched while the mutex is held (inside `pop_raw()`) or
  from a single thread (construction, or after `take_errors()`).
- A child must never call back into the parent's locking methods, or two threads
  can deadlock. `fanin` itself only locks in `add_task()`, not while pulling.
- Keep `pop_raw()` short and non-blocking. `listscanner`'s `getline` already
  holds the lock while waiting on stdin, and that blocks the whole stream.
- Return errors, do not throw. An exception escaping `pop_raw()` reaches a
  worker that has no handler, which ends in `std::terminate`.

## Hashing

`include/checksum.hpp` owns the algorithm list and the public entry points;
`src/checksum.cpp` owns the OpenSSL code. Both ends of the module start from
`ALGORITHM_LIST`, which generates `algorithm`, the name table, `str_to_alg()`
and `alg_to_str()`.

Three helpers keep the OpenSSL work in one place:

- `digest_for(algorithm)` maps a plain digest to an `EVP_MD` and returns
  `nullptr` for everything else.
- `mac_for(algorithm)` maps a keyed algorithm to a `mac_spec {name, digest}`.
  HMAC keeps its digest in a parameter (`{"HMAC", "SHA256"}`), while keyed
  BLAKE2 is a MAC algorithm of its own (`{"BLAKE2BMAC", nullptr}`).
- `stream_file(path, update)` opens the file with `O_RDONLY | O_CLOEXEC`,
  streams it in 64 KiB chunks through a `thread_local` vector, retries `EINTR`,
  and calls `update` on every chunk. `update` returning false becomes
  `operation_not_supported`.

`checksum_file()` runs that stream through `EVP_DigestInit_ex` /
`EVP_DigestUpdate` / `EVP_DigestFinal_ex`. `checksum_file_key()` runs the same
stream through `EVP_MAC_fetch` / `EVP_MAC_CTX_new` / `EVP_MAC_init` /
`EVP_MAC_update` / `EVP_MAC_final`. The MAC algorithm is fetched by name
instead of using a per-digest HMAC constructor, so one code path covers every
keyed algorithm, and `OSSL_MAC_PARAM_DIGEST` is only set when the spec names a
digest. A key is passed as raw bytes, never decoded.

Both functions return `std::unexpected(std::error_code)` on failure, never
throw: `errno` for the file, `operation_not_supported` for a crypto call that
rejects a chunk, `invalid_argument` for an algorithm the mapper does not know,
and `not_enough_memory` for a context allocation. `algorithm::none`
short-circuits to `<NONE>` without touching the file, in both. The digest or
MAC is rendered with `to_hex()` in lowercase.

`checksum_api` is the value that carries the choice from `sum_main` to the
workers: `alg` plus the optional `key`, and `calc(path)` as its only entry
point. `calc()` routes on `is_keyed(alg)`: a keyed algorithm goes to
`checksum_file_key()`, and everything else goes to `checksum_file()`.

The key policy lives in two `constexpr` helpers next to `ALGORITHM_LIST`:
`max_key_length()` returns 64 for `blake2b512_mac`, 32 for `blake2s256_mac` and
0 for "no upper bound", and `check_key_validity()` requires a non-empty key that
fits that limit (a plain digest accepts any key and ignores it). `sum_main`
calls it once, before any file is opened, and turns a failure into a specific
message plus exit status 2; `calc()` calls it again, so a caller that builds a
`checksum_api` by hand cannot skip the check and MAC with an impossible key.

Reading is done with plain `read()` calls, not `mmap` or `SEEK_DATA`, so a sparse
file is hashed over its logical length: holes are fed to the digest as zeros.
The cost therefore follows `st_size`, not the allocated block count, and memory
use stays at the 64 KiB buffer no matter how large the file claims to be.

The same `open()` is used for an explicitly named non-regular path, and it is
blocking: a FIFO with no writer blocks before the first read, and a device that
never reaches EOF keeps the worker inside the loop. Neither path observes the
interrupt flag, which is only looked at between files.

## Workers, progress and report

`work_manager` owns the worker vector, the progress thread, a `const
checksum_api api` (algorithm plus key), an `std::atomic<bool> interrupted`, and
three atomic counters. `run()` returns a `work_result` instead of writing the
report itself, so the caller decides where and how it is printed.

- `run(thread_count)` installs the SIGINT handler, starts `thread_count` workers
  and the printer, and waits.
- Each worker loops: `tasks.pop()`; if `interrupted` is already set, print
  `<NONE> <path>` and increment `unprocessed`; otherwise `api.calc(path)` and
  write one line through an `osyncstream`. A failed checksum prints
  `<NONE> <path>` and increments `failed`; a success increments `success`, or
  `unprocessed` when the algorithm is `none`. A path that `pop()` rejected never
  gets here, so it has no line and no counter — only an error entry.
- The printer, unless quiet or running with `none`, repaints a single line every
  100 ms on stderr while `interrupted` is false. It uses the terminal width of
  stderr and degenerates to a plain `[N files]` when the width is unknown.
- `wait()` joins the workers, sets `interrupted` (which stops the printer
  loop), and joins the printer.
- When everything is joined, `run()` returns a snapshot: the algorithm it
  started with, the collected errors, the three counters, the interrupt flag,
  and the wall-clock time spent between starting and joining.

The constructor takes a `catch_sigint` flag, on by default. With it off, `run()`
installs no SIGINT handler and leaves `Ctrl-C` to the caller, which is what an
embedding driver wants.

`work_result` is a plain value with `alg`, `errors`, `success`, `failed`,
`unprocessed`, `interrupted` and `duration`. `operator+` merges two results of
the same algorithm (errors appended, counters and durations added), which is
there for a future setup with more than one manager. `print(os, with_errors)`
writes the report block: `Algorithm`, `Processed` (= hashed + failed),
`Hashed`, `Failed`, `Not hashed`, `Duration`, an `Errors` count — written even
when it is 0 — and, for an interrupted run, a one-line note under the count.
When there are errors, the `# Error messages:` list follows. With
`with_errors = false` the count and the note are written but the per-entry lines
are left out.

`sum_main` writes `#` and calls `result.print(*out, true)`, then returns
`TSUBAKI_INTERRUPTED` if `interrupted` is set, `TSUBAKI_HASH_ERROR` if any error
was recorded, and `TSUBAKI_SUCCESS` otherwise. `failed` itself does not decide
the exit code; every checksum failure also leaves an error entry, so in practice
the two agree.

`SIGINT` is a separate flag, not the algorithm value: the handler stores `true`
into the worker's `std::atomic<bool> interrupted`, and each worker checks it
right after `tasks.pop()`. Remaining paths are still popped, printed and counted
as `Not hashed`, so the report accounts for everything the scanners produced; a
file already inside `api.calc()` finishes normally. A worker already inside a
blocking `open()` or an endless read never returns to that check, so one
`Ctrl-C` does not free it. The handler also bumps an atomic counter, and on the
fifth `Ctrl-C` it writes a fixed message with `write(2, ...)` and calls
`_exit(250)`. Any `Ctrl-C` sets `work_result::interrupted`, which adds a note to
the report block and makes `sum_main` return `TSUBAKI_INTERRUPTED` (130).

## Output streams

`sum_core` picks the sink once: `std::cout`, or a file opened by `--output`.
That single `std::ostream` is shared by every worker, the list scanners (for
`# Kept: <path>` markers) and the final report. Concurrent writes go through
`std::osyncstream`, which flushes complete insertions, so lines never
interleave; the report itself is written after the workers are joined, so it
goes out directly.

stderr carries three things: the progress indicator, the argument-setup failures
(`cmdline` errors and an output file that cannot be opened), and — when
`--output` is given — a second copy of the report block, printed with
`print(std::clog, false)` so that the per-entry error messages stay out of it.
Everything else lands in the report block of the result stream, including a list
file that cannot be opened.

Neither stream is checked. The workers' lines are emitted through
`std::osyncstream`, the report is written directly after the join, and the
`--output` file is closed by the `std::ofstream` destructor; a failed write sets
the stream state and is never inspected, so `sum_main` still returns
`TSUBAKI_SUCCESS`. Only `open()` is verified, before any work starts. `SIGPIPE`
keeps its default action, so a pipe whose reader went away kills the process
rather than surfacing as a write error.

## Concurrency model

| Thread | Count | Work |
| --- | --- | --- |
| main | 1 | builds the streams, then calls `run()`, which joins the workers |
| worker | `-t` | pop one path, hash it, write one line |
| printer | 1 | repaint the progress line every 100 ms |

- One mutex per taskstream. The mutex covers `pop_raw()` and the error vector;
  it is held only while a path is being taken out.
- Atomics: the `interrupted` flag, the three counters, the interrupt counter,
  and the signal target pointer. The pointer is `volatile` because the handler
  writes it; `interrupted` and the interrupt counter are `std::atomic` because
  the handler stores into them. The algorithm is not shared state: it lives in
  the manager's `const checksum_api` and never changes during a run.
- No lock is held while hashing, so file I/O is fully parallel.
- Nothing in the pipeline reorders results: output order is completion order,
  not scan order.

## Exit codes

The codes live in `include/exit_codes.hpp` and are shared by `main()` and
`sum_main()`:

| Macro | Value | Returned when |
| --- | --- | --- |
| `TSUBAKI_SUCCESS` | 0 | the run finished with no error entry |
| `TSUBAKI_HASH_ERROR` | 1 | at least one error entry was recorded |
| `TSUBAKI_INVALID_ARGUMENT` | 2 | the command line was rejected: unknown subcommand, unknown algorithm, a keyed algorithm without a key, `-K -` together with a `-` input, bad thread count, or leftover arguments |
| `TSUBAKI_FATAL` | 70 | `argc` is 0, so `main()` refuses to run (`EX_SOFTWARE`) |
| `TSUBAKI_FILE_WRITE_FAILED` | 74 | the `--output` file cannot be opened (`EX_IOERR`) |
| `TSUBAKI_FILE_READ_FAILED` | 114 | `--key-file` cannot be read or holds no key |
| `TSUBAKI_INTERRUPTED` | 130 | `work_result::interrupted` is set (`128 + SIGINT`) |
| `TSUBAKI_TERMINATED` | 250 | the fifth `Ctrl-C` called `_exit()` from the handler, so no report was written |
| `TSUBAKI_UNEXPECTED` | -1 | an exception escaped `main()`; the shell sees 255 |

`interrupted` wins over `errors`: an interrupted run reports `130` even when it
also collected errors. None of these codes describes a silent write failure;
`SIGPIPE` (141 in a shell) is the one write-side failure that is visible, and it
kills the process instead.

## Data flow example

```
tsubaki sum sha256 . -e build -l old.txt
```

1. `files` collects `.`; `-e build` is a separated value, read by `excludes`
   before the positional looks at anything, and `-l old.txt` becomes a list.
2. `.` becomes a `dirscanner` with `build` excluded; `old.txt` is opened and
   becomes a `listscanner`.
3. Both are added to the fan-in, plus an empty `cached_taskstream`.
4. Workers pull paths: first from the `dirscanner` (walking the tree), then from
   the `listscanner` once the directory is exhausted. Every path goes through
   `checksum_api::calc()`, so the same run shape works with `hmac_sha256` once
   `-k` supplied a key.
5. Kept lines from `old.txt` are written immediately as `# Kept: <path>` plus
   the original line; they never enter the hashing path.
6. When all children are exhausted, the fan-in returns nothing, the workers
   stop, the printer stops, and `run()` returns the `work_result` that
   `sum_main` prints.

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
program small, and `work_result::operator+` plus the interrupt flag leave the
door open for a caller that wants more.

## Extension points

- **New subcommand**: add a constant to `subcommands`, give its entries their
  own `cmdline_group`, add a branch to `main()`, and write a driver shaped like
  `sum_main()` that evaluates its own group. Only the entries of that group are
  parsed by the subcommand, so unrelated switches cannot interfere. Comparing
  several sets of results is the next feature planned this way; an experimental
  sketch of its data structure lives in `experiments/timeline_fs/` and is not
  built.
- **New input source**: derive from `basic_taskstream`, implement `pop_raw()`,
  and add an entry to `def_args()` if it needs syntax of its own. The fan-in does
  not care how many producers exist.
- **New output sink**: `work_manager` is the only consumer; give it a different
  `std::ostream` or replace it, keeping `basic_taskstream` untouched.
- **New algorithm**: add one line to `ALGORITHM_LIST` in
  `include/checksum.hpp`, which generates `algorithm`, `str_to_alg()` and
  `alg_to_str()`. A plain digest also needs a case in `digest_for()`; a keyed
  one needs a case in `mac_for()`, an enumerator in `is_keyed()`, a limit in
  `max_key_length()` when the algorithm caps the key, and its name in the
  algorithm list of the help text (`src/help.cpp`), which is written by hand.

## Source map

### Classes

| Class | Base | Responsibility |
| --- | --- | --- |
| `tsubaki::basic_taskstream` | — | virtual base of every task stream: `pop()` filters the candidate, `pop_raw()` is the hook each subclass implements, `errors` + `report_error()` + `take_errors()` carry failures |
| `tsubaki::dirscanner` | `basic_taskstream` | recursive directory walk with excludes and symlink control |
| `tsubaki::listscanner` | `basic_taskstream` | parses a list file or stdin and writes `# Kept: <path>` lines |
| `tsubaki::cached_taskstream` | `basic_taskstream` | replays a fixed queue of paths, in the order given |
| `tsubaki::fanin_taskstream` | `basic_taskstream` | merges child streams; owns them through `unique_ptr` |
| `tsubaki::checksum_api` | — | carries the chosen algorithm and key, and routes `calc()` to the digest or the MAC path |
| `tsubaki::work_manager` | — | worker pool, progress line, interrupt flag |
| `tsubaki::work_result` | — | value snapshot returned by `run()`, with `operator+` and `print()` |
| `tsubaki::error_entry` | — | `std::error_code` plus the operation that failed |

### Files

| Path | Contents |
| --- | --- |
| `src/main.cpp` | subcommand dispatch, `general` argument group, last-resort exception handler |
| `include/core_functions.hpp` | `sum_main()` declaration |
| `include/exit_codes.hpp` | the exit-code macros shared by `main()` and `sum_main()` |
| `src/sum_core.cpp` | `sum` argument group checks, startup sequence, stream construction |
| `include/basic_taskstream.hpp`, `src/basic_taskstream.cpp` | the base stream and its `pop()` filter |
| `include/dirscanner.hpp`, `src/dirscanner.cpp` | directory walk |
| `include/listscanner.hpp`, `src/listscanner.cpp` | list-file and stdin parsing |
| `include/cached_taskstream.hpp`, `src/cached_taskstream.cpp` | fixed path replay |
| `include/fanin_taskstream.hpp`, `src/fanin_taskstream.cpp` | multi-source merge |
| `include/checksum.hpp`, `src/checksum.cpp` | `ALGORITHM_LIST`, `str_to_alg()` / `alg_to_str()`, `is_keyed()`, `checksum_api`, `EVP_MD` and `EVP_MAC` hashing |
| `include/worker.hpp`, `src/worker.cpp` | `work_result` and `work_manager` |
| `include/utilities.hpp`, `src/utilities.cpp` | string helpers, key reading, terminal width, fd guard |
| `include/cmdline.hpp` | argument entry definitions |
| `include/help.hpp`, `src/help.cpp` | help text |
| `LC-ArgParser` (fetched) | standalone argument parser, compiled in as a source file |

`experiments/timeline_fs/` is not part of the `tsubaki` source map: it holds an
experimental sketch for comparing several sets of results
(`include/timeline_fs.hpp`, `src/timeline_fs.cpp`, and the scratch driver
`src/tmptest.cpp`). No CMake target references it, so it is neither compiled nor
tested.
