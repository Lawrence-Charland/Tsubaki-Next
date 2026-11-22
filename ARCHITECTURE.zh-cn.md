# Tsubaki-Next 架构

[English](ARCHITECTURE.md)

## 总览

`sum` 和 `verify` 是一条拉取式流水线：生产者交出路径，汇流把多条流合并起来，工作
线程负责计算，每条结果由算它的线程写一行。

```
  argv ──> main ──> cmdline ──> sum_core ──> dirscanner      ─┐
                                         ├─> listscanner     ─┼─> fanin ─> work_manager ─> 输出
                                         └─> cached_taskstream ┘                ├─> printer (stderr)
                                                                                 └─> work_result ─> 报告块
```

中间没有"文件列表"这一层：谁取到一条路径，谁就立刻开算，所以输出顺序是完成顺序。

`cmp` 不走这条流水线。它把两份清单读进内存，两边都到齐之后再比较：

```
  argv ──> main ──> cmdline ──> cmp_core ──> listscanner ×2 ──> entries ──> 比较 ──> 输出
```

`algo-info` 没有流水线。它只解析一个名字然后打印：

```
  argv ──> main ──> cmdline ──> algo_info_core ──> get_info_builtin()
                                               └─> dpf_instance ──> provider::algo_info
```

## 目录

