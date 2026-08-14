# Busybox vs Toybox 命令差异汇总

统计自 busybox master 与 toybox master（含 toys/pending/）对比。

- BusyBox applet 总数：437 个
- Toybox 命令总数：~361 个
- **BusyBox 独有命令：171 个**（其中 `nslookup`、`unxz`、`xz`、`whois`、`pscan` 已移植至 toys/pending/）

---

## 一、归档 & 压缩（14 个）

| 命令 | 功能 |
|------|------|
| `bzip2` | bzip2 算法压缩/解压文件 |
| `lzma` / `unlzma` | LZMA 算法压缩/解压 |
| `lzcat` | 解压 lzma/xz 并输出到 stdout |
| `lzop` / `unlzop` / `lzopcat` | LZO 算法压缩/解压（高速低压缩率，适合嵌入式） |
| `xz` / `unxz` | XZ 算法压缩/解压 ✅ 已移植至 toys/pending/（仅解压，-d 模式） |
| `unzip` | 解压 ZIP 归档 |
| `uncompress` | 解压传统 Unix `.Z` 格式 |
| `ar` | 创建/解压静态库格式归档（`.a` 文件） |
| `rpm` / `rpm2cpio` | RPM 包管理和转换 |
| `dpkg` / `dpkg-deb` | Debian `.deb` 包管理 |

---

## 二、网络工具（28 个）

| 命令 | 功能 |
|------|------|
| `udhcpc` / `udhcpc6` | DHCP / DHCPv6 客户端（申请 IP） |
| `udhcpd` | 轻量 DHCP 服务器 |
| `dhcprelay` | DHCP 中继代理（跨网段转发 DHCP） |
| `dnsd` | 小型静态 DNS 服务守护进程 |
| `ntpd` | NTP 时间同步客户端/服务器 |
| `nslookup` | 查询 DNS 记录 ✅ 已移植至 toys/pending/ |
| `whois` | 查询域名/IP 注册信息 ✅ 已移植至 toys/pending/ |
| `ifup` / `ifdown` | 按 `/etc/network/interfaces` 启停网络接口 |
| `ifenslave` | 网络接口 bonding（链路聚合）配置 |
| `ifplugd` | 网线插拔事件检测守护进程 |
| `nameif` | 接口关闭状态下按 MAC 地址重命名接口 |
| `ipcalc` | 根据 IP 计算子网掩码/广播地址等网络信息 |
| `ip` / `ipaddr` / `iplink` / `iproute` / `iprule` / `ipneigh` | iproute2 风格的综合网络配置工具集 |
| `pscan` | 扫描主机开放端口 ✅ 已移植至 toys/pending/ |
| `ssl_client` | 简单 TLS 客户端 |
| `ether-wake` | 发送 Magic Packet 唤醒 WoL 设备 |
| `slattach` | 将串口配置为 SLIP 网络接口 |
| `tc` | 流量控制（qdisc/class/filter 管理） |
| `zcip` | ZeroConf 169.254.x.x 链路本地地址自动配置 |
| `ftpd` | 匿名 FTP 服务器 |
| `fakeidentd` | 伪造 ident 身份验证服务 |

---

## 三、邮件 & 通信（5 个）

| 命令 | 功能 |
|------|------|
| `sendmail` | 从 stdin 读取并发送邮件 |
| `popmaildir` | POP3 收取邮件到本地 maildir |
| `makemime` | 创建 MIME 多部分邮件 |
| `reformime` | 解析 MIME 编码消息 |
| `chat` | 与调制解调器交互的脚本工具 |

---

## 四、文件系统工具（13 个）

| 命令 | 功能 |
|------|------|
| `mke2fs` / `mkfs.ext2` | 创建 ext2/ext3/ext4 文件系统 |
| `mkdosfs` / `mkfs.vfat` | 创建 FAT/FAT32 文件系统 |
| `mkfs.minix` | 创建 MINIX 文件系统 |
| `mkfs.reiser` | 创建 ReiserFS 文件系统 |
| `fsck.minix` | 检查 MINIX 文件系统完整性 |
| `fstrim` | 向 SSD 发送 TRIM/DISCARD 请求 |
| `tune2fs` | 调整 ext2/ext3 文件系统参数 |
| `e2label` | 查看或设置 ext2/3/4 卷标 |
| `findfs` | 通过 label 或 UUID 查找设备 |
| `volname` | 显示 CD-ROM 卷标 |
| `fatattr` | 修改 FAT 文件属性 |

---

## 五、MTD / Flash 存储（13 个）

嵌入式系统专用，toybox 完全没有此领域工具。

| 命令 | 功能 |
|------|------|
| `flashcp` | 将文件复制写入 MTD 设备 |
| `flash_eraseall` | 擦除 MTD 设备全部内容 |
| `flash_lock` / `flash_unlock` | 锁定/解锁 MTD 扇区 |
| `nanddump` / `nandwrite` | 转储/写入 NAND Flash 设备 |
| `ubiattach` / `ubidetach` | MTD 设备附加/分离 UBI 子系统 |
| `ubimkvol` / `ubirmvol` | 创建/删除 UBI 卷 |
| `ubirsvol` | 调整 UBI 卷大小 |
| `ubirename` | 重命名 UBI 卷 |
| `ubiupdatevol` | 更新 UBI 卷内容 |

---

## 六、runit 服务管理（10 个）

完整的 daemontools/runit 工具集，toybox 没有。

