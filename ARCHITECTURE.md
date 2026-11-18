# Tsubaki-Next Architecture

[中文](ARCHITECTURE.zh-cn.md)

## Overview

The program is a pull-based pipeline. Producers implement one method that hands
out the next path; a single stream object merges the producers; a pool of
workers pulls paths from that stream and hashes them; each worker writes its
finished result as one line of output.

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
- [Verification](#verification)
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
   positional, which takes exactly one word, plus the `--help`, `--version` and
   `--algorithm` flags. The program name is taken from `argv[0]`.
3. `--help`, `--version` and `--algorithm` are checked **before** validation, in
   that order. All three therefore work without a subcommand, and all three win
   over every argument error. Help text comes from `help_text()` in
   `src/help.cpp`, and `--algorithm` prints the table built by
   `list_algorithm()` there from the same `ALGORITHM_LIST` macro the hashing
   code uses. The version string is the CMake project version, injected as
   `TSUBAKI_VERSION`.
4. `fine()` validates the `general` entries. A missing `subcmd` is reported
   here, followed by a hint to run `--help`.
5. The value of `subcmd` is compared with `subcommands::sum` and
   `subcommands::verify`. `sum` hands the preparsed instance to
   `tsubaki::sum_main()` and returns its exit code; `verify` does the same with
   `tsubaki::verify_main()`. Anything else is rejected as an unknown subcommand
   with `TSUBAKI_INVALID_OPERATION`. The call sits in a try/catch that turns an
   escaped exception into an error message plus `TSUBAKI_UNEXPECTED`.

`sum_main(cmdline, internal)` does the following, in order:

1. `evaluate(cmdline_group::sum)` matches the remaining arguments against the
   entries of the `sum` group; entries of the `general` group keep their
   results. When `internal` is true — the invocation built by `verify` — the
   hidden `sum_int` group is evaluated too, which is where the `--verify` flag
   lives.
2. `fine(false)` runs the validation. Every failed entry is reported with
   `argparser::describe()`, followed by a hint to run `--help`.
3. Values are read out of the entries and checked: the algorithm is mapped
   through `str_to_alg()`; a keyed algorithm takes its key from `-k` or from
   `-K` (`get_file_content()` reads the file, or stdin for `-`, and drops
   trailing CR/LF), while a plain digest warns and ignores either option
   without opening the file; the thread count is parsed from `-t` (`std::stoi`
   throwing, or a non-positive result, is an error) or taken from
   `std::thread::hardware_concurrency()` when `-t` is absent, falling back to 1
   if that reports 0; and the output stream is opened.
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
5. `work_manager(tasks, *out, api, verify, verbose, true).run(threads)` starts
   the workers and returns a `work_result`; `verify` puts the manager in
   verification mode, and the last argument keeps SIGINT handling local.
6. `sum_main` writes `#`, calls `result.print(*out, true)`, then flushes the
   stream and checks `fail()`: a failed write returns
   `TSUBAKI_FILE_WRITE_FAILED`. Otherwise it maps the run onto an exit code:
   `TSUBAKI_INTERRUPTED` when the run was interrupted, `TSUBAKI_HASH_ERROR`
   when an error was recorded, and `TSUBAKI_SUCCESS` otherwise. Argument
   problems returned `TSUBAKI_INVALID_OPERATION` earlier, and an `--output` file
   that cannot be opened also returns `TSUBAKI_FILE_WRITE_FAILED`.

## Verification

`verify` is a thin driver over the `sum` pipeline, not a second pipeline:

1. `verify_main()` evaluates the `verify` group: its own positional checksum
   file plus `-v`, `-t`, `-k`, `-K` and `-o`. These options keep the same names
   as in `sum` but are separate entries logically (see 
   [Command line layer](#command-line-layer)), so a `sum` option cannot leak in.
2. It takes the algorithm from the first `# <key>: <value>` comment that names
   `Algorithm` (`get_comment_value()` in `src/utilities.cpp`). An input that is
   missing or is not a regular file exits 66. The header is read by opening the
   file, so a file that passes the type check but cannot be opened looks like a
   missing header and exits 64, as do an unknown algorithm and `none`. `none`
   is refused because such a listing carries no checksum to compare.
3. It refuses an `-o` that resolves to the input file: the input would be
   truncated before the listing is read.
4. It rebuilds an internal `sum` command line — the algorithm, `-l <input>`,
   `-t`, `-k`, `-K`, `-o` and `-v` as they were given, `--force-rescan` and the
   hidden `--verify` flag — and calls `sum_main(instance, true)`. The flag lives
   in the `sum_int` group, so a user cannot pass it to `sum`. Without `-o` the
   output defaults to `<input>.verify`, and a notice naming that file goes to
   stderr; `-o -` drops the `-o` entry, so `sum` writes to stdout instead.
5. `listscanner` sees `rescan` set and fills `task_token::sum` from the
   checksum field of a `<checksum> <path>` line. A bare path or
   `<NONE> <path>` leaves `sum` empty.
6. Each worker compares its result with `token.sum` when the manager is in
   verification mode. An empty `sum` is collected in `unsure`; so is any token
   whose line in the refreshed listing is `<NONE>` — one skipped because the run
   was already interrupted, one whose hashing failed — because such a line is
   exactly what gives the next run nothing to compare. A mismatch is collected
   in `changed`. The comparison ignores case, because `is_checksum()` accepts
   uppercase hex. Both vectors are merged under the manager mutex and printed by
   `work_result::print()`.
7. A changed or unsure entry does **not** change the exit status: `sum_main`
   still returns 1 only for recorded errors, so `verify` reports differences
   without failing the process. The output is a refreshed listing, with a new
   checksum for every path that could be hashed, so verifying that output again
   reports `Changed: 0` as long as the files did not change in the meantime. A
   path that could not be hashed stays `<NONE>`, so it is unsure here and on the
   next run; a path rejected before hashing has no line in the output at all.

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

Entries are defined once, in `include/cmdline.hpp`, and carry one or more
groups: the pinned library revision keeps the groups of an entry in a set, so a
single declaration can join several. `general` holds `subcmd`, `--help`,
`--version` and `--algorithm`; `key-file`, `key`, `threads`, `output` and
`verbose` belong to both `sum` and `verify`, because they mean the same thing in
both subcommands; the rest of `sum` is excludes, lists, follow-symlinks,
force-rescan, algorithm and files; the hidden `sum_int` group holds only
`--verify`; and `verify` adds only its positional `to_verify`. `main()`
evaluates `general`, `sum_main()` evaluates `sum` (plus `sum_int` for the
internal call), and `verify_main()` evaluates `verify`. Inside a group the
declaration order is the evaluation order: key-file, key, threads, output,
verbose, excludes, lists, follow-symlinks, force-rescan, algorithm, files for
`sum`; key-file, key, threads, output, verbose, to_verify for `verify`; and
subcmd, help, version, list_alg for `general`. The names are `inline constexpr`
constants in `cmdline_entry`, so the definitions and the `entry()` lookups share
one spelling; a hyphen in the name becomes an underscore in the identifier.

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
- `files` is the last positional. It has no barrier and no upper limit, so it
  takes every remaining simple argument.
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
virtual std::optional<task_token> pop();      // public entry
virtual std::optional<task_token> pop_raw() = 0;  // protected
void report_error(const error_entry &);
std::vector<error_entry> take_errors();
```

A `task_token` carries the path being worked on in `p` and a `sum` field.
Streams that simply walk files leave `sum` empty and fill only `p`, so the path
returned is exactly `token->p`. `listscanner` is the exception: in rescan mode
it fills `sum` from the checksum field of a `<checksum> <path>` line, which is
what a `verify` run compares against.

`pop()` is the only method workers call. It takes the mutex, calls `pop_raw()`,
releases the mutex, and then inspects the candidate outside the lock (always
through candidate `p`):

- a regular file is returned;
- `status()` setting an error code — a missing path, a dangling symlink —
  records that error and the loop continues;
- a directory is recorded as `operation_not_permitted` and the loop continues;
- a socket is recorded the same way: no amount of reading produces a checksum
  for one, so it is refused instead of being handed to a worker;
- anything else that exists — a character or block device, a FIFO — is
  returned as it is, and opening or reading it is left to the worker.

This filter lives in the base class, so producers only have to answer "what is
the next path". Two classes override `pop()`: `dirscanner`, which walks
directory entries and hands out regular files only; a directory, a device, a
FIFO or a socket met during a walk is skipped silently rather than recorded as
an error, because a walk is not an explicit request for a stream. That is what
keeps `sum sha256 ~` from blocking on a FIFO somebody left behind. The other is
`fanin_taskstream`, whose cursor logic is described below.

A path rejected here (missing, unstatable, a socket, or a directory in a list)
never gets past `pop()`, so it is not printed and not counted in any of the
three counters;
it only leaves an `error_entry`, which is enough to make the exit code 1. That
is why `Failed` can be 0 while the run still fails: `Failed` only counts files
whose checksum attempt returned an error.

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

`pop()` replaces the base-class type check with a stricter one: only regular
files come back, so a FIFO, a socket or a device in the tree is never handed to
a worker and cannot block one in `open()`. The check uses `status()`, which
follows symbolic links, so a link to a regular file is hashed while a link to a
directory or to a special file is skipped. A path whose type cannot be
determined is recorded as an error instead of being passed on.

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
makes the run exit 1 even though scanning continues. `sum_core` constructs a
`dirscanner` only after `is_directory()` said yes, so a missing path or a path
that is not a directory never reaches the constructor: it becomes a file entry
and `pop()` reports it. The constructor's own guard still throws
`std::invalid_argument`, but only for a race (the directory disappearing in
between) or a failing `exists()`; that exception is not converted into an error
entry — it escapes `sum_main` and is caught by the last-resort handler in
`main()`.

### listscanner

Reads lines from any `std::istream` — a file opened by `sum_core`, or
`std::cin` when `-` is used — and writes to the result stream when it decides to
keep a line. Each non-empty line is trimmed; lines starting with `#` are
ignored.

The line is split at the first space by `get_equation()`. If the first part is
the literal `<NONE>` — the marker this program writes for a file it did not hash
— the part after the space is the path. Otherwise, if the first part is not at
least 32 characters of hexadecimal (`is_checksum()`), the whole line is a path,
spaces included. A line with no space in it never reaches that test, so a
hex-only name listed on its own is just a path. If the first part *is*
hexadecimal, the line is a `<checksum> <path>` pair:

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
  `report_error()` or `pop()` from there self-deadlocks: the mutex is not
  recursive. `take_errors()` takes no lock, so it does not deadlock, but it
  races with a concurrent `report_error()`. Write to `errors` directly instead.
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
calls it once, before any file is opened, on the key from either `-k` or `-K`,
and turns a failure into a generic message plus exit status 64. `calc()` does not
re-check: keeping a valid key is the contract of `checksum_api`, and a caller
that builds one by hand must honour it.

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
checksum_api api` (algorithm plus key), an `std::atomic<bool> interrupted`,
three atomic counters, and — in verification mode — a `changed` and an `unsure`
vector filled under a mutex. `run()` returns a `work_result` instead of writing
the report itself, so the caller decides where and how it is printed.

- `run(thread_count)` installs the SIGINT handler (when `catch_sigint` is on),
  starts `thread_count` workers and the printer, and waits.
- Each worker loops: `tasks.pop()`; if `interrupted` is already set, print
  `<NONE> <path>` and increment `unprocessed` — in verification mode the token
  also goes to `unsure`; otherwise `api.calc(token.p)` and write one line
  through an `osyncstream`. A failed checksum prints `<NONE> <path>`, increments
  `failed` and, again in verification mode, goes to `unsure` as well. A success
  increments `success`, or `unprocessed` when the algorithm is `none`, and in
  verification mode goes to `unsure` when the token carried no checksum and to
  `changed` when the new digest differs from it. A path that `pop()` rejected
  never gets here, so it has no line and no counter — only an error entry.
- The printer, unless quiet or running with `none`, repaints a single line every
  100 ms on stderr while `interrupted` is false. It uses the terminal width of
  stderr and degenerates to a plain `[N files]` when the width is unknown.
- `wait()` joins the workers, sets `interrupted` (which stops the printer
  loop), and joins the printer.
- When everything is joined, `run()` returns a snapshot: the algorithm and the
  verify flag it started with, the collected errors, `changed` and `unsure`, the
  three counters, the interrupt flag, and the wall-clock time spent between
  starting and joining.

The constructor takes the stream, the sink, the `checksum_api`, the `verify`
flag, a `verbose` flag (off makes the progress line a no-op), and a
`catch_sigint` flag; `sum_main` passes `true` for the last one. With it off,
`run()` installs no SIGINT handler and leaves `Ctrl-C` to the caller, which is
what an embedding driver wants.

`work_result` is a plain value with `alg`, `verify`, `errors`, `changed`,
`unsure`, `success`, `failed`, `unprocessed`, `interrupted` and `duration`;
nothing in it refers back to the manager, so two results of the same algorithm
could be merged by appending the vectors and adding the counters and durations.
`print(os, in_details)` writes the report block: `Algorithm`, `Processed` (=
hashed + failed), `Hashed`, `Failed`, `Not hashed`, `Duration`, then `Changed`
and `Unsure` when `verify` is set, an `Errors` count — written even when it is
0 — and, for an interrupted run, a `#` line and a
`# Warning: The process was interrupted halfway through.` line under the count.
When there are errors, the `# Error messages:` list follows; in a verify run the
changed and unsure paths are listed under their counts. With `in_details =
false` the counts and the note are written, but those lists and the error lines
are left out.

`sum_main` writes `#`, calls `result.print(*out, true)`, flushes the stream and
checks `fail()` (returning `TSUBAKI_FILE_WRITE_FAILED` on failure), then returns
`TSUBAKI_INTERRUPTED` if `interrupted` is set, `TSUBAKI_HASH_ERROR` if any error
was recorded, and `TSUBAKI_SUCCESS` otherwise. `failed` itself does not decide
the exit code; every checksum failure also leaves an error entry, so in practice
the two agree.

`SIGINT` is a separate flag, not the algorithm value: the handler stores `true`
into the worker's `std::atomic<bool> interrupted`, and each worker checks it
right after `tasks.pop()`. Remaining paths are still popped, printed and counted
as `Not hashed` — and in a verify run also as `unsure`, because their `<NONE>`
lines give the next run nothing to compare — so the report accounts for
everything the scanners produced; a file already inside `api.calc()` finishes
normally. A worker already inside a blocking `open()` or an endless read never
returns to that check, so one `Ctrl-C` does not free it. The handler also bumps
an atomic counter, and on the fifth `Ctrl-C` it writes a fixed message with
`write(2, ...)` and calls `_exit(250)`. Any `Ctrl-C` sets
`work_result::interrupted`, which adds a note to the report block and makes
`sum_main` return `TSUBAKI_INTERRUPTED` (130).

## Output streams

`sum_core` picks the sink once: `std::cout`, or a file opened by `--output`.
That single `std::ostream` is shared by every worker, the list scanners (for
`# Kept: <path>` markers) and the final report. Concurrent writes go through
`std::osyncstream`, which flushes complete insertions, so lines never
interleave; the report itself is written after the workers are joined, so it
goes out directly.

stderr carries everything that is not part of the listing: the progress
indicator; argument and input problems (`cmdline` errors, an output file that
cannot be opened, a `verify` input without a usable header); warnings (a key
given to a plain digest, a `hardware_concurrency()` that returned 0); the
`verify` notice naming the default `<input>.verify`; the notice printed when the
final write check fails; and — when the report goes to a file — a second copy of
it, printed with `print(std::clog, false)` so that the error messages and path
lists stay out. Everything else lands in the report block of the result stream,
including a list file that cannot be opened.

The result stream is checked at the end; stderr is not. The workers' lines are
emitted through `std::osyncstream`, the report is written directly after the
join, and `sum_main` then flushes the stream and checks `fail()`: a flush that
failed — a full disk, a broken descriptor, a file on a removed mount — leaves
the stream failed and returns `TSUBAKI_FILE_WRITE_FAILED` (73). The `--output`
file is still closed by the `std::ofstream` destructor, and that close error is
not inspected, but a write failure has already shown up in the flush. `SIGPIPE`
keeps its default action, so a pipe whose reader went away kills the process
(141 in a shell) before the `73` path is reached. Opening the `--output` file
itself is checked up front, before any work starts.

## Concurrency model

| Thread | Count | Work |
| --- | --- | --- |
| main | 1 | builds the streams, then calls `run()`, which joins the workers |
| worker | `-t` | pop one path, hash it, write one line (and compare it in verify mode) |
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

The codes live in `include/exit_codes.hpp` and are shared by `main()`,
`sum_main()` and `verify_main()`:

| Macro | Value | Returned when |
| --- | --- | --- |
| `TSUBAKI_SUCCESS` | 0 | the run finished with no error entry; changed and unsure files from `verify` are reported but are not errors |
| `TSUBAKI_HASH_ERROR` | 1 | at least one error entry was recorded |
| `TSUBAKI_INVALID_OPERATION` | 64 | the command line was rejected: unknown subcommand, unknown algorithm, a keyed algorithm without a key or with a key too long for it, `-k` together with `-K`, `-K -` together with a `-` input, bad thread count, or leftover arguments; `verify` also uses it for a listing without a readable `Algorithm:` header, one written with `none`, or an output that is the input (`EX_USAGE`) |
| `TSUBAKI_FILE_READ_FAILED` | 66 | `--key-file` cannot be read, the key file holds no key, or the `verify` input is missing or is not a regular file (`EX_NOINPUT`) |
| `TSUBAKI_FATAL` | 70 | `argc` is 0, so `main()` refuses to run (`EX_SOFTWARE`) |
| `TSUBAKI_FILE_WRITE_FAILED` | 73 | the `--output` file cannot be opened, or the result stream fails (`EX_CANTCREAT`) |
| `TSUBAKI_INTERRUPTED` | 130 | `work_result::interrupted` is set (`128 + SIGINT`) |
| `TSUBAKI_TERMINATED` | 250 | the fifth `Ctrl-C` called `_exit()` from the handler, so no report was written |
| `TSUBAKI_UNEXPECTED` | 255 | an exception escaped `main()` |

`interrupted` wins over `errors`: an interrupted run reports `130` even when it
also collected errors. A write failure returns `73`; when the result goes to a
pipe whose reader is gone, `SIGPIPE` kills the process first (a shell reports
141).

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
   `-k` supplies a key.
5. Kept lines from `old.txt` are written immediately as `# Kept: <path>` plus
   the original line; they never enter the hashing path.
6. When all children are exhausted, the fan-in returns nothing, the workers
   stop, the printer stops, and `run()` returns the `work_result` that
   `sum_main` prints.

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

### Types

| Type | Kind | Base | Responsibility |
| --- | --- | --- | --- |
| `tsubaki::basic_taskstream` | class | — | virtual base of every task stream: `pop()` filters the candidate, `pop_raw()` is the hook each subclass implements, `errors` + `report_error()` + `take_errors()` carry failures |
| `tsubaki::dirscanner` | class | `basic_taskstream` | recursive directory walk with excludes and symlink control |
| `tsubaki::listscanner` | class | `basic_taskstream` | parses a list file or stdin and writes `# Kept: <path>` lines, or fills `task_token::sum` in rescan mode |
| `tsubaki::cached_taskstream` | class | `basic_taskstream` | replays a fixed queue of paths, in the order given |
| `tsubaki::fanin_taskstream` | class | `basic_taskstream` | merges child streams; owns them through `unique_ptr` |
| `tsubaki::work_manager` | class | — | worker pool, progress line, interrupt flag, and the `changed`/`unsure` vectors of a verify run |
| `tsubaki::work_result` | class | — | value snapshot returned by `run()`, with `print()`; it holds no reference to the manager, so two of them can be merged |
| `tsubaki::checksum_api` | struct | — | carries the chosen algorithm and key, and routes `calc()` to the digest or the MAC path |
| `tsubaki::task_token` | struct | — | the path a worker processes plus the checksum read from the listing, empty outside rescan mode |
| `tsubaki::error_entry` | struct | — | `std::error_code` plus the operation that failed |
| `tsubaki::fd_guard` | class | — | closes a file descriptor on destruction; used by the hashing code |
| `tsubaki::algorithm` | enum class | — | the algorithm list generated from `ALGORITHM_LIST`, plus `error` for a failed `str_to_alg()` |
| `tsubaki::mac_spec` | struct | — | internal to `src/checksum.cpp`: the `EVP_MAC` name and the digest parameter, if any |

### Files

| Path | Contents |
| --- | --- |
| `src/main.cpp` | subcommand dispatch, `general` argument group, last-resort exception handler |
| `include/core_functions.hpp` | `sum_main()` and `verify_main()` declarations |
| `include/exit_codes.hpp` | the exit-code macros shared by `main()`, `sum_main()` and `verify_main()` |
| `src/sum_core.cpp` | `sum` argument group checks, startup sequence, stream construction |
| `src/verify_core.cpp` | `verify` argument group, header lookup, internal `sum` invocation |
| `include/basic_taskstream.hpp`, `src/basic_taskstream.cpp` | the base stream and its `pop()` filter |
| `include/dirscanner.hpp`, `src/dirscanner.cpp` | directory walk |
| `include/listscanner.hpp`, `src/listscanner.cpp` | list-file and stdin parsing |
| `include/cached_taskstream.hpp`, `src/cached_taskstream.cpp` | fixed path replay |
| `include/fanin_taskstream.hpp`, `src/fanin_taskstream.cpp` | multi-source merge |
| `include/checksum.hpp`, `src/checksum.cpp` | `ALGORITHM_LIST`, `str_to_alg()` / `alg_to_str()`, `is_keyed()`, `checksum_api`, `EVP_MD` and `EVP_MAC` hashing |
| `include/worker.hpp`, `src/worker.cpp` | `work_result` and `work_manager` |
| `include/utilities.hpp`, `src/utilities.cpp` | string helpers, key reading, comment lookup, terminal width, fd guard |
| `include/cmdline.hpp` | argument entry definitions |
| `include/help.hpp`, `src/help.cpp` | help text and the `--algorithm` table |
| `LC-ArgParser` (fetched) | standalone argument parser, compiled in as a source file |

`experiments/timeline_fs/` is not part of the `tsubaki` source map: it holds an
experimental sketch for comparing several sets of results
(`include/timeline_fs.hpp`, `src/timeline_fs.cpp`, and the scratch driver
`src/tmptest.cpp`). No CMake target references it, so it is neither compiled nor
tested.
