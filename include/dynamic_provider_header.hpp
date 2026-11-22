// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_HEADER_HPP
#define LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_HEADER_HPP

#include <cstddef>

// The contract between tsubaki and a provider module, in full: a provider is a
// shared object exporting exactly these four symbols, and tsubaki reaches it
// through nothing else.
//
//   - Only plain data crosses the boundary. The `char *` arguments are
//     tsubaki's memory: read-only, and valid only for the length of the call,
//     so a module must not keep either pointer. The result is a plain aggregate
//     returned by value and its buffer belongs to tsubaki too. No std::string,
//     no std::filesystem::path, no std::expected, no std::error_code, no
//     exception and no allocation of the module's own is ever handed back.
//     That is what makes a statically linked tsubaki safe: nothing the module
//     allocates is freed by tsubaki, and nothing tsubaki owns is freed by the
//     module.
//   - Because only plain data crosses, the two sides need not share a standard
//     library, but they must agree on the ABI: same architecture, same calling
//     convention, C++23, and the module built as a position-independent shared
//     object.
//   - Every function keeps its noexcept. Anything that can throw is caught
//     inside the module; nothing may escape into tsubaki.
//   - tsubaki_checksum_file() takes the same byte string as `check_key()`, with
//     its length in `key_len`; `key_len` is 0 for a provider that ignores the
//     key. It fills `result` on every path, success or failure,
//     and starts from a zeroed struct so that no field is ever read unset.
//     `result.result` is tsubaki's buffer of TSUBAKI_MAX_RESULT_LENGTH bytes:
//     the digest goes there and nowhere else, never past the end, and a digest
//     that does not fit is an error rather than a truncation.
//   - `result.errcode` is 0 on success and an errno value otherwise, and it is
//     the only way to report a failure. Do not build a std::error_code in the
//     module: its category lives in the module's own standard library, so the
//     message it produces would be allocated there and freed by tsubaki.
//   - A successful digest is hexadecimal, at least 32 characters long, MD5's
//     width. listscanner recognises a checksum from that length up, so a
//     shorter value is read back as a path and a manifest holding it verifies
//     as unsure. Write lowercase, like the built-in algorithms, and NUL
//     terminate it inside the buffer.
//   - The name is the contract. A digest that changes length or meaning needs a
//     new name, not a new version of the old one: a manifest records only the
//     name, so keeping it means keeping the old output.
//   - tsubaki_check_key() is asked only for a keyed provider, and only about a
//     non-empty key; it decides whether the key is acceptable. The key is a byte
//     string, not a C string: it is not NUL terminated and may contain NUL
//     bytes, and `length` is the only length there is.
//   - tsubaki_algo_info() is optional. When the symbol is missing, tsubaki
//     prints "No information provided." A module that exports it returns a
//     short, static, human-readable description of the algorithm and the key
//     it wants, in the same shape the built-in providers use:
//       Digest:     32 bytes
//       Key:        none
//       Note:       BLAKE3 (RFC draft). ...
//     The string is read-only and lives as long as the module, so it may be a
//     literal. Nothing in it is parsed by tsubaki; it is printed verbatim.
//     tsubaki prints a "Source:" line above these three -- "built-in" for its
//     own algorithms, the module's path for anything loaded from disk. That
//     line belongs to tsubaki, so a module must not return one of its own.
//   - tsubaki_algo_info() is optional. When the symbol is missing, tsubaki
//     prints "No information provided." A module that exports it returns a
//     short, static, human-readable description of the algorithm and the key
//     it wants, in the same shape the built-in providers use:
//       Digest:     32 bytes
//       Key:        none
//       Note:       BLAKE3 (RFC draft). ...
//     The string is read-only and lives as long as the module, so it may be a
//     literal. Nothing in it is parsed by tsubaki; it is printed verbatim.
//     tsubaki prints a "Source:" line above these three -- "built-in" for its
//     own algorithms, the module's path for anything loaded from disk. That
//     line belongs to tsubaki, so a module must not return one of its own.
//   - tsubaki_provider_name() returns a non-null, stable string, and it must
//     not be a built-in name: while loading, tsubaki compares it against its own
//     list and refuses the module there, before resolving the other symbols.
//   - A module is never unloaded, so a provider may keep whatever the loader
//     initialized, and must not assume any cleanup call.
//   - A module is self-contained: tsubaki exports no symbols to it and hands it
//     no runtime to borrow. A statically linked tsubaki still loads modules
//     through the system C library, so the module must be built against the
//     same glibc generation that tsubaki was linked with.
//
// tsubaki 与 provider 模块之间的契约，全文如下：provider 是一个共享对象（`.so`），
// 导出下面四个必需符号，外加一个可选的 tsubaki_algo_info()；tsubaki 也只通过它们
// 访问 provider。
//
//   - 跨边界的只有纯数据。`char *` 参数是 tsubaki 的内存：只读，且只在本次调用
//     期间有效，所以模块不能保留这两个指针。返回值是按值传回的平凡聚合体，它里面
//     的缓冲同样属于 tsubaki。std::string、std::filesystem::path、std::expected、
//     std::error_code、异常，以及模块自己分配的任何对象都不会交回给 tsubaki。
//     静态链接的 tsubaki 之所以能安全加载模块，靠的正是这一点：模块分配的东西永远
//     不由 tsubaki 释放，tsubaki 的东西也永远不由模块释放。
//   - 因为只跨纯数据，两边不必共用同一套标准库；但仍须在同一 ABI 下编译：同一
//     架构、同一调用约定、C++23，模块按位置无关的方式编成共享对象。
//   - 每个函数都要守住 noexcept。任何可能抛异常的部分都在模块内部 catch 掉，什么
//     都不能逃进 tsubaki。
//   - tsubaki_checksum_file() 收到的密钥与 check_key() 相同，长度在 `length` 里；
//     忽略密钥的 provider 会拿到 0。它无论成功失败都要填好 `result`，并且从一个全零的结构
//     体开始，免得有任何字段没赋值就被读到。`result.result` 是 tsubaki 的缓冲，大小
//     为 TSUBAKI_MAX_RESULT_LENGTH：摘要只能写在这里，不许越过边界，装不下的摘要算
//     错误，不做截断。
//   - `result.errcode` 成功时为 0，失败时为 errno 值，这是报错的唯一途径。不要在
//     模块里构造 std::error_code：它的 category 住在模块自己那份标准库里，由它产生
//     的错误消息会在那边分配、由 tsubaki 释放。
//   - 成功的摘要是十六进制，长度至少 32 个字符，也就是 MD5 的宽度。listscanner 只
//     把这一长度及以上的串当成校验和，所以更短的值会被当成路径读回来，含这种值的
//     清单在 verify 时算 unsure。写小写，和内置算法一致，并在缓冲内用 NUL 结尾。
//   - 名字就是契约。摘要的长度或含义变了，就要换新名字，而不是在旧名字下出新版本：
//     清单只记录名字，留着名字就等于承诺输出不变。
//   - tsubaki_check_key() 只在 provider 带密钥时被询问，而且只会收到非空密钥；由它
//     判断该密钥是否可用。密钥是字节串而不是 C 字符串：它不以 NUL 结尾，里面可以
//     有 NUL 字节，长度只有 `length` 这一个来源。
//   - tsubaki_algo_info() 是可选的。模块没有导出这个符号时，tsubaki 打印
//     “No information provided.”。导出它的模块返回一段静态的、给人读的短说明，
//     描述算法和它要求的密钥，形状与内建 provider 一致：
//       Digest:     32 bytes
//       Key:        none
//       Note:       BLAKE3（RFC 草案）……
//     这段字符串只读，生命周期与模块相同，可以直接用字面量。tsubaki 不解析其中
//     的任何内容，原样打印。在这三行之上，tsubaki 会再打一行 “Source:”：内建
//     算法是 “built-in”，从磁盘加载的是模块路径。这一行归 tsubaki 管，模块不要
//     自己返回。
//   - tsubaki_algo_info() 是可选的。模块没有导出这个符号时，tsubaki 打印
//     “No information provided.”。导出它的模块返回一段静态的、给人读的短说明，
//     描述算法和它要求的密钥，形状与内建 provider 一致：
//       Digest:     32 bytes
//       Key:        none
//       Note:       BLAKE3（RFC 草案）……
//     这段字符串只读，生命周期与模块相同，可以直接用字面量。tsubaki 不解析其中
//     的任何内容，原样打印。在这三行之上，tsubaki 会再打一行 “Source:”：内建
//     算法是 “built-in”，从磁盘加载的是模块路径。这一行归 tsubaki 管，模块不要
//     自己返回。
//   - tsubaki_provider_name() 返回非空且稳定的字符串，而且不能是内建算法名字：加载
//     时 tsubaki 会拿它去比对自己的内建清单，命中就在那里拒掉这个模块，其余符号
//     根本不会被解析。
//   - 模块不会被卸载，所以 provider 可以一直持有加载器初始化时给的东西，也不要指望
//     有任何清理调用。
//   - 模块必须自包含：tsubaki 不向它导出符号，也不给它可借用的运行时。静态链接的
//     tsubaki 仍然要通过系统 C 库来加载模块，所以模块必须针对 tsubaki 链接时所用的
//     同一代 glibc 编译。

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

    const char *tsubaki_provider_name() noexcept;

    // Optional: a module may omit this symbol entirely.
    const char *tsubaki_algo_info() noexcept;

    bool tsubaki_is_keyed() noexcept;

    // `key` is a byte string of `length` bytes, not a C string.
    bool tsubaki_check_key(const char *key, std::size_t length) noexcept;

    // `length` is 0 for a provider that ignores the key.
    tsubaki_checksum_result tsubaki_checksum_file(const char *path,
                                                  const char *key,
                                                  std::size_t length) noexcept;
}
} // namespace tsubaki
#endif
