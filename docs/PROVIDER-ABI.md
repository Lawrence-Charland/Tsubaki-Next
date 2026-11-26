# Tsubaki-Next provider ABI

[中文](PROVIDER-ABI.zh-cn.md)

Tsubaki loads algorithms that are not built in from shared objects. This
document is the contract such a module has to keep: what it exports, what may
cross the boundary, how a digest and a failure are reported, how extra
command-line arguments are declared, what a build has to look like, and what
stability is promised in return.

[`include/dynamic_provider_header.hpp`](../include/dynamic_provider_header.hpp)
is the same contract in code: **its declarations are normative for the
signatures**, and this document is normative for the behaviour behind them. A
provider vendors that header into its own tree.

For the tsubaki side — how the loader walks directories, resolves symbols and
registers a provider's arguments — see
[ARCHITECTURE.md](ARCHITECTURE.md#dynamic-providers). For the user-facing side
of installing providers, see [USAGE.md](USAGE.md#dynamic-algorithms).

## Contents

- [Symbols](#symbols)
- [Data types](#data-types)
- [Memory and lifetime](#memory-and-lifetime)
- [The digest](#the-digest)
- [Reporting failure](#reporting-failure)
- [Keys](#keys)
- [The name](#the-name)
- [The `algo-info` text](#the-algo-info-text)
- [Extra command-line arguments](#extra-command-line-arguments)
  - [Writing the script](#writing-the-script)
- [Building a module](#building-a-module)
- [How the loader treats a module](#how-the-loader-treats-a-module)
- [Compatibility](#compatibility)
  - [The argument script](#the-argument-script)
- [Minimal module](#minimal-module)

## Symbols

Every symbol is `extern "C"`, so the exported name is the plain function name;
the `namespace tsubaki` in the header only groups the declarations for the
module's own source.

| Symbol | Required | Signature | Purpose |
| --- | --- | --- | --- |
| `tsubaki_provider_name` | yes | `const char *() noexcept` | the name used on the command line, in `-a` and in `algo-info` |
| `tsubaki_is_keyed` | yes | `bool() noexcept` | whether a key is required |
| `tsubaki_check_key` | yes | `bool(const char *key, std::size_t length) noexcept` | accept or reject a key |
| `tsubaki_checksum_file` | yes | `tsubaki_checksum_result(const char *path, const char *key, std::size_t length) noexcept` | compute the digest |
| `tsubaki_algo_info` | no | `const char *() noexcept` | three lines for `algo-info` |
| `tsubaki_need_more_args` | no | `bool() noexcept` | the switch; absent is equivalent to returning `false` |
| `tsubaki_arg_definition` | if the switch returns true | `const char *() noexcept` | the argument script |
| `tsubaki_take_args` | if the switch returns true | `const char *(tsubaki_pass_content *content, std::size_t cnt) noexcept` | receive the parsed values |

`tsubaki_need_more_args` is the switch. Leaving the symbol out is exactly the
same as exporting it and returning `false`: in both cases the provider takes no
arguments, the other two symbols are never looked up, and exporting them anyway
is harmless. Only when it returns `true` does the trio become binding — a module
that then omits `tsubaki_arg_definition` or `tsubaki_take_args` fails the load,
like any other missing required symbol.

## Data types

```cpp
struct tsubaki_checksum_result
{
    char result[TSUBAKI_MAX_RESULT_LENGTH];
    int errcode;
};

struct tsubaki_pass_content
{
    std::size_t contentc;
    const char **contentv;
};
```

`TSUBAKI_MAX_RESULT_LENGTH` is 257: 256 hexadecimal characters, twice the widest
digest tsubaki builds itself, plus the terminating NUL. `tsubaki_pass_content`
carries the values parsed for one entry of the argument script: `contentc` of
them in `contentv`, each NUL terminated. `contentv` is null when `contentc` is 0.

## Memory and lifetime

Only plain data crosses the boundary. No `std::string`, no
`std::filesystem::path`, no `std::expected`, no `std::error_code`, no exception,
and nothing the module allocated, is ever handed back. That is what makes a
statically linked tsubaki safe: nothing the module allocates is freed by
tsubaki, and nothing tsubaki owns is freed by the module.

- Every pointer tsubaki passes in — the path, the key, the argument strings — is
  read-only and valid only for the duration of the call. A module must not keep
  any of them.
- The result travels by value: the module fills a `tsubaki_checksum_result` of
  its own and returns it. Nothing of the module is referenced after the call,
  and no pointer into module-owned memory may be handed back.
- Every entry point keeps its `noexcept`. Anything that can throw is caught
  inside the module; nothing may escape into tsubaki.
- `tsubaki_take_args` is called once, before any file is hashed, on the main
  thread. The hashing callback afterwards runs on several worker threads at
  once, so anything the digest depends on has to be copied out of the argument
  strings first.
- The module is never unloaded, so it may keep whatever the loader initialized,
  and it must not assume any cleanup call.

## The digest

A successful digest is hexadecimal, lowercase, and NUL terminated inside
`result.result`. The width is free: a listing line always starts with the
checksum, so nothing has to guess where it ends and there is no minimum. The
maximum is the buffer: 256 characters. No real digest comes close, but the bound
is hard: tsubaki passes a fixed-size buffer and reads nothing beyond the NUL, so
an algorithm whose digest would not fit has to report an error through
`errcode`; writing past the end is undefined behaviour, not a truncation.

## Reporting failure

`result.errcode` is 0 on success and an errno value otherwise, and it is the
only way to report a failure. Both fields must be filled on every path: tsubaki
does not pre-fill the struct, so anything left unset is read as garbage.

Do not build a `std::error_code` inside the module: its category lives in the
module's standard library, so the message it produces would be allocated there
and freed by tsubaki. tsubaki turns the `errcode` into an error message with its
own library.

## Keys

`tsubaki_is_keyed` decides whether the algorithm needs a key. A keyed provider is
asked for one like a built-in keyed algorithm (`needs a key`, exit status 64),
and `tsubaki_check_key` decides whether the key is acceptable; a plain provider
is never asked and should accept anything. tsubaki asks about a non-empty key
only.

A key is a byte string, not a C string: it is not NUL terminated and may contain
NUL bytes, and `length` is the only length there is. `tsubaki_checksum_file`
receives the same byte string, with `length` set to 0 for a provider that
ignores it. Providers declare no key-length limit; a longer key is the
provider's own business to reject through `tsubaki_check_key`.

## The name

`tsubaki_provider_name` returns a non-null, stable string. It must not name a
built-in algorithm: while loading, tsubaki compares it against its own list and
refuses the module there, before resolving the other symbols, so a `.so` cannot
shadow `sha256`.

The name is the contract. A manifest records the name and nothing else about the
algorithm, so a digest whose length or meaning changes needs a new name, not a
new version of the old one — keeping the name means keeping the old output.

## The `algo-info` text

`tsubaki_algo_info` is optional at the ABI level; without the symbol,
`tsubaki algo-info` prints "No information provided." By policy, though, every
published provider exports it and uses it to say where the module comes from — a
project page, a repository or a URL — so that a user handed a `.so` can tell what
it is before running it.

A module that exports it returns a static, human-readable description in the same
shape the built-in algorithms use:

```
Digest:     32 bytes
Key:        none
Note:       BLAKE3 (RFC draft). ...
Origin:     https://example.org/blake3-provider
```

The origin can sit on any line under any label; what matters is that it is there.
Do not label it `Source:` — that line is printed by tsubaki itself, above the
module's text, and always names the path the module was loaded from ("built-in"
for a built-in algorithm). A second `Source:` line from the module only reads as
a contradiction.

The string is read-only, lives as long as the module, and is printed verbatim —
tsubaki parses nothing in it.

## Extra command-line arguments

An algorithm that needs more than a file name declares its own switches through
the trio described above.

- `tsubaki_need_more_args` is the switch, and leaving it out is exactly the
  same as exporting it and returning `false`: the provider takes no arguments
  and tsubaki never looks at the other two symbols.
- `tsubaki_arg_definition` returns the table of accepted arguments, one
  `{ entry_name = ...; entry_type = ...; ... }` block per entry, in the script
  language of
  [LC-ArgParser](https://github.com/Lawrence-Charland/LC-ArgParser)'s
  `docs/USAGE.md` under "Script language". `entry_name` and `entry_type`
  (positional, option or flag) are required; every other key names an `ap_entry`
  field. `CMakeLists.txt` pins the library to one commit, so that commit's copy
  of the document is the authority, not the repository's tip; from 2.1.3, a bump
  of that pin does not change what an existing script means (see [The argument
  script](#the-argument-script)). Return a literal; a null return is fatal, with
  status 70.
- `tsubaki_take_args` receives the parsed values. The array is guaranteed to
  hold exactly one entry per argument the script defines, in exactly the order
  the script defines them — index *i* always describes script entry *i*, and
  `cnt` equals the number of entries. An entry that matched nothing still
  arrives, with `contentc == 0`. Return an empty string to accept the command
  line, or a NUL-terminated message that tsubaki prints as the error and exits
  64 — which is how a module rejects a combination the script language cannot
  express.

The rules that follow from how tsubaki parses the command line:

- tsubaki enforces the counts the script declares. A command line that breaks
  `min_value_count`, `max_value_count` or `max_repetition` is reported by
  tsubaki itself and `tsubaki_take_args` is not called. In those messages the
  entry is labelled `<entry_name>@dynamic` — tsubaki's internal marker, so a
  provider option is never mistaken for one of its own and the user can see the
  requirement came from the provider rather than from the program; an
  `explanations` line in the script replaces that label in the printed message.
- A malformed script is caught when `tsubaki sum` registers it, with status 70.
  `tsubaki algo-info` prints the script verbatim without parsing it.
- Provider entries are parsed before tsubaki's own greedy arguments, which
  includes the file list, so a provider switch wins over a path — and an
  unbounded positional takes the file names. The names to leave alone and the
  bounds to set are in [Writing the script](#writing-the-script).
- Registering the script reads the command line a second time, but `--` still
  holds: everything after it stays a positional, so a provider switch written
  there is never recognised.
- The switches belong after the algorithm name — a word in front of it is still
  read as the algorithm — and may also follow the file list, because the second
  read sees the whole command line.
- `verify` forwards the positionals it does not understand, so
  `verify -u alg name -L 128` reaches the provider. A manifest records the
  algorithm name but not these parameters: rechecking means writing them again
  on the `verify` command line.

### Writing the script

The script language has more keys than a provider needs, and its defaults are
tuned for a program that owns its whole command line.

**Strongly discouraged, unless you know ArgParser's mechanics.** Two shapes cause
most of the trouble, because they let one argument consume tokens that were meant
for something else:

- **A positional entry** (`entry_type = positional`) competes with the file list,
  and provider entries are evaluated first. Unbounded, it takes the paths;
  bounded, it still takes the words a user meant as files.
- **An option with no value limit.** `max_value_count` defaults to unlimited, so
  `-L 128 dir` reads `dir` as a second value. Only `max_value_count` or
  `no_variadic =` stops it there.

Prefer `entry_type = option` with `max_value_count = 1` or `no_variadic =`, and
do without a positional argument unless the algorithm truly needs one.

**Never use.**

| Key | Why |
| --- | --- |
| `group` | tsubaki registers the entry in `#dynamic` itself, and `group` only ever adds memberships. `group = sum_greedy` makes the entry evaluate a second time, after its tokens were already consumed: worthless in the best case, and with `min_value_count > 0` it invents an `option_too_few_values`. Any other group is evaluated before the provider is registered, so the key does nothing at all. |
| `no_consume` | Whatever matched stays in the token pool: the option's value words are then collected by the file list as paths, and the switch token itself is left behind as a redundant argument, so `fine()` fails and the run exits 64 before anything is hashed. |

**Defaults to tighten.** An option keeps taking values up to the next switch, and
a positional keeps taking words up to a barrier or the end of the command line.

| Key | Default | What to write |
| --- | --- | --- |
| `max_value_count` | `-1`, unlimited | `1`, or `no_variadic =` — see above |
| `max_positional_count` | `-1`, unlimited | bound it, or write no positional at all — see above |
| `min_value_count` | `0` | keep it `0`. A positive value makes the option *required* on every `sum` run; express an optional argument's default in `tsubaki_take_args` instead |
| `min_positional_count` | `0` | keep it `0`, for the same reason |

Value counts are totals across all occurrences, not per occurrence, so
`max_value_count = 1` rejects `-L 1 -L 2`. That is the intended reading, not a
bug.

**Names.** `short_name` and `long_name` are matched against the same command line
tsubaki parses, and a provider entry claims a token before the file list does.

- Reserved short letters: `h V a K T k t o v e l s f u c`.
- Reserved long names: `help`, `version`, `algorithm`, `key-file`, `text`,
  `key`, `thread`, `threads`, `output`, `verbose`, `exclude`, `excludes`,
  `list`, `lists`, `follow-symlinks`, `force-rescan`, `verify`, `use-algo`,
  `canonical`.
- A collision fails one of two ways, both silently: a name tsubaki evaluates
  earlier (`-o`, `-k`, `-t`, …) consumes the token first, so the provider sees
  nothing — usually surfacing as `Not enough values for the option:
  <name>@dynamic` when the script sets `min_value_count`; a name evaluated later
  (`--exclude`, `--list` and the file list) is taken by the provider, breaking
  tsubaki's own behaviour.
- `entry_name` has to be unique within one script — a repeat throws and exits
  70 — and should not contain `@`, because tsubaki appends `@dynamic` to it for
  registration and for error messages.
- Registering a short name also changes how a word is split: with `L` as an
  option, `-Lv` becomes `-L` with the attached value `v`, where before the whole
  word fell through to the file list.

**Worth using.** `no_variadic =` (one value per occurrence), `explanations` (the
label a 64 error prints instead of the internal `@dynamic` name) and
`max_repetition` (flags only; option repetition is counted by
`max_value_count`). `barrier` is how a positional is bounded by token rather
than by count, but it matches literally, so naming one of tsubaki's switches
there truncates the collection early.

**What the script cannot express.** Ranges, mutually exclusive combinations,
dependencies between arguments and cross-argument checks have no syntax. Do them
in `tsubaki_take_args` and return a message (exit 64).

## Building a module

- C++23, compiled as a position-independent (`-fPIC`) shared object, for the
  same architecture and calling convention as tsubaki, against
  `dynamic_provider_header.hpp`.
- The module is self-contained: tsubaki exports no symbols to it and hands it no
  runtime to borrow. A statically linked tsubaki still loads modules through the
  system C library, so the module has to be built against the same glibc
  generation tsubaki was linked with.
- One project may emit several `.so` files, one per algorithm name, sharing the
  header and the file loop. Put each in a directory listed in
  `TSUBAKI_DYNAMIC_PROVIDER_PATHS`; the walk is recursive and only files whose
  extension is exactly `.so` are loaded, so `libx.so.1` is skipped.

## How the loader treats a module

- A path that is not a readable directory, a `.so` that cannot be loaded, or a
  missing symbol each prints one `Error:` line on stderr and is skipped; none of
  them changes the exit status.
- A module reporting a built-in name is refused at load, before the remaining
  symbols are resolved.
- Two providers claiming one name: the first loaded wins — the order in
  `TSUBAKI_DYNAMIC_PROVIDER_PATHS`, then the walk order inside each directory.
- Modules are never unloaded.

## Compatibility

There is no ABI version symbol, and none is planned: the contract is small
enough to stay stable, and a version check would only add a way for a module to
refuse to load.

From 2.1.3 onwards tsubaki makes a moderate backward-compatibility commitment.
Across 2.x, the symbols above keep their names, signatures and meaning, and both
structures keep their layout, so a module built against 2.1.3 keeps loading.
New capabilities arrive the way optional symbols already do: a module that does
not export them simply does not get the feature. A digest name keeps its meaning
for the same reason — an algorithm whose output has to change gets a new name.

That commitment is not a formal guarantee. It does not cover builds across
architectures, calling conventions or glibc generations, and it protects no
module that breaks the memory rules above. A change that cannot be made
compatibly would appear as a new symbol name, or in a new major version, never
as a silent change in what an existing symbol does. Because nothing is checked
at load time, a module is expected to be built against a header no newer than the
tsubaki that loads it.

### The argument script

The script in `tsubaki_arg_definition` is parsed by LC-ArgParser, a separate
project that carries no semantic versioning. Tsubaki-Next therefore pins one
commit instead of tracking upstream's tip: a floating reference would let the
meaning of a provider's arguments change with nothing to notice it by. The pin
is whatever `CMakeLists.txt` names, and the `docs/USAGE.md` of that exact commit
is the authority on the grammar a provider may use — not the repository's
latest.

The pin is not frozen: it advances when `CMakeLists.txt` points at a newer
commit. What is guaranteed, from 2.1.3 onwards, is per advance: a new pin does
not change how the parser reads a provider's script. The grammar and the keys an
entry may use, how a value is split into arguments and counted against
`min_value_count`, `max_value_count` and `max_repetition`, and where `--` cuts
the command line short keep their behaviour, so a bump may bring fixes but never
a reinterpretation of a script that already worked.

That covers the parser only. The `<entry_name>@dynamic` label that keeps a
provider option apart from tsubaki's own in messages, and the order in which
provider entries are parsed, are tsubaki's own integration decisions — described
under [Extra command-line arguments](#extra-command-line-arguments) — and are not
something the pinned library decides.

## Minimal module

A complete module for a 32-bit FNV-1a digest, useful as a starting point:

```cpp
#include "dynamic_provider_header.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>

namespace tsubaki
{
extern "C"
{
    const char *tsubaki_provider_name() noexcept { return "fnv32"; }

    const char *tsubaki_algo_info() noexcept
    {
        return "Digest:     8 hex characters (32-bit FNV-1a)\n"
               "Key:        none\n"
               "Note:       Example from the Tsubaki-Next docs.\n"
               "Origin:     https://github.com/Lawrence-Charland/Tsubaki-Next";
    }

    bool tsubaki_is_keyed() noexcept { return false; }

    bool tsubaki_check_key(const char *, std::size_t) noexcept { return true; }

    tsubaki_checksum_result tsubaki_checksum_file(const char *path,
                                                  const char *,
                                                  std::size_t) noexcept
    {
        tsubaki_checksum_result r{};
        std::FILE *f = std::fopen(path, "rb");
        if (f == nullptr)
        {
            r.errcode = errno;
            return r;
        }

        std::uint32_t h = 2166136261u;
        unsigned char buf[64 * 1024];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            for (std::size_t i = 0; i < n; ++i)
            {
                h ^= buf[i];
                h *= 16777619u;
            }
        const bool failed = std::ferror(f) != 0;
        const int err = errno;
        std::fclose(f);
        if (failed)
        {
            r.errcode = err;
            return r;
        }

        std::snprintf(r.result, sizeof r.result, "%08x", h);
        return r;
    }
}
} // namespace tsubaki
```

Build it with `-std=c++23 -fPIC -shared`, put the result in a directory named in
`TSUBAKI_DYNAMIC_PROVIDER_PATHS`, and it appears in `tsubaki -a`, in
`tsubaki algo-info fnv32`, and as `tsubaki sum fnv32 <path>`.
