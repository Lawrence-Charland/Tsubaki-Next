// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "help.hpp"

#include <string>
#include <string_view>

namespace tsubaki
{
constexpr std::string_view help_body =
    R"HELP({program} {version} - fast batch checksum calculator

Usage:
  {program} sum [options] <algorithm> <file|dir>...
  {program} --help
  {program} --version

Positional arguments:
  sum                    the only subcommand (Stay tuned for more features!).
                          everything below belongs to it.
  algorithm              checksum algorithm to use; one of:
                         none, md5, sha1, sha224, sha256, sha384, sha512,
                         sha3_256, sha3_512, blake2b512, blake2s256.
                         "none" only lists the files without computing
                         checksums.
  files or directories   files and directories to process. A directory is
                         scanned recursively. "-" reads a list of paths from
                         stdin instead of naming a file.

Options:
  -h, --help                  print this help and exit.
  -V, --version               print the version and exit.
  -v, --verbose               print a live progress indicator on stderr.
  -t<n>, --thread=<n>         number of worker threads; defaults to the number
                              of hardware threads. Alias: --threads=<n>.
  -o<file>, --output=<file>   write the results to <file> instead of stdout.
  -e<path>, --exclude=<path>  skip <path> and anything below it when scanning
                              directories; may be given more than once.
                              Alias: --excludes=<path>.
  -l<file>, --list=<file>     read paths from <file>, one per line; may be
                              given more than once. Alias: --lists=<file>.
  -s, --follow-symlinks       follow symbolic links to directories.
  -f, --force-rescan          recompute checksums that are already listed in
                              the input lists.

Option values:
  A long option takes its value with '=', as in --list=old.txt; a short option
  takes it attached, as in -lold.txt. A value can also follow the switch
  separately, as plain arguments: -t and -o take one, while -e and -l take as
  many as you give, as in "sha256 . -e a b -l list1 list2".

  Entries are matched in a fixed order, and a switch that has already been
  consumed is removed, so a later option can pass over that spot. When in
  doubt, use this order: the algorithm first, then all the inputs at once,
  then the options.

  A '\' ends the list of files; a plain argument after it is not treated as a
  file, and is rejected unless an option takes it. In a shell, quote it ('\'),
  otherwise it escapes the following space and never reaches the program.

File lists:
  Every non-empty line is either a path, or a checksum and a path separated by
  a space. A line starting with '#' is a comment. A line that carries both a
  checksum and a path is kept unchanged (marked with "# Kept: <path>") unless
  --force-rescan is given, in which case only the path is rescanned.

Output format:
  "<checksum> <path>" for every processed file, followed by a report block
  whose lines start with '#'. A file that cannot be opened, read, or hashed is
  printed as <NONE> and counted as failed. A path that does not exist, or a
  directory in a list, never reaches the hashing step: it produces no output
  line, increments no counter, and adds only an error entry. Devices and FIFOs
  are read as streams, so they can block or never reach EOF.

Examples:
  {program} sum sha256 .                      scan the current directory.
  {program} sum --thread=8 --output=sum.txt blake2b512 .
                                              use 8 threads and write to sum.txt.
  {program} sum sha512 --exclude=.git --exclude=build .
                                              skip .git and build.
  {program} sum sha256 --list=old.txt --force-rescan g1
                                              rescan the paths in old.txt.
  find . -name '*.c' | {program} sum sha1 -   read paths from stdin.
  {program} sum sha256 dir file - -e build -l old.txt
                                              inputs first, options last.

Exit status:
  0    no error was recorded.
  1    at least one error was recorded: a checksum failure, a missing path, an
       unopenable list file, or a missing exclude.
  2    invalid command line: unknown subcommand, unknown algorithm, bad thread
       count, or leftover arguments.
  70   fatal start-up failure (argc is 0).
  74   the --output file cannot be opened for writing.
  130  the run was interrupted with Ctrl-C.
  255  an unexpected exception escaped; TSUBAKI_UNEXPECTED is -1 internally.

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
} // namespace tsubaki
