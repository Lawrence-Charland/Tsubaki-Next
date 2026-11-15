# Tsubaki-Next

[English](README.md)

Tsubaki 校验和工具的重写版。

## 来由

Tsubaki-Next 是早先 Tsubaki 的精神续作。留下的只有名字和目标，老架构整个推翻了：文件
不再先被收集成一张表，而是由扫描器喂进共享任务流，线程池边来边算。已归档的
[Lawrence-Charland/Tsubaki](https://github.com/Lawrence-Charland/Tsubaki) 里并排放着
两套各自独立的实现：`old` 分支是本项目作者自己写的，`main` 分支是 GDJT-DSR 写的。
那个仓库已经归档；[GDJT-DSR/Tsubaki](https://github.com/GDJT-DSR/Tsubaki) 仍在
维护。

## 范围

本项目只构建一个可执行文件 `tsubaki`，而且会一直如此。新功能以**子命令**的形式加
进这个二进制，而不是再开新目标：`sum` 是第一个，也是目前唯一的子命令。

所有"必须先拿到全部校验和才能下结论"的功能——也就是比较多份结果——目前刻意不做。

## 路线图

流水线保持单向：一条路径算完就写出去，从不收集。而比较是反过来的形状——参与比较的
结果全部进了内存才能下结论——所以它会变成同一个二进制里、`sum` 旁边的另一个子命令。
除此之外还没有任何设计定下来，定下来的只是它会长成什么样：

- 它不是新程序。一个子命令需要一个 `subcommands::` 常量、`include/cmdline.hpp` 里
  自己的条目分组、`main()` 分发里的一个分支，以及一个像 `sum_main()` 那样的驱动。
  参数解析库、各扫描器和 `checksum_file()` 都是共用的，不同的只有结果格式和退出码
  映射。
- `timeline_fs` 是 `include/timeline_fs.hpp` 为"让结果可以相互比较"画出的结构：
  一棵内存树，每个节点各自带一份**按快照区分**的子节点表和一份按快照区分的校验和表。
  先按名字登记一个时间 id，再在那个 id 下建目录、建文件、更新或删除。载入任意多个
  id 并同时遍历这棵树，就能看出各次运行之间变了什么；同一张表也能看出哪些文件内容
  相同。

这些都还没纳入构建：`timeline_fs` 的源码在树里，但没有任何目标引用它，也没有任何
比较用的条目或驱动。模块划分本身见
[ARCHITECTURE.zh-cn.md](ARCHITECTURE.zh-cn.md)。

## 构建

需要 CMake 3.28+（这是 `CMakePresets.json` 的要求；`CMakeLists.txt` 本身只要求
3.10）、支持 C++23 的编译器、OpenSSL（只链接 `libcrypto`），预设里用的是 Ninja。

```sh
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
```

第一次配置会从 GitHub 拉取
[LC-ArgParser](https://github.com/Lawrence-Charland/LC-ArgParser)，所以需要联网
一次；拉下来的副本放在 `build/<预设名>/_deps`，之后复用。没有网络时，把 CMake
指向本地检出即可：

```sh
cmake --preset linux-gcc-release \
      -DFETCHCONTENT_SOURCE_DIR_LC-ARGPARSER=/path/to/LC-ArgParser
```

可执行文件在 `build/linux-gcc-release/tsubaki`。另有布局相同的
`linux-gcc-debug` 预设。

## 用法

```
tsubaki sum [选项] <算法> <文件|目录>...
tsubaki --help
tsubaki --version
```

`sum` 子命令是必需的，只有 `--help` 和 `--version` 不需要它；子命令位置上换成别的
词会按未知子命令报错。

`算法` 是位置参数，它后面的输入也是。输入可以是目录、文件，或者 `-`（标准输入），
可以混着写。单独一个 `\` 结束输入列表，它后面的普通参数不再当作输入。

稳妥的写法是：算法放最前，然后一口气写完全部输入源，最后再写选项。

```sh
tsubaki sum sha256 目录1 文件2 - -e build -l old.txt
```

选项放最后，是因为分离式取值只从开关后面那段普通参数里拿，直到下一个还在队列里的
开关为止：把 `-e` 写在输入前面会把输入吃掉。`files` 又排在 `excludes` 和 `lists`
之后评估，所以那两个选项剩下的东西仍然会流到它手里。

输入一个都不给也行：`tsubaki sum sha256` 会以一条空流跑完，只打印报告块，各计数
全为 0。

### 选项

| 选项                           | 含义                                             |
| ------------------------------ | ------------------------------------------------ |
| `-h`, `--help`                 | 打印帮助后退出                                   |
| `-V`, `--version`              | 打印版本后退出                                   |
| `-v`, `--verbose`              | 在 stderr 上显示实时进度                         |
| `-t<n>`, `--thread=<n>`        | 工作线程数，正整数，默认为硬件并发数             |
| `-o<file>`, `--output=<file>`  | 结果写入 `<file>`（新建或截断），而不是 stdout   |
| `-e<path>`, `--exclude=<path>` | 遍历目录时跳过 `<path>` 及其下所有内容，可重复   |
| `-l<file>`, `--list=<file>`    | 从 `<file>` 读取路径列表，可重复                 |
| `-s`, `--follow-symlinks`      | 跟随指向目录的符号链接                           |
| `-f`, `--force-rescan`         | 重算输入列表中已有的校验和                       |

别名：`--threads`、`--excludes`、`--lists`。

解析本身由独立的小库
[LC-ArgParser](https://github.com/Lawrence-Charland/LC-ArgParser) 完成；
`include/cmdline.hpp` 里只负责描述本程序的命令行。

### 选项的取值

长选项用 `=` 赋值，短选项用粘滞写法：

```sh
tsubaki sum sha256 . --exclude=build --list=old.txt
tsubaki sum sha256 . -ebuild -lold.txt
```

分离式写法取的是开关后面紧跟的那一串普通参数，取几个由选项决定：`-t` 和 `-o` 只取
一个，`-e` 和 `-l` 你给几个取几个，直到碰到下一个还在队列里的开关。

```sh
tsubaki sum sha256 . -e a b c -l list1 list2   # 三个排除项、两个列表
```

这里 `-e` 会在 `-l` 前停住，因为 `lists` 排在 `excludes` 之后评估；每个条目各取
自己那一串值。

匹配不到任何条目的开关不会被当成选项错误：它仍然是一个普通参数，会被位置参数收
进去，然后按“文件不存在”报错。所以把 `--exclude` 敲成 `--exlude=build` 时，错误
出现在报告的错误块里，而不是一句选项错误。

条目按固定顺序评估，被消费掉的开关会从队列里消失，所以后面的选项能跨过先前已被取走
的开关继续吃参数：

```sh
# threads 先跑，取走 "-t 4"；
# 然后 excludes 看到的是 "c.txt ."，两个都收
tsubaki sum sha256 -e c.txt -t 4 .        # 没剩输入：空跑一次，退出码 0
```

所以稳妥的写法是把输入全部放在选项前面：

```sh
tsubaki sum sha256 目录1 文件2 - -e build -l old.txt  # -e 取 build，-l 取 old.txt
tsubaki sum sha256 -e build 目录1                     # -e 把 目录1 也收了，输入为空
```

单独一个 `\` 结束输入列表；它后面的普通参数不再算输入，所以除非被别的选项吃掉，
否则会报参数错误。在 shell 里要给反斜杠加引号，写成 `'\'`；不加引号时 `\ ` 会
转义后面的空格，程序根本收不到这个参数。

### 算法

```
none  md5  sha1  sha224  sha256  sha384  sha512
sha3_256  sha3_512  blake2b512  blake2s256
```

`none` 只列文件不计算，所有文件都计入 `Not hashed`。

## 输入

命令行上的文件与目录直接扫描。列表文件（`-l`，或 `-` 从标准输入读）逐行解析。
每一行都会先 trim，所以首尾空白不会变成路径的一部分：

- 空行、以 `#` 开头的行忽略；
- 以 `<NONE> `（标记加一个空格）开头的行，取标记后面的路径；
- `<校验和> <路径>` 形式的行原样保留，输出中打 `# Kept: <路径>` 加 trim 后的
  原行；只有给了 `--force-rescan` 才会按路径重算；
- 其它行整行都算路径，空格也算在内。

被保留的行与计算过程解耦：不参与哈希，也不出现在报告的计数里。它是在读列表时
就写进结果流的，所以可能夹在算完的行中间。

列表里的每一行都当作文件处理。行里写目录会被拒掉，不会展开；只有命令行上的目录
参数才会被遍历。以 `#` 开头的行即使本意是路径也算注释，所以名字以 `#` 开头的文件
要写成 `./#名字` 或绝对路径。

如果路径里带空格、而第一个空格前的整段又是纯十六进制，它就会被当成“校验和 +
路径”，这类条目要写成 `./名字` 或绝对路径；没有空格的纯十六进制文件名不受影响。
带空格的路径不用加引号，认不出来的行整行都算路径。

`<NONE> <路径>` 也能作为输入，所以一次运行的输出可以直接用 `-l` 喂回去，只重做
那些没有计算校验和的文件。

排除项是用 `equivalent()` 跟目录遍历中遇到的每个条目比较的，因此它既不是通配
模式，也不是字符串前缀。它只作用于目录扫描：命令行上直接给出的文件、或列表里
读进来的文件，即使被排除也照样计算。若排除项就是被扫描的目录本身、或它的某个
上级目录，则什么都匹配不到，会被静默忽略——要排除就排除子目录。也别用 `-e` 去
“排除”一个同时作为输入传进来的目录：不想要它，就别在命令行上写它。

目录遍历时只算普通文件，路上遇到的设备或 FIFO 会被静默跳过。显式命名的输入不
一样：命令行上给出的、或列表里写着的设备/FIFO 会被当作流打开并读完。所以
`/dev/null` 算出的是空内容，`/dev/stdin` 算管道送来的东西，进程替换的
`/dev/fd/N` 也能用。这类流可能阻塞或者永远不结束，见“输出”和“中断”。

路径不做去重：同一个路径写两次就处理两次。

稀疏文件按逻辑长度计算：空洞读出来就是零，所以只占几 MB 的 1 TB 稀疏镜像仍要读满
1 TB。

## 输出

结果默认写到 stdout，可用 `--output` 改到文件。每个文件一行：

```
<校验和> <路径>
```

行的顺序是完成顺序，不是输入顺序，所以多线程下每次运行的顺序都可能不同。路径按
扫描器产出的原样打印：`.` 下面找到的条目带 `./`，列表里的条目则按原写法。

没算过的文件——被中断的、或者算法是 `none` 的——写成 `<NONE> <路径>`。已经通过
扫描、但打开/读取/哈希失败的文件也写成 `<NONE>`，并计入 Failed。

不存在的路径会在任何工作线程看到它之前就被过滤掉：没有输出行，也不进下面任何
一个计数，只往错误块里加一条记录，但它一样会让退出码变成 1。列表里写的目录也
按同样方式拒绝。

随后是报告块，每行都以 `#` 开头：

```
#
# ---------- Summary ----------
# Algorithm:         sha256
# Processed:         3
# Hashed:            3
# Failed:            0
# Not hashed:        0
# Duration:          1.23456 seconds
# Errors:            0
```

`Algorithm` 是本次运行选定的算法，被 `Ctrl-C` 打断也不会在报告里变成 `none`。
`Duration` 量的是从启动工作线程到 join 结束，不含参数解析和扫描准备。

`Processed` 等于 `Hashed + Failed`，未计算校验和的文件不计入；被保留的行和跳过
的路径也不算在内。

`# Errors:` 条数行总会打印，即使为 0 也在。记录过至少一条错误时，条数行后面跟一个
`# Error messages:` 块，每条一行 `# Value: / Message: / Operation:`；被打断的运行
会在条数下面多出一行 `# The process was interrupted halfway through.`：

```
# Errors:            2
#
# Error messages:
# Value: 2, Message: No such file or directory, Operation: determining the file type of plain.txt
```

错误不一定来自计算失败：不存在的输入路径、打不开的列表文件、不存在的排除项都会
记在这里，所以一次运行的 `Failed` 可以是 0、退出码却是 1（而且不存在的排除项
虽然报错，扫描本身照常继续）。

用 `--output` 时，报告块还会额外写一份到 stderr（不含逐条错误信息），这样结果
进文件、摘要仍留在终端上。

进度指示只写 stderr，不混进结果流。

## 退出状态

| 退出码 | 含义 |
| --- | --- |
| `0` | 没有记录任何错误 |
| `1` | 记录过至少一条错误：校验失败、不存在的路径、打不开的列表文件、不存在的排除项 |
| `2` | 命令行无效：未知子命令、未知算法、线程数不对，或者有多余参数 |
| `70` | 启动即失败：`argc` 为 0，进程拒绝运行 |
| `74` | `--output` 指定的文件打不开 |
| `130` | 运行被 `Ctrl-C` 打断 |
| `-1`（shell 里看到的是 `255`） | 有异常逃出了 `main()` |

`--help` 和 `--version` 的退出码是 `0`。`70` 和 `74` 取自 sysexits 的
`EX_SOFTWARE` 和 `EX_IOERR`；`130` 就是 shell 的 `128 + SIGINT`。

这些码定义在 `include/exit_codes.hpp` 里：`TSUBAKI_SUCCESS`、`TSUBAKI_HASH_ERROR`、
`TSUBAKI_INVALID_ARGUMENT`、`TSUBAKI_FILE_WRITE_FAILED`、`TSUBAKI_INTERRUPTED`、
`TSUBAKI_FATAL` 和 `TSUBAKI_UNEXPECTED`。

## 中断

`Ctrl-C` 会把本次运行切到 `none` 算法：还没开始的文件照样被取出，写成
`<NONE>` 并计入 `Not hashed`，因此报告能为扫描器产出的每个文件留个交代。已经进入
计算函数的文件不会被丢掉；如果那是设备或 FIFO，就意味着它会一直读到 EOF。而卡在
`open()` 或无限读取里的工作线程根本回不到检查点，所以一次 `Ctrl-C` 停不下它——
只有第五次才会直接中止进程。

这让中断可以续算：把那次的结果文件直接当 `-l` 的输入喂回去，已经带校验和的行原样
保留，`<NONE>` 行重新计算。报告块以 `#` 开头会被忽略，所以结果文件不用先过滤。
被中断的运行即使没有任何失败，退出码也是 `130`，所以 `1` 仍然代表真的出错。

连按五次 `Ctrl-C` 则彻底放弃：处理函数往 stderr 写一行消息并调用
`std::terminate()`。

## 架构

模块划分、任务流接口、线程模型和数据流见 [ARCHITECTURE.zh-cn.md](ARCHITECTURE.zh-cn.md)。

## 许可证

Tsubaki-Next 以 GNU 通用公共许可证第 2 版发布，全文见 [COPYING](COPYING)。

解析库是同一作者的另一个项目，按 MIT 许可发布；它自带的 `LICENSE` 随源码一起
分发，构建时取用的那份仍归 MIT。
