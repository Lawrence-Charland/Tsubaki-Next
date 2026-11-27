// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_HEADER_HPP
#define LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_HEADER_HPP

#include <cstddef>

// The ABI between tsubaki and a provider module. These declarations are
// normative for the signatures; the behaviour behind them -- memory ownership,
// digest and key rules, the argument script, build requirements and the
// compatibility policy -- is docs/PROVIDER-ABI.md:
// https://github.com/Lawrence-Charland/Tsubaki-Next/blob/main/docs/PROVIDER-ABI.md
//
// Breaking one of these is undefined behaviour, not an error message:
//   - Only plain data crosses the boundary. The path, the key, result.result
//     and the argument strings are tsubaki's memory: read-only, and valid only
//     for the duration of the call. Never keep a pointer.
//   - Fill `result` and `errcode` on every path. The digest is hexadecimal,
//     lowercase and NUL terminated inside result.result, never past its end,
//     and at most TSUBAKI_MAX_RESULT_LENGTH bytes including the NUL.
//   - Report failures through `errcode`, an errno value. No exception, no
//     std::error_code and nothing the module allocated may cross the boundary.
//   - Every entry point keeps its noexcept.
//   - The module is self-contained and never unloaded: tsubaki exports no
//     symbols to it and hands it no runtime, so build it as a C++23
//     position-independent shared object for the same architecture, calling
//     convention and glibc generation.
//
// 上方英文为契约不变式；完整说明（内存归属、摘要与密钥规则、参数脚本、构建要求、
// 兼容性承诺）见 docs/PROVIDER-ABI.zh-cn.md。

// 256 hex characters, twice the widest digest built here, plus the terminating
// NUL that lets tsubaki read the result as a C string.
#define TSUBAKI_MAX_RESULT_LENGTH 257

namespace tsubaki
{
extern "C"
{
    struct tsubaki_checksum_result
    {
        char result[TSUBAKI_MAX_RESULT_LENGTH];
        int errcode;
    };

    // The values parsed for one entry of tsubaki_arg_definition(): `contentc`
    // of them in `contentv`, each NUL terminated. `contentv` may be null when
    // `contentc` is 0. Both belong to tsubaki and live only for the call.
    struct tsubaki_pass_content
    {
        std::size_t contentc;
        const char **contentv;
    };

    const char *tsubaki_provider_name() noexcept;

    // Optional at the ABI level, but a published provider exports it and states
    // where the module comes from. Do not label that line `Source:`; tsubaki
    // prints its own above this text.
    const char *tsubaki_algo_info() noexcept;

    // Optional. Leaving this symbol out is exactly equivalent to exporting it
    // and returning false: the provider is unkeyed and the checker below is
    // never looked up.
    bool tsubaki_is_keyed() noexcept;

    // `key` is a byte string of `length` bytes, not a C string. Required when
    // tsubaki_is_keyed() returns true.
    bool tsubaki_check_key(const char *key, std::size_t length) noexcept;

    // `length` is 0 for a provider that ignores the key.
    tsubaki_checksum_result tsubaki_checksum_file(const char *path,
                                                  const char *key,
                                                  std::size_t length) noexcept;

    // Optional. Leaving this symbol out is exactly equivalent to exporting it
    // and returning false: the provider takes no arguments and the two symbols
    // below are never looked up.
    bool tsubaki_need_more_args() noexcept;

    // Required when tsubaki_need_more_args() returns true; must not be null.
    const char *tsubaki_arg_definition() noexcept;

    // Required when tsubaki_need_more_args() returns true. `content` holds one
    // entry per argument the script defines, in that same order, `cnt` of them;
    // an entry that matched nothing has contentc == 0. An empty string accepts
    // the command line; anything else is printed as the error.
    const char *tsubaki_take_args(tsubaki_pass_content *content,
                                  std::size_t cnt) noexcept;
}
} // namespace tsubaki
#endif
