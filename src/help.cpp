// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "help.hpp"
#include "checksum.hpp"

#include <string>
#include <string_view>

namespace tsubaki
{
constexpr std::string_view help_body =
    R"HELP({program} {version} - build a checksum manifest, and verify or compare it

Usage:
  {program} sum [options] <algorithm> <file|dir>...
  {program} verify [options] <manifest>
  {program} cmp [options] <left-manifest> <right-manifest>
  {program} --help | --version | --algorithm

Subcommands:
  sum       calculate checksums and write a manifest
  verify    re-check a manifest written by sum
  cmp       compare two manifests

Algorithms:
  none  md5  sha1  sha224  sha256  sha384  sha512  sha3_256  sha3_512
  blake2b512  blake2s256
  hmac_md5  hmac_sha1  hmac_sha224  hmac_sha256  hmac_sha384  hmac_sha512
  hmac_sha3_256  hmac_sha3_512  blake2b512_mac  blake2s256_mac

General:
  -h, --help                  print this help
  -V, --version               print the version
  -a, --algorithm             list algorithms and exit

Options (sum, verify):
  -v, --verbose               progress on stderr
  -t<n>, --thread=<n>         worker threads
  -k<key>, --key=<key>        key for keyed algorithms
  -K<file>, --key-file=<file> key file, "-" for stdin
  -o<file>, --output=<file>   output file, "-" for stdout

Options (sum only):
  -e<path>, --exclude=<path>  exclude a path (repeatable)
  -l<file>, --list=<file>     read paths from a file (repeatable)
  -s, --follow-symlinks       follow directory symlinks
  -f, --force-rescan          recompute listed checksums

Options (cmp only):
  -c, --canonical             compare canonical paths
  -o<file>, --output=<file>   output file, "-" for stdout

Option forms:
  -ksecret   -k secret        --key=secret   --key secret
  -llist1    -l list1 list2   --list=list1   --list list1 list2

Examples:
  {program} sum sha256 .
  {program} sum sha256 . -e build -l old.txt
  {program} sum hmac_sha256 . -K key
  {program} verify sums.txt -o -
  {program} cmp old.txt new.txt -o -

Exit status:
  0 ok  1 error  64 usage  66 input  70 startup  73 output
  130 interrupted  250 aborted  255 unexpected

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
