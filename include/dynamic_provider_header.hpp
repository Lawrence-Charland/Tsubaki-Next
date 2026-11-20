// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_HEADER_HPP
#define LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_HEADER_HPP

// The contract between tsubaki and a provider module, in full: a provider is a
// shared object exporting exactly these four symbols, and tsubaki reaches it
// through nothing else.
//
//   - Build it as C++23 with the same compiler and standard library as the
//     tsubaki binary. std::string, std::filesystem::path and
//     std::expected<std::string, std::error_code> cross the boundary by
//     reference, so another libstdc++ is not compatible, and a C compiler
//     cannot produce one.
//   - Every function keeps its noexcept. Nothing may escape into tsubaki; wrap
//     a body that can throw.
//   - tsubaki_checksum_file() assigns `out` on every path, success or failure.
//     A default-constructed `out` means "success, empty digest" and would be
//     written to the manifest as a blank checksum.
//   - A successful digest is hexadecimal and at least 32 characters long,
//     MD5's width. listscanner recognises a checksum from that length up, so a
//     shorter value is read back as a path and a manifest holding it verifies
//     as unsure. Write lowercase, like the built-in algorithms.
//   - The name is the contract. A digest that changes length or meaning needs a
//     new name, not a new version of the old one: a manifest records only the
//     name, so keeping it means keeping the old output.
//   - tsubaki_check_key() is asked only for a keyed provider, and only about a
//     non-empty key; it decides whether the key is acceptable.
//   - tsubaki_provider_name() returns a non-null, stable string, and it must
//     not be a built-in name: while loading, tsubaki compares it against its own
//     list and refuses the module there, before resolving the other symbols.
//   - A module is never unloaded, so a provider may keep whatever the loader
//     initialized, and must not assume any cleanup call.
//
// tsubaki 与 provider 模块之间的契约，全文如下：provider 是一个共享对象（`.so`），
// 只导出下面这四个符号，tsubaki 也只通过它们访问 provider。
//
//   - 按 C++23 编译，编译器与标准库要和编译出 tsubaki 可执行文件的那一套一致。
//     std::string、std::filesystem::path 和
//     std::expected<std::string, std::error_code> 都是按引用跨边界的，
//     换成另一份 libstdc++ 就不兼容，C 编译器也造不出这样的模块。
//   - 每个函数都要守住 noexcept。任何东西都不能逃进 tsubaki；函数体里可能
//     抛异常的部分自己用 try/catch 包住。
//   - tsubaki_checksum_file() 无论走哪条分支都要给 `out` 赋值，成功失败都
//     一样。默认构造的 `out` 表示"成功、摘要为空"，这个空值会被当成空白
//     校验和写进清单。
//   - 成功的摘要是十六进制，长度至少 32 个字符，也就是 MD5 的宽度。
//     listscanner 只把这一长度及以上的串当成校验和，所以更短的值会被当成
//     路径读回来，含这种值的清单在 verify 时算 unsure。写小写，和内置算法
//     一致。
//   - 名字就是契约。摘要的长度或含义变了，就要换新名字，而不是在旧名字下
//     出新版本：清单只记录名字，留着名字就等于承诺输出不变。
//   - tsubaki_check_key() 只在 provider 带密钥时被询问，而且只会收到非空
//     密钥；由它判断该密钥是否可用。
//   - tsubaki_provider_name() 返回非空且稳定的字符串，而且不能是内建算法
//     名字：加载时 tsubaki 会拿它去比对自己的内建清单，命中就在那里拒掉
//     这个模块，其余符号根本不会被解析。
//   - 模块不会被卸载，所以 provider 可以一直持有加载器初始化时给的东西，
//     也不要指望有任何清理调用。

#include <expected>
#include <filesystem>
#include <string>
#include <system_error>

namespace tsubaki::provider
{
extern "C"
{
    const char *tsubaki_provider_name() noexcept;

    bool tsubaki_is_keyed() noexcept;

    bool tsubaki_check_key(const std::string &key) noexcept;

    void tsubaki_checksum_file(
        const std::filesystem::path &path, const std::string &key,
        std::expected<std::string, std::error_code> &out) noexcept;
}
} // namespace tsubaki::provider
#endif
