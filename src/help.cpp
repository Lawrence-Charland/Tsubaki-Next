// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "help.hpp"
#include "ap_script.hpp"
#include "argparser.hpp"
#include "checksum.hpp"
#include "cmdline.hpp"
#include "dpframework.hpp"
#include "exit_codes.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <openssl/crypto.h>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tsubaki
{
const constexpr std::string_view help_body =
    R"HELP(Tsubaki-Next {version} - Generate, verify and compare checksum manifests

Usage:
  {program} sum [options] <algorithm> <file|dir>...
  {program} verify [options] <manifest|->
  {program} cmp [options] <left-manifest> <right-manifest>
  {program} algo <algorithm>
  {program} --help | --version | --algorithm

Subcommands:
  sum       calculate checksums and write a manifest
  verify    re-check a manifest written by sum
  cmp       compare two manifests
  algo      print what an algorithm is and how to key it

Built-in Algorithms:
  none  md5  sha1  sha224  sha256  sha384  sha512  sha3_256  sha3_512
  blake2b512  blake2s256
  hmac_md5  hmac_sha1  hmac_sha224  hmac_sha256  hmac_sha384  hmac_sha512
  hmac_sha3_256  hmac_sha3_512  blake2b512_mac  blake2s256_mac
  (Try "{program} -a" to see the whole list, which includes dynamic providers.)

General:
  -h, --help                  print this help
  -V, --version               print the version
  -a, --algorithm             list algorithms and exit
  -C, --completions           print words for shell completion and exit

Options (sum, verify):
  -v, --verbose               progress on stderr
  -t<n>, --thread=<n>         worker threads
  -k<key>, --key=<key>        key for keyed algorithms
  -K<file>, --key-file=<file> key file, "-" for stdin
  -T, --text                  read a key file as text: CRLF to LF, trim endings
  -o<file>, --output=<file>   output file, "-" for stdout (the default)

Options (sum only):
  -e<path>, --exclude=<path>  exclude a path (repeatable)
  -l<file>, --list=<file>     read a "<checksum> <path>" listing (repeatable)
  -s, --follow-symlinks       follow directory symlinks
  -f, --force-rescan          recompute listed checksums
)HELP"
#if defined(_WIN32)
    "      --crlf                  write the output file with CRLF line "
    "endings\n"
#endif
    R"HELP(
Options (cmp only):
  -c, --canonical             compare canonical paths
  -o<file>, --output=<file>   output file, "-" for stdout (the default)

Option forms:
  -ksecret   -k secret        --key=secret   --key secret
  -llist1    -l list1 list2   --list=list1   --list list1 list2

Examples:
  {program} sum sha256 .
  {program} sum sha256 . -e build -l old.txt
  {program} sum hmac_sha256 . -K key
  {program} verify sums.txt
  {program} cmp old.txt new.txt
  {program} algo sha256

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
        replace_placeholder(std::string(help_body), "{program}", program),
        "{version}", version);
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
    const auto &providers = dpf_instance::ins().show_mapper();
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
           << from_fs_path(it.second.display_path) << std::endl;
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

namespace
{
std::string platform_prober()
{
    std::string result;
#if defined(__APPLE__)
    result = "macOS";
#elif defined(__linux__)
    result = "Linux";
#elif defined(__FreeBSD__)
    result = "FreeBSD";
#elif defined(__NetBSD__)
    result = "NetBSD";
#elif defined(__OpenBSD__)
    result = "OpenBSD";
#elif defined(__DragonFly__)
    result = "DragonFly";
#elif defined(__illumos__)
    result = "illumos";
#elif defined(__sun)
    result = "Solaris";
#elif defined(__CYGWIN__)
    result = "Cygwin";
#elif defined(_WIN32)
    result = "Windows";
#else
    result = "Unknown OS";
#endif
    result += " (";
#if defined(__x86_64__) || defined(_M_X64)
    result += "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(__APPLE__) || defined(_WIN32)
    result += "arm64";
#else
    result += "aarch64";
#endif
#elif defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 64
    result += "riscv64";
#elif defined(__loongarch64) ||                                                \
    (defined(__loongarch__) && defined(__loongarch_lp64))
    result += "loongarch64";
#elif defined(__powerpc64__) && defined(__BYTE_ORDER__) &&                     \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    result += "ppc64le";
#elif defined(__powerpc64__) && defined(__BYTE_ORDER__) &&                     \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    result += "ppc64";
#elif defined(__i386__) || defined(_M_IX86)
    result += "x86";
#elif defined(__arm__) || defined(_M_ARM)
#if (defined(__ARM_ARCH) && __ARM_ARCH >= 7) || defined(_M_ARM)
    result += "armv7l";
#else
    result += "arm";
#endif
#else
    result += "unknown arch";
#endif
#if defined(__GLIBC__)
    result += ", glibc " + std::to_string(__GLIBC__) + '.' +
              std::to_string(__GLIBC_MINOR__);
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__UCLIBC__)
    // musl defines no self-identifying macro; glibc-free Linux is musl here.
    result += ", musl";
#endif
    result += ')';
    return result;
}

