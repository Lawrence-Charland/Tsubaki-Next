# Tsubaki-Next

[English](README.md)

生成、核对和比较校验和清单。输入可以是一个文件、一棵目录树、一份已有的清单
或者标准输入，一次运行里任意组合、并行计算。输出是一份清单：每个文件一行
`<校验和> <路径>`，末尾跟一块 `#` 开头的报告，写明算法、处理数、耗时和错误。
清单行与 GNU coreutils 的哈希工具输出互相兼容：`sha256sum -c` 能校验它写出的
清单，它也能读 `sha256sum` 写出的清单。

内建算法覆盖常见摘要与密钥哈希（HMAC、带密钥 BLAKE2），完整名单见
`tsubaki -a`；不够的由 provider 补，见[更多的算法支持](#更多的算法支持)。

四个子命令：`sum` 生成清单，`verify` 按清单重算并报出变了和无法判断的条目，
`cmp` 比较两份清单，`algo` 说明单个算法。`verify` 回答
“和记录相比变了没有”，`cmp` 回答“两次之间具体变了什么”，两者可以接力：先用
`verify` 刷新，再用 `cmp` 和旧清单细比。

它是旧版 [Tsubaki](https://github.com/Lawrence-Charland/Tsubaki) 的精神续作，但
不是功能超集：缺什么见[与旧版 Tsubaki 的关系](#与旧版-tsubaki-的关系)。

## 更多的算法支持

程序本体只提供 20 种内建算法——10 种摘要加 10 种密钥哈希，名单见
[算法](docs/USAGE.zh-cn.md#算法)。不够用时不必改代码：算法可以由 provider 补上，
一个 provider 就是一个模块（POSIX 上是 `.so`，Windows 上是 `.dll`），一个模块
提供一个算法，所在目录写进 `TSUBAKI_DYNAMIC_PROVIDER_PATHS` 之后就能当算法名用
（详见[动态算法](docs/USAGE.zh-cn.md#动态算法)）。

现成的 provider 在本项目的附属工程
[Tsubaki-Sample-Providers](https://github.com/Lawrence-Charland/Tsubaki-Sample-Providers)：
这些算法由同一个 CMake 工程构建，连几类内建完全没有的构造也在其中。

- 非加密校验和：CRC-32、CRC-32C、CRC-64/XZ、Adler-32、XXH64
- SHA-2 截断变体：SHA-512/224、SHA-512/256，以及它们的 HMAC
- SHA-3 剩下的档位与 Keccak 原始版：SHA3-224、SHA3-384、Keccak-224/256/384/512
- XOF 一族：SHAKE、TurboSHAKE、cSHAKE、KT（KangarooTwelve）、KMAC
- BLAKE3（含带密钥模式），以及可调 `--length`、`--salt`、`--personal` 的 BLAKE2b/BLAKE2s
- RIPEMD-160、国密 SM3 与 HMAC-SM3
- CMAC 一族：AES、ARIA、Camellia、SEED、SM4

（这里的列表可能不是最新的，具体支持范围以那个仓库的内容为准）

`tsubaki algo <名字>` 能看到宽度、密钥格式和它接受的
开关；想自己写一个，看 [provider 契约](docs/PROVIDER-ABI.zh-cn.md)。

## 快速开始

预编译二进制在[发布页](https://github.com/Lawrence-Charland/Tsubaki-Next/releases)：
静态链接、OpenSSL 已经编进去，解压就能用。每个包里还带 `providers/`，里面是为
同一个目标构建的动态 provider，把 `TSUBAKI_DYNAMIC_PROVIDER_PATHS` 指向那个
目录就能用上。其它平台从源码构建，需要 CMake 3.28+、C++23 编译器、OpenSSL
3.0+ 和 Ninja：

```sh
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
```

二进制落在 `build/linux-gcc-release/tsubaki`；`cmake --install` 装到
`CMAKE_INSTALL_PREFIX/bin`（默认 `/usr/local`）。第一次 configure 会从 GitHub
拉取 LC-ArgParser，之后复用；没有网络时用
`-DFETCHCONTENT_SOURCE_DIR_LC-ARGPARSER=/path/to/LC-ArgParser` 指向本地副本。

跑一遍：

```sh
tsubaki sum sha256 . -o sums.txt     # 生成清单
tsubaki verify sums.txt              # 重算，结果打到 stdout
tsubaki cmp old.txt new.txt          # 比较两份清单，差异打到 stdout
```

`sum` 的输出就是清单，`verify` 和 `cmp` 都吃这份清单。除非用 `-o` 指定文件，
结果都打到 stdout；`-v` 在 stderr 上显示实时进度。选项写在输入之后：分离写出的值
会一直取到下一个开关为止，`-e` 和 `-l` 尤其如此。

## 支持的平台

| 系统 | 预设 | 说明 |
| --- | --- | --- |
| Linux（glibc 或 musl） | `linux-gcc-debug`、`linux-gcc-release` | 唯一提供预编译二进制的平台，五个架构都支持 |
| macOS 12+ | `macos-debug`、`macos-release` | Xcode 16+，Apple 工具链，自动找 Homebrew 的 `openssl@3` |
| FreeBSD 13+ | `freebsd-debug`、`freebsd-release` | 系统 clang；`pkg install openssl3` |
| 其它 POSIX（NetBSD、OpenBSD、Solaris 等） | `posix-debug`、`posix-release` | 要 OpenSSL 3.0+，LibreSSL 不行 |
| Windows 10+ | `windows-x86_64-debug`、`windows-x86_64-release` | 在 Linux 上交叉编译，需要 MinGW-w64 和对应目标的静态 libcrypto |

另有 `linux-clang-debug`，唯一的非 GCC Linux 预设，用来看编译器警告。
`linux-gcc-san` 在 Debug 构建上开着 ASan 和 UBSan，只用于本机跑。

两个 Windows 预设从 POSIX 宿主交叉编译，需要 `x86_64-w64-mingw32-g++`，以及
`scripts/build-static-openssl.sh <版本> windows-x86_64` 写到
`res/openssl-static-for-tsubaki-windows-x86_64` 的 `libcrypto.a`。不提供 Windows
预编译二进制；交叉构建时运行时是静态链接的，exe 只依赖系统 DLL。

## 文档

- [用法](docs/USAGE.zh-cn.md)：子命令、选项、清单格式、报告块、退出状态、中断
- [provider 契约](docs/PROVIDER-ABI.zh-cn.md)：写一个动态算法模块要导出什么
- [架构](docs/ARCHITECTURE.zh-cn.md)：模块划分、线程模型、数据流

文档与程序实际行为不一致时，以文档的解释为准，而不是程序的实际行为；作者保留
修改文档的权利。

## 与旧版 Tsubaki 的关系

旧版用 C++17 和 OpenSSL，一个二进制里装着三种模式——计算校验和、查找重复文件、
比较两份结果，仓库现已归档。重写后模式变成子命令，清单变成能回喂的中间产物，
算法从编译期列表变成可加载的模块。

也有取舍：去重模式不再单列，重复文件从 `cmp` 的 `[D]` 分组看；按大小过滤、
`--test` 预演、`--balance` 排序没有保留；纯路径列表要写成 `<NONE> <路径>` 或
走 `-l`。旧版 `sum` 的命令行多半不用改：`sum <算法> <路径>` 的顺序没变，
`--exclude`、`-v`、`--force-rescan` 原样有效；`--allow-symlinks` 与
`--thd-amount` 分别改叫 `--follow-symlinks`、`--thread`，读列表的 `stdin` 换成
`-`。清单两边可以互读，只有报告块的字段换了名字。

名字是旧版就定下的，取自《四月是你的谎言》里的泽部椿（Sawabe Tsubaki），
纯纯个人喜好，和校验和没有关系。

## 状态

个人项目，单作者维护；暂时没有添加安全审计，也没有 CI 或模糊测试。
正确性目前靠 `tests/` 下三个黑盒套件，由 `tests/check.py` 一口气跑完：`general.py` 的契约测试、`abi.py` 的 provider ABI 测试（需要 C++ 编译器）、`differential.py` 对 coreutils / OpenSSL 的差分测试。另有两条不变式：清单行与 coreutils 对齐、报告字段已冻结。
内建算法全部走 OpenSSL；动态 provider 是第三方代码，用之前先用 `tsubaki algo` 看它的来源和脚本。

## 许可证

Tsubaki-Next 以 GNU 通用公共许可证第 2 版发布，全文见 [COPYING](COPYING)。

解析库 LC-ArgParser 是同一作者的另一个项目，按 MIT 许可发布；它自带的 `LICENSE`
随源码一起分发，构建时取用的那份仍归 MIT。
