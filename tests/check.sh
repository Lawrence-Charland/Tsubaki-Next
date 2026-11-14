#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# tsubaki 的黑盒契约测试。
#
# 每条用例对应 ARCHITECTURE.zh-cn.md / README.zh-cn.md 里的一句承诺。
# 这里不测内部函数，只测"给它这些字节，它应该还回来那些字节"。
#
#   ./tests/check.sh                          # 自动找 build/ 下的二进制
#   TSUBAKI=/usr/bin/tsubaki ./tests/check.sh # 指定二进制
#
# 退出码：0 = 全部通过，1 = 有失败。
#
# 第 09 条目前是红的：listscanner 不检查 is.bad()，读失败的列表源
# 与"列表已读完"返回同一个 false，于是程序报告"成功处理 0 个文件"。
# 它是已知缺陷，不是脚本坏了。

set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

BIN=${TSUBAKI:-}
if [ -z "$BIN" ]; then
    for candidate in "$ROOT/build/linux-gcc-release/tsubaki" \
                     "$ROOT/build/linux-gcc-debug/tsubaki"; do
        if [ -x "$candidate" ]; then
            BIN=$candidate
            break
        fi
    done
fi
if [ -z "$BIN" ]; then
    BIN=$(command -v tsubaki || true)
fi
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "找不到 tsubaki。用 TSUBAKI=/path/to/tsubaki 指定。" >&2
    exit 1
fi

FX=$(mktemp -d)
trap 'rm -rf "$FX"' EXIT
ERR=$FX/.stderr

pass=0
fail=0
xfail=0
xfail_names=()
declare -a failed_names=()

out=""
rc=0

run() {
    out=$("$BIN" "$@" 2>"$ERR")
    rc=$?
}

# 从报告块里取字段，例如 field 'Total Processed'
field() {
    printf '%s\n' "$out" | sed -n "s/^# $1:[[:space:]]*//p" | head -1
}

# 结果行（校验和 + 路径），$1 是摘要长度，默认 64
results() {
    printf '%s\n' "$out" | grep -E "^[0-9a-f]{${1:-64}} " || true
}

# 结果行数
count_results() {
    results "${1:-64}" | wc -l | tr -d ' '
}

ok() {
    pass=$((pass + 1))
    printf '  \033[32mPASS\033[0m  %s  %s\n' "$1" "$2"
}

xfail() {
    xfail=$((xfail + 1))
    xfail_names+=("$1 $2")
    printf '  \033[33mXFAIL\033[0m %s  %s\n' "$1" "$2"
    [ -n "${3:-}" ] && printf '        %s\n' "$3"
}

xpass() {
    pass=$((pass + 1))
    printf '  \033[32mXPASS\033[0m %s  %s  <- 已修复，可以转成普通用例\n' "$1" "$2"
}

bad() {
    fail=$((fail + 1))
    failed_names+=("$1 $2")
    printf '  \033[31mFAIL\033[0m  %s  %s\n' "$1" "$2"
    if [ -n "${3:-}" ]; then
        printf '        %s\n' "$3"
    fi
}

expect() { # expect <用例号> <名字> <条件描述> <命令...>
    local id=$1 name=$2 what=$3
    shift 3
    if "$@"; then
        ok "$id" "$name"
    else
        bad "$id" "$name" "$what"
    fi
}

section() { printf '\n%s\n' "$1"; }

# ---------------------------------------------------------------- 01
# 承诺："每行 <校验和> <路径>"，摘要必须与参考实现一致。
section "01-02 基本计算"
name="输出与 sha256sum 逐行一致（子目录/空文件/空格名/= 号名）"
fx=$FX/t1
mkdir -p "$fx/sub/deep"
printf 'hello\n' >"$fx/a.txt"
: >"$fx/empty"
printf 'x\n' >"$fx/sub/with space.txt"
printf 'y\n' >"$fx/sub/deep/a=b.txt"
head -c 100000 /dev/urandom >"$fx/sub/deep/big.bin"

run sha256 "$fx"
if [ $rc -ne 0 ]; then
    bad 01 "$name" "退出码 $rc"
