# Tsubaki-Next 架构

[English](ARCHITECTURE.md)

## 总览

整条流水线是拉取式的。生产者只实现一个"交出下一条路径"的方法；一个汇流对象
把若干生产者合起来；一组工作线程从这个流里取路径并算哈希，每条算完的结果再由
算它的工作线程写成一行输出。

```
  argv ──> main ──> cmdline ──> sum_core ──> dirscanner      ─┐
                                         ├─> listscanner     ─┼─> fanin ─> work_manager ─> 输出
                                         └─> cached_taskstream ┘                ├─> printer (stderr)
                                                                                 └─> work_result ─> 报告块
```

中间没有"文件列表"这一层。扫描之后不再排序、不再统计、不再过滤：谁取到一条
路径，谁就立刻开算。

## 目录

- [启动流程](#启动流程)
- [命令行层](#命令行层)
- [任务流层](#任务流层)
- [线程安全规则](#线程安全规则)
- [哈希计算](#哈希计算)
- [工作线程、进度与报告](#工作线程进度与报告)
- [输出流](#输出流)
- [并发模型](#并发模型)
- [退出码](#退出码)
- [数据流示例](#数据流示例)
- [为什么现在只有一个 manager](#为什么现在只有一个-manager)
- [扩展点](#扩展点)
- [源码映射](#源码映射)

## 启动流程

`main()`（在 `src/main.cpp` 里）负责子命令分发，以及所有与子命令无关的事：

1. `def_args()` 用 `include/cmdline.hpp` 里的条目定义构造
   `argparser::ap_instance`；`pre_parse()` 把参数切成带风格的 token。
2. `evaluate(cmdline_group::general)` 匹配 `general` 组：只收一个词的 `subcmd`
   位置参数，加上 `--help` 和 `--version` 两个标志。程序名取自 `argv[0]`。
3. `--help` 和 `--version` 在**校验之前**检查，所以两者都不需要子命令就能用，
   也优先于其它参数错误。帮助文本来自 `src/help.cpp` 的 `help_text()`；版本
   字符串就是 CMake 项目版本，由 `TSUBAKI_VERSION` 注入。
4. `fine()` 校验 `general` 组的条目。缺 `subcmd` 就在这里报错，末尾补一句提示
   去跑 `--help`。
5. `subcmd` 的值与 `subcommands::sum` 比较：相等就把预解析好的实例交给
   `tsubaki::sum_main()` 并返回它的退出码；否则按未知子命令报错，返回
   `TSUBAKI_INVALID_ARGUMENT`。这层调用包在 try/catch 里，逃出来的异常变成一条
   错误信息和 `TSUBAKI_UNEXPECTED`。

`sum_main(cmdline)` 按顺序做这些事：

1. `evaluate(cmdline_group::sum)` 把剩下的参数匹配到 `sum` 组的条目上；`general`
   组的条目保留之前的结果。
2. `fine(false)` 做校验。每个失败的条目都用 `argparser::describe()` 报出来，
   末尾补一句提示去跑 `--help`。
3. 取条目的值并检查：算法经 `str_to_alg()` 映射；密钥算法从 `-k` 或 `-K` 取密钥
   （`-K` 用 `get_file_content()` 读文件，路径是 `-` 时读 stdin，并去掉尾部
   CR/LF），普通算法带了任一选项都只发警告，连文件都不会打开；线程数由 `-t`
   解析（`std::stoi` 抛异常或结果不是正数就报错），没给 `-t` 时取
   `std::thread::hardware_concurrency()`，它返回 0 时退回 1；输出流打开。
   `check_key_validity()` 拒绝空密钥和超长密钥；`-K -` 与 `-` 输入同时出现会被
   拒绝，同一条 stdin 只能指定一次。算法和密钥最后装进同一个 `checksum_api`，
   传给工作线程的是它，而不是光秃秃的 `algorithm`。
4. 把输入变成生产者，加进 `fanin_taskstream`：每个目录一个 `dirscanner`，每个
   `-` 或列表文件一个 `listscanner`，普通的文件参数汇总成一个
   `cached_taskstream`。队列就按这个顺序建：命令行上出现的目录和 `-`，然后是
   缓存的文件，最后是 `-l` 列表。即使文件参数为空，这个流也会被加进去；输入
   全空也是合法的——`files` 没有最小个数限制，空跑一次只是打印一份空报告。
5. `work_manager(tasks, *out, api, verbose).run(threads)` 启动工作线程，返回一个
   `work_result`。
6. `sum_main` 往输出流写 `#`，再调 `result.print(*out, true)`，然后 `flush()`
   并检查 `fail()`：写失败就返回 `TSUBAKI_FILE_WRITE_FAILED`。否则把这次运行
   映射成退出码：被打断时是 `TSUBAKI_INTERRUPTED`，记录过错误时是
   `TSUBAKI_HASH_ERROR`，否则是 `TSUBAKI_SUCCESS`。更早的参数问题返回
   `TSUBAKI_INVALID_ARGUMENT`，`--output` 文件打不开也返回
   `TSUBAKI_FILE_WRITE_FAILED`。

## 命令行层

解析工作交给 `LC-ArgParser`
（<https://github.com/Lawrence-Charland/LC-ArgParser>），一个独立的 C++17 库，
由 CMake 在配置阶段拉取，并作为源文件编进二进制，而不是链接共享库。Tsubaki-Next 只用它的公开接口（`ap_instance`、
`ap_entry`、`ap_defkey`），这个库对校验和一无所知，别的程序也可以原样复用。

落到本程序上能看见的行为有这么几条：

- `pre_parse` 把每个 token 标成 simple、single dash、double dash 或
  `argv_zero`；`--` 之后的一律是 simple。匹配不到任何条目的 `-x`、`--foo` 同样
  留在 simple 里，所以拼错的选项会被位置参数收走——子命令位、算法或文件列表——
  最后按未知子命令、未知算法或“文件不存在”报错，而不是报成选项错误。
- 长名是 `--name` 或 `--name=value`；短名是 `-v`、合并的 `-vs`、或者粘滞的
  `-ovalue`。分离式取值（`--output out.txt`）库本身是支持的。
- 选项会收集它后面连续的 simple token，直到遇到非 simple 的值或到达上限；标志
  只累计出现次数。`max_value_count` 缺省是 `-1`，也就是不设上限，所以每个只该
  取一个值的选项都必须显式写明 `1`。
- 位置参数在 min/max 区间内收集 simple token，barrier token 会让收集提前结束，
  并且和它一起被消费掉。
- `evaluate()` 按声明顺序逐个处理条目并删掉各自消费的参数，所以谁先谁后决定了一个
  分离式取值最后落到谁手里。`evaluate(group)` 只处理一个组，其它组的条目原样不动。
- `fine()` 与 `errors()` 汇总各条目的结果，可选地把没被消费掉的参数报成
  `invalid_arguments`。
- 这个库不承诺兼容性，vendored 的那份副本钉在某个提交上。

条目只在 `include/cmdline.hpp` 里定义一次，并且带一个组：`general` 放
`subcmd`/`--help`/`--version`，其余都在 `sum` 组。`main()` 评估 `general`，
`sum_main()` 评估 `sum`。组内的声明顺序就是评估顺序：`sum` 组是 excludes、
lists、key-file、key、threads、output、follow-symlinks、force-rescan、
verbose、algorithm、files，`general` 组是 subcmd、version、help。条目的名字是
`cmdline_entry` 里的 `inline constexpr` 常量，定义和 `entry()` 查询共用同一份
拼写；名字里的连字符在标识符里写成下划线。

- `subcmd` 是 `general` 组的第一个条目，也是个数恰好为 1 的位置参数，所以它拿走
  第一个普通参数，不管那是什么。这就是 `tsubaki sha256 .` 会被当成未知子命令，
  而不是被读成算法的原因。
- `key-file`、`key`、`threads` 和 `output` 都写了 `max_value_count` 为 `1`，
  所以分离式取值只拿一个普通参数，剩下的留给后面的条目。没有这个上限，解析库会把
  下一个开关之前的普通参数全给它们，`-k secret file.txt` 就会把 `file.txt` 也算进
  密钥里。
- `excludes` 和 `lists` 排在 `files` 之前，它们的值在位置参数之前就被取走，所以
  分离式取值（`-e build`）能用，前提是它后面不再跟文件参数。
- 这两项的取值个数都没有上限，因此 `-e build .` 会把 `.` 也当成排除项，让 `files`
  收不到东西。
- `files` 是最后一个位置参数，没有 barrier，也没有数量上限，所以它会把剩下的
  simple 参数全部收走。
- `-h`/`--help` 是标志，而帮助检查在参数校验之前就返回了，所以 `--help=something`
  照样打印帮助、退出码 0：解析库会记下 `flag_unexpected_values`，但 `main`
  根本没走到校验那一步。`-V`/`--version` 同理。短名只比较第一个字符，因此 `-h1`
  等同于 `-h`。

## 任务流层

### 接口

`basic_taskstream`（`include/basic_taskstream.hpp`）是生产者唯一要实现的东西：

```cpp
virtual std::optional<std::filesystem::path> pop();           // 对外入口
virtual std::optional<std::filesystem::path> pop_raw() = 0;   // 子类实现
void report_error(const error_entry &);
std::vector<error_entry> take_errors();
```

工作线程只调 `pop()`。它先加锁、调用 `pop_raw()`、解锁，然后在锁外检查取到的
候选路径：

- 是普通文件，返回；
- `status()` 设置了错误码——路径缺失、悬空链接——记下这条错误，继续循环；
- 是目录，记一条 `operation_not_permitted`，继续循环；
- 是 socket，同样记一条：读多久也读不出一个校验和，所以直接拒绝，不交给工作线程；
- 其余存在的类型——字符/块设备、FIFO——原样返回，打开还是读取交给工作线程。

这层过滤放在基类里，所以子类只需要回答"下一条路径是什么"。覆盖 `pop()` 的有两个
类：`dirscanner` 遍历目录项，只交出普通文件；遍历中遇到的目录、设备、FIFO 或
socket 都静默跳过，而不是逐个记错误，因为遍历本身不是"我要读一个流"的明确请求。
`sum sha256 ~` 不会卡在别人留下的 FIFO 上，靠的就是这一条。另一个是
`fanin_taskstream`，它的游标逻辑见后文。

在这里被 `pop()` 拒掉的路径（缺失、stat 失败、socket、列表里的目录）根本到不了
计算那一步，所以既不打印也不进三个计数器的任何一个，只留下一条 `error_entry`，而它
足以让退出码变成 1。这也是 `Failed` 可以是 0、运行却仍然失败的原因：`Failed`
只统计算哈希那一步真的返回了错误的文件。

`error_entry` 是 `std::error_code` 加一段自由文本的操作描述；进报告的是那段文本，
所以生产者要写清"当时在做什么"，而不只是"哪一步失败了"。

### dirscanner

构造时接收目录、排除列表和软链接开关，内部持有一个
`std::filesystem::recursive_directory_iterator`，打开时带
`skip_permission_denied`，需要时再加 `follow_directory_symlink`。构造函数会拒绝
不存在的路径和非目录（抛 `std::invalid_argument`），并把不存在的排除项丢掉，每
丢一个记一条错误。

`pop()` 用一条比基类更严的类型检查取代基类的：只交出普通文件，遍历里的 FIFO、
socket 或设备既不会送到工作线程，也就不可能在 `open()` 里把它卡住。检查用的是
`status()`，它会跟随符号链接，所以指向普通文件的链接照常计算，而指向目录或特殊
文件的链接会被跳过。类型判定失败的路径记为错误，不再往下传。

`pop_raw()` 先调 `skip_if_possible()`：只要当前项与某个排除项 `equivalent()`，
就先关掉这一项（如果是目录）的递归，跨过去，再看下一项。路径比较用
`equivalent()` 而不是字符串前缀，所以同一个 inode 从哪条路径进来都能被排除掉。

这个检查只能看到迭代器交出的条目，而迭代器从不交出它打开的那个根目录。因此，
排除项如果等于被扫描的根、或者是它的上级目录，就什么都匹配不到，会被静默忽略；
正确的用法是排除子目录。排除项也从不作用于命令行上直接给出的文件、或列表里读
进来的文件，因为它们压根不走这个扫描器。

不存在的排除项会被丢掉，但先记一条 `error_entry`，所以它同样会让运行退出 1，
哪怕扫描照常继续。目录不存在、或者路径不是目录时，构造函数会抛
`std::invalid_argument`；这个异常不会被转换成错误条目——它会逃出
`sum_main`，由 `main()` 的最后兜底捕获。

### listscanner

从任意 `std::istream` 读行——文件由 `sum_core` 打开，`-` 时用 `std::cin`
——决定保留某行时还要往结果流里写点东西。每个非空行先 trim，以 `#` 开头的行
忽略。

用 `get_equation()` 在第一个空格处切开。如果前半段正好是 `<NONE>`——本程序给
未计算文件打的标记——空格后面那段就是路径。否则如果前半段不是至少 32 个字符的
全十六进制（`is_checksum()`），整行就是路径，空格也算在内。没有空格的行根本走不到这个判断，
所以纯十六进制的文件名单独成行时就是一个普通路径。如果前半段**是**十六进制，这一
行就是 `<校验和> <路径>`：

- 带 `--force-rescan` 时，把路径交出去重新计算；
- 否则这一行和本次运行解耦：往输出里写 `# Kept: <路径>` 和 trim 后的行，它既不
  参与计算，也不进报告的任何计数。

认不出来的行整行都算路径，名字里的空格因此无害；这也是 `<NONE> <路径>` 的输出能
直接当输入喂回去的原因。剩下唯一的歧义是带空格、且第一个空格前的整段全为十六进制
的名字：它会被拆成“校验和 + 路径”，只能靠让前半段不是十六进制来保住整行
（`./名字`、绝对路径）。`<NONE> ` 是保留前缀，永远按标记读。

列表行永远只代表一个文件。行里写目录时，它跟其他路径一样被交出去，然后被
`pop()` 拒掉，绝不会被展开遍历；只有命令行上的目录参数才会变成 `dirscanner`。

### cached_taskstream

持有一个路径 `std::queue`，从队首往外给，所以一批普通文件参数保持命令行上的顺序。
它自己不检查路径是否存在，无效路径由 `basic_taskstream::pop()` 过滤。

### fanin_taskstream

它是若干个任务流的顺序聚合体，自己也是一个 taskstream，线程池因此只看见一条流。
`pop()` 覆盖了基类版本，按原子游标看当前子流：子流交出路径就返回；子流枯竭就用
compare-and-swap 把游标推进一步，再看下一个。子流不会被丢弃，它们的错误也不在这
里合并：`take_errors()` 把汇流自己的错误列表和每个子流的列表拼在一起，而它由
`work_manager::run()` 在工作线程 join 之后调用一次。

`pop()` 自己不持锁，串行来自子流：子流的 `pop()` 各自加自己的互斥锁。所以只要
当前子流还有东西可给，所有要路径的线程都排在那一个锁上。并行只发生在算哈希这一段。
这也意味着某个 `listscanner` 卡在 `std::cin` 上时，堵住的是整条流，而不只是一个
线程。

## 线程安全规则

`pop_raw()` 在基类 `pop()` 调用它时一定持有 `mutex_`（`dirscanner` 覆盖了 `pop()`，
自己加同一把锁），而 `errors` 没有自己的锁。

- `pop_raw()` 里访问自己的状态不用加锁，但在里面调 `report_error()` 或 `pop()` 会
  自锁：这把 mutex 不可重入。`take_errors()` 不取锁，所以不会自锁，但会和并发的
  `report_error()` 竞争。要记错误就直接写 `errors`。
- 只有在持锁时（`pop_raw()` 内）或单线程时（构造期、`take_errors()` 之后）才能碰
  `errors`。
- 子流绝不能回调父流的加锁方法，否则两个线程可能互等。`fanin` 只在 `add_task()`
  里锁自己，拉取路径时并不持锁。
- `pop_raw()` 要短、不要阻塞。`listscanner` 的 `getline` 等 stdin 时就持着锁，堵住
  的是整条流。
- 错误要返回，不要抛。异常逃出 `pop_raw()` 会落到没有处理器的 worker 上，最终
  `std::terminate`。

## 哈希计算

算法清单和公开入口在 `include/checksum.hpp`，OpenSSL 的代码在
`src/checksum.cpp`。模块两头都从 `ALGORITHM_LIST` 出发，它生成 `algorithm`、
名字表、`str_to_alg()` 和 `alg_to_str()`。

三个辅助函数把 OpenSSL 的活集中在一处：

- `digest_for(algorithm)` 把普通摘要映射成 `EVP_MD`，其余值一律返回 `nullptr`。
- `mac_for(algorithm)` 把密钥算法映射成 `mac_spec {name, digest}`。HMAC 把摘要
  放在参数里（如 `{"HMAC", "SHA256"}`），带密钥的 BLAKE2 本身就是一种 MAC 算法
  （`{"BLAKE2BMAC", nullptr}`）。
- `stream_file(path, update)` 以 `O_RDONLY | O_CLOEXEC` 打开文件，用一个 64 KiB 的
  `thread_local` 缓冲区按块读完，`EINTR` 重试，每块交给 `update`；`update` 返回
  false 就变成 `operation_not_supported`。

`checksum_file()` 把这条流穿过 `EVP_DigestInit_ex` / `EVP_DigestUpdate` /
`EVP_DigestFinal_ex`。`checksum_file_key()` 把同一条流穿过 `EVP_MAC_fetch` /
`EVP_MAC_CTX_new` / `EVP_MAC_init` / `EVP_MAC_update` / `EVP_MAC_final`。MAC 算法
按名字 fetch，而不是给每个摘要配一个 HMAC 构造函数，所以一条代码路径覆盖所有密钥
算法；只有 `mac_spec` 写了摘要时才设置 `OSSL_MAC_PARAM_DIGEST`。密钥按原始字节
传入，不做任何解码。

两个函数都用 `std::unexpected(std::error_code)` 报错，从不抛异常：文件错误用
`errno`，加密调用拒绝某一块是 `operation_not_supported`，映射表不认识的算法是
`invalid_argument`，上下文分配失败是 `not_enough_memory`。`algorithm::none` 在
两边都直接短路成 `<NONE>`，根本不碰文件。摘要或 MAC 用 `to_hex()` 转成小写
十六进制。

`checksum_api` 是把选择从 `sum_main` 带到工作线程的值：`alg` 加可选的 `key`，
唯一的入口是 `calc(path)`。`calc()` 按 `is_keyed(alg)` 分流：密钥算法走
`checksum_file_key()`，其余走 `checksum_file()`。

密钥策略放在 `ALGORITHM_LIST` 旁边的两个 `constexpr` 函数里：`max_key_length()`
对 `blake2b512_mac` 返回 64、对 `blake2s256_mac` 返回 32、其余返回 0 表示不设
上限；`check_key_validity()` 要求密钥非空且不超过这个上限（普通摘要接受任意密钥
并忽略它）。`sum_main` 在打开任何文件之前对 `-k` 或 `-K` 得到的密钥查一次，失败
就给出通用消息并退出 64。`calc()` 不再复查；传入有效的密钥是 `checksum_api` 的
约定，手工构造它的调用方要自己保证。

读取用的是普通 `read()`，不是 `mmap`，也不查 `SEEK_DATA`，所以稀疏文件按逻辑长度
计算：空洞会被当作零喂进摘要。开销跟的是 `st_size`，不是实际分配的块数；不管文件
声称多大，内存占用始终只有那个 64 KiB 缓冲区。

显式命名的非普通路径用的还是同一个阻塞 `open()`：没有写端的 FIFO 会在第一次读之前
就卡住，永不 EOF 的设备则会把工作线程一直留在循环里。两者都看不到中断标志——那个
标志只在文件与文件之间检查。

## 工作线程、进度与报告

`work_manager` 持有工作线程数组、进度线程、一个 `const checksum_api api`（算法加
密钥）、一个 `std::atomic<bool> interrupted`，以及三个原子计数器。`run()` 返回一个
`work_result` 而不是自己写报告，打印到哪里、怎么打印由调用方决定。

- `run(thread_count)` 安装 SIGINT 处理函数，起 `thread_count` 个工作线程和进度
  线程，然后等待。
- 每个工作线程循环：`tasks.pop()`；若 `interrupted` 已置位，就打印
  `<NONE> <路径>` 并让 `unprocessed` 加一；否则调 `api.calc(path)`，再用
  `osyncstream` 写一行。算失败打印 `<NONE> <路径>` 并让 `failed` 加一；算成功让
  `success` 加一，算法是 `none` 时改让 `unprocessed` 加一。被 `pop()` 拒掉的
  路径根本走不到这里，所以既没有输出行也没有计数，只有一条错误条目。
- 进度线程在非 quiet、算法不是 `none` 时，每 100 ms 在 stderr 上重画一行，直到
  `interrupted` 置位为止。它用 stderr 的终端宽度，宽度未知时退化成朴素的
  `[N files]`。
- `wait()` 先 join 工作线程，再把 `interrupted` 置位（进度循环因此退出），最后
  join 进度线程。
- 全部 join 完之后，`run()` 返回一份快照：启动时的算法、收集到的错误、三个计数
  器、中断标志，以及从启动到 join 结束的墙钟耗时。

构造函数接收一个 `catch_sigint` 开关，默认打开。关掉后 `run()` 不安装 SIGINT 处理
函数，把 `Ctrl-C` 留给调用方，适合嵌进别的驱动里。

`work_result` 是个纯值对象，字段为 `alg`、`errors`、`success`、`failed`、
`unprocessed`、`interrupted`、`duration`。一个被注释掉的 `operator+` 画出了两个
同算法结果如何合并（错误追加、计数和耗时相加），是为将来开多个 manager 留的草稿。
`print(os, with_errors)` 负责写报告块：`Algorithm`、`Processed`（成功 + 失败）、
`Hashed`、`Failed`、`Not hashed`、`Duration`、`Errors` 条数（即使为 0 也写），
被打断时在条数下面再加一个 `#` 行和一行
`# Warning: The process was interrupted halfway through.`；有错误时后面跟
`# Error messages:` 列表。`with_errors = false` 时只写条数和说明，不写逐条内容。

`sum_main` 写 `#`、调 `result.print(*out, true)`，然后 `flush()` 并检查
`fail()`（失败返回 `TSUBAKI_FILE_WRITE_FAILED`），再依次判断：`interrupted` 为真
返回 `TSUBAKI_INTERRUPTED`，有错误返回 `TSUBAKI_HASH_ERROR`，否则返回
`TSUBAKI_SUCCESS`。退出码不由 `failed` 决定；不过每个计算失败同时也留下一条错误
条目，所以实际上两者是一致的。

`SIGINT` 走的是单独一个标志，而不是算法值：处理函数把 `true` 存进工作线程的
`std::atomic<bool> interrupted`，每个工作线程在 `tasks.pop()` 之后马上检查它。
剩下的路径照样被取出、打印、计入 `Not hashed`，所以报告能为扫描器产出的每个文件
留个交代；已经进入 `api.calc()` 的文件会正常算完。已经卡在阻塞 `open()` 或无限
读取里的工作线程回不到那个检查点，所以一次 `Ctrl-C` 救不出它。处理函数还会递增
一个原子计数器，按到第五次时用 `write(2, ...)` 写一条固定消息并调用 `_exit(250)`。
任何一次 `Ctrl-C` 都会让 `work_result::interrupted` 为真，它会给报告块加一行
说明，并让 `sum_main` 返回 `TSUBAKI_INTERRUPTED`（130）。

## 输出流

`sum_core` 只选定一次输出端：`std::cout`，或者 `--output` 打开的某个文件。这个
`std::ostream` 由所有工作线程、列表扫描器（写 `# Kept: <路径>` 标记）和最终报告
共用。并发写入都走 `std::osyncstream`，它会把一次完整的插入整体刷出，所以行不会
交错；报告本身在工作线程 join 之后才写，因此直接写进流。

写到 stderr 的有四类东西：进度指示，构造参数集合阶段的问题（`cmdline` 报错、输出
文件打不开），最后写失败时的一行提示，以及给了 `--output` 时报告块的第二份拷贝
——它用 `print(std::clog, false)` 输出，所以不含逐条错误信息。其余错误都进结果流
里的报告块，包括打不开的列表文件。

结果流在结束时会被检查，stderr 不会。工作线程的行经 `std::osyncstream` 刷出，
报告在 join 之后直接写入，随后 `sum_main` 调用 `flush()` 并检查 `fail()`：刷出
失败——磁盘写满、描述符损坏、文件所在挂载点消失——会在流上留下失败状态，返回
`TSUBAKI_FILE_WRITE_FAILED`（73）。`--output` 文件仍由 `std::ofstream` 的析构
函数关闭，这一步的错误不检查；但写失败已经在前面那次 flush 里暴露出来。
`SIGPIPE` 保持默认动作：读端消失时进程直接被杀（shell 看到 141），走不到 `73`。
`--output` 文件本身在开工之前就会检查能否打开。

## 并发模型

| 线程 | 数量 | 工作 |
| --- | --- | --- |
| 主线程 | 1 | 搭好各条流，调用 `run()`，由它 join 工作线程 |
| 工作线程 | `-t` 个 | 取一条路径、算哈希、写一行 |
| 进度线程 | 1 | 每 100 ms 重画进度行 |

- 每个 taskstream 一把互斥锁。锁覆盖 `pop_raw()` 和错误数组，只在取路径的那一小
  段时间里被持有。
- 原子变量：`interrupted` 标志、三个计数器、中断计数器、信号处理函数用的目标指针。
  指针之所以是 `volatile`，是因为信号处理函数会写它；`interrupted` 和中断计数器
  用 `std::atomic`，因为处理函数要往里存值。算法不再是共享状态：它待在 manager 的
  `const checksum_api` 里，运行期间不会变。
- 算哈希时不持任何锁，文件 I/O 完全并行。
- 流水线里没有任何地方对结果重排序：输出顺序是完成顺序，不是扫描顺序。

## 退出码

这些码定义在 `include/exit_codes.hpp`，由 `main()` 和 `sum_main()` 共用：

| 宏 | 值 | 何时返回 |
| --- | --- | --- |
| `TSUBAKI_SUCCESS` | 0 | 跑完之后没有错误条目 |
| `TSUBAKI_HASH_ERROR` | 1 | 记录过至少一条错误条目 |
| `TSUBAKI_INVALID_ARGUMENT` | 64 | 命令行被拒：未知子命令、未知算法、密钥算法没给密钥或密钥过长、`-k` 与 `-K` 同时给、`-K -` 与 `-` 同时出现、线程数不对，或者有多余参数（`EX_USAGE`） |
| `TSUBAKI_FILE_READ_FAILED` | 66 | `--key-file` 读不了，或者里面没有密钥（`EX_NOINPUT`） |
| `TSUBAKI_FATAL` | 70 | `argc` 为 0，`main()` 拒绝运行（`EX_SOFTWARE`） |
| `TSUBAKI_FILE_WRITE_FAILED` | 73 | `--output` 文件打不开，或者结果流写失败（`EX_CANTCREAT`） |
| `TSUBAKI_INTERRUPTED` | 130 | `work_result::interrupted` 为真（`128 + SIGINT`） |
| `TSUBAKI_TERMINATED` | 250 | 第五次 `Ctrl-C` 让处理函数直接 `_exit()`，报告没写出来 |
| `TSUBAKI_UNEXPECTED` | 255 | 有异常逃出 `main()` |

`interrupted` 优先于 `errors`：被打断的运行即使也收集到了错误，报告的也是 `130`。
写失败返回 `73`；如果结果写往读端已消失的管道，会先被 `SIGPIPE` 杀掉（shell 看到
141）。

## 数据流示例

```
tsubaki sum sha256 . -e build -l old.txt
```

1. `files` 收走 `.`；`-e build` 是分离式取值，`excludes` 在位置参数之前就把它取走，
   `-l old.txt` 则变成一个列表输入。
2. `.` 变成一个带 `build` 排除项的 `dirscanner`；`old.txt` 被打开，变成一个
   `listscanner`。
3. 两者加进汇流，另外再加一个空的 `cached_taskstream`。
4. 工作线程开始取路径：先从 `dirscanner`（遍历目录树），目录走完后从
   `listscanner` 取。每条路径都走 `checksum_api::calc()`，所以只要 `-k` 给了密钥，
   同一条流程换成 `hmac_sha256` 也照跑。
5. `old.txt` 里带校验和的行立刻以 `# Kept: <路径>` 加原行写出，从不进入计算路径。
6. 所有子流枯竭后，汇流返回空，工作线程停下，进度线程停下，`run()` 返回的
   `work_result` 由 `sum_main` 打印。

## 为什么现在只有一个 manager

`work_manager` 只依赖 `basic_taskstream`，而共享的部分大多已经由接口解决了：
`run()` 返回的 `work_result` 可以用（目前被注释掉的）`operator+` 合并，报告由调用方
打印，`catch_sigint = false` 则能把中断交给外层驱动。所以把一次运行拆给多个
manager，剩下的主要是琐事：

- `-t` 要在几个 manager 之间分；
- 每个 manager 都会画自己的进度行，会抢同一行终端，或者需要一个共享 printer；
- 输入本身要分片，这才是真正的活：多个 manager 共用一个 fanin，仍然会在它的锁上
  串行；
- 错误和退出码来自合并后的 `work_result`。

这些都不难，但也没什么收益：瓶颈在 I/O 和哈希，取路径是唯一串行的步骤。单 manager
让程序保持简单，而草稿里的 `work_result::operator+` 和中断标志把门留着，需要时再
开。

## 扩展点

- **新增子命令**：在 `subcommands` 里加一个常量，给它的条目分配自己的
  `cmdline_group`，在 `main()` 里加一个分支，再写一个像 `sum_main()` 那样、只
  评估自己那一组的驱动。子命令只解析自己的组，所以不相干的开关不会互相干扰。下一件
  计划这样加的功能是比较多份结果；它的数据结构实验草稿放在
  `experiments/timeline_fs/`，没有纳入构建。
- **新增输入源**：从 `basic_taskstream` 派生，实现 `pop_raw()`；需要自己的语法就
  在 `def_args()` 里加条目。汇流不关心有几个生产者。
- **换输出端**：`work_manager` 是唯一的消费者，给它换一个 `std::ostream`，或者整个
  替换掉，`basic_taskstream` 不用动。
- **新增算法**：在 `include/checksum.hpp` 的 `ALGORITHM_LIST` 里加一行，它会生成
  `algorithm`、`str_to_alg()`、`alg_to_str()`。普通摘要还要在 `digest_for()` 里加
  一个 case；密钥算法还要在 `mac_for()` 里加一个 case、在 `is_keyed()` 里加一个
  枚举值；如果该算法限制密钥长度，还要在 `max_key_length()` 里给一个 case；最后
  把它加进帮助文本里的算法清单（`src/help.cpp`，那份是手写的）。

## 源码映射

### 类

| 类 | 基类 | 职责 |
| --- | --- | --- |
| `tsubaki::basic_taskstream` | — | 所有任务流类的虚基类：`pop()` 负责过滤候选，`pop_raw()` 是子类实现的钩子，`errors` + `report_error()` + `take_errors()` 承载错误 |
| `tsubaki::dirscanner` | `basic_taskstream` | 递归遍历目录，处理排除项与软链接 |
| `tsubaki::listscanner` | `basic_taskstream` | 解析列表文件或 stdin，输出 `# Kept: <路径>` 行 |
| `tsubaki::cached_taskstream` | `basic_taskstream` | 按给定顺序回放一组固定的路径 |
| `tsubaki::fanin_taskstream` | `basic_taskstream` | 汇流多个子流，用 `unique_ptr` 持有它们 |
| `tsubaki::checksum_api` | — | 携带选定的算法与密钥，把 `calc()` 分到摘要或 MAC 路径 |
| `tsubaki::work_manager` | — | 工作线程池、进度行、中断标志 |
| `tsubaki::work_result` | — | `run()` 返回的值快照，带 `print()` 和一个注释掉的 `operator+` 草稿 |
| `tsubaki::error_entry` | — | `std::error_code` 加上出错时的操作描述 |

### 文件

| 路径 | 内容 |
| --- | --- |
| `src/main.cpp` | 子命令分发、`general` 参数组、最后的异常兜底 |
| `include/core_functions.hpp` | `sum_main()` 的声明 |
| `include/exit_codes.hpp` | `main()` 与 `sum_main()` 共用的退出码宏 |
| `src/sum_core.cpp` | `sum` 参数组的检查、启动流程、流的组装 |
| `include/basic_taskstream.hpp`, `src/basic_taskstream.cpp` | 基类流与它的 `pop()` 过滤 |
| `include/dirscanner.hpp`, `src/dirscanner.cpp` | 目录遍历 |
| `include/listscanner.hpp`, `src/listscanner.cpp` | 列表文件与 stdin 解析 |
| `include/cached_taskstream.hpp`, `src/cached_taskstream.cpp` | 固定路径回放 |
| `include/fanin_taskstream.hpp`, `src/fanin_taskstream.cpp` | 多源汇流 |
| `include/checksum.hpp`, `src/checksum.cpp` | `ALGORITHM_LIST`、`str_to_alg()` / `alg_to_str()`、`is_keyed()`、`checksum_api`、`EVP_MD` 与 `EVP_MAC` 哈希 |
| `include/worker.hpp`, `src/worker.cpp` | `work_result` 与 `work_manager` |
| `include/utilities.hpp`, `src/utilities.cpp` | 字符串工具、密钥读取、终端宽度、fd 守卫 |
| `include/cmdline.hpp` | 参数条目定义 |
| `include/help.hpp`, `src/help.cpp` | 帮助文本 |
| `LC-ArgParser`（拉取） | 独立的参数解析库，作为源文件编入 |

`experiments/timeline_fs/` 不属于 `tsubaki` 的源码映射：它保存着"比较多份结果"
这件事的实验草稿（`include/timeline_fs.hpp`、`src/timeline_fs.cpp`，以及草稿驱动
`src/tmptest.cpp`）。没有任何 CMake 目标引用它，所以既不参与编译，也不参与测试。
