// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "help.hpp"
#include "checksum.hpp"

#include <string>
#include <string_view>

namespace tsubaki
{
constexpr std::string_view help_body =
    R"HELP({program} {version} - fast batch checksum calculator

Usage:
  {program} sum [options] <algorithm> <file|dir>...
  {program} verify [options] <checksum-file>
  {program} cmp [options] <left-list> <right-list>
  {program} --help
  {program} --version
  {program} --algorithm

Subcommands:
  sum                    calculate checksums; everything marked "sum" below
                         belongs to it.
  verify                 re-check the paths named in a checksum file written
                         by sum. The algorithm comes from the file's
                         "Algorithm:" header. A path whose stored checksum no
                         longer matches is reported as changed; a path with no
                         checksum to compare, or one that could not be hashed
                         (cut off by an interrupt, or a read/hash failure), is
                         reported as unsure. The freshly computed checksums are
                         written to <checksum-file>.verify unless -o is given.
                         Changed and unsure files are reported in the summary
                         but are not errors by themselves.
  cmp                    compare two result files written by sum. Every path is
                         put in one of four sections: modified (same path,
                         different checksum), moved/copied/merged/renamed (same
                         checksum at different paths), deleted/added (present
                         on one side only), or matched (same path and checksum).
                         The result goes to <left>_to_<right>.cmp unless -o is
                         given. Differences are reported but are not errors.

Positional arguments (sum):
  algorithm              checksum algorithm to use; one of:
                         none, md5, sha1, sha224, sha256, sha384, sha512,
                         sha3_256, sha3_512, blake2b512, blake2s256,
                         hmac_md5, hmac_sha1, hmac_sha224, hmac_sha256,
                         hmac_sha384, hmac_sha512, hmac_sha3_256,
                         hmac_sha3_512, blake2b512_mac, blake2s256_mac.
                         "none" only lists the files without computing
                         checksums. The hmac_* and blake2*_mac algorithms are
                         keyed hashes and require -k/--key or -K/--key-file.
  files or directories   files and directories to process. A directory is
                         scanned recursively, hashing regular files only.
                         "-" reads a list of paths from stdin instead of
                         naming a file.

Positional arguments (verify):
  checksum-file          the result file to verify, normally written by sum.

Positional arguments (cmp):
  left-list              the older result file to compare, normally written by
                         sum; "-" reads it from stdin.
  right-list             the newer result file; "-" reads it from stdin too, but
                         only one of the two lists may be stdin.

Options (sum and general):
  -h, --help                  print this help and exit.
  -V, --version               print the version and exit.
  -a, --algorithm             print the algorithms in a table, marking which
                              ones are keyed, and exit; needs no subcommand.
  -v, --verbose               print a live progress indicator on stderr;
                              not shown when the algorithm is "none".
  -t<n>, --thread=<n>         number of worker threads; defaults to the number
                              of hardware threads, or 1 if it reports 0.
                              Alias: --threads=<n>.
  -k<key>, --key=<key>        secret key for the keyed algorithms (hmac_*,
                              blake2*_mac); ignored with a warning when the
                              algorithm needs none. Keyed BLAKE2 caps the key
                              at 64 bytes (blake2b512_mac) or 32 bytes
                              (blake2s256_mac). The key is part of the
                              command line, so it shows up in "ps" and in the
                              shell history; do not pass a real secret on a
                              shared machine.
  -K<file>, --key-file=<file> read the key from <file> instead of the command
                              line; "-" reads it from stdin. Trailing CR/LF are
                              dropped. Conflicts with -k. With a keyed
                              algorithm, "-" cannot be named twice (as key
                              source and as an input), and a missing,
                              unreadable or empty file exits 66. Ignored with
                              a warning by a plain digest.
  -o<file>, --output=<file>   write the results to <file> instead of stdout
                              (sum), <checksum-file>.verify (verify) or
                              <left>_to_<right>.cmp (cmp).
  -c, --canonical             make both paths absolute, resolve symlinks and
                              normalize separators before comparing them; cmp
                              only. Canonicalization needs the path to exist, so
                              a deleted file becomes an error entry instead of a
                              deletion.
  -e<path>, --exclude=<path>  skip <path> and anything below it when scanning
                              directories; may be given more than once.
                              Alias: --excludes=<path>.
  -l<file>, --list=<file>     read paths from <file>, one per line; may be
                              given more than once. Alias: --lists=<file>.
  -s, --follow-symlinks       follow symbolic links to directories.
  -f, --force-rescan          recompute checksums that are already listed in
                              the input lists.

  verify accepts -v, -t, -k, -K and -o with the same meanings. It takes no
  algorithm or file argument and does not accept -e, -l, -s or -f: the paths,
  the algorithm and the unconditional rescan all come from the checksum file.

  cmp accepts only -o and -c. It never hashes anything, so it has no algorithm,
  key, thread or scan option; -v, -t, -k, -K, -e, -l, -s and -f exit 64.

Option values:
  A long option takes its value with '=', as in --list=old.txt; a short option
  takes it attached, as in -lold.txt. A value can also follow the switch
  separately, as plain arguments: -t, -k, -K and -o take one, while -e and -l
  take as many as you give, as in "sha256 . -e a b -l list1 list2".

  Entries are matched in a fixed order, and a switch that has already been
  consumed is removed, so a later option can pass over that spot. When in
  doubt, use this order: the algorithm first, then all the inputs at once,
  then the options.

File lists:
  Every non-empty line is either a path, or a checksum (at least 32 hex
  digits) and a path separated by a space. The marker "<NONE>" followed by a
  space names the path after it. A line starting with '#' is a comment. A line
  that carries both a checksum and a path is kept unchanged (marked with
  "# Kept: <path>") unless --force-rescan is given, in which case only the path
  is rescanned.

