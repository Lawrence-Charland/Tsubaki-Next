// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "help.hpp"
#include "checksum.hpp"
#include "dpframework.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <openssl/crypto.h>
#include <ostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace tsubaki
{
const std::string help_body =
    R"HELP({program} {version} - build a checksum manifest, and verify or compare it

Usage:
  {program} sum [options] <algorithm> <file|dir>...
  {program} verify [options] <manifest|->
  {program} cmp [options] <left-manifest> <right-manifest>
  {program} algo-info <algorithm>
  {program} --help | --version | --algorithm

Subcommands:
  sum       calculate checksums and write a manifest
  verify    re-check a manifest written by sum
  cmp       compare two manifests
  algo-info print what an algorithm is and how to key it

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
  -T, --text                  read a key file as text: CRLF to LF, trim endings
  -o<file>, --output=<file>   output file, "-" for stdout

Options (sum only):
  -e<path>, --exclude=<path>  exclude a path (repeatable)
  -l<file>, --list=<file>     read a "<checksum> <path>" listing (repeatable)
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

std::string replace_placeholder(const std::string &body, const std::string &key,
                                const std::string &value)
{
    std::string result;
    result.reserve(body.size() + value.size() * 8);
    for (std::size_t pos = 0;;)
    {
        const std::size_t found = body.find(key, pos);
        if (found == std::string::npos)
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

void print_help(std::ostream &os, const std::string &program)
{
    os << replace_placeholder(
        replace_placeholder(help_body, "{program}", program), "{version}",
        version);
}

void list_algorithm(std::ostream &os)
{
    os << "---------- Built-in ----------" << std::endl;
    size_t w = std::string("ALGORITHM").length();
    for (const auto &it : mapper)
        if (it != "none")
            w = std::max(w, std::string(it).length());
    w += 2;
    os << std::left << std::setw(w) << "ALGORITHM" << "KEYED\n";
    for (const auto &it : mapper)
        if (it != "none")
            os << std::left << std::setw(w) << it
               << (is_keyed(str_to_alg(it)) ? "YES" : "NO") << '\n';
    os << "(" << mapper.size() - 1 << " built-in algorithms in total)"
       << std::endl;
    dpf_instance dpf;
    const auto &providers = dpf.show_mapper();
    if (providers.empty())
        return;
    os << std::endl << "---------- Dynamic providers ----------" << std::endl;
    std::size_t sw = std::string("ALGORITHM").length();
    std::size_t kw = std::string("KEYED").length() + 2;
    std::size_t mw = std::string("EXTRA ARGS").length() + 2;
    std::vector<std::pair<std::string, provider>> pros(providers.begin(),
                                                       providers.end());
    std::sort(pros.begin(), pros.end(),
              [](const std::pair<std::string, provider> &l,
                 const std::pair<std::string, provider> &r) -> bool
              {
                  if (l.second.need_args() != r.second.need_args())
                      return l.second.need_args() < r.second.need_args();
                  if (l.second.is_keyed() != r.second.is_keyed())
                      return l.second.is_keyed() < r.second.is_keyed();
                  return l.first < r.first;
              });
    for (auto &it : pros)
        sw = std::max(sw, it.first.length());
    sw += 2;
    os << std::setw(sw) << "ALGORITHM" << std::setw(kw) << "KEYED"
       << std::setw(mw) << "EXTRA ARGS" << "SOURCE" << std::endl;
    for (auto &it : pros)
        os << std::setw(sw) << it.first << std::setw(kw)
           << (it.second.is_keyed() ? "YES" : "NO") << std::setw(mw)
           << (it.second.need_args() ? "YES" : "NO")
           << it.second.display_path.string() << std::endl;
    os << "(" << pros.size() << " dynamic algorithms loaded)" << std::endl;
}

std::string get_compiler_info()
{
#if defined(__clang__)
    return "Clang " + std::string(__clang_version__);
#elif defined(__GNUC__)
    return "GCC " + std::string(__VERSION__);
#else
    return "Unknown";
#endif
}

void show_version(std::ostream &os)
{
    // First line stays machine-parseable: "<name> <version>".
    os << "Tsubaki " << version << '\n';

#if defined(TSUBAKI_GIT_COMMIT)
    const std::string commit = TSUBAKI_GIT_COMMIT;
#else
    const std::string commit = "unknown";
#endif

    std::string config;
#if defined(NDEBUG)
    config = "release";
#else
    config = "debug";
#endif
#if defined(__OPTIMIZE__)
    config += ", optimized";
#else
    config += ", unoptimized";
#endif

    std::string cxxstd;
#if __cplusplus >= 202302L
    cxxstd = "C++23";
#elif __cplusplus >= 202002L
    cxxstd = "C++20";
#elif __cplusplus >= 201703L
    cxxstd = "C++17";
#else
    cxxstd = "C++" + std::to_string(__cplusplus);
#endif

    std::string platform;
#if defined(__APPLE__)
    platform = "macOS";
#elif defined(__linux__)
    platform = "Linux";
#elif defined(__FreeBSD__)
    platform = "FreeBSD";
#else
    platform = "unknown OS";
#endif
    platform += " (";
#if defined(__x86_64__) || defined(_M_X64)
    platform += "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    platform += "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    platform += "x86";
#elif defined(__arm__) || defined(_M_ARM)
    platform += "arm";
#else
    platform += "unknown arch";
#endif
#if defined(__GLIBC__)
    platform += ", glibc " + std::to_string(__GLIBC__) + '.' +
                std::to_string(__GLIBC_MINOR__);
#elif defined(__MUSL__)
    platform += ", musl";
#endif
    platform += ')';

    // Runtime CPU dispatch underlies checksum throughput, so report it.
    std::string features;
#if (defined(__x86_64__) || defined(__i386__)) &&                              \
    (defined(__GNUC__) || defined(__clang__))
    const auto add = [&features](const char *name)
    {
        if (!features.empty())
            features += ' ';
        features += name;
    };
    // __builtin_cpu_supports() only accepts string literals.
    if (__builtin_cpu_supports("aes"))
        add("aes");
    if (__builtin_cpu_supports("avx2"))
        add("avx2");
    if (__builtin_cpu_supports("sha"))
        add("sha");
    if (__builtin_cpu_supports("avx512f"))
        add("avx512f");
#endif
    if (features.empty())
        features = "not reported";

    std::string openssl;
#if defined(OPENSSL_FULL_VERSION_STRING)
    openssl = OpenSSL_version(OPENSSL_FULL_VERSION_STRING);
#else
    openssl = OpenSSL_version(OPENSSL_VERSION_STRING);
#endif

    os << "     Commit:         " << commit << std::endl;
    os << "     C++ Standard:   " << cxxstd << std::endl;
    os << "     Compiler:       " << get_compiler_info() << std::endl;
    os << "     Build config:   " << config << std::endl;
    os << "     OpenSSL:        " << openssl << std::endl;
    os << "     Platform:       " << platform << std::endl;
    os << "     CPU features:   " << features << std::endl;
    os << "     CPU threads:    "
       << std::to_string(std::thread::hardware_concurrency()) << std::endl;
#if defined(TSUBAKI_BUILD_TIMESTAMP)
    os << "     Built:          " << TSUBAKI_BUILD_TIMESTAMP << std::endl;
#endif
    os << "     License:        GPL-2.0-only" << std::endl;
    os << "     Homepage:       "
          "https://github.com/Lawrence-Charland/Tsubaki-Next"
       << std::endl;
}
} // namespace tsubaki