- [启动与分发](#启动与分发)
- [校验](#校验)
- [比较](#比较)
- [命令行层](#命令行层)
- [任务流层](#任务流层)
- [线程安全规则](#线程安全规则)
- [哈希计算](#哈希计算)
- [工作线程、进度与报告](#工作线程进度与报告)
- [输出流](#输出流)
- [并发模型](#并发模型)
- [退出码](#退出码)
- [数据流示例](#数据流示例)
- [扩展点](#扩展点)
- [源码映射](#源码映射)

## 启动与分发

`main()`（`src/main.cpp`）用 `include/cmdline.hpp` 里 `def_args()` 的定义解析
`general` 组——`subcmd` 位置参数加上 `--help`、`--version`、`--algorithm`。三个标志
在校验之前处理，所以不需要子命令，也优先于参数错误。`fine()` 校验完 `general` 后，
`subcmd` 选择 `sum_main()`、`verify_main()`、`cmp_main()` 或
`algo_info_core()`；其它值退出 64。分发包在
try/catch 里，逃出的异常映射成 255。

`sum_main(cmdline, internal)` 评估 `sum` 组（`internal` 为真时再加上 `sum_int`，
`verify` 就是这样传入隐藏标志的），校验条目，读取算法/密钥/线程数/输出，把每个输入
变成生产者，然后运行 `work_manager`。最后打印报告并映射退出码：被打断 130，记录过
错误 1，否则 0。

## 校验

`verify_main()` 是套在 `sum` 上的薄驱动，而不是第二套流水线：

- 算法在给了 `-u`/`--use-algo` 时取自它（名字未知退出 64），否则取自第一条
  `# Algorithm:` 注释（`get_comment_value()`）。输入不存在或是目录退出 66；其余已
  存在的路径都照读，所以 FIFO 和 `/dev/fd/N` 也能用，`-` 表示 stdin。stdin 没法先预读
  再重新读一遍，所以 `-` 输入必须配 `-u`（否则 64）；没有头、头里的算法未知或头里写的
  是 `none` 退出 64；`-u none` 则是允许的，只是什么哈希都不算。
- 它重新拼出内部 `sum` 命令行——已确定的算法、`-l <输入>`、`--force-rescan` 和隐藏
  的 `--verify` 标志，加上按原样转发的 `-t`、`-k`、`-K`、`-o`、`-v`——再调用
  `sum_main(instance, true)`。`-u` 不会被转发，因为它在这之前就已经解开。不给 `-o`
  时输出为 `<输入>.verify`，输入是 `-` 时为 `stdin.verify`；`-o` 指向输入文件会被拒绝
  （64）。
- `listscanner` 从每条 `<校验和> <路径>` 行填入 `task_token::sum`。工作线程忽略
  大小写地比较：`sum` 为空或该行是 `<NONE>` 的记入 `unsure`，不相同的记入
  `changed`。没有被真正计算哈希的路径——被中断、计算失败或算法为 `none`——同样记入
  `unsure`。这些计数都不改变退出码，刷新后的清单可以再次校验。

## 比较

`cmp_main()` 完全不计算哈希，两份清单都读完之后才下结论。

- 它评估 `cmp` 组（`--canonical`、`to_compare`、`-o`），先定输出端：`-o -` 用
  stdout，否则用指定文件；没给 `-o` 时是 `.` 下的
  `<左文件名去扩展名>_to_<右文件名去扩展名>.cmp`，并在 stderr 上提示。输出文件打
  不开时退出 73；指向任意一份输入都会被拒绝（64），因为两份清单是在输出端打开之后
  才读的。
- 每份清单都经 `rescan = true`、`trust = true` 的 `listscanner` 读入：校验和来自
  清单，文件类型过滤关闭（路径可能已被删除），没有校验和的行记一条错误。分隔符后面
  紧跟的 `*` 是 `sha256sum --binary` 的二进制标记，会被丢掉。两边最多只能有一个是
  stdin；清单打不开时退出 66。
- 校验和统一转成小写；除此之外没有任何东西标识一个校验和，所以用不同算法或不同密钥
  写出的两份清单没有共同值。给了 `-c` 时，每条路径先经
  `std::filesystem::canonical()`；它要求路径存在，否则后续比较将直接使用未被规范化
  的路径并记录错误。
- 先丢掉同一侧同一路径的重复项；同一侧同一路径带着不同校验和则退出 64。然后按路径
  排序遍历，把两侧同路径的条目配成 `[!]`（修改）或 `[=]`（相同）。剩下的按校验和
  排序再走一遍：只被一侧持有的变成 `[U][L]`/`[U][R]`，两侧共有的校验和各成一个带
  编号的 `[D][n]` 组。
- 各节顺序是修改、移动、删除/新增、相同，最后写 `[E]` 错误。流像 `sum` 一样刷出
  并检查；记录过错误则退出 1。

## 命令行层

解析交给 `LC-ArgParser`：一个独立的 C++17 库，由 CMake 拉取并作为源文件编入。
Tsubaki-Next 只用 `ap_instance`、`ap_entry`、`ap_defkey`。

- `pre_parse()` 给 token 打风格标记。`--` 之后、以及匹配不到任何条目的 `-x`/
  `--foo` 都保持 simple，所以拼错的选项会落到位置参数上，而不是报成选项错误。
- 长选项是 `--name=value`，短选项是 `-ovalue`；分离式取值一直收集到下一个开关或
  条目的取值上限，所以只取一个值的选项都显式声明 `max_value_count = 1`。
- `evaluate()` 按声明顺序处理条目并删掉各自消费的参数，这个顺序决定了分离式取值
  落到谁手里；`evaluate(group)` 只处理一个组。

`include/cmdline.hpp` 里的条目可以同时属于多个组：`general` 放 `subcmd`、
`--help`、`--version`、`--algorithm`；`key-file`、`key`、`strip-crlf`、
`threads`、`verbose` 属于 `sum` 和 `verify`；`output` 属于 `sum`、`verify` 和
`cmp`；`sum` 还有 excludes、lists、follow-symlinks、force-rescan、algorithm、files；`sum_int` 放
内部的 `verify_sumint` 标志；`verify` 加 `to_verify` 和 `use_algo`；`cmp` 加
`canonical` 和 `to_compare`。每个驱动只评估自己那一组。

由此带来的几点：`subcmd` 拿走第一个普通参数，所以 `tsubaki sha256 .` 是未知
子命令；`-e`/`-l` 先于 `files` 取值，所以分离式取值后面不能再跟文件参数；
`--help` 优先于校验；短名只比较第一个字符。

## 任务流层

`basic_taskstream`（`include/basic_taskstream.hpp`）是生产者的接口：

```cpp
virtual std::optional<task_token> pop();           // 公开入口
virtual std::optional<task_token> pop_raw() = 0;   // 由子类实现
void report_error(const error_entry &);
std::vector<error_entry> take_errors();
```

`task_token` 用 `p` 放路径、用 `sum` 放可选的校验和；只有重扫模式下的
`listscanner` 会填 `sum`。`pop()` 在互斥锁下调用 `pop_raw()`，然后过滤候选：普通
文件直接返回；不存在的路径、目录或 socket 变成一条 `error_entry`；FIFO 和设备作为
流交给工作线程。

| 类 | 职责 |
| --- | --- |
| `dirscanner` | 递归遍历目录，处理排除项与软链接；只交出普通文件，遍历中遇到的特殊文件和被排除的子目录静默跳过 |
| `listscanner` | 读列表文件或 stdin；输出 `# Kept:` 行，或者在重扫模式下填 `sum` |
| `cached_taskstream` | 按命令行顺序回放普通文件参数 |
| `fanin_taskstream` | 把多个子流合成一条，子流枯竭时用原子游标推进 |

汇流本身不加锁：每个子流自己串行化 `pop()`，所以工作线程会排队等在当前交出路径的
那个子流上，只有计算真正并行。`listscanner` 卡在 stdin 上就会卡住整条流。

## 线程安全规则

- `pop_raw()` 在持有 `mutex_` 时运行；可以碰自己的状态，但不能调用
  `report_error()` 或 `pop()`（互斥锁不可重入）。`take_errors()` 不加锁，所以必须
  在工作线程 join 之后调用。
- 只能在持锁时或单线程阶段访问 `errors`。
- 子流不能回调父级的加锁方法。
- `pop_raw()` 要短：`listscanner` 在等 stdin 时是持锁的。
- 返回错误，不要抛异常：`pop_raw()` 之上没有处理者。

## 哈希计算

`include/checksum.hpp` 里的 `ALGORITHM_LIST` 是唯一的内建名字清单：每一行给出枚举名
和命令行字面量；同一个宏生成 `algorithm` 枚举和 `mapper`（字面量向量），
`str_to_alg()` 与 `alg_to_str()` 就在它里面查找。它并不替掉每个算法各自的行为，那些
仍是手写的 `switch`：`is_keyed()` 和 `max_key_length()` 在同一头文件里，
`digest_for()` 和 `mac_for()` 在 `src/checksum.cpp` 里。`check_key_validity()` 把前
两者合成密钥策略。所以加一行是必要但不充分的，还得补上对应的 `digest_for()` 或
`mac_for()` case。OpenSSL 的调用都在 `src/checksum.cpp`：`digest_for()` 把普通摘要
映射到 `EVP_MD`，`mac_for()` 把密钥算法映射到 `mac_spec`，`stream_file()` 按 64 KiB
分块喂给回调。`checksum_file()` 与 `checksum_file_key()` 分别走 `EVP_Digest*` 和
`EVP_MAC*`。

程序其余部分只接触 `checksum_api`（`include/checksum_api.hpp`、`src/checksum_api.cpp`）
这一个句柄：`set_algo()` 解析字面算法名，失败时记下 `unknown_algorithm` 而不是返回
一个哨兵；`set_key()` 是提供密钥的唯一入口，失败时记下 `missing_key` 或
`invalid_key`；`hashing()`、`keyed()`、`name()`、`calc(path)` 回答各 driver 需要的
问题。调用方因此完全不碰 `algorithm` 枚举，provider 才能加入 `ALGORITHM_LIST` 里
没有的算法。失败一律以 `std::unexpected(std::error_code)` 返回，不抛异常；
`algorithm::none` 不打开文件，直接返回 `<NONE>`。

文件用普通 `read()` 按逻辑长度读取：稀疏文件的空洞按实际大小收费，内存始终只有
一块 64 KiB 缓冲。显式给出的 FIFO 或设备按阻塞方式打开，可能让工作线程一直等下去；
中断标志只在文件与文件之间检查。

### 动态 provider

`algorithm::dynamic` 是哨兵而不是表里的一行：`mapper` 里没有它，`alg_to_str()` 会抛
异常，报告里显示的名字由 provider 自己给出。加载逻辑在 `dpf_instance`
（`include/dpframework.hpp`）里：

- 构造函数按 `:` 切分 `TSUBAKI_DYNAMIC_PROVIDER_PATHS`，丢掉空段，然后用
  `dirscanner` 逐个遍历，所以扫描是递归的，且不跟随指向目录的软链接。路径不存在或
  不是目录时 `dirscanner` 抛 `std::invalid_argument`，构造函数按路径接住、打印并继续。
- 后缀恰好是 `.so` 的文件才会变成 `provider`：先 `dlopen()`（`RTLD_NOW |
  RTLD_LOCAL`），再 `dlsym()` 取 `tsubaki_provider_name()`，拿它自报的名字去问
  `str_to_alg()`。自报的名字若是内建算法，就在这一步被拒掉，其余符号根本不会去查，
  所以即使这个 `.so` 先被加载，也抢不走 `sha256`。活下来的模块继续为
  `include/dynamic_provider_header.hpp` 里声明的其余必需 `tsubaki_*` 符号
  `dlsym()`，外加可选的 `tsubaki_algo_info()`：查不到时指针就指向
  `no_algo_info()`，`algo-info` 打印的正是它。
  `provider_msg` 记下走到哪一步；只要不是 `success` 就打印出来并丢弃。句柄永不
  关闭，模块与进程同寿。
- `provider_mapper` 以模块自报的名字为键，用 `emplace()` 填充，所以重名时保留先
  认领那个名字的模块。

`checksum_api` 把 `dpf_instance` 存成 `inline static`，所以整个进程只扫一次——第一
个 `checksum_api` 构造时扫，之后每个都复用那张表：`sum` 与 `verify` 共用一次扫描，
尽管 `verify` 会构造两个 `checksum_api`（自己那个加上内部 `sum` 的那个）；`cmp` 一个
都不构造，因此从不扫描。`src/help.cpp` 里的 `list_algorithm()` 是例外：`--algorithm`
会自己造一个局部 `dpf_instance`。`set_algo()` 先试 `str_to_alg()` 再问
`find_provider()`，所以按名字查找同样碰不到内建名字对应的 provider。对
provider 来说，`keyed()` 问 `tsubaki_is_keyed()`，`set_key()` 问
`tsubaki_check_key()` 而不是 `check_key_validity()`，`calc()` 调
`tsubaki_checksum_file()`，它按值返回一个 `tsubaki_checksum_result`：一块
`TSUBAKI_MAX_RESULT_LENGTH` 字节、归 `checksum_api` 所有的摘要缓冲，加上一个
errno 风格的错误码，模块无论成败都必须填好。

这条边界上走的是纯数据。`calc()` 把结果留在自己的栈上，交给模块的只有两个只能读的
`const char *`；模块把摘要写进随结构体一起回来的缓冲，用 `int` 报错。模块分配的东西
这里永远不释放，这里的东西模块也永远不释放，静态链接的 `tsubaki`（自带一份 glibc）
才能加载一个把共享 glibc 带进来的模块而不发生跨分配器 free。返回的 `expected` 里那个
`std::error_code` 由 `checksum_api` 用 `std::make_error_code()` 从那个 `int` 造出来，
category 和它产生的消息都留在 tsubaki 自己那份标准库里；模块内部造出来的
`error_code` 会带着模块那份库的 category，它的消息也会在那边分配、在这边释放。模块
仍然必须自包含，不能保留那两个指针，并且要匹配 tsubaki 链接时所用的那一代 glibc。
它算出的摘要至少要有 32 个十六进制字符（也就是 MD5 的长度），因为 `listscanner` 只从
这一长度起才认校验和；更短的值会被当成路径读回来，`verify` 会把那一行算作 unsure。

## 工作线程、进度与报告

`work_manager` 持有工作线程数组、进度线程、一个 `const checksum_api`、中断标志、
计数器，以及校验模式下的 `changed`/`unsure` 向量。`run(n)` 在需要时装好 SIGINT
处理函数，写出 `# tsubaki checksum result` 头，启动 `n` 个工作线程和进度线程，然后
等待。

- 工作线程取一条路径；已中断就打印 `<NONE>` 并计入未计算（校验模式下同时计为
  unsure）；否则调用 `api.calc()`，经 `std::osyncstream` 写一行，并更新计数器、
  `changed` 或 `unsure`。
- 进度线程每 100 ms 往 stderr 重画一行，直到运行结束或被中断。
- `run()` 返回 `work_result`（算法名、verify 标志、错误、changed、unsure、计数器、
  中断标志、耗时），打印到哪里由调用方决定。

`work_result::print(os, details)` 写报告块：算法名、processed（= hashed + failed）、
hashed、failed、not hashed、耗时，校验模式下再加 `Changed` 和 `Unsure`，然后是错误
数、中断警告；`details` 为真时再写错误列表和各计数下的路径列表。

`SIGINT` 只置管理器的标志。工作线程仍会把流取空，所以扫描器产出的每条路径都有
交代；已经进入计算的文件会正常算完，而卡在 `open()` 或无限读取里的线程回不到检查
点。第五次 `Ctrl-C` 会写一条固定消息并调用 `_exit(250)`。

## 输出流

`sum_core` 只选定一个输出端（stdout 或 `--output` 打开的文件），由工作线程、列表
扫描器和报告共用；并发写入走 `std::osyncstream`。`work_manager::run()` 先写
`# tsubaki checksum result` 头。`cmp_core` 直接往自己的输出端写，开头是
`# tsubaki comparison result`。

不属于清单本身的东西都写往 stderr：参数和输入问题、警告、`verify` 与 `cmp` 的默认
输出提示、结果写往文件时报告的第二份拷贝（`print(std::clog, false)`），以及最后写
失败时的提示。结果流在结束时刷出并检查：刷出失败退出 73；`SIGPIPE` 保持默认动作
（shell 看到 141）。

## 并发模型

| 线程 | 数量 | 工作 |
| --- | --- | --- |
| 主线程 | 1 | 搭好各条流，调用 `run()`，由它 join 工作线程 |
| 工作线程 | `-t` 个 | 取一条路径、算哈希、写一行（校验模式下再比较一次） |
| 进度线程 | 1 | 每 100 ms 重画进度行 |

- 每条任务流一把互斥锁，只在取路径时持有。
- `interrupted` 标志和计数器是原子变量；算法待在 manager 的 `const checksum_api`
  里，运行期间不变。
- 计算时不持任何锁，文件 I/O 完全并行。

## 退出码

这些码定义在 `include/exit_codes.hpp`，由 `main()`、`sum_main()`、`verify_main()`、
`cmp_main()` 共用：

| 宏 | 值 | 何时返回 |
| --- | --- | --- |
| `TSUBAKI_SUCCESS` | 0 | 没有错误条目；`verify` 和 `cmp` 报出的差异都不算错误 |
| `TSUBAKI_HASH_ERROR` | 1 | 记录过错误条目；对 `cmp` 来说是没有校验和的清单行、`-c` 无法规范化的路径，或清单读取失败 |
| `TSUBAKI_INVALID_OPERATION` | 64 | 命令行被拒；`verify` 对不可用的 `Algorithm:` 头、没配 `-u` 的 `-` 输入或输出指向输入也返回它；`cmp` 在两份清单都是 stdin、输出指向其中一份清单、或同一路径重复且校验和不同时也返回它（`EX_USAGE`） |
| `TSUBAKI_FILE_READ_FAILED` | 66 | `--key-file` 读不了、密钥文件为空、`verify` 的输入不存在或是个目录、`cmp` 的清单读不了（`EX_NOINPUT`） |
| `TSUBAKI_FATAL` | 70 | `argc` 为 0（`EX_SOFTWARE`） |
| `TSUBAKI_FILE_WRITE_FAILED` | 73 | `--output` 文件打不开，或结果流写失败（`EX_CANTCREAT`） |
| `TSUBAKI_INTERRUPTED` | 130 | `work_result::interrupted` 为真（`128 + SIGINT`） |
| `TSUBAKI_TERMINATED` | 250 | 第五次 `Ctrl-C` 直接 `_exit()`，报告没写出来 |
| `TSUBAKI_UNEXPECTED` | 255 | 有异常逃出 `main()` |

`interrupted` 优先于 `errors`；写失败返回 73，但读端消失的管道会先被 `SIGPIPE`
杀掉（141）。

## 数据流示例

```
tsubaki sum sha256 . -e build -l old.txt
```

1. `files` 收走 `.`，`excludes` 取走 `build`，`old.txt` 变成一个列表。
2. `.` 变成带 `build` 排除项的 `dirscanner`；`old.txt` 变成 `listscanner`；两者
   连同空的 `cached_taskstream` 一起加进汇流。
3. 工作线程先取目录、再取列表。带校验和的行立刻以 `# Kept: <路径>` 写出，从不进入
   计算路径。
4. 汇流枯竭后，`run()` 返回的 `work_result` 由 `sum_main` 打印。

`cmp` 没有这条流程：`cmp_main()` 把两份清单导入同一个向量，排两次序并写出各节，
不涉及任务流或工作线程。

## 扩展点

- **新增子命令**：加一个 `subcommands` 常量、一个 `cmdline_group`、`main()` 里的
  一个分支，以及一个像 `sum_main()` 的驱动。`cmp` 是最小的例子：复用了
  `listscanner`，但没有工作线程池。
- **新增输入源**：从 `basic_taskstream` 派生，实现 `pop_raw()`；需要自己的语法就
  在 `def_args()` 里加条目。
- **换输出端**：`work_manager` 是唯一的消费者，换一个 `std::ostream` 或整个替换。
- **新增算法**：在 `ALGORITHM_LIST` 里加一行写字面量，扩展 `is_keyed()`（密钥有
  长度上限时再加 `max_key_length()`），然后普通摘要加一个 `digest_for()` case，
  密钥算法加一个 `mac_for()` case。`mapper`、名字查找和 `--algorithm` 表都跟着那一
  行走；是否密钥算法、密钥上限是对同一枚举的 switch。
- **不重新编译本项目就加算法**：照 `include/dynamic_provider_header.hpp` 写一个
  provider 动态库，把 `TSUBAKI_DYNAMIC_PROVIDER_PATHS` 指向它。名字随后经
  `dpf_instance` 解析，`--algorithm` 会连 `SOURCE` 一起列出，`verify -u` 也认。
  自报名字是内建算法的模块在加载时就被拒掉，抢不走内建名字；摘要至少要有 32 个
  十六进制字符，否则清单读不回来。

## 源码映射

### 类型

| 类型 | 种类 | 基类 | 职责 |
| --- | --- | --- | --- |
| `tsubaki::basic_taskstream` | class | — | 所有任务流类的虚基类：`pop()` 负责过滤候选，`pop_raw()` 是子类实现的钩子，`errors` + `report_error()` + `take_errors()` 承载错误 |
| `tsubaki::dirscanner` | class | `basic_taskstream` | 递归遍历目录，处理排除项与软链接 |
| `tsubaki::listscanner` | class | `basic_taskstream` | 解析列表文件或 stdin，输出 `# Kept: <路径>` 行；重扫模式下填 `task_token::sum` |
| `tsubaki::cached_taskstream` | class | `basic_taskstream` | 按给定顺序回放一组固定的路径 |
| `tsubaki::fanin_taskstream` | class | `basic_taskstream` | 汇流多个子流，用 `unique_ptr` 持有它们 |
| `tsubaki::work_manager` | class | — | 工作线程池、进度行、中断标志，以及校验运行用的 `changed`/`unsure` 向量 |
| `tsubaki::work_result` | class | — | `run()` 返回的值快照，带 `print()`；不引用 manager |
| `tsubaki::checksum_api` | class | — | 解析字面算法名、持有密钥，回答 `hashing()` / `keyed()` / `name()`，把 `calc()` 分到摘要路径、MAC 路径或 provider |
| `tsubaki::checksum_error` | enum class | — | `checksum_api` 不可用的原因：`none`、`unknown_algorithm`、`missing_key`、`invalid_key` |
| `tsubaki::dpf_instance` | class | — | 构造时扫描一次 `TSUBAKI_DYNAMIC_PROVIDER_PATHS`，把 provider 名字映射到已加载模块 |
| `tsubaki::provider` | struct | — | 一个已经 `dlopen` 的模块：句柄、已经解析到的符号，以及停在哪个 `provider_msg` |
| `tsubaki::provider_msg` | enum class | — | `success`、`dlopen_fail`、`dlsym_fail`、`refused`、`unknown`：模块走到了哪一步 |
| `tsubaki::task_token` | struct | — | 一条任务：路径加上清单里读到的校验和（重扫模式之外为空） |
| `tsubaki::error_entry` | struct | — | `std::error_code` 加上出错时的操作描述 |
| `tsubaki::fd_guard` | class | — | 析构时关闭文件描述符，哈希代码在用 |
| `tsubaki::algorithm` | enum class | — | 由 `ALGORITHM_LIST` 生成的内建枚举，外加表示 provider 算法的 `dynamic` 和表示 `str_to_alg()` 失败的 `error` |
| `tsubaki::mac_spec` | struct | — | 只在 `src/checksum.cpp` 内部使用：`EVP_MAC` 名字和摘要参数（如果有） |

### 文件

| 路径 | 内容 |
| --- | --- |
| `src/main.cpp` | 子命令分发、`general` 参数组、最后的异常兜底 |
| `include/core_functions.hpp` | `sum_main()`、`verify_main()` 与 `cmp_main()` 的声明 |
| `include/exit_codes.hpp` | 四个驱动共用的退出码宏 |
| `src/sum_core.cpp` | `sum` 参数组的检查、启动流程、流的组装 |
| `src/verify_core.cpp` | `verify` 参数组、`-u` 覆盖、头部查找、内部 `sum` 调用 |
| `src/cmp_core.cpp` | `cmp` 参数组、两份清单的导入、路径与校验和匹配、分节输出 |
| `include/algo_info_core.hpp`, `src/algo_info_core.cpp` | `algo-info` 参数组、`get_info_builtin()`，以及打印内建或 provider 说明的驱动 |
| `include/basic_taskstream.hpp`, `src/basic_taskstream.cpp` | 基类流与它的 `pop()` 过滤 |
| `include/dirscanner.hpp`, `src/dirscanner.cpp` | 目录遍历 |
| `include/listscanner.hpp`, `src/listscanner.cpp` | 列表文件与 stdin 解析 |
| `include/cached_taskstream.hpp`, `src/cached_taskstream.cpp` | 固定路径回放 |
| `include/fanin_taskstream.hpp`, `src/fanin_taskstream.cpp` | 多源汇流 |
| `include/checksum.hpp` | `ALGORITHM_LIST`、`algorithm` 枚举、`mapper`、`str_to_alg()` / `alg_to_str()`、密钥策略，以及 `checksum_file()` / `checksum_file_key()` 的声明 |
| `src/checksum.cpp` | OpenSSL 那一侧：`digest_for()`、`mac_for()`、`stream_file()`、`checksum_file()`、`checksum_file_key()` |
| `include/checksum_api.hpp`, `src/checksum_api.cpp` | `checksum_error` 与 `checksum_api`：名字解析、密钥处理、`calc()` 分派 |
| `include/dpframework.hpp`, `src/dpframework.cpp` | `provider_msg`、`provider`（一次 `dlopen`/`dlsym`）与 `dpf_instance`（环境变量扫描） |
| `include/dynamic_provider_header.hpp` | provider 编译时对照的 ABI：四个必需的 `extern "C"` 符号，外加一个可选的 `tsubaki_algo_info()` |
| `include/worker.hpp`, `src/worker.cpp` | `work_result` 与 `work_manager` |
| `include/utilities.hpp`, `src/utilities.cpp` | 字符串工具、密钥读取、注释查找、终端宽度、fd 守卫 |
| `include/cmdline.hpp` | 参数条目定义 |
| `include/help.hpp`, `src/help.cpp` | 帮助文本与 `--algorithm` 表格 |
| `LC-ArgParser`（拉取） | 独立的参数解析库，作为源文件编入 |
