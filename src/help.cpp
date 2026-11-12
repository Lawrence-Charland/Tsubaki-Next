// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "help.hpp"

#include <string>
#include <string_view>

namespace tsubaki
{
constexpr std::string_view help_body =
    R"HELP({program} {version} - high-speed batch checksum calculator

Usage:
  {program} [options] <algorithm> <file|dir>...

Positional arguments:
  algorithm              checksum algorithm to use, one of:
                         none, md5, sha1, sha224, sha256, sha384, sha512,
                         sha3_256, sha3_512, blake2b512, blake2s256.
                         "none" only lists the files without calculating.
  files or directories   files and directories to be processed. A directory is
                         scanned recursively. "-" reads a list of paths from
                         stdin instead of naming a file.

Options:
  -h, --help                  show this message and exit.
  -v, --verbose               print a live progress indicator on stderr.
  -t<n>, --thread=<n>         number of worker threads; defaults to the
                              hardware concurrency. Alias: --threads=<n>.
  -o<file>, --output=<file>   write the result to <file> instead of stdout.
  -e<path>, --exclude=<path>  skip <path>, or anything below it, while
                              scanning directories; may be given more than
                              once. Alias: --excludes=<path>.
  -l<file>, --list=<file>     read paths from <file>, one per line; may be
                              given more than once. Alias: --lists=<file>.
  -s, --follow-symlinks       follow symbolic links to directories.
  -f, --force-rescan          recompute the checksums already listed in the
                              input lists.

Option values:
  A long option takes its value with '=', as in --list=old.txt, and a short
  option takes it sticky, as in -lold.txt. A separated value is the run of
  plain arguments after the switch: -t and -o take one, -e and -l take as many
  as you give, as in "sha256 . -e a b -l list1 list2".

  Entries are matched in a fixed order and a switch that has been taken is
  removed, so a later option can walk past it. The shape that always works:
  algorithm first, then every input in one go, then the options.

  A '\' ends the list of files; a plain argument after it is not a file, and
  is rejected unless an option takes it. In a shell, write it quoted ('\'),
  otherwise it escapes the following space and never reaches the program.

File lists:
  Every non-empty line is either a path, or a checksum and a path separated by
  a space. A line starting with '#' is a comment. A line carrying both a
  checksum and a path is kept unchanged (marked with "# Kept") unless
  --force-rescan is given, in which case only the path is rescanned.

Output format:
  "<checksum> <path>" for every processed file, followed by a report block
  whose lines start with '#'. Files that cannot be read are reported as
  <NONE>, and are counted as failures.

Examples:
  {program} sha256 .                            scan the current directory.
  {program} --thread=8 --output=sum.txt blake2b512 .
                                                use 8 threads, write to sum.txt.
  {program} sha512 --exclude=.git --exclude=build .
                                                skip .git and build.
  {program} sha256 --list=old.txt --force-rescan g1
                                                rescan the paths in old.txt.
  find . -name '*.c' | {program} sha1 -         read paths from stdin.
  {program} sha256 dir file - -e build -l old.txt
                                                inputs first, options last.

Exit status:
  0 if every file was processed successfully, 1 otherwise.
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
