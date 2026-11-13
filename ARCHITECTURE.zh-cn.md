# Tsubaki-Next 架构

[English](ARCHITECTURE.md)

## 总览

整条流水线是拉取式的。生产者只实现一个"交出下一条路径"的方法；一个汇流对象
把若干生产者合起来；一组工作线程从这个流里取路径并算哈希；最后由一个写入者把
每条结果变成一行输出。

```
  argv ──> cmdline ──> tsubaki_core ──> dirscanner      ─┐
                                    ├─> listscanner     ─┼─> fanin ─> work_manager ─> 输出
                                    └─> cached_taskstream ┘                ├─> printer (stderr)
                                                                            └─> work_result ─> 报告块
```

中间没有"文件列表"这一层。扫描之后不再排序、不再统计、不再过滤：谁取到一条
路径，谁就立刻开算。

## 启动流程

`main()` 只做一件事：把 `tsubaki::tsubaki_main()` 包在 try/catch 里，逃出来的
异常变成一条错误信息和退出码 1。

`tsubaki_main()` 按顺序做这些事：

1. `def_args()` 用 `include/cmdline.hpp` 里的条目定义构造
   `argparser::ap_instance`。
2. `pre_parse(argc, argv)` 把参数切成带风格的 token，`evaluate()` 把它们匹配到
   各条目上。
3. 从 `argv[0]` 取程序名；`--help` 和 `--version` 在**校验之前**检查，所以两者
   都不需要其它参数就能用，也优先于其它参数错误。帮助文本来自
   `src/help.cpp` 的 `help_text()`；版本字符串就是 CMake 项目版本，由
   `TSUBAKI_VERSION` 注入。
4. `fine(false)` 做校验。每个失败的条目都用 `argparser::describe()` 报出来，
   末尾补一句提示去跑 `--help`。
5. 取条目的值并检查：算法经 `str_to_alg()` 映射，线程数解析（失败就退回
   `std::thread::hardware_concurrency()`），输出流打开。
6. 把输入变成生产者，加进 `fanin_taskstream`：每个目录一个 `dirscanner`，每个
   `-` 或列表文件一个 `listscanner`，普通的文件参数汇总成一个
   `cached_taskstream`。队列就按这个顺序建：命令行上出现的目录和 `-`，然后是
   缓存的文件，最后是 `-l` 列表。即使文件参数为空，这个流也会被加进去；输入
   全空也是合法的——`files` 没有最小个数限制，空跑一次只是打印一份空报告。
7. `work_manager(tasks, *out, alg, verbose).run(threads)` 启动工作线程，返回一个
   `work_result`。
8. `tsubaki_main` 把 `#` 和结果写进输出流，并根据 `errors.empty()` 给出退出码。

## 命令行层

解析工作交给 `LC-ArgParser`
（<https://github.com/Lawrence-Charland/LC-ArgParser>），一个独立的 C++17 库，
由 CMake 在配置阶段拉取，并作为源文件编进二进制，而不是链接共享库。Tsubaki-Next 只用它的公开接口（`ap_instance`、
`ap_entry`、`ap_defkey`），这个库对校验和一无所知，别的程序也可以原样复用。

落到本程序上能看见的行为有这么几条：

- `pre_parse` 把每个 token 标成 simple、single dash、double dash 或
  `argv_zero`；`--` 之后的一律是 simple。
- 长名是 `--name` 或 `--name=value`；短名是 `-v`、合并的 `-vs`、或者粘滞的
  `-ovalue`。分离式取值（`--output out.txt`）库本身是支持的。
- 选项会收集它后面连续的 simple token，直到遇到非 simple 的值或到达上限；标志
  只累计出现次数。
- 位置参数在 min/max 区间内收集 simple token，barrier token 会让收集提前结束，
  并且和它一起被消费掉。
- `evaluate()` 按声明顺序逐个处理条目并删掉各自消费的参数，所以谁先谁后决定了一个
  分离式取值最后落到谁手里。
- `fine()` 与 `errors()` 汇总各条目的结果，可选地把没被消费掉的参数报成
  `invalid_arguments`。
- 这个库不承诺兼容性，vendored 的那份副本钉在某个提交上。

