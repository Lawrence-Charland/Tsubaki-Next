# Tsubaki-Next Usage

[中文](USAGE.zh-cn.md)

How to run Tsubaki-Next: the four subcommands, every option, the checksum-file
format it reads and writes, the report block, the exit statuses and what
`Ctrl-C` does to a run. Building it, the platforms it supports and the history
of the project are in the [README](../README.md).

## Contents

- [Usage](#usage)
  - [Algorithms](#algorithms)
  - [Dynamic algorithms](#dynamic-algorithms)
  - [Options](#options)
  - [Option values](#option-values)
  - [Keyed hashes](#keyed-hashes)
  - [Verifying](#verifying)
  - [Comparing](#comparing)
  - [Shell completion](#shell-completion)
- [Inputs](#inputs)
- [Output](#output)
  - [Report keys](#report-keys)
- [Exit status](#exit-status)
- [Interrupt](#interrupt)

## Usage

```
tsubaki sum [options] <algorithm> <file|dir>...
tsubaki verify [options] <checksum-file|->   (- for stdin, needs -u)
tsubaki cmp [options] <left-list> <right-list>
tsubaki algo <algorithm>
tsubaki --help
tsubaki --version
tsubaki --algorithm
tsubaki --completions
```

A subcommand is required: `sum` calculates checksums, `verify` re-checks a
file written by `sum`, `cmp` compares two such files, and `algo` explains one
algorithm. Those four names are the contract: they keep their spelling, and
nothing is added in their place. `algo-info` is still accepted as an older,
longer spelling of `algo`, but it comes with no promise and may disappear; the
name to use is `algo`.

`--help`, `--version`, `--algorithm` (or `-a`) and `--completions` (or `-C`)
are the only invocations that do not need a subcommand; any other word in the
subcommand position is rejected as an unknown subcommand. Those four flags are
checked before validation, so they also win over every argument error: running
`tsubaki -a junk` prints the algorithm table and exits 0. When several of them
are given, `--help` wins over `--version`, both win over `--algorithm`, and
that one wins over `--completions`.

Everything up to [Verifying](#verifying) describes `sum`; `verify` reuses the
checksum-file format and the `-v`, `-t`, `-k`, `-K`, `-o` and `-u` options. `cmp`
reuses the same format, but takes two files and only the `-o` and `-c`
options. `algo` takes no options and no file, only an
algorithm name.

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
(`ALGORITHM`, `KEYED`). A second `ALGORITHM`, `KEYED`, `EXTRA ARGS`, `SOURCE`
table follows when provider modules were loaded; see below.

`tsubaki algo <algorithm>` prints the same
facts for one name: a `Source:` line (`built-in`, or the path of the module
that supplied it), the digest width, the key format, and a short note. It
accepts provider names as well, and falls back to "No information provided."
for a module that does not export the optional `tsubaki_algo_info()`. A
provider that declares its own arguments (below) also gets its ArgParser
script printed verbatim, under an `ArgParser script:` line, so the switches it
accepts can be read off the module itself.

### Dynamic algorithms

An algorithm that is not built in can come from a shared object. List the
directories in `TSUBAKI_DYNAMIC_PROVIDER_PATHS`, separated by `:`; each one is
walked recursively and every file whose extension is exactly `.so` is loaded
(`libx.so.1` is skipped). Empty segments are ignored.

Ready-made providers live in
[Tsubaki-Sample-Providers](https://github.com/Lawrence-Charland/Tsubaki-Sample-Providers)
as one CMake project: clone it, build it, and the dynamic algorithms it ships
are ready to use. That repository's README lists what it currently contains.
It is also the shortest way to see the contract compiled and is a fair
template for a provider of your own.

A module you did not build yourself is worth a look before it goes into
`TSUBAKI_DYNAMIC_PROVIDER_PATHS`:

```sh
tsubaki -a                  # everything loaded, and the path each came from
tsubaki algo blake3         # width, key, note, and the switches it claims
```

`algo` prints the module's path and, when the provider declares arguments,
the whole script it accepts — enough to see whether the path is the copy you
meant and which switches it will claim. A provider that follows the contract
also names its own project or URL in that text; the `Source:` line above it is
tsubaki's own and only ever carries the path. It loads the module in order to
answer, so it tells you what the module claims, not whether it can be trusted.

- **Where the name works.** Everywhere an algorithm name is accepted, `verify`
  `-u` included. A built-in name always wins: a module that reports one is
  refused while it loads, with an error naming the file, and never reaches the
  table, so a `.so` cannot shadow `sha256`.
- **Keys.** A keyed provider is asked for a key like any built-in keyed
  algorithm (`needs a key`, exit status 64).
- **Extra command-line arguments.** An algorithm that needs more than a file
  name can declare its own switches, and `tsubaki algo <name>` prints the
  script that documents them. The switches go after the algorithm name, and
  `verify` forwards whatever it does not recognise, so
  `verify -u shake128 file.sum -L 128` works too; a provider's switches are
  matched first, so they must not reuse `tsubaki`'s own letters. A manifest
  records the algorithm name but not these parameters: `-L 128` never appears in
  it, and `verify` cannot infer it. Rechecking means writing the same parameters
  again on the `verify` command line; leaving them out or changing them makes the
  module hash with other settings, and every entry then reads as changed.
- **What goes wrong.** A path that is not a readable directory, a `.so` that
  cannot be loaded, a missing symbol: each prints one `Error:` line on stderr
  and is skipped, and none of them changes the exit status. Modules stay loaded
  for the life of the process; nothing unloads them.

Writing a provider — the symbols to export, the digest buffer limit, the build
requirements and the argument-script syntax — is described in
[PROVIDER-ABI.md](PROVIDER-ABI.md) and in
`include/dynamic_provider_header.hpp`.

`--algorithm` lists what it found in its own table:

```
---------- Dynamic providers ----------
ALGORITHM          KEYED  EXTRA ARGS  SOURCE
blake3             NO     NO          /home/user/lib/tsubaki/blake3.so
cshake128          NO     YES         /home/user/lib/tsubaki/cshake128.so
```

`SOURCE` is the module path, which makes a stale duplicate easy to spot when two
directories both hold a build of the same algorithm. `EXTRA ARGS` is `YES` for a
module that exports `tsubaki_need_more_args()`, so it takes command-line
arguments of its own; `tsubaki algo <name>` prints the script that names
them. Rows are ordered by that column first, then by `KEYED`, then by name, so
the modules that take no arguments come first.

### Options

| Option                          | Meaning                                                                                                                                                               |
| ------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `-h`, `--help`                  | print help and exit                                                                                                                                                   |
| `-V`, `--version`               | print the version and exit                                                                                                                                            |
| `-a`, `--algorithm`             | print the algorithms, whether each one is keyed, whether a provider takes extra arguments, and for a provider the module it came from, then exit; needs no subcommand |
| `-C`, `--completions`           | print words for shell completion, then exit; needs no subcommand                                                                                                      |
| `-v`, `--verbose`               | live progress indicator on stderr                                                                                                                                     |
| `-t<n>`, `--thread=<n>`         | worker threads, a positive integer; defaults to hardware concurrency (1 if unknown)                                                                                   |
| `-k<key>`, `--key=<key>`        | secret key for the keyed algorithms; ignored with a warning by the others                                                                                             |
| `-K<file>`, `--key-file=<file>` | read the key from `<file>` (`-` for stdin), byte for byte; ignored by plain digests                                                                                   |
| `-T`, `--text`                  | read the key file (or `-K -` stdin) as text: CRLF folds to LF, the trailing line ending goes, a lone CR stays; ignored by plain digests                               |
| `-o<file>`, `--output=<file>`   | write results to `<file>`, created or truncated, instead of stdout; `-` is stdout and the default                                                                     |
| `-u<algo>`, `--use-algo=<algo>` | verify with `<algo>` instead of the listing's `Algorithm:` header (verify only)                                                                                       |
| `-c`, `--canonical`             | make both paths absolute, resolve symlinks and normalize separators before comparing (cmp only)                                                                       |
| `-e<path>`, `--exclude=<path>`  | skip `<path>` and everything below it when walking a directory; repeatable                                                                                            |
| `-l<file>`, `--list=<file>`     | read a `"<checksum> <path>"` listing from `<file>`; repeatable                                                                                                        |
| `-s`, `--follow-symlinks`       | follow symbolic links to directories                                                                                                                                  |
| `-f`, `--force-rescan`          | recompute checksums already listed in the input                                                                                                                       |

`verify` accepts `-v`, `-t`, `-k`, `-K`, `-T` (`--text`) and `-o` with the same
meanings, plus `-u` to name the algorithm. It takes no algorithm or file
positional, and rejects `-e`, `-l`, `-s` and `-f`: the paths, the algorithm when
`-u` is absent and the unconditional rescan all come from the checksum file.
That file may be `-` for stdin, but the `Algorithm:` header cannot then be
prefetched, so a `-` input requires `-u`.

`cmp` accepts only `-o` and `-c`. It never hashes anything, so it takes no
algorithm, key, thread or scan option; `-v`, `-t`, `-k`, `-K`, `-T`,
`-e`, `-l`, `-s` and `-f` are rejected with exit status 64. Its two positionals
are the result files to compare, not paths to hash.

### Option values

Values are written the usual way: `=` for long options, and a short option may
carry its value sticky or be stacked. An option that takes no value consumes the
one argument after it.

```sh
tsubaki sum sha256 . --exclude=build -lold.txt
tsubaki sum hmac_sha256 . --key=secret
tsubaki sum sha256 . -svo./sum.txt     # -s -v -o ./sum.txt
```

Only two things break with convention:

- `-e` and `-l` are greedy: they keep taking plain arguments until the next
  switch. `tsubaki sum sha256 . -e a b c -l list1 list2` is three excludes and
  two lists. `-e` stops before `-l` because `excludes` is evaluated before
  `lists`.
- A switch that matches no entry is not an option error. It stays a plain
  argument and is reported as a missing file, so `--exlude=build` shows up in
  the error block.

A switch nobody defined is not an option, and the program does not try to guess
which one was meant. That is what makes the second bullet hurt: the typo passes
parsing and comes back as a missing file only once the report block is printed.
Word-list completion ([Shell completion](#shell-completion)) puts the correct
spelling in front of you before that can happen.

### Keyed hashes

A key comes from one of two places, never both:

- `--key` takes it as a literal string: the bytes of the argument are used as
  they are, with no hex or escape decoding, so `-k 00ff` is the four-byte key
  `0`, `0`, `f`, `f`.
- `--key-file` reads it from a file, or from stdin when the path is `-`. The
  whole stream is read and the bytes become the key as they are, so
  `echo secret > key` yields `secret` plus one newline (seven bytes), not
  `secret`. The key is exactly what the file holds; `xxd key | tail` tells you
  what it is. A missing, unreadable or empty file exits 66. Add `-T` (`--text`)
  when the file may have been written on Windows: it reads the file as text,
  the way `$(cat key)` would, so every CRLF folds to LF and the CR/LF left at
  the tail are dropped, while a lone CR stays as part of the key. That makes
  `secret\r\n` give `secret` and `a\r\nb\r\n` give `a\nb`. It never rewrites a
  `--key` literal.

`-k` and `-K` together are rejected with exit status 64, and so is `-K -`
together with a `-` input: stdin may be named only once. With a plain digest,
`-k`, `-K` and `-T` are ignored with a warning and the key file is
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
tsubaki verify sums.txt                 # prints the refreshed checksums
tsubaki verify sums.txt -o new.txt      # writes new.txt
tsubaki verify sums.txt -u sha256       # uses sha256 instead of the header
cat sums.txt | tsubaki verify - -u sha256   # reads the listing from stdin
```

The listing may be `-` for stdin. The `Algorithm:` header cannot be read without
consuming the stream that the paths still have to come from, so a `-` listing
requires `-u` and exits 64 without it; stdin may also be named only once, so
`-K -` is refused alongside a `-` listing. The refreshed listing goes to stdout
unless `-o` names a file.

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

A _changed_ file is one whose stored checksum differs from the recomputed one.
An _unsure_ file is one the run could not decide about: the listing gave no
checksum for it (`<NONE> <path>`), or it could not be hashed at
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
error. A dynamic algorithm with extra parameters is the same story: the listing
does not record them, `-u` names only the algorithm, and they have to be written
out again on the `verify` command line. A listing written with `none` is
rejected (exit 64), because it holds no checksums to compare; `-u` with any
other algorithm proceeds, while `-u none` runs and leaves every path unsure.
`-o` must not name the input file (exit 64), because the input would be
truncated before it is read.

### Comparing

`cmp` reads two result files written by `sum` and reports how they differ:

```sh
tsubaki cmp old.txt new.txt               # prints the comparison
tsubaki cmp old.txt new.txt -o diff.cmp   # writes diff.cmp
```

Both inputs use the checksum-file format: `#` lines, including a whole report
block, are ignored, and every other line must be a `<checksum> <path>` pair.
The separator is either two spaces or a space and the `*` binary marker, which
is not part of the path. A line that is not a `<checksum> <path>` pair —
including a bare path — and a `<NONE> <path>` line are recorded as errors and
left out, because there is no checksum to compare. One of the two
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

- `[!]` _Modified_: the same path in both listings with a different checksum.
  The left spelling is printed.
- `[D][n]` _Moved, copied, merged or renamed_: one group per checksum held by
  both sides at different paths; `[D][n][L]` lists the left paths and
  `[D][n][R]` the right ones. A checksum that only one side holds produces no
  group; its paths fall under deleted or added.
- `[U]` _Deleted or added_: paths that only the left (`[U][L]`) or only the
  right (`[U][R]`) listing names, with no matching checksum on the other side.
- `[=]` _Matched_: the same path in both listings with the same checksum.
- `[E]` _Error messages_: only when something was recorded — a line without a
  checksum, a path that `-c` could not canonicalize, or a list that failed
  while being read. The comparison continues, and the exit status is then 1.

A path repeated in one listing with the same checksum is compared once. The
same path repeated with different checksums in one listing is refused with exit
status 64, because there is no single value to compare.

Without `-c`, paths are compared exactly as written, so `./a.txt` and
`b/../a.txt` are different paths. With `-c` (or `--canonical`) each path is
first made absolute, symlinks are resolved and separators normalized, so
differently spelled routes to the same file match. Canonicalization needs the
path to exist: a deleted file cannot be canonicalized, so it will be compared
with an uncanonicalized path, with an error reported.

The result goes to stdout unless `-o` names a file, and then a notice on stderr
names that file. `-o` must not name either input (exit 64), because both lists
are read after the output is opened. The `# L = ..., R = ...` lines repeated
between sections name the two inputs; an initial copy goes to stderr so it stays
visible when the result goes to a file.

`cmp` never hashes, walks or scans anything: it reads two files, so the
interrupt handling described below does not apply to it and a `Ctrl-C` simply
kills the process.

### Shell completion

`-C` (or `--completions`) prints one line and exits: every option name — the
built-in ones and the ones a provider declares — plus every built-in algorithm
and every loaded provider, separated by spaces. Like `-a` it needs no
subcommand.

```sh
tsubaki -C                        # the line, space separated
tsubaki -C | tr ' ' '\n' | sort   # one word per line
```

That line is meant for a shell's word-list completion. In bash:

```sh
complete -o default -W "$(tsubaki -C)" tsubaki
```

The quotes are part of the command. Without them the shell splits the output
into one argument per word, `complete -W` keeps only the first of them, and
nothing but a single algorithm name ends up completing. `-o default` keeps
filename completion alive, which is what `sum` and `cmp` mostly take.

Wrapped in a function, the same line is also cheap to refresh:

```sh
tsubaki_completion() {
    complete -o default -W "$(tsubaki -C)" tsubaki
}
tsubaki_completion
```

The call on the last line runs the body once per shell, so the list is a
snapshot: run `tsubaki_completion` again after installing a provider and that
provider is in the list without a new shell. Export
`TSUBAKI_DYNAMIC_PROVIDER_PATHS` (see [Dynamic
algorithms](#dynamic-algorithms)) before the call, because the scan walks that
path and nothing else.

zsh has no `complete`; `compadd` and `_files` do the same job:

```zsh
_tsubaki() {
    local words="$(tsubaki -C)"
    compadd -- ${(s: :)words}
    _files
}
compdef _tsubaki tsubaki
```

The `--` is required, because the words begin with `-`.

What the list leaves out: `--verify`, which is internal, and any positional
context, because this is one flat list offered at every position. Subcommand
names are in it too, so they also complete once an option has started the
line. Prefixes shared by several words, such as `--text` and `--thread`,
insert nothing until a second `<TAB>` lists them.

## Inputs

Files and directories on the command line are scanned directly. A list file
(`-l`, or stdin via `-`) is read line by line, and every line is a
`<checksum> <path>` pair: there is one format, not two. A lone `-` and
`-l -` are the same thing: one treats it as an input source, the other as the
file name for `-l`, and both read stdin. Lines are read the way coreutils reads
them, so a path keeps its own leading and trailing spaces:

- an empty line, or a line whose first non-blank character is `#`, is ignored;
- a line starting with `<NONE>` followed by a whitespace character names the
  path after the separator;
- a line in `<checksum> <path>` form keeps the checksum as it is, writes
  `# Kept: <path>` and the original line to the output, and only rescans the
  path when `--force-rescan` is given;
- anything else is not a listing line: it is reported as an error and skipped.

A kept line is decoupled from the hashing run: it is not hashed and does not
show up in the report counts. It is written to the result stream while the list
is being read, so it can appear between computed lines.

Every list entry is treated as a file. A directory on a list line is rejected,
not expanded; only directory arguments on the command line are walked. A line
beginning with `#` is a comment even if it was meant as a path, so a file named
`#name` has to be written as `./#name` or by an absolute path.

The checksum may be any width, down to a single hexadecimal digit: every line
has the same two-field shape, so a checksum can never be mistaken for a path and
no minimum has to be guessed. The path is everything after the first space, so
it may contain spaces.

A file of bare paths is therefore not a listing, and is reported as an error
rather than guessed at. To hash one, give the paths a `<NONE> ` prefix or pass
them on the command line:

```sh
sed 's/^/<NONE> /' paths.txt | tsubaki sum sha256 -
xargs -d '\n' tsubaki sum sha256 < paths.txt   # GNU xargs; keeps spaces
```

`$(cat paths.txt)` also works, but only while no path contains a space: the
shell splits the result on whitespace before tsubaki sees it.

coreutils separates a checksum from its path with two spaces, or with a space
and a `*` for a binary read (`sha256sum --binary`); the `*` is dropped and is
never part of the path, so a listing written either way reads the same. `sum`
writes the two spaces and never the marker, so its output reads back unchanged
even when a path starts with `*`.

The reader takes a line apart exactly as coreutils does: leading blanks are
skipped, the checksum ends at the first space or tab, one whitespace character
is consumed, and one more is consumed only when it is a space or the `*` marker
and more than one character follows. Everything after that is the path, so a
name keeps its own leading and trailing spaces and a third space belongs to the
name. A single-space line reads like a two-space one, except that its path may
not start with `*`: write such a path as `./*name`, as an absolute path, or
after two spaces. A trailing `\r` is dropped before parsing, so a CRLF listing
reads like an LF one.

A name that contains a newline, a carriage return or a backslash is written the
way coreutils writes it: the line starts with `\` and those three characters
become `\n`, `\r` and `\\` inside the name, and such a line is read back the
same way. Any other `\x` sequence makes the line invalid.

Since `<NONE> <path>` is accepted as input as well, the output of one run can be
fed back with `-l` to redo only the files that were not hashed.

Excludes name paths, not patterns: an exclude is compared against every entry
met during a directory walk, so `-e build` drops that directory tree while
`-e '*.tmp'` matches nothing. They only affect directory scans: a file named on
the command line, or read from a list, is hashed even when it is excluded. An
exclude that is the scanned directory itself, or a parent of it, matches nothing
and is silently ignored — exclude a subdirectory instead. Do not use `-e` to
drop a directory that you also pass as an input; just leave that argument out.

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

The two fields are separated by two spaces, as in a coreutils listing; a `*`
binary marker is accepted on input but never written.

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

A failed write — a full disk, a broken descriptor, a file on a removed mount —
exits `73`; the result stream is checked when the run ends. If the results go to
a pipe whose reader has already closed it, the process is killed by `SIGPIPE`
instead (a shell reports 141), so the `73` path is not reached.

### Report keys

These key names are fixed: they are not renamed, removed or given another
meaning. `Changed` and `Unsure` come from `verify`; the other seven are written
by both subcommands.

| Key          | Meaning                                                               |
| ------------ | --------------------------------------------------------------------- |
| `Algorithm`  | the algorithm the run used; written even when the run was interrupted |
| `Processed`  | `Hashed + Failed`                                                     |
| `Hashed`     | files whose checksum was computed in this run                         |
| `Failed`     | files that could not be opened, read or hashed                        |
| `Not hashed` | files left without a checksum: interrupted, or the `none` algorithm   |
| `Duration`   | seconds spent by the workers, as a decimal number                     |
| `Changed`    | paths whose checksum differs from the manifest                        |
| `Unsure`     | paths that could not be decided: missing, unreadable or not hashed    |
| `Errors`     | recorded errors; this line is always written                          |

The headings inside the block are fixed too: `Changed files:`, `Unsure files:`,
`Error messages:`, and each error line is `Value: <number>, Message: <text>,
Operation: <text>`.

Everything else about the block is formatting rather than contract: the spaces
after the colon, the way `Duration` prints its value, and the order of the
result lines above the block.

## Exit status

| Code  | Meaning                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `0`   | no error was recorded; a `verify` run reports changed and unsure files, and a `cmp` run reports every difference, without treating them as errors                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| `1`   | at least one error was recorded: a checksum failure, a missing path, an unopenable list file, or a missing exclude; `cmp` also records a list line without a checksum, a path that `-c` could not canonicalize, or a list read error                                                                                                                                                                                                                                                                                                                                                                     |
| `64`  | invalid command line: unknown subcommand, unknown algorithm, a keyed algorithm without a key or with a key too long for it, `-k` together with `-K`, `-K -` together with a `-` input, bad thread count, or leftover arguments; `verify` also uses 64 for an input without an `Algorithm:` header when `-u` is absent, a `-` input with no `-u`, a header written with `none`, an unknown `-u` name, or an output that is the input file; `cmp` uses 64 when both lists are stdin, when the output names one of the lists, and when one list names the same path more than once with different checksums |
| `66`  | the `--key-file` cannot be read, the key file holds no key, the `verify` input is missing or is a directory, or a `cmp` list cannot be read                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `70`  | fatal start-up failure: a provider that declares arguments without a usable argument script                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `73`  | the `--output` file cannot be opened for writing, or the output stream fails                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| `130` | the run was interrupted with `Ctrl-C`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| `250` | `Ctrl-C` was pressed five times; the process aborted immediately, so the output can end without its report block                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| `255` | an unexpected exception escaped `main()`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |

`--help` and `--version` exit with `0`. `64`, `66`, `70` and `73` are the
sysexits values `EX_USAGE`, `EX_NOINPUT`, `EX_SOFTWARE` and `EX_CANTCREAT`;
`130` is the shell convention `128 + SIGINT`; `250` is just below the `255`
used for an unexpected exception, so the two cannot be confused.

## Interrupt

The first `Ctrl-C` stops the run. Workers stop before hashing the next path:
files that have not started yet are still popped, written as `<NONE>` and
counted as `Not hashed`, so the report accounts for every file the scanners
produced. A file that has already entered the hashing call is not abandoned;
for a device or a FIFO that means it keeps reading until EOF. A worker blocked
in `open()` or in an endless read never gets back to the check, so a single
`Ctrl-C` cannot stop it.

Pressing `Ctrl-C` five times gives up altogether: the handler writes a fixed
message to stderr and calls `_Exit(250)` on the spot. Nothing is flushed after
that, so the output can end without its report block and the counters are lost.

This makes an interrupted run resumable. Feed the output file back as a `-l`
input: lines that already carry a checksum are kept unchanged, and the `<NONE>`
lines are hashed again. The report block starts with `#`, so it is ignored and
the file can be used as it is. An interrupted run exits `130` even when nothing
actually failed, so a `1` still means a real error.