else
    printf '%s\n' "$out" | grep -E '^[0-9a-f]{64} ' |
        while IFS= read -r line; do
            h=${line%% *}
            p=${line#* }
            printf '%s ./%s\n' "$h" "${p#"$fx/"}"
        done | sort >"$FX/got"
    (cd "$fx" && find . -type f -print0 | sort -z | xargs -0 sha256sum) |
        sed 's/^\([0-9a-f]\{64\}\)  /\1 /' | sort >"$FX/want"
    if diff -q "$FX/want" "$FX/got" >/dev/null; then
        ok 01 "$name"
    else
        bad 01 "$name" "$(diff -u "$FX/want" "$FX/got" | sed -n '3,7p' | tr '\n' '|')"
    fi
fi

# 承诺："输入一个都不给也行……各计数全为 0"。
name="空输入：四个计数全 0，退出码 0"
run sha256
if [ $rc -eq 0 ] &&
   [ "$(field 'Total Processed')" = 0 ] &&
   [ "$(field 'Succeeded')" = 0 ] &&
   [ "$(field 'Failed')" = 0 ] &&
   [ "$(field 'Unprocessed')" = 0 ] &&
   [ -z "$(field 'Errors')" ]; then
    ok 02 "$name"
else
    bad 02 "$name" "rc=$rc total=$(field 'Total Processed') errors=$(field 'Errors')"
fi

# ---------------------------------------------------------------- 03
# 承诺："用 equivalent() 跟每个条目比较，因此不是通配模式，也不是字符串前缀"。
section "03 过滤语义"
name="-e /a/b 不会连带排除 /a/bc"
fx=$FX/t3
mkdir -p "$fx/b" "$fx/bc"
printf '1\n' >"$fx/b/in_b.txt"
printf '2\n' >"$fx/bc/in_bc.txt"
run sha256 "$fx" -e "$fx/b"
if [ $rc -eq 0 ] && [ "$(count_results)" = 1 ] &&
   grep -q '/bc/in_bc.txt$' <<<"$out"; then
    ok 03 "$name"
else
    bad 03 "$name" "rc=$rc 行数=$(count_results) 输出=$out"
fi

# ---------------------------------------------------------------- 04-05
# 承诺："被保留的行与计算过程解耦：不参与哈希，也不出现在报告的计数里"。
section "04-05 列表回喂"
fx=$FX/t4
mkdir -p "$fx"
printf 'hello\n' >"$fx/a.txt"
sum=$(sha256sum "$fx/a.txt" | cut -d' ' -f1)
printf '%s %s\n' "$sum" "$fx/a.txt" >"$FX/keep.list"

name="<校验和> <路径> 行只打 # Kept:，不进计数"
run sha256 -l "$FX/keep.list"
if [ $rc -eq 0 ] && [ "$(field 'Total Processed')" = 0 ] &&
   grep -q "^# Kept: $fx/a.txt" <<<"$out"; then
    ok 04 "$name"
else
    bad 04 "$name" "rc=$rc total=$(field 'Total Processed') 无 # Kept: 行"
fi

# 承诺："<NONE> 行重新计算"，"报告块以 # 开头会被忽略，所以结果文件不用先过滤"。
name="<NONE> 行回喂后重算，结果与直接计算一致"
fx=$FX/t5
mkdir -p "$fx/sub"
printf 'a\n' >"$fx/one.txt"
printf 'b\n' >"$fx/sub/two.txt"
run none "$fx"
printf '%s\n' "$out" >"$FX/none.txt"
run sha256 -l "$FX/none.txt"
refeed=$out
run sha256 "$fx"
if [ "$(results | sort)" = "$(printf '%s\n' "$refeed" | grep -E '^[0-9a-f]{64} ' | sort)" ] &&
   [ "$(field 'Succeeded')" = 2 ]; then
    ok 05 "$name"
else
    bad 05 "$name" "回喂结果与直接计算不一致"
fi

# ---------------------------------------------------------------- 06-07
# 承诺："列表里的目录……被 pop() 拒掉，绝不会被展开遍历"。
section "06-07 错误路径"
fx=$FX/t6
mkdir -p "$fx/d"
printf 'x\n' >"$fx/d/f.txt"
printf '%s\n' "$fx/d" >"$FX/dir.list"
name="列表里写目录：无输出行、Errors 恒为 1、退出 1（重复 20 次）"
loss=0
for _ in $(seq 20); do
    run sha256 -l "$FX/dir.list"
    if [ "$rc" -ne 1 ] || [ "$(field 'Errors')" != 1 ] || [ "$(count_results)" != 0 ]; then
        loss=$((loss + 1))
    fi
done
if [ "$loss" -eq 0 ]; then
    xpass 06 "$name"
else
    xfail 06 "$name" "已知缺陷 A：20 次里 $loss 次丢失错误/退出码为 0。\
fanin 用「子流 pop() 返回空」判定子流已死，但此刻别的线程还在它里面写错误"
fi

# 承诺："不存在的路径……没有输出行，也不进任何一个计数，只加一条错误记录"。
name="不存在的路径：Errors 恒为 1、Failed 0、退出 1（重复 20 次）"
loss=0
for i in $(seq 20); do
    run sha256 "$FX/does-not-exist-$i"
    if [ "$rc" -ne 1 ] || [ "$(field 'Errors')" != 1 ] || [ "$(field 'Failed')" != 0 ]; then
        loss=$((loss + 1))
    fi
done
if [ "$loss" -eq 0 ]; then
    xpass 07 "$name"
else
    xfail 07 "$name" "已知缺陷 A：20 次里 $loss 次丢失错误/退出码为 0（与 06 同源）"
fi

# ---------------------------------------------------------------- 08
# 承诺："none 只列文件不计算，所有文件都记为未处理"。
section "08-09 算法与流"
fx=$FX/t8
mkdir -p "$fx"
printf 'a\n' >"$fx/1.txt"
printf 'b\n' >"$fx/2.txt"
printf 'c\n' >"$fx/3.txt"
name="none：全部 <NONE>，Succeeded 0、Unprocessed 3"
run none "$fx"
if [ $rc -eq 0 ] &&
   [ "$(printf '%s\n' "$out" | grep -c '^<NONE> ')" = 3 ] &&
   [ "$(field 'Succeeded')" = 0 ] &&
   [ "$(field 'Unprocessed')" = 3 ]; then
    ok 08 "$name"
else
    bad 08 "$name" "rc=$rc none行=$(printf '%s\n' "$out" | grep -c '^<NONE> ') unprocessed=$(field 'Unprocessed')"
fi

# 承诺（反向）：读失败的列表源必须被报告，而不是被当成"列表已读完"。
# 已知缺陷：现在 rc=0，报告 0 个文件。
name="读失败的列表源必须退出非 0（已知缺陷）"
fx=$FX/t9
mkdir -p "$fx/d"
printf 'x\n' >"$fx/d/f.txt"
run sha256 -l "$fx/d"
if [ $rc -ne 0 ]; then
    xpass 09 "$name"
else
    xfail 09 "$name" "已知缺陷 B：rc=0，报告 Total=$(field 'Total Processed')、Errors=$(field 'Errors')。\
listscanner 未检查 is.bad()，EOF 与 I/O 错误不可区分"
fi

# ---------------------------------------------------------------- 10-11
# 承诺："名字以 # 开头的文件要写成 ./名字 或绝对路径"、"带空格的路径不用加引号"。
section "10-11 路径与报告"
fx=$FX/t10
mkdir -p "$fx/w"
printf 'a\n' >"$fx/w/#hash.txt"
printf 'b\n' >"$fx/w/a b.txt"
printf 'c\n' >"$fx/w/a=b.txt"
name="含 #/空格/= 的路径都能处理"
run sha256 "$fx/w"
if [ $rc -eq 0 ] && [ "$(count_results)" = 3 ] &&
   grep -q '/#hash.txt$' <<<"$out" &&
   grep -q '/a b.txt$' <<<"$out" &&
   grep -q '/a=b.txt$' <<<"$out"; then
    ok 10 "$name"
else
    bad 10 "$name" "rc=$rc 行数=$(count_results)"
fi

fx=$FX/t11
mkdir -p "$fx/d"
printf 'x\n' >"$fx/d/f.txt"
name="报告块以 # 开头，Algorithm 字段正确"
run sha256 "$fx"
if [ $rc -eq 0 ] && [ "$(field 'Algorithm')" = "sha256" ] &&
   ! printf '%s\n' "$out" | grep -vE '^(#|[0-9a-f]{64} |<NONE> )' | grep -q .; then
    ok 11 "$name"
else
    bad 11 "$name" "存在不属于结果行/报告块的行"
fi

# ---------------------------------------------------------------- 汇总
printf '\n--------------------------------\n'
printf '%d passed, %d failed, %d known-issue\n' "$pass" "$fail" "$xfail"
if [ "$xfail" -gt 0 ]; then
    printf '已知缺陷（不影响退出码）：\n'
    printf '  - %s\n' "${xfail_names[@]}"
fi
if [ "$fail" -gt 0 ]; then
    printf '失败用例：\n'
    printf '  - %s\n' "${failed_names[@]}"
    exit 1
fi
exit 0