条目只在 `include/cmdline.hpp` 里定义一次，声明顺序就是评估顺序：threads、
output、各个标志、algorithm、excludes、lists、files、version、help。

- `excludes` 和 `lists` 排在 `files` 之前，它们的值在位置参数之前就被取走，所以
  分离式取值（`-e build`）能用，前提是它后面不再跟文件参数。
- 这两项的取值个数都没有上限，因此 `-e build .` 会把 `.` 也当成排除项，让 `files`
  收不到东西。
- `files` 是最后一个位置参数，它会一路收集 simple 参数直到 `\` barrier；barrier
  之后的普通参数没人消费，`fine(false)` 会报成 `invalid_arguments`。
- `-h`/`--help` 是标志，所以 `--help=something` 是校验错误，而不是帮助请求；
  `-V`/`--version` 同理。

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
- 其余存在的类型——字符/块设备、FIFO、socket——原样返回，打开还是读取交给
  工作线程。

这层过滤放在基类里，所以子类只需要回答"下一条路径是什么"。`dirscanner` 同时
覆盖了 `pop()`，原因只有一个：它是遍历目录项，遇到非文件应该静默跳过，而不是为
每个子目录记一条错误。这也是目录遍历不会顺手打开 FIFO 或设备的原因。

在这里被过滤掉的路径（缺失、stat 失败、列表里的目录）根本没到工作线程手上，所以
既不打印也不进三个计数器的任何一个，只留下一条 `error_entry`，而它足以让退出码
变成 1。这也是 `Failed` 可以是 0、运行却仍然失败的原因：`Failed` 只统计算哈希
那一步真的返回了错误的文件。

`error_entry` 是 `std::error_code` 加一段自由文本的操作描述；进报告的是那段文本，
所以生产者要写清"当时在做什么"，而不只是"哪一步失败了"。

### dirscanner

构造时接收目录、排除列表和软链接开关，内部持有一个
`std::filesystem::recursive_directory_iterator`，打开时带
`skip_permission_denied`，需要时再加 `follow_directory_symlink`。构造函数会拒绝
不存在的路径和非目录（抛 `std::invalid_argument`），并把不存在的排除项丢掉，每
丢一个记一条错误。

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
`tsubaki_main`，由 `main()` 的最后兜底捕获。

### listscanner

从任意 `std::istream` 读行——文件由 `tsubaki_core` 打开，`-` 时用 `std::cin`
——决定保留某行时还要往结果流里写点东西。每个非空行先 trim，以 `#` 开头的行
忽略。

用 `get_equation()` 在第一个空格处切开。如果前半段正好是 `<NONE>`——本程序给
未计算文件打的标记——空格后面那段就是路径。否则如果前半段不是全十六进制
（`is_checksum()`），整行就是路径，空格也算在内。如果前半段**是**十六进制，这一
行就是 `<校验和> <路径>`：

- 带 `--force-rescan` 时，把路径交出去重新计算；
- 否则这一行和本次运行解耦：往输出里写 `# Kept: <路径>` 和 trim 后的行，它既不
  参与计算，也不进报告的任何计数。

认不出来的行整行都算路径，名字里的空格因此无害；这也是 `<NONE> <路径>` 的输出能
直接当输入喂回去的原因。剩下唯一的歧义是全为十六进制字符的名字：它在字面上和校验
和完全一样，只能靠让前半段不是十六进制来区分（`./名字`、绝对路径）。`<NONE> ` 是
保留前缀，永远按标记读。

列表行永远只代表一个文件。行里写目录时，它跟其他路径一样被交出去，然后被
`pop()` 拒掉，绝不会被展开遍历；只有命令行上的目录参数才会变成 `dirscanner`。

### cached_taskstream

持有一个路径 vector，从尾部往外给，所以一批普通文件参数是倒序处理的。它自己
不检查路径是否存在，无效路径由 `basic_taskstream::pop()` 过滤。

### fanin_taskstream

它是若干个任务流的顺序聚合体，自己也是一个 taskstream，线程池因此只看见一条流。`pop_raw()` 看队首的子流：
子流交出路径就返回；子流枯竭就用 `take_errors()` 把它的错误取出来并进自己的错误
列表，丢掉这个子流，再看下一个。

