# Tsubaki-Next Architecture

[中文](ARCHITECTURE.zh-cn.md)

This document is about tsubaki itself. The contract a provider module has to
keep is [PROVIDER-ABI.md](PROVIDER-ABI.md).

## Overview

`sum` and `verify` are a pull-based pipeline: producers hand out paths, a
fan-in merges them, workers hash them, and each worker writes its result as one
line.

```
  argv ──> main ──> cmdline ──> sum_core ──> dirscanner      ─┐
                                         ├─> listscanner     ─┼─> fanin ─> work_manager ─> out
                                         └─> cached_taskstream ┘                ├─> printer (stderr)
                                                                                 └─> work_result ─> report
```

No intermediate "file list" object exists: a path is hashed as soon as a worker
takes it, so output order is completion order.

`cmp` does not use that pipeline. It reads two listings into memory and decides
once both are loaded:

```
  argv ──> main ──> cmdline ──> cmp_core ──> listscanner ×2 ──> entries ──> compare ──> out
```

`algo-info` has no pipeline at all. It resolves one name and prints it:

```
  argv ──> main ──> cmdline ──> algo_info_core ──> get_info_builtin()
                                               └─> dpf_instance ──> provider::algo_info
                                                                    └─> provider::arg_def
```

`provider::algo_info` is the module's `tsubaki_algo_info()`; `provider::arg_def`
is its `tsubaki_arg_definition()`, printed verbatim under an `ArgParser script:`
line, and only asked for when `tsubaki_need_more_args()` says yes.

All four drivers live in one executable: a feature is a subcommand, not a second
binary.

## Contents

