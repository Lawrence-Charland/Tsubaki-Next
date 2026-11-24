// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_HEADER_HPP
#define LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_HEADER_HPP

#include <cstddef>

// The contract between tsubaki and a provider module, in full: a provider is a
// shared object exporting the four required symbols below, an optional
// tsubaki_algo_info(), and an optional trio that lets it declare extra
// command-line arguments. tsubaki reaches it through nothing else.
//
//   - Only plain data crosses the boundary. The `char *` and `const char **`
//     arguments are tsubaki's memory: read-only, and valid only for the length
//     of the call, so a module must not keep any pointer it is handed. The
//     result is a plain aggregate returned by value and its buffer belongs to
//     tsubaki too. No std::string, no std::filesystem::path, no std::expected,
//     no std::error_code, no exception and no allocation of the module's own
//     is ever handed back.
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
//     its length in `length`; `length` is 0 for a provider that ignores the
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
//     non-empty key; it decides whether the key is acceptable. The key is a
//     byte string, not a C string: it is not NUL terminated and may contain NUL
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
//   - Extra command-line arguments are optional, and they come as a trio: a
//     provider exports all three symbols or none of them. Without
//     tsubaki_need_more_args() the provider takes no arguments and tsubaki
//     never looks at the other two. Exporting it while leaving out
//     tsubaki_arg_definition() or tsubaki_take_args() fails the load, like any
//     other missing required symbol.
//   - tsubaki_arg_definition() returns the table of arguments the algorithm
//     accepts, in the script language of argparser -- one
//     `{ entry_name = ...; entry_type = ...; ... }` block per entry, `key =
//     value` statements separated by ';', with entry_name and entry_type
//     (positional, option or flag) required and every other key naming an
//     ap_entry field -- whose grammar and keys are documented in
//     https://github.com/Lawrence-Charland/LC-ArgParser (docs/USAGE.md,
//     "Script language"). Return a literal: the string is read-only and needed
//     only for the call. `tsubaki algo-info` prints it verbatim under an
//     "ArgParser script:" line once tsubaki_need_more_args() says yes, and
//     does not parse it, so what the module accepts is visible from the module
//     itself. A null return is fatal on both paths, with status 70; a
//     malformed script is caught only when `tsubaki sum` registers it, also
//     with status 70, and tsubaki_take_args() is not called.
//   - tsubaki_take_args() is called once, before any file is hashed and on the
//     main thread, with one tsubaki_pass_content per entry in script order.
//     Entries that matched nothing still arrive, with contentc == 0. The
//     strings are tsubaki's and live only for the call; the hashing callback
//     later runs on several worker threads at once, so copy anything the
//     digest depends on. Return an empty string, or a NUL-terminated message
//     that tsubaki prints as the error and exits with status 64, so a module
//     can reject a combination the script language cannot express.
//   - tsubaki enforces the counts the script declares. A command line that
//     breaks min_value_count, max_value_count or max_repetition is reported by
//     tsubaki itself, naming the entry as `<entry_name>@dynamic` so that it is
//     never mistaken for one of tsubaki's own options; an `explanations` line
//     in the script chooses the label printed instead. tsubaki_take_args() is
//     not called in that case.
//   - Provider entries are parsed before tsubaki's own greedy arguments, the
//     file list included, so a name in the script overrides tsubaki's: do not
//     reuse -e, -l, -o, -t, -k, -K, -T, -v, -u, -a, -h or -V, and keep every
//     positional bounded, or it takes the file names. Registering the script
//     also reads the command line again, so `--` does not protect a word that
//     looks like one of the provider's options. The switches belong after the
//     algorithm name -- a word in front of it is still read as the algorithm --
//     though after the file list works too, since that second read sees the
//     whole command line.
//   - tsubaki_provider_name() returns a non-null, stable string, and it must
//     not be a built-in name: while loading, tsubaki compares it against its
//     own list and refuses the module there, before resolving the other
//     symbols.
//   - A module is never unloaded, so a provider may keep whatever the loader
//     initialized, and must not assume any cleanup call.
//   - A module is self-contained: tsubaki exports no symbols to it and hands it
//     no runtime to borrow. A statically linked tsubaki still loads modules
//     through the system C library, so the module must be built against the
//     same glibc generation that tsubaki was linked with.
//
// tsubaki 与 provider 模块之间的契约，全文如下：provider 是一个共享对象
// （`.so`），导出下面四个必需符号，外加一个可选的 tsubaki_algo_info()，以及一组
// 可选的、用来声明额外命令行参数的三个符号；tsubaki 也只通过它们访问 provider。
//
//   - 跨边界的只有纯数据。`char *` 和 `const char **` 参数是 tsubaki 的内存：
//     只读，且只在本次调用期间有效，所以模块不能保留任何交到它手里的指针。返回值
//     是按值传回的平凡聚合体，它里面
//     的缓冲同样属于
//     tsubaki。std::string、std::filesystem::path、std::expected、
//     std::error_code、异常，以及模块自己分配的任何对象都不会交回给 tsubaki。
//     静态链接的 tsubaki
//     之所以能安全加载模块，靠的正是这一点：模块分配的东西永远 不由 tsubaki
//     释放，tsubaki 的东西也永远不由模块释放。
//   - 因为只跨纯数据，两边不必共用同一套标准库；但仍须在同一 ABI 下编译：同一
//     架构、同一调用约定、C++23，模块按位置无关的方式编成共享对象。
//   - 每个函数都要守住 noexcept。任何可能抛异常的部分都在模块内部 catch
//   掉，什么
//     都不能逃进 tsubaki。
//   - tsubaki_checksum_file() 收到的密钥与 check_key() 相同，长度在 `length`
//   里；
//     忽略密钥的 provider 会拿到 0。它无论成功失败都要填好
//     `result`，并且从一个全零的结构
//     体开始，免得有任何字段没赋值就被读到。`result.result` 是 tsubaki
//     的缓冲，大小 为
//     TSUBAKI_MAX_RESULT_LENGTH：摘要只能写在这里，不许越过边界，装不下的摘要算
//     错误，不做截断。
//   - `result.errcode` 成功时为 0，失败时为 errno
//   值，这是报错的唯一途径。不要在
//     模块里构造 std::error_code：它的 category
//     住在模块自己那份标准库里，由它产生 的错误消息会在那边分配、由 tsubaki
//     释放。
//   - 成功的摘要是十六进制，长度至少 32 个字符，也就是 MD5 的宽度。listscanner
//   只
//     把这一长度及以上的串当成校验和，所以更短的值会被当成路径读回来，含这种值的
//     清单在 verify 时算 unsure。写小写，和内置算法一致，并在缓冲内用 NUL
//     结尾。
//   -
//   名字就是契约。摘要的长度或含义变了，就要换新名字，而不是在旧名字下出新版本：
//     清单只记录名字，留着名字就等于承诺输出不变。
//   - tsubaki_check_key() 只在 provider
//   带密钥时被询问，而且只会收到非空密钥；由它
//     判断该密钥是否可用。密钥是字节串而不是 C 字符串：它不以 NUL
//     结尾，里面可以 有 NUL 字节，长度只有 `length` 这一个来源。
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
//   - 额外命令行参数是可选的，而且是三个符号一组：要么全导出，要么都不导出。
//     没有 tsubaki_need_more_args() 就表示这个 provider 不接受额外参数，tsubaki
//     也不会去看另两个符号。导出它却缺了 tsubaki_arg_definition() 或
//     tsubaki_take_args()，模块会像缺任何必需符号一样加载失败。
//   - tsubaki_arg_definition() 返回这个算法接受的参数表，写法是 argparser 的
//     脚本语言——每个条目一个 `{ entry_name = ...; entry_type = ...; ... }` 块，
//     语句是 `key = value`、彼此用 ';' 分隔，entry_name 与 entry_type
//     （positional、option 或 flag）必需，其余键都是 ap_entry 的字段名——完整语法
//     与键名见 https://github.com/Lawrence-Charland/LC-ArgParser
//     （docs/USAGE.md 的 “Script language”）。直接返回字面量即可：字符串只读，
//     且只在本次调用内需要。只要 tsubaki_need_more_args() 说是，`tsubaki
//     algo-info` 就会把这段脚本原样打印在 “ArgParser script:” 之下，且不解析
//     它，所以模块接受什么参数，从模块本身就能看出来。返回空指针在两条路径上
//     都是致命错误（状态码 70）；脚本写错则只在 `tsubaki sum` 注册条目时才被
//     发现，同样是 70，且 tsubaki_take_args() 不会被调用。
//   - tsubaki_take_args() 在任何文件开始计算之前、在主线程上被调用一次，按脚本
//     顺序给每个条目一个 tsubaki_pass_content。没有匹配到值的条目也会传进来，
//     此时 contentc 为 0。这些字符串是 tsubaki 的内存，只在本次调用期间有效；
//     而稍后计算摘要的回调会在多个工作线程上并发执行，所以摘要要用到的东西必须
//     自己转存。返回空串表示命令行可用；返回非空的、以 NUL 结尾的消息时，tsubaki
//     会把它当作错误打印并以状态码 64 退出，脚本语言表达不了的组合可以这样拒绝。
//   - 脚本里声明的数量限制由 tsubaki 自己检查。违反 min_value_count、
//     max_value_count 或 max_repetition 时，报错由 tsubaki 发出，条目以
//     `<entry_name>@dynamic` 的形式出现，以便和 tsubaki 自己的选项区分开；在脚本
//     里写上 `explanations` 可以换掉最终打印的标签。这种情况下
//     tsubaki_take_args() 不会被调用。
//   - provider 的条目先于 tsubaki 自己的贪婪参数（包括文件列表）解析，所以脚本里
//     的名字会盖过 tsubaki 的：不要复用 -e、-l、-o、-t、-k、-K、-T、-v、-u、-a、
//     -h、-V，位置参数要有上限，否则文件列表会被吃光。注册脚本时命令行会被重新
//     读取一遍，所以 `--` 也保护不了看起来像 provider 选项的词。开关要写在算法名
//     之后——写在它前面的词仍会被当成算法名——写在文件列表之后也可以，因为那次
//     重读看到的是整条命令行。
//   - tsubaki_provider_name()
//   返回非空且稳定的字符串，而且不能是内建算法名字：加载
//     时 tsubaki 会拿它去比对自己的内建清单，命中就在那里拒掉这个模块，其余符号
//     根本不会被解析。
//   - 模块不会被卸载，所以 provider
//   可以一直持有加载器初始化时给的东西，也不要指望
//     有任何清理调用。
//   - 模块必须自包含：tsubaki
//   不向它导出符号，也不给它可借用的运行时。静态链接的
//     tsubaki 仍然要通过系统 C 库来加载模块，所以模块必须针对 tsubaki
//     链接时所用的 同一代 glibc 编译。

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

    // Optional: a module may omit this symbol entirely.
    const char *tsubaki_algo_info() noexcept;

    bool tsubaki_is_keyed() noexcept;

    // `key` is a byte string of `length` bytes, not a C string.
    bool tsubaki_check_key(const char *key, std::size_t length) noexcept;

    // `length` is 0 for a provider that ignores the key.
    tsubaki_checksum_result tsubaki_checksum_file(const char *path,
                                                  const char *key,
                                                  std::size_t length) noexcept;

    // Optional, but all three of the argument symbols stand or fall together.
    bool tsubaki_need_more_args() noexcept;

    // Required when tsubaki_need_more_args() is exported; must not be null.
    const char *tsubaki_arg_definition() noexcept;

    // Required when tsubaki_need_more_args() is exported. An empty string
    // accepts the command line; anything else is printed as the error.
    const char *tsubaki_take_args(tsubaki_pass_content *content,
                                  std::size_t cnt) noexcept;
}
} // namespace tsubaki
#endif