因为 `pop_raw()` 跑在 `basic_taskstream::pop()` 的互斥锁里，从子流拉取的整个过程
是串行的。并行只发生在算哈希这一段；线程一旦要取下一条路径，就得排在其它要路径
的线程后面。这也意味着某个 `listscanner` 卡在 `std::cin` 上时，堵住的是整条流，
而不只是一个线程。

## 线程安全规则

`pop_raw()` 一定在 `mutex_` 持有下运行，而 `errors` 没有自己的锁。

- `pop_raw()` 里访问自己的状态不用加锁，但在里面调 `report_error()`、
  `take_errors()`、`pop()` 或 `add_task()` 会自锁：这把 mutex 不可重入。要记错误
  就直接写 `errors`。
- 只有在持锁时（`pop_raw()` 内）或单线程时（构造期、`take_errors()` 之后）才能碰
  `errors`。
- 锁顺序是父后子：`fanin` 先锁自己，再锁子流。子流绝不能回调父流的加锁方法，否则
  两个线程可能互等。
- `pop_raw()` 要短、不要阻塞。`listscanner` 的 `getline` 等 stdin 时就持着锁，堵住
  的是整条流。
- 错误要返回，不要抛。异常逃出 `pop_raw()` 会落到没有处理器的 worker 上，最终
  `std::terminate`。

## 哈希计算

`src/checksum.cpp` 的 `checksum_file()` 把算法映射成 `EVP_MD`（`to_evp()`），以
`O_RDONLY | O_CLOEXEC` 打开文件，然后经 `EVP_DigestInit_ex` /
`EVP_DigestUpdate` / `EVP_DigestFinal_ex` 流式处理。读缓冲区是一个 64 KiB 的
`thread_local` vector，每个工作线程分配一次、反复复用。`EINTR` 重试，其余失败
都包成 `std::unexpected` 里的 `std::error_code` 返回，从不抛异常。
`algorithm::none` 直接短路成 `<NONE>`，根本不碰文件。

读取用的是普通 `read()`，不是 `mmap`，也不查 `SEEK_DATA`，所以稀疏文件按逻辑长度
计算：空洞会被当作零喂进摘要。开销跟的是 `st_size`，不是实际分配的块数；不管文件
声称多大，内存占用始终只有那个 64 KiB 缓冲区。

显式命名的非普通路径用的还是同一个阻塞 `open()`：没有写端的 FIFO 会在第一次读之前
就卡住，永不 EOF 的设备则会把工作线程一直留在循环里。两者都看不到算法切换——那个
开关只在文件与文件之间检查。

摘要用 `to_hex()` 转成小写十六进制。

## 工作线程、进度与报告

`work_manager` 持有工作线程数组、进度线程、原子算法值和三个原子计数器。`run()`
返回一个 `work_result` 而不是自己写报告，打印到哪里、怎么打印由调用方决定。

- `run(thread_count)` 先记下启动时用的算法，安装 SIGINT 处理函数，起
  `thread_count` 个工作线程和进度线程，然后等待。
- 每个工作线程循环：`tasks.pop()`、`checksum_file(path, alg.load())`、用
  `osyncstream` 写一行。算失败打印 `<NONE> <路径>` 并让 `failed` 加一；算成功让
  `success` 加一，当前算法是 `none` 时改让 `unprocessed` 加一。被 `pop()` 拒掉的
  路径根本走不到这里，所以既没有输出行也没有计数，只有一条错误条目。
- 进度线程在非 quiet、且算法不是 `none` 时，每 100 ms 在 stderr 上重画一行。它用
  stderr 的终端宽度，宽度未知时退化成朴素的 `[N files]`。
- `wait()` 先 join 工作线程，再把算法置成 `none`（进度循环因此退出），最后 join
  进度线程。
- 全部 join 完之后，`run()` 返回一份快照：启动时的算法、收集到的错误、三个计数
  器，以及从启动到 join 结束的墙钟耗时。

构造函数接收一个 `catch_sigint` 开关，默认打开。关掉后 `run()` 不安装 SIGINT 处理
函数，把 `Ctrl-C` 留给调用方，适合嵌进别的驱动里。

