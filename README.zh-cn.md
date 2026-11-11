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

当前只构建一个目标 `tsubaki`，它只做校验和计算。

所有"必须先拿到全部校验和才能下结论"的功能——也就是 `cmp`、`dup` 这类比较
——是刻意不做的。这些功能如果将来回来，大概率会作为独立的构建目标，而不是
`tsubaki` 的一个模式。

## 构建

需要 CMake 3.28+、支持 C++23 的编译器、OpenSSL（只链接 `libcrypto`），预设里
用的是 Ninja。

```sh
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
```

第一次配置会从 GitHub 拉取
[LC-ArgParser](https://github.com/Lawrence-Charland/LC-ArgParser)，所以需要联网
一次；拉下来的副本放在 `build/_deps`，之后复用。没有网络时，把 CMake 指向本地
检出即可：

```sh
cmake --preset linux-gcc-release \
      -DFETCHCONTENT_SOURCE_DIR_LC-ARGPARSER=/path/to/LC-ArgParser
```

可执行文件在 `build/linux-gcc-release/tsubaki`。另有布局相同的
`linux-gcc-debug` 预设。

## 用法

```
tsubaki [选项] <算法> <文件|目录>...
```

`算法` 是位置参数，它后面的输入也是。输入可以是目录、文件，或者 `-`（标准输入），
可以混着写。单独一个 `\` 结束输入列表，它后面的普通参数不再当作输入。

稳妥的写法是：算法放最前，然后一口气写完全部输入源，最后再写选项。

```sh
tsubaki sha256 目录1 文件2 - -e build -l old.txt
```

选项放最后，是因为 `-e` 和 `-l` 会吃掉它们后面所有的普通参数；这样写时输入已经在前
面收集完了。

### 选项

| 选项                           | 含义                                             |
| ------------------------------ | ------------------------------------------------ |
| `-h`, `--help`                 | 打印帮助后退出                                   |
| `-v`, `--verbose`              | 在 stderr 上显示实时进度                         |
| `-t<n>`, `--thread=<n>`        | 工作线程数，默认为硬件并发数                     |
| `-o<file>`, `--output=<file>`  | 结果写入 `<file>`，而不是 stdout                 |
| `-e<path>`, `--exclude=<path>` | 扫描目录时跳过 `<path>` 及其下所有内容，可重复   |
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
tsubaki sha256 . --exclude=build --list=old.txt
tsubaki sha256 . -ebuild -lold.txt
```

分离式写法取的是开关后面紧跟的那一串普通参数，取几个由选项决定：`-t` 和 `-o` 只取
一个，`-e` 和 `-l` 你给几个取几个，直到碰到下一个还在队列里的开关。

```sh
tsubaki sha256 . -e a b c -l list1 list2   # 三个排除项、两个列表
```

这里 `-e` 会在 `-l` 前停住，因为 `lists` 排在 `excludes` 之后评估；每个条目各取
自己那一串值。

条目按固定顺序评估，被消费掉的开关会从队列里消失，所以后面的选项能跨过先前已被取走
的开关继续吃参数：

```sh
# threads 先跑，取走 "-t 4"；
# 然后 excludes 看到的是 "c.txt ."，两个都收
tsubaki sha256 -e c.txt -t 4 .        # 报错：没剩输入了
```

所以稳妥的写法是把输入全部放在选项前面：

```sh
tsubaki sha256 目录1 文件2 - -e build -l old.txt  # -e 取 build，-l 取 old.txt
tsubaki sha256 -e build 目录1                     # -e 把 目录1 也收了，输入为空
```

单独一个 `\` 结束输入列表；它后面的普通参数不再算输入，所以除非被别的选项吃掉，
否则会报参数错误。在 shell 里要给反斜杠加引号，写成 `'\'`；不加引号时 `\ ` 会
转义后面的空格，程序根本收不到这个参数。

### 算法

```
none  md5  sha1  sha224  sha256  sha384  sha512
sha3_256  sha3_512  blake2b512  blake2s256
```

`none` 只列文件不计算，所有文件都记为未处理。

## 输入

命令行上的文件与目录直接扫描。列表文件（`-l`，或 `-` 从标准输入读）逐行解析：

- 空行、以 `#` 开头的行忽略；
- 以 `<NONE> ` 开头的行，取标记后面的路径；
- `<校验和> <路径>` 形式的行原样保留，输出中打 `# Kept` 加原行；只有给了
  `--force-rescan` 才会按路径重算；
- 其它行整行都算路径，空格也算在内。

被保留的行与计算过程解耦：不参与哈希，也不出现在报告的计数里。

纯十六进制字符的路径会被当成校验和，所以列表里的路径写成 `./名字` 或绝对路径；
有 `/` 在，这段就不会被误判。带空格的路径不用加引号，认不出来的行整行都算路径。

`<NONE> <路径>` 也能作为输入，所以一次运行的输出可以直接用 `-l` 喂回去，只重做
那些没算成的文件。

路径不做去重：同一个路径写两次就处理两次。

## 输出

结果默认写到 stdout，可用 `--output` 改到文件。每个文件一行：

```
<校验和> <路径>
```

没算过的文件——被中断的、或者算法是 `none` 的——写成 `<NONE> <路径>`。随后是
报告块，每行都以 `#` 开头：

```
#
# ----------General Report----------
# Algorithm: sha256
# Total Processed: 3
# Succeeded: 3
# Failed: 0
# Unprocessed: 0
# Duration: 1.23456 seconds
```

`Algorithm` 是本次运行选定的算法，被 `Ctrl-C` 打断也不会在报告里变成 `none`。
`Duration` 量的只是计算本身，不含参数解析和扫描准备。

`Total Processed` 等于 `Succeeded + Failed`，未处理的文件不计入；被保留的行和跳过
的路径也不算在内。

出错时后面再跟一个 `# Error messages:` 块，每条一行
`# Value= / Message= / Operation=`。进度指示只写 stderr，不混进结果流。

## 退出状态

所有文件都成功且没有记录错误时为 `0`，否则为 `1`；参数错误同样是 `1`。

## 中断

`Ctrl-C` 会把本次运行切到 `none` 算法：还没开始的文件照样被取出，写成
`<NONE>` 并计入未处理，因此报告能为扫描器产出的每个文件留个交代。已经进入
计算函数的文件会正常算完。

这让中断可以续算：把那次的结果文件直接当 `-l` 的输入喂回去，已经带校验和的行原样
保留，`<NONE>` 行重新计算。报告块以 `#` 开头会被忽略，所以结果文件不用先过滤。

## 架构

模块划分、任务流接口、线程模型和数据流见 [ARCHITECTURE.zh-cn.md](ARCHITECTURE.zh-cn.md)。

## 许可证

Tsubaki-Next 以 GNU 通用公共许可证第 2 版发布，全文见 [COPYING](COPYING)。

解析库是同一作者的另一个项目，按 MIT 许可发布；它自带的 `LICENSE` 随源码一起
分发，构建时取用的那份仍归 MIT。