std::string cpu_feature_prober()
{
    std::string result;
#if (defined(__x86_64__) || defined(__i386__)) &&                              \
    (defined(__GNUC__) || defined(__clang__))
    const auto add = [&result](const char *name)
    {
        if (!result.empty())
            result += ' ';
        result += name;
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
    if (result.empty())
        result = "not reported";
    return result;
}

std::string cxxstd_prober()
{
    std::string result;
#if __cplusplus >= 202302L
    result = "C++23";
#elif __cplusplus >= 202002L
    result = "C++20";
#elif __cplusplus >= 201703L
    result = "C++17";
#else
    result = "C++" + std::to_string(__cplusplus);
#endif
    return result;
}

std::string build_config_prober()
{
    std::string result;
#if defined(NDEBUG)
    result = "release";
#else
    result = "debug";
#endif
#if defined(__OPTIMIZE__)
    result += ", optimized";
#else
    result += ", unoptimized";
#endif
    return result;
}

std::string openssl_ver_prober()
{
    std::string result;
#if defined(OPENSSL_FULL_VERSION_STRING)
    result = OpenSSL_version(OPENSSL_FULL_VERSION_STRING);
#else
    result = OpenSSL_version(OPENSSL_VERSION_STRING);
#endif
    return result;
}

std::string build_time_prober()
{
#if defined(TSUBAKI_BUILD_TIMESTAMP)
    std::string result = TSUBAKI_BUILD_TIMESTAMP;
#else
    std::string result = "unknown";
#endif
    return result;
}

std::string commit_prober()
{
#if defined(TSUBAKI_GIT_COMMIT)
    const std::string result = TSUBAKI_GIT_COMMIT;
#else
    const std::string result = "unknown";
#endif
    return result;
}
} // namespace

void show_version(std::ostream &os)
{
    // First line stays machine-parseable: "<name> <version>".
    os << "Tsubaki " << version << '\n';
    os << "     Commit:         " << commit_prober() << std::endl;
    os << "     C++ Standard:   " << cxxstd_prober() << std::endl;
    os << "     Compiler:       " << get_compiler_info() << std::endl;
    os << "     Build config:   " << build_config_prober() << std::endl;
    os << "     OpenSSL:        " << openssl_ver_prober() << std::endl;
    os << "     Platform:       " << platform_prober() << std::endl;
    os << "     CPU features:   " << cpu_feature_prober() << std::endl;
    os << "     CPU threads:    "
       << std::to_string(std::thread::hardware_concurrency()) << std::endl;
    os << "     Build time:     " << build_time_prober() << std::endl;
    os << "     License:        GPL-2.0-only" << std::endl;
    os << "     Homepage:       "
          "https://github.com/Lawrence-Charland/Tsubaki-Next"
       << std::endl;
}

std::unordered_set<std::string> get_completion()
{
    auto result = def_args().ls_options();
    // Must exist, or it is unexpected error.
    result.erase(result.find("--verify"));
    result.insert(mapper.begin(), mapper.end());
    auto dynamic_mapper = dpf_instance::ins().show_mapper();
    for (auto &look : dynamic_mapper)
    {
        result.insert(look.first);
        if (!look.second.need_args())
            continue;
        if (look.second.arg_def == nullptr)
        {
            std::cerr
                << "Fatal: the provider needs more args but didn't define "
                   "the ArgParser script."
                << std::endl;
            _Exit(TSUBAKI_FATAL);
        }
        auto dynamic_options =
            ap_instance(parse_script(std::string(look.second.arg_def())))
                .ls_options();
        result.insert(dynamic_options.begin(), dynamic_options.end());
    }
#define SUBCOMMAND_COMP(name) result.insert(#name);
    SUBCOMMAND_LIST(SUBCOMMAND_COMP)
#undef SUBCOMMAND_COMP
    return result;
}

void print_completion(std::ostream &os)
{
    auto completions = get_completion();
    for (auto &comp : completions)
        os << comp << ' ';
    os << std::endl;
}
} // namespace tsubaki