| 命令 | 功能 |
|------|------|
| `runsv` | 启动并监控服务进程（支持日志服务） |
| `runsvdir` | 为每个子目录启动 runsv 实例 |
| `sv` / `svc` | 控制 runsv 监控的服务（sv 新接口，svc 兼容 daemontools） |
| `svlogd` | 从 stdin 读取日志并写入轮转日志文件 |
| `svok` | 检测 runsv 是否在运行 |
| `chpst` | 改变进程状态后运行程序（daemontools 工具） |
| `envdir` | 从目录文件设置环境变量后运行程序 |
| `envuidgid` | 设置 `$UID/$GID` 后运行程序 |
| `softlimit` | 设置软资源限制后运行程序 |

---

## 七、进程 & 系统管理（13 个）

| 命令 | 功能 |
|------|------|
| `iostat` | 报告 CPU 和 I/O 统计信息 |
| `mpstat` | 显示各 CPU 核心统计数据 |
| `pstree` | 树状显示进程关系 |
| `powertop` | 分析系统电源消耗 |
| `nmeter` | 实时监控各项系统指标 |
| `adjtimex` | 读取/设置内核时钟频率变量 |
| `runlevel` | 查询当前系统运行级别 |
| `start-stop-daemon` | 搜索匹配进程并启停服务（sysvinit 风格） |
| `setpriv` | 以不同权限运行程序 |
| `depmod` | 生成 modules.dep 依赖文件 |
| `raidautorun` | 通知内核自动扫描启动 RAID 阵列 |
| `logread` | 读取 syslogd 环形缓冲区日志 |
| `acpid` | ACPI 事件监听守护进程 |

---

## 八、用户 & 认证（8 个）

| 命令 | 功能 |
|------|------|
| `chpasswd` | 批量从 stdin 读取 `user:password` 并更新密码 |
| `cryptpw` | 生成 crypt(3) 加密密码哈希 |
| `add-shell` / `remove-shell` | 向 `/etc/shells` 添加/删除 shell |
| `users` | 列出当前已登录用户 |
| `vlock` | 锁定虚拟终端，需密码解锁 |
| `wall` | 向所有已登录用户广播消息 |
| `mesg` | 控制其他用户向你终端写入的权限 |

---

## 九、终端 & 控制台（20 个）

| 命令 | 功能 |
|------|------|
| `less` | 分页查看文件（比 more 功能更强） |
| `script` / `scriptreplay` | 录制/回放终端会话 |
| `fbset` / `fbsplash` | 帧缓冲设置 / 启动画面 |
| `loadfont` / `setfont` | 加载控制台字体 |
| `loadkmap` / `dumpkmap` | 加载/导出键盘映射表 |
| `setkeycodes` | 修改扫描码到键码映射 |
| `showkey` | 显示按键信息（调试用） |
| `resize` | 重新检测并设置终端尺寸 |
| `ttysize` | 打印 tty 的行列数 |
| `conspy` | 文本模式 VNC，监控虚拟控制台 |
| `kbd_mode` | 报告/设置 VT 控制台键盘模式 |
| `fgconsole` | 获取当前活动的虚拟控制台编号 |
| `setconsole` / `setlogcons` | 重定向控制台输出到指定 VT |
| `beep` | 让 PC 扬声器发出蜂鸣声 |
| `linux64` / `setarch` | 以不同 CPU 架构个性设置运行程序 |

---

## 十、SELinux（6 个）

toybox 完全没有 SELinux 支持。

| 命令 | 功能 |
|------|------|
| `getsebool` / `setsebool` | 读取/修改 SELinux 布尔值 |
| `selinuxenabled` | 检查 SELinux 是否启用 |
| `sestatus` | 显示 SELinux 状态 |
| `matchpathcon` | 显示路径的 SELinux 安全上下文 |
| `setfiles` | 按策略重置文件安全上下文 |

---

## 十一、打印（3 个）

| 命令 | 功能 |
|------|------|
| `lpd` | 行式打印机守护进程（LPD 打印服务器） |
| `lpq` | 查看打印队列中的任务 |
| `lpr` | 提交文件到打印队列 |

---

## 十二、硬件 & 存储设备（5 个）

| 命令 | 功能 |
|------|------|
| `hdparm` | 读取/设置硬盘参数（DMA/缓存/省电） |
| `mt` | 控制磁带驱动器操作 |
| `fdformat` / `fdflush` | 格式化软盘 / 检测软盘变化 |
| `rx` | xmodem 协议接收文件 |

---

## 十三、文本处理（3 个）

| 命令 | 功能 |
|------|------|
| `ed` | 经典行式文本编辑器（Unix 最原始编辑器） |
| `dc` | 任意精度 RPN 逆波兰式计算器 |
| `unexpand` | 将空格转换为制表符 |

---

## 分类汇总

| 领域 | busybox 独有数量 | 说明 |
|------|----------------|------|
| 网络工具 | 28 | DHCP 套件、邮件、流量控制等 |
| MTD/Flash 存储 | 13 | 嵌入式 NAND/UBI 专属，toybox 完全没有 |
| 文件系统 | 13 | 各种 mkfs/fsck 工具 |
| 终端控制台 | 20 | 字体、键盘映射、帧缓冲等 |
| runit 服务管理 | 10 | daemontools/runit 生态工具集 |
| 进程/系统管理 | 13 | iostat、pstree、start-stop-daemon 等 |
| 压缩格式 | 14 | 更多压缩格式支持（xz/unxz 已移植） |
| SELinux | 6 | toybox 完全没有 SELinux 支持 |
| 用户/认证 | 8 | chpasswd、vlock、wall 等 |
| 其他 | 9 | 打印、磁带、文本处理等 |