`work_result` 是个纯值对象，字段为 `alg`、`errors`、`success`、`failed`、
`unprocessed`、`duration`。`operator+` 合并两个同算法的结果（错误追加、计数和耗时
相加），是为将来开多个 manager 准备的。`operator<<` 负责写报告块：`Algorithm`、
`Total Processed`（成功 + 失败）、三个计数、`Duration`，以及 `errors` 非空时的
`Errors` 条数和后面的 `# Error messages:` 列表。

`tsubaki_main` 打印 `#` 和结果（`*out << "#\n" << result`），`errors` 为空时返回
0。退出码不由 `failed` 决定；不过每个计算失败同时也留下一条错误条目，所以实际上
两者是一致的。

`SIGINT` 是靠算法值实现的，而不是单独一个标志：处理函数把 `algorithm::none` 存进
工作线程的原子变量，之后每次 `checksum_file()` 调用都立刻返回 `<NONE>`。剩下的
路径照样被取出、打印、计入未处理，所以报告能为扫描器产出的每一个文件留个交代。
已经卡在阻塞 `open()` 或无限读取里的工作线程回不到那个检查点，所以一次 `Ctrl-C`
救不出它；处理函数还会递增一个原子计数器，按到第五次时用 `write(2, ...)` 写一条
固定消息并调用 `std::terminate()`。

## 输出流

`tsubaki_core` 只选定一次输出端：`std::cout`，或者 `--output` 打开的某个文件。
这个 `std::ostream` 由所有工作线程、列表扫描器（写 `# Kept: <路径>` 标记）和最终
报告共用。写入都走 `std::osyncstream`，它会把一次完整的插入整体刷出，所以行不会
交错。

进度指示是唯一写到 stderr 的东西。构造参数集合阶段发现的问题（`cmdline` 报错、
输出文件打不开、列表文件找不到）也写 stderr；运行期间发现的错误则进结果流里的
报告块。

## 并发模型

| 线程 | 数量 | 工作 |
| --- | --- | --- |
| 主线程 | 1 | 搭好各条流，调用 `run()`，由它 join 工作线程 |
| 工作线程 | `-t` 个 | 取一条路径、算哈希、写一行 |
| 进度线程 | 1 | 每 100 ms 重画进度行 |

- 每个 taskstream 一把互斥锁。锁覆盖 `pop_raw()` 和错误数组，只在取路径的那一小
  段时间里被持有。
- 原子变量：算法、三个计数器、中断计数器、信号处理函数用的目标指针。指针之所以
  是 `volatile`，是因为信号处理函数会写它；算法和中断计数器用 `std::atomic`，
  因为处理函数要往里存值。
- 算哈希时不持任何锁，文件 I/O 完全并行。
- 流水线里没有任何地方对结果重排序：输出顺序是完成顺序，不是扫描顺序。

## 数据流示例

```
tsubaki sha256 . -e build -l old.txt
```

1. `files` 收走 `.`；`-e build` 是分离式取值，`excludes` 在位置参数之前就把它取走，
   `-l old.txt` 则变成一个列表输入。
2. `.` 变成一个带 `build` 排除项的 `dirscanner`；`old.txt` 被打开，变成一个
   `listscanner`。
3. 两者加进汇流，另外再加一个空的 `cached_taskstream`。
4. 工作线程开始取路径：先从 `dirscanner`（遍历目录树），目录走完后从
   `listscanner` 取。
5. `old.txt` 里带校验和的行立刻以 `# Kept: <路径>` 加原行写出，从不进入计算路径。
6. 所有子流枯竭后，汇流返回空，工作线程停下，进度线程停下，`run()` 返回的
   `work_result` 由 `tsubaki_main` 打印。

## 为什么现在只有一个 manager

`work_manager` 只依赖 `basic_taskstream`，而共享的部分大多已经由接口解决了：
`run()` 返回的 `work_result` 可以用 `operator+` 合并，报告由调用方打印，
`catch_sigint = false` 则能把中断交给外层驱动。所以把一次运行拆给多个 manager，
剩下的主要是琐事：