  verify reads the same format, so a file written by sum can be passed to it as
  is: comment lines, including the report block, are ignored.

  cmp reads the same format too, but requires a checksum on every line: a bare
  path or <NONE> <path> has nothing to compare and becomes an error entry.

Output format:
  A sum or verify stream starts with "# tsubaki checksum result", then
  "<checksum> <path>" for every processed file, followed by a report block
  whose lines start with '#'. A file that cannot be opened, read, or hashed is
  printed as <NONE> and counted as failed. A path that does not exist, or a
  directory in a list, never reaches the hashing step: it produces no output
  line, increments no counter, and adds only an error entry. A device or a FIFO
  named explicitly is read as a stream, so it can block or never reach EOF; a
  socket is refused. A directory scan yields regular files only, so special
  files met during a walk are skipped.

  A verify run adds "# Changed:" and "# Unsure:" lines to the report. Unsure
  means the run had nothing to compare: the listing gave no checksum for that
  path, or the path could not be hashed at all. The affected paths are then
  listed under each count in the result stream; the copy of the report written
  to stderr omits them.

  A cmp stream starts with "# tsubaki comparison result" and then the sections
  "[!] Modified:", "[D] Moved,copied,merged or renamed:" (one numbered group
  per checksum shared by both sides, with [L] and [R] path lists), "[U]
  Deleted or added:" ([L] and [R] lists), "[=] Matched:", and, only when
  something was recorded, "[E] Error messages:". Checksums are compared as
  lowercase hexadecimal.

  Write errors are checked at the end through std::ostream::fail(): a full disk,
  a broken descriptor, or a file on a removed mount leaves the stream failed
  and the exit status 73. A pipe with no reader raises SIGPIPE, which kills the
  process (141 in a shell) before that check.

Examples:
  {program} sum sha256 .                      scan the current directory.
  {program} sum --thread=8 --output=sum.txt blake2b512 .
                                              8 threads, results in sum.txt.
  {program} sum hmac_sha256 . --key=secret    HMAC-SHA256 of every file below.
  {program} sum hmac_sha256 . -K secret.key   the same, key read from a file.
  {program} sum sha512 --exclude=.git --exclude=build .
                                              skip .git and build.
  {program} sum sha256 --list=old.txt --force-rescan g1
                                              rescan the paths in old.txt.
  find . -name '*.c' | {program} sum sha1 -   read paths from stdin.
  {program} sum sha256 dir file - -e build -l old.txt
                                              inputs first, options last.
  {program} verify sums.txt                   re-check the files in sums.txt and
                                              write sums.txt.verify.
  {program} verify sums.txt -o -              print the refreshed checksums.
  {program} cmp old.txt new.txt               compare, writing old_to_new.cmp.
  {program} cmp old.txt new.txt -o -          print the comparison.
  {program} cmp old.txt new.txt -c -o -       resolve symlinks first, then
                                              compare.

Exit status:
  0    no error was recorded. verify reports changed and unsure files, and cmp
       reports every difference, without treating them as errors.
  1    at least one error was recorded: a checksum failure, a missing path, an
       unopenable list file, or a missing exclude. cmp also records a list line
       without a checksum, a path it could not canonicalize, or a list read
       error.
  64   invalid command line: unknown subcommand, unknown algorithm, a keyed
       algorithm without a key or with a key too long for it, -k together
       with -K, stdin named twice, bad thread count, or leftover arguments.
       verify also uses 64 for an input file without an "Algorithm:" header,
       one written with algorithm "none", or an output file that is the input.
       cmp uses 64 when both lists are stdin, or when one list names a path
       more than once with different checksums.
  66   the --key-file cannot be read, the key file holds no key, the verify
       input is missing or is not a regular file, or a cmp list cannot be read.
  70   fatal start-up failure (argc is 0).
  73   the --output file cannot be opened for writing, or the output stream
       fails.
  130  the run was interrupted with Ctrl-C.
  250  Ctrl-C was pressed five times, so the process aborted on the spot; the
       output can end without its report block.
  255  an unexpected exception escaped main().

Repository:
  https://github.com/Lawrence-Charland/Tsubaki-Next
)HELP";

std::string replace_placeholder(std::string_view body, std::string_view key,
                                std::string_view value)
{
    std::string result;
    result.reserve(body.size() + value.size() * 8);
    for (std::size_t pos = 0;;)
    {
        const std::size_t found = body.find(key, pos);
        if (found == std::string_view::npos)
        {
            result.append(body.substr(pos));
            break;
        }
        result.append(body.substr(pos, found - pos));
        result.append(value);
        pos = found + key.size();
    }
    return result;
}

std::string help_text(std::string_view program)
{
    return replace_placeholder(
        replace_placeholder(help_body, "{program}", program), "{version}",
        version);
}

void list_algorithm(std::ostream &os)
{
    size_t w = std::string_view{"ALGORITHM"}.size();
    for (const auto &it : mapper)
        w = std::max(w, std::string_view(it).size());
    w += 2;

    os << std::left << std::setw(w) << "ALGORITHM" << "KEYED\n";
    for (const auto &it : mapper)
        os << std::left << std::setw(w) << it
           << (is_keyed(str_to_alg(it)) ? "Yes" : "No") << '\n';
}
} // namespace tsubaki
