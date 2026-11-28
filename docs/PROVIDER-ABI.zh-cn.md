# Tsubaki-Next provider ABI

[English](PROVIDER-ABI.md)

Tsubaki 从动态库里加载不在内建清单中的算法（POSIX 上是 `.so`，Windows 上是
`.dll`）。本文就是这样一个模块必须遵守的契约：导出什么、什么能跨过边界、摘要
和失败怎么报、额外命令行参数怎么声明、构建成什么样子，以及作为交换，tsubaki
给出怎样的稳定性承诺。

[`include/dynamic_provider_header.hpp`](../include/dynamic_provider_header.hpp) 就是
同一份契约的代码形式：**签名以它的声明为准**，本文则以这些声明背后的行为为准。
provider 把那个头文件复制进自己的源码树使用。

tsubaki 这一侧——加载器怎么遍历目录、解析符号、注册 provider 的参数——见
[ARCHITECTURE.zh-cn.md](ARCHITECTURE.zh-cn.md#动态-provider)；安装 provider 的用户
视角见 [USAGE.zh-cn.md](USAGE.zh-cn.md#动态算法)。

这份契约里，一个运行中的进程能观察到的部分——哪些符号可以缺、加载器拒掉什么、失败
怎么变成 `Error:` 行和退出码——由 `tests/abi.py` 逐个现场编译模块来验证。

## 目录

- [符号](#符号)
- [数据类型](#数据类型)
- [内存与生命周期](#内存与生命周期)
- [摘要](#摘要)
- [报错](#报错)
- [密钥](#密钥)
- [名字](#名字)
- [`algo-info` 文本](#algo-info-文本)
- [额外的命令行参数](#额外的命令行参数)
  - [写脚本时的取舍](#写脚本时的取舍)
- [构建模块](#构建模块)
- [加载器怎么对待模块](#加载器怎么对待模块)
- [兼容性](#兼容性)
  - [参数脚本](#参数脚本)
- [最小模块](#最小模块)

## 符号

所有符号都是 `extern "C"`，所以导出的名字就是函数名本身；头文件里的
`namespace tsubaki` 只是替模块自己的源码把声明归拢在一起。

| 符号 | 必需 | 签名 | 用途 |
| --- | --- | --- | --- |
| `tsubaki_provider_name` | 是 | `const char *() noexcept` | 命令行、`-a` 与 `algo` 里用的名字 |
| `tsubaki_is_keyed` | 否 | `bool() noexcept` | 密钥开关；不导出与导出后返回 `false` 完全等价 |
| `tsubaki_check_key` | 密钥开关返回 true 时 | `bool(const char *key, std::size_t length) noexcept` | 接受或拒绝一个密钥 |
| `tsubaki_checksum_file` | 是 | `tsubaki_checksum_result(const char *path, const char *key, std::size_t length) noexcept` | 计算摘要 |
| `tsubaki_algo_info` | 否 | `const char *() noexcept` | 给 `algo` 的三行说明 |
| `tsubaki_need_more_args` | 否 | `bool() noexcept` | 开关；不导出与导出后返回 `false` 完全等价 |
| `tsubaki_arg_definition` | 开关返回 true 时 | `const char *() noexcept` | 参数脚本 |
| `tsubaki_take_args` | 开关返回 true 时 | `const char *(tsubaki_pass_content *content, std::size_t cnt) noexcept` | 接收解析好的取值 |

`tsubaki_need_more_args` 是开关。不导出这个符号，与导出它并返回 `false` 完全等价：
两种情况下 provider 都不接受额外参数，另外两个符号根本不会被查找；即使导出了它们也
没有影响。只有它返回 `true` 时这组三符号才具有约束力——此时缺了
`tsubaki_arg_definition` 或 `tsubaki_take_args`，加载就会失败，和缺任何必需符号
一样。

`tsubaki_is_keyed` 是密钥开关，规则相同：不导出这个符号，与导出它并返回 `false`
完全等价——此时 provider 不带密钥，`tsubaki_check_key` 根本不会被查找；即使导出了
那个符号也没有影响。只有它返回 `true` 时 `tsubaki_check_key` 才具有约束力，此时
缺了这个符号，加载就会失败。

## 数据类型

```cpp
struct tsubaki_checksum_result
{
    char result[TSUBAKI_MAX_RESULT_LENGTH];
    int errcode;
};

struct tsubaki_pass_content
{
    std::size_t contentc;
    const char **contentv;
};
```

`TSUBAKI_MAX_RESULT_LENGTH` 是 257：256 个十六进制字符——是 tsubaki 自己最宽的摘要
的两倍——再加一个结尾的 NUL。`tsubaki_pass_content` 装的是参数脚本里某一个条目解析
出的值：`contentc` 个，放在 `contentv` 里，每个都以 NUL 结尾。`contentc` 为 0 时
`contentv` 是空指针。

## 内存与生命周期

跨边界的只有纯数据。没有 `std::string`、没有 `std::filesystem::path`、没有
`std::expected`、没有 `std::error_code`、没有异常，模块自己分配的任何东西也不会交回
去。静态链接的 tsubaki 之所以安全，靠的就是这一点：模块分配的东西永远不由 tsubaki
释放，tsubaki 的东西也永远不由模块释放。

- tsubaki 传进来的每个指针——路径、密钥、参数字符串——都是只读的，且只在本次调用
  期间有效。模块不能保留其中任何一个。
- 结果是按值返回的：模块自己填一个 `tsubaki_checksum_result` 再交回来，这次调用之后
  模块里的任何东西都不再被引用；也不要交回指向模块自有内存的指针。
- 每个入口都守住 `noexcept`。任何可能抛异常的部分都在模块内部 catch 掉，什么都
  不能逃进 tsubaki。
- `tsubaki_take_args` 只被调用一次，在任何文件开始计算之前，在主线程上。之后的
  计算回调会在多个工作线程上并发执行，所以摘要要用到的东西必须先从参数字符串里
  转存出来。
- 模块不会被卸载，因此可以一直持有加载器初始化时给的东西，但也不要指望有任何清理
  调用。

## 摘要

成功的摘要是十六进制、小写，并在 `result.result` 内用 NUL 结尾。宽度不限：清单行
总是以校验和开头，不需要猜它在哪结束，也就没有下限。上限就是缓冲：256 个字符。
实际上没有这么宽的摘要，但这条边界是硬的：tsubaki 交给模块的是定长缓冲，读的时候
只读到 NUL 为止，所以摘要放不下的算法必须通过 `errcode` 报错；写出边界之外是未定义
行为，不是截断。

## 报错

`result.errcode` 成功时为 0，失败时为 errno 值，这是报错的唯一途径。两个字段在每条
路径上都必须填好：tsubaki 不会预先清零这个结构体，没赋值的字段读出来就是垃圾值。

不要在模块里构造 `std::error_code`：它的 category 住在模块自己那份标准库里，由它
产生的消息会在那边分配、由 tsubaki 释放。tsubaki 用自己那份库把 `errcode` 变成错误
消息。

## 密钥

`tsubaki_is_keyed` 决定算法是否需要密钥；没有这个符号就表示 provider 不带密钥。
带密钥的 provider 索取密钥的方式与内建密钥算法一致（`needs a key`，退出码 64），由
`tsubaki_check_key`——`tsubaki_is_keyed` 返回 `true` 后它才成为必需——判断密钥是否
可用。不带密钥的 provider 永远不会被询问，它的 `tsubaki_check_key` 也不会被查找，
因此不必导出。tsubaki 只会拿非空密钥来问。

密钥是字节串，不是 C 字符串：它不以 NUL 结尾，里面可以有 NUL 字节，长度只有
`length` 这一个来源。`tsubaki_checksum_file` 收到的是同一个字节串；忽略密钥的
provider 拿到的 `length` 是 0。provider 不声明密钥长度上限；过长的密钥该不该拒绝，
由 provider 自己在 `tsubaki_check_key` 里判断。

## 名字

`tsubaki_provider_name` 返回非空且稳定的字符串，而且不能是内建算法名字：加载时
tsubaki 会拿它比对自己的内建清单，命中就在那里拒掉这个模块，其余符号根本不会被
解析，所以模块抢不走 `sha256`。

名字就是契约。清单只记录名字、不记录算法的其它信息，所以长度或含义变了的摘要必须
换新名字，而不是在旧名字下出新版本——留着名字就等于承诺输出不变。

## `algo-info` 文本

`tsubaki_algo_info` 在 ABI 层面是可选的；没有这个符号时，`tsubaki algo` 打印
“No information provided.”。但按约定，每个对外发布的 provider 都应当导出它，并用它
说明模块的出处——项目主页、代码仓库或某个网址——这样用户拿到一个模块时，在运行
之前就能知道它是什么。

导出它的模块返回一段静态的、给人读的说明，形状与内建算法一致：

```
Digest:     32 bytes
Key:        none
Note:       BLAKE3 (RFC draft). ...
Origin:     https://example.org/blake3-provider
```

出处放在哪一行、用什么标签都行，关键是要有。**不要用 `Source:` 当标签**：那一行由
tsubaki 自己打印，位于模块文本之上，永远写着模块的加载路径（内建算法写
`built-in`）。模块再返回一个 `Source:` 只会显得自相矛盾。

这段字符串只读、与模块同寿，且原样打印——tsubaki 不解析其中任何内容。

## 额外的命令行参数

需要文件之外参数的算法，用上面那组三符号声明自己的开关。

- `tsubaki_need_more_args` 是开关，不导出它，与导出后返回 `false` 完全等价：provider
  不接受额外参数，tsubaki 也不会去看另外两个符号。
- `tsubaki_arg_definition` 返回可接受的参数表，每个条目一个
  `{ entry_name = ...; entry_type = ...; ... }` 块，写法是
  [LC-ArgParser](https://github.com/Lawrence-Charland/LC-ArgParser) 的脚本语言，
  见它 `docs/USAGE.md` 的 “Script language”。`entry_name` 与 `entry_type`
  （positional、option 或 flag）是必需项，其余键都是 `ap_entry` 的字段名。
  `CMakeLists.txt` 把该库固定在一个提交上，所以以那个提交里的文档为准，而不是仓库
  最新版；从 2.1.3 起，推进这个锁定不会改变现有脚本的含义（见
  [参数脚本](#参数脚本)）。直接返回字面量即可；返回空指针是致命错误，状态码 70。
- `tsubaki_take_args` 接收解析好的取值。数组保证与脚本声明的条目一一对应：条目数量
  恰好等于脚本定义的条目数，顺序也与定义顺序完全一致——下标 *i* 永远是脚本里第 *i*
  个条目，`cnt` 就是条目总数。没有匹配到值的条目同样会传进来，此时 `contentc == 0`。
  返回空串表示命令行可用；返回以 NUL 结尾的消息时，tsubaki 把它当作错误打印并以 64
  退出——脚本语言表达不了的组合可以这样拒绝。

以下几件事来自 tsubaki 解析命令行的方式：

- 脚本声明的数量由 tsubaki 自己检查。违反 `min_value_count`、`max_value_count` 或
  `max_repetition` 的命令行由 tsubaki 报错，且 `tsubaki_take_args` 不会被调用。
  这些报错里条目写成 `<entry_name>@dynamic`——这是 tsubaki 的内部标记，用来把
  provider 选项和它自己的选项区分开，也让用户看出这条要求来自 provider 而不是程序
  本身；脚本里写了 `explanations` 时，打印出来的标签换成它。
- 脚本写错只会在 `tsubaki sum` 注册条目时被发现，状态码 70。`tsubaki algo`
  只原样打印脚本，不解析它。
- provider 的条目先于 tsubaki 自己的贪婪参数（其中包含文件列表）解析，所以 provider
  的开关会盖过路径，而一个没有上限的位置参数会把文件列表吃光。哪些名字不能碰、哪些
  上限必须设，见[写脚本时的取舍](#写脚本时的取舍)。
- 注册脚本时命令行会被重新读取一遍，但 `--` 仍然有效：它后面的一切都留作位置参数，
  写在那里的 provider 开关不会被认出。
- 开关写在算法名之后——写在它前面的词仍会被当成算法名——写在文件列表之后也可以，
  因为那次重读看到的是整条命令行。
- `verify` 会把不认识的位置参数转发下去，所以 `verify -u alg 名字 -L 128` 也能到达
  provider。清单只记算法名、不记这些参数：复核时得在 `verify` 命令行上再写一遍。

### 写脚本时的取舍

脚本语言的键比 provider 需要的多，默认值又是按"这个程序独占整条命令行"设的。

**强烈建议不要用——除非你非常熟悉 ArgParser 的工作机制。** 两类写法几乎制造了
provider 命令行的全部麻烦，因为它们会让某个参数吃掉本该属于别人的 token：

- **位置参数**（`entry_type = positional`）直接和文件列表竞争，而 provider 的条目
  先被评估：不设上限就吃掉路径，设了上限也仍会吃掉用户本想当文件的词。
- **没有取值数量上限的选项**。`max_value_count` 默认不限，于是 `-L 128 dir` 会把
  `dir` 当成第二个取值；只有 `max_value_count` 或 `no_variadic =` 能让它停在这里。

所以优先用 `entry_type = option` 加 `max_value_count = 1` 或 `no_variadic =`；
除非算法真的需要位置参数，否则别写。

**绝对不要用。**

| 键 | 原因 |
| --- | --- |
| `group` | tsubaki 自己会把条目注册进 `#dynamic`，而 `group` 只能增加组。写 `group = sum_greedy` 会让条目在 token 已被消费之后再评估一次：最好情况纯属无效，`min_value_count > 0` 时则会凭空报 `option_too_few_values`；写别的组则是在 provider 注册之前就评估过，等于没写。 |
| `no_consume` | 匹配到的 token 会留在池里：选项的值随后被文件列表当成路径收走，开关本身则作为冗余参数留下，于是 `fine()` 失败，运行在开始计算之前就以 64 退出。 |

**必须收紧的默认值。** 选项会一直取值到下一个开关为止，位置参数会一直收词到 barrier
或命令行结束。

| 键 | 默认 | 该写成什么 |
| --- | --- | --- |
| `max_value_count` | `-1`，不限 | `1`，或 `no_variadic =`——见上文 |
| `max_positional_count` | `-1`，不限 | 设上限，或干脆不写位置参数——见上文 |
| `min_value_count` | `0` | 保持 `0`。正数会让该选项变成**必需项**，每次 `sum` 都得给；"可省但有默认值"应放到 `tsubaki_take_args` 里处理 |
| `min_positional_count` | `0` | 同理，保持 `0` |

值的计数是**全部出现次数的总计**，不是每次：所以 `max_value_count = 1` 会拒绝
`-L 1 -L 2`。这是有意的读法，不是 bug。

**名字。** `short_name` 与 `long_name` 匹配的是 tsubaki 正在解析的同一条命令行，
provider 条目又先于文件列表认领 token。

- 短名禁区：`h V a K T k t o v e l s f u c`。
- 长名禁区：`help`、`version`、`algorithm`、`key-file`、`text`、`key`、
  `thread`、`output`、`verbose`、`exclude`、`list`、`follow-symlinks`、
  `force-rescan`、`verify`、`use-algo`、`canonical`。
- 撞名有两种后果，都是静默的：早于 provider 评估的名字（`-o`、`-k`、`-t` 等）由
  tsubaki 先消费，provider 什么也拿不到——脚本里设了 `min_value_count` 时，通常表现
  为 `Not enough values for the option: <名字>@dynamic`；晚于它评估的名字
  （`--exclude`、`--list` 以及文件列表）会被 provider 抢走，破坏 tsubaki 自己的
  行为。
- `entry_name` 在同一份脚本里必须唯一——重复会抛异常、退出 70；也不要含 `@`，
  因为 tsubaki 会追加 `@dynamic` 作为注册键和报错标签。
- 注册短名还会改变一个词怎么切：`L` 是 option 时，`-Lv` 变成 `-L` 加粘着值 `v`，
  而在此之前这个整词会落到文件列表。

**值得用的。** `no_variadic =`（每次只取一个值）、`explanations`（64 报错里替代
内部 `@dynamic` 名字打印的标签）、`max_repetition`（只对 flag 有意义；option 的
重复由 `max_value_count` 管）。`barrier` 是按 token 而不是按数量给位置参数划边界
的手段，但它是字面匹配，写了 tsubaki 的开关名会提前截断收集。

**脚本表达不了的。** 数值范围、互斥组合、参数之间的依赖与交叉校验都没有语法可写；
放到 `tsubaki_take_args` 里判断，返回消息（退出 64）。

## 构建模块

- C++23，按位置无关（`-fPIC`）编成共享对象，架构与调用约定要和 tsubaki 一致，对着
  `dynamic_provider_header.hpp` 编译。Windows 上同样的源码用 `-shared -static`
  编成 `.dll`，模块自带运行时。不加 `-static` 就会导入 `libstdc++-6.dll` 和
  `libwinpthread-1.dll`，只有在 DLL 搜索路径上有这两个文件的地方才加载得起来。
  不需要 `dllexport`——MinGW 会把每个 `extern "C"` 符号都导出。
- 模块必须自包含：tsubaki 不向它导出符号，也不给它可借用的运行时。静态链接的
  tsubaki 仍然通过系统 C 库加载模块，所以在 POSIX 上，模块要针对 tsubaki 链接时
  所用的同一代 glibc 构建；Windows 版同样是静态链接，模块只要对上系统自带的 UCRT。
- 一个工程可以产出多个模块，一个算法名字一个，共用头文件和读取文件的循环。把它们
  放进 `TSUBAKI_DYNAMIC_PROVIDER_PATHS` 列出的目录（POSIX 上用 `:` 分隔，Windows
  上用 `;`）；遍历是递归的，只有扩展名匹配的文件会被加载，所以 `libx.so.1` 会被
  跳过。

## 加载器怎么对待模块

- 路径不是可读目录时打一行 `Error:` 到 stderr 然后跳过，不改变退出码。
- 加载不了、或者没有导出 `tsubaki_provider_name` 的模块静默跳过。provider 目录里
  可能躺着别的共享库——Windows 上 C++ 运行时就是其中之一——逐个报出来只会把真正
  失败的模块淹掉。
- 自报名字是内建算法的模块在加载时就被拒掉，其余符号根本不会被解析，并且会在
  stderr 上说一句。导出了 `tsubaki_provider_name` 却缺了头文件规定必需的符号的
  模块同样如此。
- 两个 provider 抢同一个名字时先加载的胜出——先是 `TSUBAKI_DYNAMIC_PROVIDER_PATHS`
  里的顺序，然后才是各目录内的遍历顺序。
- 模块永远不被卸载。

## 兼容性

没有 ABI 版本符号，也不打算加：这份契约足够小、足够稳定，而版本校验只会多出一条
“模块拒绝加载”的路径。

从 2.1.3 起，tsubaki 对向下兼容作出适度承诺：2.x 期间，上面这些符号的名称、签名与
含义保持不变，两个结构体的布局保持不变，所以对着 2.1.3 编译的模块能继续加载。新增
能力沿用可选符号那一套——模块不导出就不启用该能力，不会因此加载失败。放宽也走同一
方向：原本必需的符号可以变成可选，因为按旧契约编译的模块本来就导出了它。算法名字的
含义同理不变：输出必须改变的算法要换新名字。

这不是形式化保证：跨架构、跨调用约定、跨 glibc 代的构建不在承诺范围内，违反上述
内存规则的模块也不受保护。确实无法兼容的改动会以新的符号名（或新的大版本）出现，
而不是悄悄改变现有符号的行为。由于加载时什么都不校验，模块应当针对不比加载它的
tsubaki 更新的头文件编译。

### 参数脚本

`tsubaki_arg_definition` 里的脚本由 LC-ArgParser 解析，那是另一个项目，本身没有
语义化版本管理。所以 Tsubaki-Next 锁定一个提交，而不是无条件追上游的最新版：跟着
浮动引用走，provider 参数的含义可能在无人察觉的情况下变掉。锁定到哪个提交由
`CMakeLists.txt` 决定，脚本语法以**那个提交**的 `docs/USAGE.md` 为准，而不是仓库
的最新文档。

锁定的提交不是一成不变的：`CMakeLists.txt` 指向更新的提交时，锁就随之推进。从
2.1.3 起保证的是**每一次推进**：新的锁定不会改变解析器读取 provider 脚本的方式。
条目可用的语法与键名、取值如何切分成参数并按 `min_value_count`、
`max_value_count`、`max_repetition` 计数、`--` 在哪里截断命令行，这些行为保持
不变——推进锁定可以带来修复，但不会让一个本来能用的脚本被重新解释。

这条承诺只覆盖解析器本身。报错里把 provider 选项写成 `<entry_name>@dynamic`、让它
和 tsubaki 自己的选项区分开，以及 provider 条目先于文件列表解析的先后顺序，都是
tsubaki 自己的整合决定（见[额外的命令行参数](#额外的命令行参数)），不由锁定的库
决定。

## 最小模块

下面是一个完整的 32 位 FNV-1a 摘要模块，可以当作起点。它没有导出
`tsubaki_is_keyed` 和 `tsubaki_check_key`，因为 FNV-1a 不需要密钥：没有
`tsubaki_is_keyed` 就表示不带密钥。带密钥的算法要导出它并返回 `true`，此时还必须
一并导出 `tsubaki_check_key`。

```cpp
#include "dynamic_provider_header.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>

namespace tsubaki
{
extern "C"
{
    const char *tsubaki_provider_name() noexcept { return "fnv32"; }

    const char *tsubaki_algo_info() noexcept
    {
        return "Digest:     8 hex characters (32-bit FNV-1a)\n"
               "Key:        none\n"
               "Note:       Example from the Tsubaki-Next docs.\n"
               "Origin:     https://github.com/Lawrence-Charland/Tsubaki-Next";
    }

    tsubaki_checksum_result tsubaki_checksum_file(const char *path,
                                                  const char *,
                                                  std::size_t) noexcept
    {
        tsubaki_checksum_result r{};
        std::FILE *f = std::fopen(path, "rb");
        if (f == nullptr)
        {
            r.errcode = errno;
            return r;
        }

        std::uint32_t h = 2166136261u;
        unsigned char buf[64 * 1024];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            for (std::size_t i = 0; i < n; ++i)
            {
                h ^= buf[i];
                h *= 16777619u;
            }
        const bool failed = std::ferror(f) != 0;
        const int err = errno;
        std::fclose(f);
        if (failed)
        {
            r.errcode = err;
            return r;
        }

        std::snprintf(r.result, sizeof r.result, "%08x", h);
        return r;
    }
}
} // namespace tsubaki
```

用 `-std=c++23 -fPIC -shared` 编译，把产物放进 `TSUBAKI_DYNAMIC_PROVIDER_PATHS`
列出的目录，它就会出现在 `tsubaki -a`、`tsubaki algo fnv32` 里，也能用
`tsubaki sum fnv32 <路径>` 直接算。