- `-t` 要在几个 manager 之间分；
- 每个 manager 都会画自己的进度行，会抢同一行终端，或者需要一个共享 printer；
- 输入本身要分片，这才是真正的活：多个 manager 共用一个 fanin，仍然会在它的锁上
  串行；
- 错误和退出码来自合并后的 `work_result`。

这些都不难，但也没什么收益：瓶颈在 I/O 和哈希，取路径是唯一串行的步骤。单 manager
让程序保持简单，而 `work_result::operator+` 和信号开关把门留着，需要时再开。

## 扩展点

- **新增输入源**：从 `basic_taskstream` 派生，实现 `pop_raw()`；需要自己的语法就
  在 `def_args()` 里加条目。汇流不关心有几个生产者。
- **换输出端**：`work_manager` 是唯一的消费者，给它换一个 `std::ostream`，或者整个
  替换掉，`basic_taskstream` 不用动。
- **新增算法**：在 `include/checksum.hpp` 的 `ALGORITHM_LIST` 里加一行
  （`algorithm`、`str_to_alg()`、`alg_to_str()` 都由它生成），再在 `to_evp()` 里加
  case。
- **新增构建目标**：这套 API 是刻意收窄的，所以另一个可执行文件（比如需要一次性
  拿到全部校验和的比较工具）可以复用 `checksum_file()`、各扫描器和参数解析库，
  而不必共用 `work_manager` 和结果格式。那是另一支程序，不是 `tsubaki` 的一个模式。

## 源码映射

### 类

| 类 | 基类 | 职责 |
| --- | --- | --- |
| `tsubaki::basic_taskstream` | — | 所有任务流类的虚基类：`pop()` 负责过滤候选，`pop_raw()` 是子类实现的钩子，`errors` + `report_error()` + `take_errors()` 承载错误 |
| `tsubaki::dirscanner` | `basic_taskstream` | 递归遍历目录，处理排除项与软链接 |
| `tsubaki::listscanner` | `basic_taskstream` | 解析列表文件或 stdin，输出 `# Kept: <路径>` 行 |
| `tsubaki::cached_taskstream` | `basic_taskstream` | 回放一组固定的路径 |
| `tsubaki::fanin_taskstream` | `basic_taskstream` | 汇流多个子流，用 `unique_ptr` 持有它们 |
| `tsubaki::work_manager` | — | 工作线程池、进度行、SIGINT 开关 |
| `tsubaki::work_result` | — | `run()` 返回的值快照，带 `operator+` 与 `operator<<` |
| `tsubaki::error_entry` | — | `std::error_code` 加上出错时的操作描述 |

### 文件

| 路径 | 内容 |
| --- | --- |
| `src/tsubaki_main.cpp` | 入口，以及最后的异常兜底 |
| `src/tsubaki_core.cpp` | 启动流程、参数检查、流的组装 |
| `include/basic_taskstream.hpp`, `src/basic_taskstream.cpp` | 基类流与它的 `pop()` 过滤 |
| `include/dirscanner.hpp`, `src/dirscanner.cpp` | 目录遍历 |
| `include/listscanner.hpp`, `src/listscanner.cpp` | 列表文件与 stdin 解析 |
| `include/cached_taskstream.hpp`, `src/cached_taskstream.cpp` | 固定路径回放 |
| `include/fanin_taskstream.hpp`, `src/fanin_taskstream.cpp` | 多源汇流 |
| `include/checksum.hpp`, `src/checksum.cpp` | `ALGORITHM_LIST`、`str_to_alg()` / `alg_to_str()`、EVP 哈希 |
| `include/worker.hpp`, `src/worker.cpp` | `work_result` 与 `work_manager` |
| `include/utilities.hpp`, `src/utilities.cpp` | 字符串工具、终端宽度、fd 守卫 |
| `include/cmdline.hpp` | 参数条目定义 |
| `include/help.hpp`, `src/help.cpp` | 帮助文本 |
| `LC-ArgParser`（拉取） | 独立的参数解析库，作为源文件编入 |

`src/kousei_main.cpp`、`src/timeline_fs.cpp` 和 `include/timeline_fs.hpp` 是尚未
完成的实验（一个还没在 `CMakeLists.txt` 中启用的后续构建目标），不属于 `tsubaki`
的源码映射。
