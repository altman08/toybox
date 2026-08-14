# Toybox 新增命令开发规范

---

## 一、文件结构

在 `toys/pending/` 下新建 `<cmd>.c`，结构固定如下：

```c
/* cmd.c - 一行功能描述
 *
 * Copyright YYYY 作者
 *
 * No standard.

USE_CMD(NEWTOY(cmd, "optstring", TOYFLAG_USR|TOYFLAG_BIN))

config CMD
  bool "cmd"
  default n
  help
    usage: cmd [-options] ARGS

    功能描述。

    -x  选项说明
*/

#define FOR_cmd
#include "toys.h"

GLOBALS(
  // 与 optstring 带参数选项对应的字段
)

void cmd_main(void)
{
}
```

---

## 二、optstring 格式

### 位置参数约束

| 写法 | 含义 |
|------|------|
| `<N` | 至少 N 个位置参数 |
| `>N` | 至多 N 个位置参数 |

### 选项类型

| 后缀 | GLOBALS 字段类型 | 示例 |
|------|----------------|------|
| 无 | —（布尔） | `v` → `FLAG(v)` |
| `:` | `char *` | `h:` → `TT.h` |
| `#` | `long` | `p#` → `TT.p` |
| `#<N>M=D` | `long` | `p#<1>65535=80`（下限/上限/默认） |
| `*` | `struct arg_list *` | `e*` → `TT.e` 链表 |
| `@` | `long`（计数） | `v@` → `TT.v` 出现次数 |

### GLOBALS 字段顺序

GLOBALS 字段顺序必须与 optstring 中带参数选项**从右到左**的顺序一致：

```
optstring: "<1h:p#<1>65535=43i"
                 ↑  ↑
            this[1] this[0]   <- 最右的 p# 对应 this[0]

GLOBALS(
  long p;    // this[0]
  char *h;   // this[1]
)
```

### 安装路径 flag

| Flag 组合 | 安装路径 |
|-----------|---------|
| `TOYFLAG_USR\|TOYFLAG_BIN` | `usr/bin/` |
| `TOYFLAG_BIN` | `bin/` |
| `TOYFLAG_USR\|TOYFLAG_SBIN` | `usr/sbin/` |
| `TOYFLAG_SBIN` | `sbin/` |

---

## 三、常用内置函数

### 输出

| 函数 | 说明 |
|------|------|
| `xprintf(fmt, ...)` | printf，失败退出 |
| `xputc(c)` | putchar，失败退出 |
| `xputs(s)` | puts（带换行） |

### 错误处理

| 函数 | 说明 |
|------|------|
| `error_msg(fmt, ...)` | 打印错误，继续运行 |
| `error_exit(fmt, ...)` | 打印错误并退出 |
| `perror_msg(fmt, ...)` | 打印错误 + errno，继续运行 |
| `perror_exit(fmt, ...)` | 打印错误 + errno 并退出 |

### 内存

| 函数 | 说明 |
|------|------|
| `xmalloc(size)` | malloc，失败退出 |
| `xzalloc(size)` | malloc + 清零，失败退出 |
| `xrealloc(ptr, size)` | realloc，失败退出 |
| `xstrdup(s)` | strdup，失败退出 |

### 文件 I/O

| 函数 | 说明 |
|------|------|
| `xopen(path, flags)` | open，失败退出 |
| `xfopen(path, mode)` | fopen，失败退出 |
| `xread(fd, buf, len)` | read，返回实际字节数 |
| `xreadall(fd, buf, len)` | read，必须读满否则退出 |
| `xwrite(fd, buf, len)` | write，必须写完否则退出 |

### 数值解析

| 函数 | 说明 |
|------|------|
| `atolx(s)` | atol，支持 k/m/g 等后缀 |
| `atolx_range(s, lo, hi)` | atol + 范围检查，超范围退出 |

### 网络

| 函数 | 说明 |
|------|------|
| `xgetaddrinfo(host, port, ...)` | getaddrinfo，失败退出 |
| `xconnectany(ai)` | 连接 addrinfo 列表中第一个可用地址 |
| `xsocket(domain, type, proto)` | socket，失败退出 |

### 时间

| 函数 | 说明 |
|------|------|
| `millitime()` | 返回当前毫秒时间戳（`long long`） |

### 其他