- [Startup and dispatch](#startup-and-dispatch)
- [Verification](#verification)
- [Comparison](#comparison)
- [Command line layer](#command-line-layer)
- [Task streams](#task-streams)
- [Thread-safety rules](#thread-safety-rules)
- [Hashing](#hashing)
- [Workers, progress and report](#workers-progress-and-report)
- [Output streams](#output-streams)
- [Concurrency model](#concurrency-model)
- [Exit codes](#exit-codes)
- [Data flow example](#data-flow-example)
- [Extension points](#extension-points)
- [Build system](#build-system)
- [Source map](#source-map)

## Startup and dispatch

`main()` (`src/main.cpp`) parses the `general` group — the `subcmd` positional
plus `--help`, `--version`, `--algorithm` and `--completions` — from `def_args()`
in `include/cmdline.hpp`. The four flags are handled before validation, so they
need no subcommand and win over argument errors. After `fine()` validates
`general`, `subcmd` selects one of `sum_main()`, `verify_main()`,
`cmp_main()` or `algo_info_core()`; anything else exits 64. The dispatch sits in a try/catch that maps
an escaped exception to 255.

`sum_main(cmdline, internal)` evaluates the `sum` group (`sum_int` too when
`internal` is set, which is how `verify` passes its hidden flag), validates the
entries, reads algorithm/key/threads/output, turns each input into a producer,
and runs `work_manager`. It then prints the report and maps the run to an exit
code: 130 when interrupted, 1 when an error was recorded, 0 otherwise.

## Verification

`verify_main()` is a thin driver over `sum`, not a second pipeline:

- The algorithm comes from `-u`/`--use-algo` when given (an unknown name exits
  64), otherwise from the first `# Algorithm:` comment (`get_comment_value()`).
  A missing input or a directory exits 66; any other existing path is read, so
  a FIFO or `/dev/fd/N` works, and `-` names stdin. Stdin cannot be prefetched
  and then read again, so a `-` input requires `-u` (64 without it). A missing
  header or an unknown algorithm in the header exits 64, and so does a header
  naming `none`. A `-u none` is accepted and simply hashes nothing.
- It rebuilds an internal `sum` command line — the resolved algorithm, `-l
  <input>`, `--force-rescan` and the hidden `--verify` flag, plus `-t`, `-k`,
  `-K`, `-o`, `-v` as given — and calls `sum_main(instance, true)`. `-u` is not
  forwarded because it is already resolved. Without `-o` the output is
  `<input>.verify`, or `stdin.verify` for a `-` input; an `-o` naming the input
  is refused (64).
- `listscanner` fills `task_token::sum` from each `<checksum> <path>` line.
  Workers compare case-insensitively: an empty or `<NONE>` token goes to
  `unsure`, a mismatch to `changed`. A path the run does not hash — interrupted,
  failed, or `none` — also goes to `unsure`. Neither count changes the exit
  status, and the refreshed listing can be verified again.

## Comparison

`cmp_main()` never hashes; it decides only after both listings are read. It
shares nothing with the pipeline but `listscanner`, and could live in a separate
binary; it stays in the same target only to avoid a second thing to maintain.

- It evaluates the `cmp` group (`--canonical`, `to_compare`, `-o`) and chooses
  the sink first: stdout for `-o -`, otherwise the named file, or
  `<left-stem>_to_<right-stem>.cmp` in `.` when `-o` is absent (announced on
  stderr). An output that cannot be opened exits 73, and one naming either
  input is refused (64): both lists are read after the sink is opened.
- Each list goes through `listscanner` with `rescan = true` and
  `trust = true`: checksums are taken from the listing, the file-type filter is
  off because a path may have been deleted, and a line without a checksum
  becomes an error entry. A `*` right after the separator is the binary marker
  of `sha256sum --binary` and is dropped. At most one side may be stdin; an
  unreadable list exits 66.
- Checksums are lowercased; nothing else identifies them, so two listings
  written with different algorithms or keys share no values. With `-c`, each
  path is replaced by `std::filesystem::canonical()`; if the path doesn't exist, 
  the uncanonicalized path will join the comparison and an error will be reported.
- Duplicates of one path on one side are dropped; two different checksums for
  one path on one side exits 64. A pass over path-sorted entries then pairs the
  same path on both sides as `[!]` (modified) or `[=]` (matched). A second pass
  over the checksum-sorted leftovers emits `[U][L]`/`[U][R]` for runs held by
  one side and one numbered `[D][n]` group per checksum shared by both sides.
- Sections are written as modified, moved, deleted/added, matched, then `[E]`
  errors. The stream is flushed and checked like `sum`; recorded errors exit 1.

## Command line layer

Parsing is done by `LC-ArgParser`, a standalone C++17 library fetched by CMake
and compiled in as a source file. Tsubaki-Next only uses `ap_instance`,
`ap_entry` and `ap_defkey`; `include/cmdline.hpp` only describes this program's
command line.

- `pre_parse()` tags tokens. An unmatched `-x`/`--foo` is `simple` and is
  re-tokenized by `reparse()`; a word after `--` is `forced_simple` and is
  skipped there as it stands, so it stays a positional for good. A misspelled
  option therefore reaches a positional instead of becoming an option error,
  and nothing after `--` is re-read as an option once a provider registers.
- Long options take `--name=value`, short ones take `-ovalue`; a separated value
  is collected up to the next switch or the entry's value limit, so
  single-value options declare `max_value_count = 1` explicitly.
- `evaluate()` processes entries in declaration order and erases what each one
  consumed, so that order decides who sees a separated value;
  `evaluate(group)` restricts this to one group.

Entries in `include/cmdline.hpp` may join several groups: `general` holds
`subcmd`, `--help`, `--version`, `--algorithm`, `--completions`; `key-file`,
`key`, `text`,
`threads` and `verbose` belong to `sum` and `verify`; `output` joins `sum`,
`verify` and `cmp`; `sum` also has excludes, lists, follow-symlinks,
force-rescan, algorithm
and files; `sum_int` holds the internal `verify_sumint` flag; `verify` adds
`to_verify` and `use_algo`; `cmp` adds `canonical` and `to_compare`. Each
driver evaluates only its own group.

Consequences worth knowing: `subcmd` takes the first plain argument, so
`tsubaki sha256 .` is an unknown subcommand; `-e`/`-l` consume values before
`files`, so a separated value must not be followed by file arguments; `--help`
wins over validation; and a short name only compares its first character. `-`
can be an input source in `files` or a value of `-l`, and both end up as a
`listscanner` reading stdin, so `-` and `-l -` are equivalent.

`excludes` and `lists` deliberately leave `no_variadic` unset: excludes and
listings tend to come in runs, and `-e a b c` is less typing than
`-e a -e b -e c`. The price is that a separated value keeps consuming until the
next switch. Anyone who prefers getopt's one-value-per-option can add
`no_variadic` to those two entries and rebuild; `-e a b c` then becomes `-e a`
plus two path arguments, with no error and a silently different meaning, and the
usage reference's "only two things break with convention" loses one of them.

## Task streams

`basic_taskstream` (`include/basic_taskstream.hpp`) is the producer interface:

```cpp
virtual std::optional<task_token> pop();           // public entry
virtual std::optional<task_token> pop_raw() = 0;   // implemented by subclasses
void report_error(const error_entry &);
std::vector<error_entry> take_errors();
```

`task_token` holds the path in `p` and an optional checksum in `sum`; only
`listscanner` in rescan mode fills `sum`. `pop()` calls `pop_raw()` under the
mutex, then filters the candidate: a regular file is returned; a missing path, a
directory or a socket becomes an `error_entry`; a FIFO or device is passed on to
the worker as a stream.

| Class | Responsibility |
| --- | --- |
| `dirscanner` | recursive walk with excludes and symlink control; yields regular files only, silently skipping special files and subdirectories that match an exclude |
| `listscanner` | reads a list file or stdin, one `<checksum> <path>` per line; writes `# Kept:` lines, or fills `sum` in rescan mode |
| `cached_taskstream` | replays the plain file arguments in command-line order |
| `fanin_taskstream` | merges children behind one stream, advancing an atomic cursor when a child is exhausted |

The fan-in holds no lock of its own: each child serialises `pop()`, so workers
queue on whichever child is yielding and only hashing runs in parallel. A
`listscanner` blocked on stdin therefore blocks the whole stream.

Excludes are compared with `std::filesystem::equivalent()`, so an exclude names
an inode rather than a spelling: neither a glob pattern nor a string prefix. An
exclude that is the scanned directory itself, or a parent of it, matches nothing.

## Thread-safety rules

- `pop_raw()` runs under `mutex_`; it may touch its own state, but must not call
  `report_error()` or `pop()` (the mutex is not recursive). `take_errors()`
  locks nothing, so it must run after the workers are joined.
- `errors` is touched only under the mutex or by a single thread.
- A child must not call back into its parent's locking methods.
- Keep `pop_raw()` short: `listscanner` holds the lock while waiting on stdin.
- Return errors; never throw from `pop_raw()`, which has no handler above it.

## Hashing

`ALGORITHM_LIST` in `include/checksum.hpp` is the single list of built-in
names. Each row gives the enum name and the command-line literal; the same
macro generates the `algorithm` enum and `mapper`, the literal vector that
`str_to_alg()` and `alg_to_str()` index. It does not absorb the per-algorithm
behaviour, which stays in hand-written `switch`es over the enum: `is_keyed()`
and `max_key_length()` in the same header, `digest_for()` and `mac_for()` in
`src/checksum.cpp`. `check_key_validity()` turns the first two into the key
policy. A new row is therefore necessary but not sufficient: it still needs the
matching `digest_for()` or `mac_for()` case.

The `--algorithm` table is generated the same way: `list_algorithm()` walks
`mapper` for the names (skipping `none`) and asks `is_keyed()` for the `KEYED`
column, so the table cannot drift from the implementation.

`src/checksum.cpp` holds all OpenSSL use: `digest_for()` maps plain
digests to `EVP_MD`, `mac_for()` maps keyed algorithms to a `mac_spec`, and
`stream_file()` feeds 64 KiB chunks to a callback. `checksum_file()` and
`checksum_file_key()` run that stream through `EVP_Digest*` and `EVP_MAC*`
respectively.

`checksum_api` (`include/checksum_api.hpp`, `src/checksum_api.cpp`) is the only
handle the rest of the program uses. `set_algo()` resolves a literal algorithm
name and records `unknown_algorithm` instead of handing back a sentinel;
`set_key()` is the only way to supply a key and records `missing_key` or
`invalid_key`; `hashing()`, `keyed()`, `name()` and `calc(path)` answer the
questions the drivers ask. Callers never touch the `algorithm` enum, which is
what lets a provider add an algorithm `ALGORITHM_LIST` does not name. Failures
come back as `std::unexpected(std::error_code)`, never as exceptions, and
`algorithm::none` returns `<NONE>` without opening the file.

Files are read with plain `read()` calls over their logical length, so holes in
a sparse file cost their full size and memory stays at one 64 KiB buffer. An
explicitly named FIFO or device is opened blocking and can keep a worker
indefinitely; the interrupt flag is only checked between files.

### Dynamic providers

The contract a module has to keep — exports, memory rules, digest and key rules,
the argument script, build requirements, compatibility — is
[PROVIDER-ABI.md](PROVIDER-ABI.md). This section is only the loader.

`algorithm::dynamic` is a sentinel, not a table row: `mapper` has no entry for
it, so `alg_to_str()` would throw and the name shown in the report comes from
the provider instead. `dpf_instance` (`include/dpframework.hpp`) does
the loading:

- The constructor splits `TSUBAKI_DYNAMIC_PROVIDER_PATHS` on `:`, drops empty
  segments, and walks each one with `dirscanner`, so the scan is recursive and
  does not follow symlinked directories. `dirscanner` rejects a path that does
  not exist or is not a directory with `std::invalid_argument`; the constructor
  catches it per path, prints it and moves on.
- Every file whose extension is exactly `.so` becomes a `provider`: `dlopen()`
  with `RTLD_NOW | RTLD_LOCAL`, then `dlsym()` for `tsubaki_provider_name()`,
  whose answer is checked against `str_to_alg()`. A module reporting a built-in
  name is refused at that point, before the other symbols are looked up, so a
  `.so` cannot shadow `sha256` even when it loads first. What survives gets
  `dlsym()` for the remaining required `tsubaki_*` symbols declared in
  `include/dynamic_provider_header.hpp`, plus the optional
  `tsubaki_algo_info()`: if that lookup fails the pointer becomes
  `no_algo_info()`, which is what `algo-info` prints instead. `provider_msg` records how far that
  got; anything but `success` is printed and dropped. The handle is never
  closed, so a module lives as long as the process.
- `provider_mapper` is keyed by the name the module reports and filled with
  `emplace()`, so a duplicate keeps whichever module claimed the name first —
  the environment variable's order, then the walk order inside each directory.
- A provider may also declare the command-line arguments its algorithm needs.
  `tsubaki_need_more_args()` is the switch, and a missing symbol is treated
  exactly like one that returns false. `provider::provider()` wires that up by
  jumping to the same `no_args` label in both cases and replacing the pointer
  with `dont_need_args()`, so every later call goes through one function. Only a
  true return leads to looking up `tsubaki_arg_definition()` and
  `tsubaki_take_args()`, which then both have to be there; a half-declared trio
  fails the load like any missing required symbol.
  `checksum_api::check_args()` parses the definition with
  `argparser::parse_script()`, whose script language is documented in
  LC-ArgParser's `docs/USAGE.md` under "Script language" (`CMakeLists.txt` pins
  the library to one commit, so that copy is the authority), registers
  every entry in
  the `#dynamic` group under `<name>@dynamic` — the suffix keeps a provider from
  colliding with, or being confused for, one of tsubaki's own entries — calls
  `reparse()` so the command line recognises the new names, evaluates that
  group, and builds one `tsubaki_pass_content` per parsed entry by iterating the
  script's own vector, which is what makes the count and the order match the
  definition exactly. It calls `fine()` first, so a command line
  that breaks the declared counts is reported exactly like a built-in option,
  with the provider's `explanations` line as the label, and
  `tsubaki_take_args()` is not called at all.
- `algo_info_core()` writes the script out too: when `tsubaki_need_more_args()`
  returns true it asks for `tsubaki_arg_definition()` and prints it untouched
  under `ArgParser script:`, so it never parses the script and a malformed one
  is only reported later, by `check_args()`. A null return is fatal, status 70.
- `sum_main()` evaluates the `sum` group, then `check_args()`, and only then
  `sum_greedy` — `--exclude`, `--list` and the file list. That order is what
  keeps a provider option out of the file list: the name has to be registered
  before the positional collects tokens, and a word left over from
  `pre_parse()` is only re-examined by `reparse()`. The price is that a provider
  name wins over tsubaki's own, which the contract warns about. `verify`
  appends the positionals it does not understand to the internal `sum` command
  line as they are, so `verify -u alg name -L 128` reaches the provider.

`checksum_api` holds its `dpf_instance` as `inline static`, so the process scans
once, when the first `checksum_api` is constructed, and every later one reuses
that map: `sum` and `verify` share a single scan even though `verify` builds two
`checksum_api` objects (its own and the one inside the internal `sum`), and
`cmp` builds none, so it never scans. `list_algorithm()` and
`get_completion()` in `src/help.cpp` are the exception: `--algorithm` and
`--completions` each construct a local `dpf_instance` of their own.
`set_algo()` tries `str_to_alg()` before `find_provider()`, so name lookup
cannot reach a provider for a built-in name either. For a provider,
`keyed()` asks `tsubaki_is_keyed()`, `set_key()` asks `tsubaki_check_key()`
instead of `check_key_validity()`, and `calc()` calls `tsubaki_checksum_file()`,
which returns a `tsubaki_checksum_result` by value: a digest buffer of
`TSUBAKI_MAX_RESULT_LENGTH` bytes that belongs to `checksum_api`, plus an
errno-style code the module must fill on every path.

The boundary carries plain data only. `calc()` keeps the result on its own
stack and hands the module the path and the key as `const char *` it may only
read, the key with its own length because it is a byte string, not a C string;
the module writes the digest into the buffer that came back with the struct and
reports failure as an `int`. Nothing the module allocates is ever freed here, and
nothing here is freed there, which is what lets a statically linked `tsubaki` —
one private copy of glibc — load a module that brings in the shared one without
a cross-allocator free. The `std::error_code` in the returned `expected` is
built by `checksum_api` from that `int` with `std::make_error_code()`, so its
category and the message it produces stay in tsubaki's own standard library. The
digest buffer holds at most 256 characters: `TSUBAKI_MAX_RESULT_LENGTH` is 257
and the last byte is the NUL.

The rules a module must keep are in [PROVIDER-ABI.md](PROVIDER-ABI.md); the ones
whose violation is undefined behaviour are repeated at the top of
`include/dynamic_provider_header.hpp`.

## Workers, progress and report

`work_manager` owns the worker vector, the printer thread, a `const
checksum_api`, the interrupt flag, the counters and — in verification mode — the
`changed` and `unsure` vectors. `run(n)` installs the SIGINT handler when asked,
writes the `# tsubaki checksum result` header, starts `n` workers plus the
printer, and waits.

- A worker pops a path; when interrupted it prints `<NONE>` and counts the file
  as not hashed (and unsure in a verify run); otherwise it calls `api.calc()`,
  writes one line through `std::osyncstream` and updates the counters,
  `changed` or `unsure`.
- The printer repaints one stderr line every 100 ms until the run finishes or
  is interrupted.
- `run()` returns a `work_result` (algorithm name, verify flag, errors,
  changed, unsure, counters, interrupt flag, duration), so the caller decides
  where to print it.

`work_result::print(os, details)` writes the report block: algorithm name, processed
(= hashed + failed), hashed, failed, not hashed, duration, then `Changed` and
`Unsure` in verify mode, the error count, the interrupt warning, and — with
`details` — the error list and the per-count path lists.

`SIGINT` sets the manager flag. Workers still drain the stream, so every path a
scanner produced is accounted for; a file already being hashed finishes
normally, while a worker blocked in `open()` or an endless read never returns to
the check. The fifth `Ctrl-C` writes a fixed message and calls `_exit(250)`.

## Output streams

`sum_core` picks one sink (stdout or the `--output` file) shared by the workers,
the list scanners and the report; concurrent lines go through
`std::osyncstream`. `work_manager::run()` writes the
`# tsubaki checksum result` header first. `cmp_core` writes its own stream
directly and starts with `# tsubaki comparison result`.

stderr carries everything outside the listing: argument and input problems,
warnings, the default-output notices of `verify` and `cmp`, a second copy of the
report when the results go to a file (`print(std::clog, false)`), and the final
write-failure message. The result stream is flushed and checked at the end: a
failed flush exits 73, while `SIGPIPE` keeps its default action (141 in a
shell).

## Concurrency model

| Thread | Count | Work |
| --- | --- | --- |
| main | 1 | builds the streams, then calls `run()`, which joins the workers |
| worker | `-t` | pop one path, hash it, write one line (and compare it in verify mode) |
| printer | 1 | repaint the progress line every 100 ms |

- One mutex per task stream, held only while a path is taken out.
- The `interrupted` flag and the counters are atomics; the algorithm lives in
  the manager's `const checksum_api` and never changes during a run.
- No lock is held while hashing, so file I/O is fully parallel.

## Exit codes

The codes live in `include/exit_codes.hpp` and are shared by `main()`,
`sum_main()`, `verify_main()` and `cmp_main()`:

| Macro | Value | Returned when |
| --- | --- | --- |
| `TSUBAKI_SUCCESS` | 0 | no error entry; `verify` differences and `cmp` differences are not errors |
| `TSUBAKI_HASH_ERROR` | 1 | an error entry was recorded; for `cmp`, a list line without a checksum, a path `-c` could not canonicalize, or a list read error |
| `TSUBAKI_INVALID_OPERATION` | 64 | a rejected command line; `verify` also for an unusable `Algorithm:` header, a `-` input with no `-u`, or an output equal to the input; `cmp` also when both lists are stdin, when the output names one of the lists, or one path repeats with different checksums (`EX_USAGE`) |
| `TSUBAKI_FILE_READ_FAILED` | 66 | an unreadable `--key-file`, an empty key file, a missing or directory `verify` input, or an unreadable `cmp` list (`EX_NOINPUT`) |
| `TSUBAKI_FATAL` | 70 | `argc` is 0; a provider needs arguments but has no script, or the script fails to parse (`EX_SOFTWARE`) |
| `TSUBAKI_FILE_WRITE_FAILED` | 73 | the `--output` file cannot be opened, or the result stream fails (`EX_CANTCREAT`) |
| `TSUBAKI_INTERRUPTED` | 130 | `work_result::interrupted` is set (`128 + SIGINT`) |
| `TSUBAKI_TERMINATED` | 250 | the fifth `Ctrl-C` called `_exit()`, so no report was written |
| `TSUBAKI_UNEXPECTED` | 255 | an exception escaped `main()` |

`interrupted` wins over `errors`; a write failure returns 73, but `SIGPIPE`
kills the process first when the reader of a pipe is gone (141).

## Data flow example

```
tsubaki sum sha256 . -e build -l old.txt
```

1. `files` collects `.`, `excludes` takes `build`, and `old.txt` becomes a list.
2. `.` becomes a `dirscanner` with `build` excluded; `old.txt` becomes a
   `listscanner`; both join an empty `cached_taskstream` in the fan-in.
3. Workers pull from the directory first, then the list. Kept lines are written
   immediately as `# Kept: <path>` and never enter the hashing path.
4. When the fan-in is exhausted, `run()` returns the `work_result` that
   `sum_main` prints.

`cmp` has no such flow: `cmp_main()` imports both listings into one vector,
sorts it twice and writes the sections; no task stream or worker is involved.

## Extension points

- **New subcommand**: add a `subcommands` constant, a `cmdline_group`, a branch
  in `main()`, and a driver like `sum_main()`. `cmp` is the minimal example: it
  reuses `listscanner` but no worker pool.
- **New input source**: derive from `basic_taskstream`, implement `pop_raw()`,
  and add an entry to `def_args()` if it needs its own syntax.
- **New output sink**: `work_manager` is the only consumer; hand it a different
  `std::ostream` or replace it.
- **New algorithm**: add one row to `ALGORITHM_LIST` with its literal, extend
  `is_keyed()` (and `max_key_length()` if the key has a limit), then give a
  plain digest a `digest_for()` case and a keyed one a `mac_for()` case.
  `mapper`, name lookup and the `--algorithm` table follow the row; the keyed
  flag and the key limit are switches over the same enum.
- **New algorithm without rebuilding this tree**: write a provider `.so` against
  `include/dynamic_provider_header.hpp` and point
  `TSUBAKI_DYNAMIC_PROVIDER_PATHS` at it. The name then resolves through
  `dpf_instance`, `--algorithm` lists it with its `SOURCE` and whether it takes
  extra arguments, and `verify -u` accepts it. A module that reports a built-in name is refused at load, so it
  cannot shadow one, and its digest holds at most 256 characters.

## Build system

`CMakeLists.txt` accepts CMake 3.10 and `CMakePresets.json` requires 3.28; the
presets are what the README documents, and the older floor only matters when
configuring by hand. The target is POSIX-only: the sources include `dlfcn.h`
(dynamic providers), `unistd.h` and `fcntl.h` (file reads), `pthread.h`
(workers) and `sys/ioctl.h` (terminal width), and use `::open()`/`::read()` with
`O_CLOEXEC`. A native Windows configure therefore stops immediately with an
explicit `FATAL_ERROR`, while Cygwin passes. OpenSSL 3.0 is the floor because
`EVP_MAC` and `<openssl/core_names.h>` are used unconditionally, and a LibreSSL
`FindOpenSSL` result is rejected with a message instead of failing at the first
include. macOS needs Xcode 16 or newer, because libc++ provides `std::expected`
only from that version.

`LC-ArgParser` is fetched at configure time and compiled into the target as two
source files rather than linked as a library.
`FETCHCONTENT_SOURCE_DIR_LC-ARGPARSER` points the fetch at a local checkout;
otherwise the copy stays under `build/<preset>/_deps` and is reused. Its
`GIT_TAG` pins one commit: upstream carries no semantic versioning, so a
floating reference would let the meaning of a provider's script drift with no
version to notice it by. That commit's `docs/USAGE.md` is the authority on the
script language a provider may use for its arguments, not the repository's tip,
and from 2.1.3 a bump of that pin is guaranteed not to change the meaning of an
existing provider's script — see
[PROVIDER-ABI.md](PROVIDER-ABI.md#the-argument-script).

The Linux presets pin GCC by absolute path (`/usr/bin/g++`) so a stray `CC` in
the environment cannot silently change the toolchain; `linux-clang-debug` is the
only non-GCC preset and exists as a second opinion on warnings. The macOS,
FreeBSD and other-POSIX presets leave the compiler to the platform default.

`-V` reports build provenance through three compile definitions:
`TSUBAKI_VERSION` from `project()`, `TSUBAKI_GIT_COMMIT` from
`git rev-parse --short=12 HEAD` with `-dirty` appended when tracked files are
modified, and `TSUBAKI_BUILD_TIMESTAMP`, stamped at configure time in Beijing
time (UTC+8) so the printed time does not depend on the build host's zone.

## Source map

### Types

| Type | Kind | Base | Responsibility |
| --- | --- | --- | --- |
| `tsubaki::basic_taskstream` | class | — | virtual base of every task stream: `pop()` filters the candidate, `pop_raw()` is the hook each subclass implements, `errors` + `report_error()` + `take_errors()` carry failures |
| `tsubaki::dirscanner` | class | `basic_taskstream` | recursive directory walk with excludes and symlink control |
| `tsubaki::listscanner` | class | `basic_taskstream` | parses a list file or stdin, one `<checksum> <path>` per line, writing `# Kept: <path>` lines or filling `task_token::sum` in rescan mode |
| `tsubaki::cached_taskstream` | class | `basic_taskstream` | replays a fixed queue of paths, in the order given |
| `tsubaki::fanin_taskstream` | class | `basic_taskstream` | merges child streams; owns them through `unique_ptr` |
| `tsubaki::work_manager` | class | — | worker pool, progress line, interrupt flag, and the `changed`/`unsure` vectors of a verify run |
| `tsubaki::work_result` | class | — | value snapshot returned by `run()`, with `print()`; it holds no reference to the manager |
| `tsubaki::checksum_api` | class | — | resolves a literal algorithm name, holds the key, answers `hashing()` / `keyed()` / `name()`, declares a provider's extra arguments through `check_args()`, and routes `calc()` to the digest path, the MAC path or a provider |
| `tsubaki::checksum_error` | enum class | — | why a `checksum_api` is unusable: `none`, `unknown_algorithm`, `missing_key`, `invalid_key` |
| `tsubaki::dpf_instance` | class | — | scans `TSUBAKI_DYNAMIC_PROVIDER_PATHS` once at construction and maps provider names to loaded modules |
| `tsubaki::provider` | struct | — | one `dlopen`ed module: its handle, the symbols resolved so far, and the `provider_msg` of the step that stopped it |
| `tsubaki::provider_msg` | enum class | — | `success`, `dlopen_fail`, `dlsym_fail`, `refused` or `unknown`: how far a module got |
| `tsubaki::task_token` | struct | — | the path a worker processes plus the checksum read from the listing, empty outside rescan mode |
| `tsubaki::error_entry` | struct | — | `std::error_code` plus the operation that failed |
| `tsubaki::fd_guard` | class | — | closes a file descriptor on destruction; used by the hashing code |
| `tsubaki::algorithm` | enum class | — | the built-in enum generated from `ALGORITHM_LIST`, plus `dynamic` for a provider-backed name and `error` for a failed `str_to_alg()` |
| `tsubaki::mac_spec` | struct | — | internal to `src/checksum.cpp`: the `EVP_MAC` name and the digest parameter, if any |

### Files

| Path | Contents |
| --- | --- |
| `src/main.cpp` | subcommand dispatch, `general` argument group, last-resort exception handler |
| `include/core_functions.hpp` | `sum_main()`, `verify_main()` and `cmp_main()` declarations |
| `include/exit_codes.hpp` | the exit-code macros shared by the four drivers |
| `src/sum_core.cpp` | `sum` argument group checks, provider argument declaration, the greedy stage that follows it, startup sequence, stream construction |
| `src/verify_core.cpp` | `verify` argument group, `-u` override, header lookup, internal `sum` invocation |
| `src/cmp_core.cpp` | `cmp` argument group, import of both listings, path and checksum matching, section output |
| `include/algo_info_core.hpp`, `src/algo_info_core.cpp` | `algo-info` argument group, `get_info_builtin()` and the driver that prints a built-in or provider description, plus a provider's argument script |
| `include/basic_taskstream.hpp`, `src/basic_taskstream.cpp` | the base stream and its `pop()` filter |
| `include/dirscanner.hpp`, `src/dirscanner.cpp` | directory walk |
| `include/listscanner.hpp`, `src/listscanner.cpp` | list-file and stdin parsing |
| `include/cached_taskstream.hpp`, `src/cached_taskstream.cpp` | fixed path replay |
| `include/fanin_taskstream.hpp`, `src/fanin_taskstream.cpp` | multi-source merge |
| `include/checksum.hpp` | `ALGORITHM_LIST`, the `algorithm` enum, `mapper`, `str_to_alg()` / `alg_to_str()`, the key policy, and the `checksum_file()` / `checksum_file_key()` declarations |
| `src/checksum.cpp` | the OpenSSL side: `digest_for()`, `mac_for()`, `stream_file()`, `checksum_file()` and `checksum_file_key()` |
| `include/checksum_api.hpp`, `src/checksum_api.cpp` | `checksum_error` and `checksum_api`: name resolution, key handling, provider argument declaration through `check_args()`, `calc()` routing |
| `include/dpframework.hpp`, `src/dpframework.cpp` | `provider_msg`, `provider` (one `dlopen`/`dlsym` load) and `dpf_instance` (the environment scan) |
| `include/dynamic_provider_header.hpp` | the ABI a provider compiles against: four required `extern "C"` symbols, an optional `tsubaki_algo_info()`, and an optional trio that declares extra command-line arguments |
| `include/worker.hpp`, `src/worker.cpp` | `work_result` and `work_manager` |
| `include/utilities.hpp`, `src/utilities.cpp` | string helpers, key reading, comment lookup, terminal width, fd guard |
| `include/cmdline.hpp` | argument entry definitions |
| `include/help.hpp`, `src/help.cpp` | help text, the `--algorithm` table and the completion word list |
| `LC-ArgParser` (fetched) | standalone argument parser, compiled in as a source file |