| 函数 / 变量 | 说明 |
|------------|------|
| `toys.optargs[]` | 位置参数数组 |
| `toys.optc` | 位置参数个数 |
| `FLAG(x)` | 检测选项 `-x` 是否被设置 |
| `TT.field` | 访问 GLOBALS 中的字段 |
| `toybuf[4096]` | 全局临时缓冲区，避免 malloc |
| `xfork()` | fork，失败退出 |
| `xsignal(sig, fn)` | signal，失败退出 |

---

## 四、编译单个命令

```bash
# GCC 版本低于 5 时需要先激活 devtoolset-8
gcc_ver=$(gcc -dumpversion | cut -d. -f1)
[ "$gcc_ver" -lt 5 ] && source /opt/rh/devtoolset-8/enable

make <cmd>           # 编译为项目根目录下的独立二进制
./<cmd> --help       # 验证 help 输出
```

---

## 五、开启 kconfig（必须按 oldconfig 顺序）

直接手写 `CONFIG_XXX=y` 到 kconfig 文件可能破坏顺序，须用 `oldconfig`：

```bash
for cfg in kconfig/asus.config kconfig/cat.config; do
  cp $cfg .config
  echo "CONFIG_CMD=y" >> .config
  yes "" | make oldconfig > /dev/null 2>&1
  # oldconfig 对 default n 的新选项会置为 not set，手动改为 =y：
  sed -i 's/# CONFIG_CMD is not set/CONFIG_CMD=y/' .config
  cp .config $cfg
done
```

---

## 六、如果是从 busybox 移植

还需额外更新 `docs/busybox-diff.md`：

1. 在对应命令行末尾加 `✅ 已移植至 toys/pending/`
2. 更新文件顶部的已移植命令列表

---

## 七、提交规范

### 标题格式

```
<scope>: <动作> <cmd> command [ported from busybox]
```

- **scope**：通常为 `toys/pending`，若同时改动多处可省略
- **动作**：`add`（新增）、`fix`（修复）、`extend`（扩展）
- `ported from busybox` 仅移植命令加

示例：

```
toys/pending: add mhz command to measure CPU clock frequency
toys/pending: add whois command ported from busybox
toys/pending: extend xzcat.c to add unxz and xz commands ported from busybox
```

### 正文格式

正文与标题之间空一行，依次包含：

1. **功能描述**：一到两句话说明命令的作用
2. **Options 列表**（选项较多时列出）：
```
Options:
  -c  Show closed ports too
  -p  First port to scan (default 1)
```
3. **Key changes from the original**（仅移植命令，说明与原版的关键差异）：
```
Key changes from the original:
  - Replaced getopt32/libbb with toybox optstring and FLAG()/TT macros
  - Replaced bb_error_msg_and_die with error_exit
```
4. **Also 行**（同一 commit 涉及多个文件时）：
```
Also: enable CONFIG_CMD in kconfig/asus.config and kconfig/cat.config,
update docs/busybox-diff.md.
```

### 完整示例

```
toys/pending: add pscan command ported from busybox

Ports the pscan simple port scanner to toybox coding conventions.

Uses non-blocking connect() to probe TCP ports in sequence, estimating
RTT adaptively to avoid waiting the full timeout for every blocked port.

Options:
  -c  Show closed ports too
  -p  First port to scan (default 1)
  -P  Last port to scan (default 1024)
  -t  Timeout in milliseconds (default 5000)

Key changes from the original:
  - Replaced getopt32/libbb with toybox optstring and FLAG()/TT macros
  - Replaced xhost2sockaddr/len_and_sockaddr with xgetaddrinfo
  - Replaced ndelay_on with fcntl(F_GETFL/F_SETFL, O_NONBLOCK)
  - Replaced monotonic_us with millitime()*1000

Also: enable CONFIG_PSCAN in kconfig/asus.config and kconfig/cat.config,
update docs/busybox-diff.md.
```

### 提交时需要 add 的文件

| 文件 | 新增命令 | 移植命令 |
|------|---------|---------|
| `toys/pending/<cmd>.c` | ✅ | ✅ |
| `kconfig/asus.config` | ✅ | ✅ |
| `kconfig/cat.config` | ✅ | ✅ |
| `docs/busybox-diff.md` | — | ✅ |
| `docs/new-command-guide.md` | 规范有变动时 | 规范有变动时 |

### 注意事项

- commit message 全部用**英文**
- 标题不超过 72 个字符
- 正文每行不超过 72 个字符
- 不要在标题末尾加句号
- 用 `git commit -F <file>` 提交多行 message，避免 shell 换行符问题
