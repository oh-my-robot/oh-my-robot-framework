# 崩溃时刻日志落盘 / 黑匣子式日志存储：成熟系统调研

> 调研范围：Linux（pstore / ramoops / systemd-journald）、Android（pmsg / logd / tombstone）、车规与工业（AUTOSAR DLT、EDR）、飞行与竞速控制器（ArduPilot DataFlash、Betaflight Blackbox）。
> 每条事实后附一手来源 URL；无法证实的内容统一放在「存疑与未证实」一节。
> 调研日期：2026-09-15。

---

## 事实

### 1. Linux pstore / ramoops / efi-pstore

#### 1.1 pstore 的定位：通用前端 + 可插拔后端

- pstore 的核心定位是「通过 pstore 文件系统对平台级持久存储的**通用访问**」（generic access to platform level persistent storage via "pstore" filesystem）。Kconfig 明确说 pstore 核心只有在平台级驱动注册进来之后才可用，否则应选 N。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/Kconfig]
- 后端通过填充 `struct pstore_info` 并调用 `pstore_register()` 注册；`pstore_info` 含 `owner`/`name`/`buf`/`bufsize`/`flags` 以及 `open`/`close`/`read`/`write`/`write_buf`/`write_buf_user`/`erase` 回调。**同一时刻只能有一个后端注册**：`pstore_register()` 在已有后端时返回 `-EBUSY` 并打印 `"backend '%s' already loaded: ignoring '%s'"`；注销路径下有注释 `"Only one backend can be registered at a time."`。当前后端名可通过 `/sys/module/pstore/parameters/backend` 查看（`backend` 是 `charp` 模块参数，权限 0444，描述 "specific backend to use"）。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/platform.c]
- 「前端 / 记录类型」是独立于后端的一维：`PSTORE_TYPE_DMESG`（oops/panic）、MCE、`PSTORE_TYPE_CONSOLE`、`PSTORE_TYPE_FTRACE`、用户态 pmsg。对应开关 `CONFIG_PSTORE`、`CONFIG_PSTORE_CONSOLE`、`CONFIG_PSTORE_FTRACE`、`CONFIG_PSTORE_PMSG`、`CONFIG_PSTORE_RAM`、`CONFIG_PSTORE_BLK`。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/Kconfig]
- 后端实例：**ramoops**（保留 RAM）、**pstore/blk**（块设备 / MTD 等非块设备）、**efi-pstore / ACPI ERST**、**chromeos_pstore**。内核 shutdown 调试文档把可选后端列为 `CONFIG_EFI_VARS_PSTORE`、`CONFIG_PSTORE_RAM`、`CONFIG_CHROMEOS_PSTORE`、`CONFIG_PSTORE_BLK`。
  [来源: https://docs.kernel.org/power/shutdown-debugging.html]
- 记录在 pstore 文件系统中的文件名由 `pstore_mkfile()` 用 `"%s-%s-%llu%s"` 拼接：类型名 + **后端名**（`record->psi->name`）+ 记录号 +（压缩时）`.enc.z`。因此 `dmesg-ramoops-0` 里的 `ramoops` 是后端名，换后端就换名字。文件权限 `S_IFREG | 0444`（只读）；删除文件（unlink）会调用后端的 `erase()` 把记录从持久介质里真正抹掉；后端没有 `erase` 回调时 unlink 返回 `-EPERM`。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/inode.c]
- 压缩：`CONFIG_PSTORE_COMPRESS` 默认 y，只用 **zlib deflate 库实现**（明确不再走 crypto API），理由是「降低记录 panic 元数据时发生二次 oops 之类问题的概率」。压缩只对 dmesg 记录生效。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/Kconfig]
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/platform.c]
- panic 路径的并发策略：`pstore_cannot_block_path()` 对 NMI / panic / emergency dump 返回 true，这些路径用 `raw_spin_trylock_irqsave()` 而不是阻塞锁，拿不到就打印 `"dump skipped in %s path because of concurrent dump"` 并**放弃本次转储**（宁丢不卡）。`update_ms` 默认为 -1（关闭运行时更新），注释警告开启它「可能不安全，并可能在 oops 时造成进一步损坏」。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/platform.c]
- 单次快照的日志量上限由 `kmsg_bytes` 控制（模块参数，描述 "amount of kernel log to snapshot (in bytes)"），初值来自 `CONFIG_PSTORE_DEFAULT_KMSG_BYTES`，**默认 10240 字节**；Kconfig 说「可以放大，不建议缩小」。dumper 的注释是 "Save as much as we can (up to kmsg_bytes) from the end of the buffer."（**从日志缓冲区尾部往前取**）。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/Kconfig]
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/platform.c]

#### 1.2 ramoops：保留内存 + ECC

- 定位：ramoops 是「在系统崩溃前把日志写进 RAM」的 oops/panic logger，用环形缓冲记录 oops 和 panic，**前提是有"持久 RAM"**（persistent RAM），使该区域内容能在重启后存活。
  [来源: https://docs.kernel.org/admin-guide/ramoops.html]
- **区域划分**：`mem_size` 向下取 2 的幂；区域按 `record_size`（同样向下取 2 的幂）切块，每次 kmsg dump 占一块。源码里 dmesg 区是"剩余部分"：`dump_mem_sz = cxt->size - cxt->console_size - cxt->ftrace_size - cxt->pmsg_size;`，物理布局顺序为 **dmesg → console → pmsg → ftrace**。dmesg 与 ftrace 是**多 zone**（按 record_size / nr_cpu_ids 切分），console 与 pmsg 是**单 zone**。
  [来源: https://docs.kernel.org/admin-guide/ramoops.html]
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/ram.c]
- **DT 绑定参数**（`Documentation/devicetree/bindings/reserved-memory/ramoops.yaml`，`compatible = "ramoops"`，必需 `compatible` + `reg`，且 `record-size`/`console-size`/`ftrace-size`/`pmsg-size` 至少一个非零）：

  | 属性 | 类型 | 默认 | 说明 |
  |---|---|---|---|
  | `record-size` | uint32 | 0 | 每次 kmsg dump 的最大字节数 |
  | `console-size` | uint32 | 0 | 内核消息日志缓冲大小 |
  | `ftrace-size` | uint32 | 0 | 函数追踪日志大小 |
  | `pmsg-size` | uint32 | 0 | 用户态消息日志大小 |
  | `ecc-size` | uint32 | 0（无 ECC） | 启用 ECC 并指定校验缓冲字节数 |
  | `mem-type` | uint32 | 0 | 0=write-combined，1=unbuffered，2=cached |
  | `max-reason` | uint32 | 2 | 保存的最高 kmsg dump 原因；0 表示交给 `printk.always_kmsg_dump` 决定 |
  | `flags` | uint32 | 0 | `RAMOOPS_FLAG_*` |
  | `no-dump-oops` | bool | 弃用 | 等价于 `max_reason = 1`（KMSG_DUMP_PANIC） |
  | `unbuffered` | bool | 弃用 | 等价于 `mem_type = 1` |

  [来源: https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/reserved-memory/ramoops.yaml]
- 模块参数默认值（源码）：`record_size` / `ramoops_console_size` / `ramoops_ftrace_size` / `ramoops_pmsg_size` 都初始化为 `MIN_MEM_SIZE`（`4096UL`）；`ramoops_max_reason = -1`（描述里写 "maximum reason for kmsg dump (default 2: Oops and Panic)"）；`mem_type` 默认 0；`ramoops_ecc` 默认 0。`ecc` 参数「1 是特殊值，表示 16 字节 ECC」（`ecc_size = ramoops_ecc == 1 ? 16 : ramoops_ecc`）。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/ram.c]
- **`max_reason` 的枚举取值**（`include/linux/kmsg_dump.h`，未显式赋值，按 C 顺序递增）：0 `KMSG_DUMP_UNDEF`、1 `KMSG_DUMP_PANIC`、2 `KMSG_DUMP_OOPS`、3 `KMSG_DUMP_EMERG`、4 `KMSG_DUMP_SHUTDOWN`、5 `KMSG_DUMP_MAX`。头文件注释：「KMSG_DUMP_OOPS 之后的项默认不会被记录，除非传入 `printk.always_kmsg_dump`」。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/include/linux/kmsg_dump.h]
- **满了怎么办 / 轮转**：没有"找最老"的扫描，dmesg 区是**轮转复用**——写入时 `prz = cxt->dprzs[cxt->dump_write_cnt];`，写完 `cxt->dump_write_cnt = (cxt->dump_write_cnt + 1) % cxt->max_dump_cnt;`，计数回绕即覆盖最老的一条。**只保存一次 dump 的第一部分**：`if (record->part != 1) return -ENOSPC;`（即超过 `record_size` 的日志**被截断丢弃，不会拆到下一块**）。写之前会先 zap 该 zone，注释解释否则新 dump「会被追加」而解析器期望头部在缓冲区起始处。`max_reason` 的过滤被有意交给 kmsg dumper 而不是在 ramoops 里做。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/ram.c]
- **计数器在重启时清零**：ramoops 文档原文——模块里有个计数器跟踪历次 dump，但「该计数器在机器重启时被清除，因此重启之后产生的 dump 会覆盖更早的那些」。
  [来源: https://docs.kernel.org/admin-guide/ramoops.html]
- **ECC**：软件 ECC 保护持久区域，文档说它对「硬件复位（例如看门狗触发）之后」特别有用——此时 RAM「可能有些损坏，但通常是可以恢复的」。实现是 **Reed-Solomon**（内核 `rslib`，`encode_rs8`/`decode_rs8`，`init_rs` 创建解码器），参数：块大小 128、`ecc_size` 默认 16、符号位宽 8、多项式 `0x11d`。布局：`buffer_size` 先减去 `ecc_total`，`par_buffer` 指向 `buffer->data + buffer_size`，`par_header` 紧随其后——即 **header → data → 每块校验 → 头部校验**。解码返回值 >0 累加到 `prz->corrected_bytes`，<0 表示不可纠错并累加 `prz->bad_blocks`；启动时解头部，会打印 `"error in header"` 或限速的 `"uncorrectable error in header"`。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/ram_core.c]
  [来源: https://docs.kernel.org/admin-guide/ramoops.html]
- **zone 的有效性判定（magic）**：`struct persistent_ram_buffer` 含 `sig`、原子 `start`、原子 `size` 和柔性数组 `data[]`；签名常量 `PERSISTENT_RAM_SIG = 0x43474244`（ASCII `"DBGC"`）。`persistent_ram_post_init()` 用 `PERSISTENT_RAM_SIG ^ 期望值` 与 `buffer->sig` 比较。分支语义：
  - sig 匹配且 size/start 为 0 → `"found existing empty buffer"`，不 zap；
  - sig 匹配但 `size > buffer_size` 或 `start > size` → `"found existing invalid buffer"`，zap；
  - sig 匹配且字段有效非零 → 通过 `persistent_ram_save_old()` **保留旧数据**；
  - sig 不匹配 → 写入当前 sig 并 zap（`start=0, size=0`，刷新头部 ECC）。
  **头部没有时间戳字段**，"是不是新的一次启动"完全由 magic 匹配与 size/start 的合法性决定。`PRZ_FLAG_ZAP_OLD` 可强制 zap。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/ram_core.c]
- **dump 格式**：每条 dump 头部是 `====` 加时间戳和换行，随后是原始数据。
  [来源: https://docs.kernel.org/admin-guide/ramoops.html]
- **读取 / 消费**：dump 出现在 pstore 文件系统里，命名 `dmesg-ramoops-N`（N 为内存中的记录号）；「要删除 RAM 中已存的记录，直接 unlink 对应 pstore 文件即可」。
  [来源: https://docs.kernel.org/admin-guide/ramoops.html]
- **console 前端的行为**：`pstore_console` 注册为一个真正的 console（`.write = pstore_console_write`），flags 为 `CON_PRINTBUFFER | CON_ENABLED | CON_ANYTIME`（每次注册都重新赋值，注释特别点名 `CON_ENABLED`）。`pstore_console_write` **不做任何 loglevel 过滤**（结构体未设 `.level`，回调里也没有检查），直接把 console core 交给它的内容组成 `PSTORE_TYPE_CONSOLE` 记录交给后端（`record.buf = (char *)s;`，零拷贝）。只有后端 flags 含 `PSTORE_FLAGS_CONSOLE` 时才注册/注销 console。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/platform.c]
  - 注意：`CONFIG_PSTORE_CONSOLE` 的 Kconfig 帮助文字说的是「打开后 pstore 会记录**所有**内核消息，即使没有发生 oops 或 panic」。
    [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/Kconfig]
  - 但社区文档（二手）称 `console-ramoops` 「受 printk level 控制，可能不含全部内容」——与源码不符，见「存疑」一节。
- **参数设置的四种途径**：A) 模块参数（如 `mem=128M ramoops.mem_address=0x8000000 ramoops.ecc=1`）；B) DT 绑定；C) platform device + `struct ramoops_platform_data`（字段 `mem_size`/`mem_address`/`mem_type`/`record_size`/`max_reason`/`ecc`）；D) `reserve_mem` 命令行保留内存（如 `reserve_mem=2M:4096:oops ramoops.mem_name=oops`）。文档对 D 明确警告：`reserve_mem`「可能并不总是在同一位置分配内存，不能依赖它」，要求实测，「请把它当作 best-effort 方案」。
  [来源: https://docs.kernel.org/admin-guide/ramoops.html]
- `mem_type` 的取舍：默认 `0` 让 pstore 用 `pgprot_writecombine`；`1` 尝试 `pgprot_noncached`「只在某些平台可用」——因为 pstore 依赖原子操作，在 ARM 上强序映射会让这些原子操作的行为变成 implementation-defined，在很多 ARM 部件（如 omap）上会失败；`2` 当作普通内存并开缓存，可提升性能。
  [来源: https://docs.kernel.org/admin-guide/ramoops.html]

#### 1.3 pstore/blk 与 mtdpstore：写块设备 / 裸 flash

- pstore/blk 是「在系统崩溃前把日志写到块设备和非块设备」的 oops/panic logger；非块设备通过 `register_pstore_device()` + `struct pstore_device_info` 注册。oops/panic 数据按 chunk 逐块填充，**没有空闲 chunk 后覆盖最老的**。
  [来源: https://docs.kernel.org/admin-guide/pstore-blk.html]
- 容量配置：`kmsg_size`/`pmsg_size`/`console_size`/`ftrace_size` 单位为 KB，**必须是 4 的倍数**；`kmsg_size = 0` 表示禁用 oops/panic 区；`total_size` 必须大于 4096 且为同倍数。Kconfig 说 pstore/blk 的这几个尺寸**默认各 64 KB**。pmsg 与 console 各占 1 个 chunk，ftrace 每 CPU 一个 chunk（每个 `ftrace_size / 处理器数`）。`kmsg_size` 为 0 时禁用。dump 文件名形如 `dmesg-pstore-blk-[N]`。
  [来源: https://docs.kernel.org/admin-guide/pstore-blk.html]
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/Kconfig]
- 明确**不建议压缩**：「we do not recommend data compression」，因为头部信息被插在 oops/panic 数据的第一行。
  [来源: https://docs.kernel.org/admin-guide/pstore-blk.html]
- **panic 上下文对存储驱动的硬性约束**（这是全篇对我方最直接相关的一段）：panic 时任务调度停止、资源停摆，「看起来像单核计算机上的单线程程序」，因此后端钩子必须遵守：
  1. **不能分配任何内存**——在驱动 init 时就把内存分配好；
  2. **必须轮询，不能中断驱动**；延时不得睡眠；
  3. **不能拿任何锁**——「你被允许打破所有锁」；
  4. 用 CPU 拷贝，避免 DMA，「除非你确定 DMA 不会一直持锁」；
  5. 直接控制寄存器，在 init 时完成 I/O 映射；
  6. 状态不明时「重置你的块设备和控制器」。
  [来源: https://docs.kernel.org/admin-guide/pstore-blk.html]
- **mtdpstore（裸 flash 后端）的设计**：
  - panic 时**不能擦除**：`mtdpstore_security()` 处注释 `"As there is no erase for panic case, we should ensure at least one zone is writable."`——正常写入路径负责保证至少有一个空闲（已擦）zone，全满时才退化为擦掉整个 block（跳过坏块，全坏则报 `"all blocks bad!"`）。
  - panic 时**不能探测坏块**：`mtd_block_isbad()` 在 panic 中不可调用，所以**事先维护 `badmap` 位图**（正常路径调用 `mtd_block_isbad()` 后 `set_bit()`），panic 路径的 `mtdpstore_panic_block_isbad()` 只 `test_bit()`。
  - `mtdpstore_panic_write()` 用 `mtd_panic_write()`（MTD 层的轮询/panic 写路径）而非普通 `mtd_write()`；遇到坏块或已用 zone 返回 **`-ENOMSG`**，语义是"请试下一个 zone"，pstore/blk 据此前进。
  - **注册期强制的容量约束**：MTD 分区大小必须 ≥ 2 × record size（否则 `"MTD partition %d not big enough"`）；**eraseblock 不得小于 `kmsg_size`**（`"eraseblock size of MTD partition %d too small"`，因为设计假设一个 zone 放得进一个块）；`kmsg_size % mtd->writesize == 0`（`"record size %lu KB must align to write size %d KB"`）。
  - **懒擦除**：`mtdpstore_erase` 的注释 `"Avoiding over erasing, do erase block only when the whole block is unused."`——只有块内所有 zone 都不再使用时才立即擦；否则记入 `rmmap`，等 `flush_removed()`（注销时）把仍有效的 log 读出来、擦块、再写回。
  [来源: https://raw.githubusercontent.com/torvalds/linux/refs/heads/master/drivers/mtd/mtdpstore.c]
- **块设备 panic 写的现状（2026 年的一个 patch）**：pstore/blk 的 best-effort 路径**原本不支持 panic dmesg 记录**，因为通用块写路径「会睡眠」；没有 `panic_write` 回调时「panic 记录只在内存里被标记为 dirty」，重启会跑在正常 flush 之前。该 patch 提出用**静态预留的 bio + 固定 bvec 数组**（panic 路径零分配）、以 `REQ_POLLED | REQ_NOWAIT | REQ_FUA` 提交、再用 `bio_poll(BLK_POLL_ONESHOT)` 轮询完成（30,000 次 × 100 µs 上限，超时 `-ETIMEDOUT`）；**不能用 `REQ_PREFLUSH`**，因为 flush 请求不提供可轮询的 poll cookie。门控条件：队列要有 `BLK_FEAT_POLL` 与 `HCTX_TYPE_POLL`、支持 nowait、开写缓存时要有 FUA，否则 `panic_write` 直接不注册。作者明确说这是「**不是**驱动原生 panic 钩子的替代品」。
  [来源: https://patchew.org/linux/20260819105416.24436-1-knirmal@nvidia.com/]
- MTD 在 panic 中无法擦除/探测坏块这一点，在 MMC 的 pstore 支持讨论中也被 Ulf Hansson 指出很脆弱：`mmc_claim_host()` 用到锁与 runtime PM（时钟、稳压器、re-tuning、卡重新初始化），在 panic 中（中断已关，未完成的请求永远完不成）可能挂死；他建议驱动只「注册/声明」自己具备 panic 写能力，而不是直接挂 pstore 钩子。
  [来源: https://lkml.org/lkml/2021/1/20/613]

#### 1.4 重启后由谁读走：systemd-pstore

- 内核文档给出的三步流程：启动后 pstore 日志位于 `/sys/fs/pstore`，可由用户态读取；在 systemd 系统上由 `systemd-pstore` 服务处理：**① 在 `/sys/fs/pstore` 定位 pstore 数据；② 读取并保存到 `/var/lib/systemd/pstore`；③ 清除 pstore 数据以备下次事件使用**。
  [来源: https://docs.kernel.org/power/shutdown-debugging.html]
- `systemd-pstore.service` 的自述：「把 Linux 持久存储文件系统 pstore 的内容归档到其他存储」，从而既保留已有信息、又清空 pstore 供下一次故障使用。它**独立于 kdump 服务**；手册页明确对比：云环境下 kdump 依赖可用的网络栈/硬件/基础设施，可能拿不到 core dump，而「pstore 后端则是完全本地的」，能跨重启存活以辅助事后分析。
  [来源: https://man.archlinux.org/man/systemd-pstore.8.en]
- **为什么必须有人来搬**：pstore 各后端映射到的持久存储「通常只有很小一块——大约 64KiB——很快就会填满，从而使后续的内核崩溃无法记录错误」。所以要监控并提取。
  [来源: https://man.archlinux.org/man/systemd-pstore.8.en]
- `pstore.conf(5)` 的选项（均在 `[PStore]` 段，systemd 243 引入）：

  | 选项 | 取值 | 默认 | 语义 |
  |---|---|---|---|
  | `Storage=` | `none` / `external` / `journal` | `external` | `none`：直接退出不处理；`external`：归档到 `/var/lib/systemd/pstore/` 并同时写 journal；`journal`：只写 journal，不落盘副本 |
  | `Unlink=` | 布尔 | `true` | `true` 时归档后删除 pstore 文件；`false` 时正常处理但文件留在 pstore。默认值的目的就是让 pstore「保持近乎空的状态，以便下次内核错误事件有空间可用」 |
  （`ProcessFull=` 在该版本手册页中**不存在**——见「存疑」）
  [来源: https://man.archlinux.org/man/pstore.conf.5.en]
- 两个内核参数的写盘时机由 systemd 通过 tmpfiles.d（`/usr/lib/tmpfiles/systemd-pstore.conf`）管理：`/sys/module/kernel/parameters/crash_kexec_post_notifiers`（panic/crash 时把 dmesg 含栈回溯写入 pstore）与 `/sys/module/printk/parameters/always_kmsg_dump`（**正常关机、重启、halt 时**也写）。`crash_kexec_post_notifiers` 在 `power/shutdown-debugging` 文档正文里并未出现，只在 systemd 手册页里出现。
  [来源: https://man.archlinux.org/man/systemd-pstore.8.en]

#### 1.5 ramoops / pstore 的已知局限

- **掉电即失**：ramoops 依赖「真正能在重启后存活的 RAM」。OpenWrt 打包 ramoops 的提交原文：**「The files in RAM survive a warm reboot, but not a cold reboot.」**（RAM 里的文件能挺过热重启，但挺不过冷启动。）该提交同时提醒：如果 DTS 里没有 ramoops 定义，「设备不会存储任何崩溃日志」。
  [来源: https://github.com/openwrt/openwrt/commit/97158fe10e6090a8b21629df130734bac53f87ee]
- **冷启动后可能出现垃圾数据**：`pstore/ram: verify ramoops header before saving record` 补丁（Ben Zhang `<benzh@chromium.org>`，2015-05-21，commit `e036bd330d21e929e94ed4d5432f9d279b19ce47`）说明：「在某些设备上，冷启动后持久内存里是垃圾数据，`/dev/pstore/dmesg-ramoops-*` 会以随机数据被创建出来，而这些并不是内核崩溃的结果。」修复方式是在 `ramoops_pstore_read()` 里循环查找下一个**头部合法**的 `persistent_ram_zone`（用 `ramoops_read_kmsg_hdr()` 校验），`header_length == 0` 时用 `persistent_ram_free_old()` + `persistent_ram_zap()` 清掉并跳过。同一补丁还把 `time->tv_sec/tv_nsec` 与 `*compressed` 初始化为 0（因为 `PSTORE_TYPE_CONSOLE` 与 `PSTORE_TYPE_FTRACE` 记录没有有效时间戳）。
  [来源: https://github.com/torvalds/linux/commit/e036bd330d21e929e94ed4d5432f9d279b19ce47]
- **持久性要求固件知情**：Qualcomm pstore minidump 补丁讨论指出「pstore ram 区域应当是固定的，且启动固件必须知道这个区域，才能让它跨启动持久」；并提到「很多 QCOM SoC 不支持硬件热启动，但有软件 minidump 支持」——因为有些 SoC 上 DRAM ECC scrub 会破坏 carveout，ramoops 不可用。
  [来源: https://lkml.iu.edu/hypermail/linux/kernel/2305.1/02022.html]
  [来源: https://patchew.org/linux/20260819105416.24436-1-knirmal@nvidia.com/]
- **单条日志被截断**：`record_size` 决定单条 dump 上限，**超出部分被丢弃而不是续写到下一块**（`if (record->part != 1) return -ENOSPC;`）。LKML 上 Guilherme G. Piccoli 与 Tony Luck 的讨论（2021-12 ~ 2022-01）指出：ramoops 只保存 dmesg 的前 `record_size` 字节；虽然 ramoops 理论上可以设很大的 `record_size`，但内核不愿意给出 >2MB 的分配——实测 `log_buf_len=4M` 配 4M `record_size` 直接失败（page_alloc 刷屏、ramoops 停止工作），2M 可用但会丢掉一半 dmesg。Tony Luck 的观点是 pstore 的目的本来就是抓「崩溃时丢失的最后那段 console 日志」，正常日志另有 `/var/log/messages`，所以加多 chunk 支持「价值不大」；提出者担心的则是没有 kdump、只靠 pstore 的场景下可能丢掉的恰是 panic 原因。**该讨论没有产出补丁或结论。**
  [来源: https://www.spinics.net/lists/linux-fsdevel/msg209893.html]
  [来源: https://lkml.org/lkml/2022/1/4/739]
- **单后端限制**：同一时刻只能有一个 pstore 后端（见 1.1），因此不能"ramoops + blk 同时用"。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/platform.c]
- **后端名会影响文件名**：`dmesg-ramoops-N` 中的 `ramoops` 来自后端名，脚本里硬编码该名字在后端更换后会失效（换 pstore/blk 就变成 `dmesg-pstore-blk-N`）。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/inode.c]
  [来源: https://docs.kernel.org/admin-guide/pstore-blk.html]
- **panic 时并发转储会被直接放弃**：`pstore_cannot_block_path()` 为真时用 trylock，失败即 `"dump skipped ... because of concurrent dump"`。
  [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/platform.c]

#### 1.6 Android 侧如何接走 pstore：`recovery-persist`

- AOSP `bootable/recovery/recovery-persist.cpp` 是明确读走 pstore 的组件之一，常量路径原文：
  - `LAST_LOG_FILE    = "/data/misc/recovery/last_log"`
  - `LAST_PMSG_FILE  = "/sys/fs/pstore/pmsg-ramoops-0"`
  - `LAST_KMSG_FILE  = "/data/misc/recovery/last_kmsg"`
  - `LAST_CONSOLE_FILE     = "/sys/fs/pstore/console-ramoops-0"`
  - `ALT_LAST_CONSOLE_FILE = "/sys/fs/pstore/console-ramoops"`
- 它的头注释说明其职责：「**严格用于处理 OTA 之后重启回系统、/data 挂载之后**」，把最后的 pmsg 文件数据取出放到 `/data/misc/recovery/` 目录并轮转。`/sys/fs/pstore/pmsg-ramoops-0` 通过 `__android_log_pmsg_file_read()` 读取，logger 格式中 INFO 及以上的条目交给 `logsave` 回调写入 `/data/misc/recovery/last_log`，写之前**先与已有内容比较，相同则直接返回**（幂等），否则先 `rotate_logs(LAST_LOG_FILE, LAST_KMSG_FILE)`。`console-ramoops-0`（或回退的 `console-ramoops`）在确认与 `/data/misc/recovery/last_kmsg` 不同后，由 `copy_file()` 拷成 `last_kmsg`。
  [来源: https://android.googlesource.com/platform/bootable/recovery/+/refs/heads/main/recovery-persist.cpp]
- 历史脉络（二手，仅作背景）：Android 早期用 `/proc/last_kmsg`（`CONFIG_ANDROID_RAM_CONSOLE` / `CONFIG_ANDROID_PERSISTENT_RAM`）；这些 Android 专有实现被移除后由 Linux **PSTORE** 取代——`PSTORE_RAM` 记 panic/oops 到 `dmesg-ramoops`，`PSTORE_CONSOLE` 记所有内核 console 消息到 `console-ramoops` / `console-ramoops-N`。Android `init` 默认把 pstore 挂到 `/sys/fs/pstore`。
  [来源: https://android.stackexchange.com/posts/213460/revisions]

---

### 2. Android：pmsg / logd / tombstone

#### 2.1 pmsg（/dev/pmsg0）——把内核现场带过重启的用户态通道

- pmsg 是 **pstore 的一个 frontend（前端），不是后端**。源文件是 `fs/pstore/pmsg.c`（**不是** `drivers/soc/qcom/pmsg.c`——该文件在内核中不存在）。
  [来源: https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/fs/pstore/pmsg.c]
  [来源: https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/drivers/soc/qcom/]
- Kconfig 原文：`config PSTORE_PMSG / bool "Log user space messages" / depends on PSTORE`，帮助文本「When the option is enabled, pstore will export a character interface **/dev/pmsg0** to log user space messages. **On reboot data can be retrieved from /sys/fs/pstore/pmsg-ramoops-[ID]**.」
  [来源: https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/fs/pstore/Kconfig]
- 前端注册受后端 flag 门控：`if (psi->flags & PSTORE_FLAGS_PMSG) pstore_register_pmsg();`；ramoops 后端在 `cxt->pmsg_size` 非 0 时置位该 flag（`if (cxt->pmsg_size) cxt->pstore.flags |= PSTORE_FLAGS_PMSG;`）。`pmsg_size` 默认 `MIN_MEM_SIZE`（4096 字节）。
  [来源: https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/fs/pstore/platform.c]
  [来源: https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/fs/pstore/ram.c]
- 写路径：`write_pmsg()` 把用户写入包装成 `record.type = PSTORE_TYPE_PMSG` 交给后端 `psinfo->write_user()`；设备节点权限由 `pmsg_devnode()` 设为 **0220**。
  [来源: https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/fs/pstore/pmsg.c]
- **liblog 的普通写路径本来就会写到 pmsg**：`logger_write.cpp` 中 `int ret = LogdWrite(...); PmsgWrite(...);`。
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/liblog/logger_write.cpp]
- **但非 debuggable 构建几乎全被拦掉**：`PmsgWrite()` 里 `if (!ANDROID_DEBUGGABLE) { if (logId != LOG_ID_EVENTS && logId != LOG_ID_SECURITY) return -1; ... }`，且 EVENTS 还必须等于 `SNET_EVENT_LOG_TAG`。该宏由编译选项定义：默认 `-DANDROID_DEBUGGABLE=0`，只有 debuggable variant 才是 1。
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/liblog/pmsg_writer.cpp]
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/liblog/Android.bp]
- liblog 另有私有 API `__android_log_pmsg_file_write()` / `__android_log_pmsg_file_read()`，把「文件」按序切块写入 pmsg 再读出，供 recovery 等使用。
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/liblog/pmsg_writer.cpp]
- **init 只负责挂载与授权，不解析内容**：`init.rc` 里 `mount pstore pstore /sys/fs/pstore nodev noexec nosuid`、`chown system log /sys/fs/pstore`、`chmod 0550`，并单独为 `console-ramoops`、`console-ramoops-0`、`pmsg-ramoops-0` 设 `chown system log` / `chmod 0440`。
  [来源: https://android.googlesource.com/platform/system/core/+/refs/heads/main/rootdir/init.rc]
- **读走者不是 logd**（`platform/system/logging/logd/` 下没有任何 pmsg/pstore 相关源文件）。实际读取方有三类：
  - **`logcat -L/--last`**：帮助文本 `-L, --last    Dump logs from prior to last reboot from pstore.`；`-c -L` 会 `unlink("/sys/fs/pstore/pmsg-ramoops-0")`。读取链路 `ANDROID_LOG_PSTORE` → `logger_read.cpp` → `PmsgRead()` → `open("/sys/fs/pstore/pmsg-ramoops-0", O_RDONLY|O_CLOEXEC)`。
    [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/logcat/logcat.cpp]
    [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/liblog/pmsg_reader.cpp]
    [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/liblog/logger_read.cpp]
  - **dumpstate（bugreport）**：`RunCommand("LAST LOGCAT", {"logcat", "-L", "-b", "all", ...})`，源码注释写明 `/* kernels must set CONFIG_PSTORE_PMSG, slice up pstore with device tree */`。
    [来源: https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/cmds/dumpstate/dumpstate.cpp]
  - **recovery**：`recovery-persist` 转存到 `/data/misc/recovery/last_log` / `last_kmsg`；`recovery-refresh` 则把上次 pmsg 内容**重新写回** `/dev/pmsg0`，以防意外重启导致内容过期。
    [来源: https://android.googlesource.com/platform/bootable/recovery/+/refs/heads/main/recovery-persist.cpp]
    [来源: https://android.googlesource.com/platform/bootable/recovery/+/refs/heads/main/recovery-refresh.cpp]

#### 2.2 logd：它自己不落盘

- 官方文档现状：`https://source.android.com/docs/core/architecture/logging` 与 `.../logd` 实测 **HTTP 404**（不存在）。现存相关页面是 `Understanding logging`（只讲 RFC 5424 与各级别对应，不讲 logd 架构）。logd 行为的权威描述在源码内文档 `logd/README.property`。
  [来源: https://source.android.com/docs/core/tests/debug/understanding-logging]
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/logd/README.property]
- **logd 只维护内存 ring buffer，默认不写盘**；落盘由独立的 `logcatd`（logpersist）完成——它本身是一个读 logd 的 logcat 守护进程，写入 **`/data/misc/logd/`**。服务定义原文：
  `service logcatd /system/bin/logcatd -L -b ${logd.logpersistd.buffer:-all} -v threadtime -v usec -v printable -v uid -D -f /data/misc/logd/logcat -r ${logd.logpersistd.rotate_kbytes:-2048} -n ${logd.logpersistd.size:-256} --id=${ro.build.id}`
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/logcat/logcatd.rc]
- **文件名规律**：当前文件 `/data/misc/logd/logcat`，轮转文件 `logcat.NNN`（`StringPrintf("%s.%.*d", output_file_name_, max_rotation_count_digits, i)`），另有 `--id` 生成的 `logcat.id`。轮转条件是 `out_byte_count_ / 1024 >= log_rotate_size_kb_`。（网上流传的 `log.<time>` 命名**不成立**。）
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/logcat/logcat.cpp]
- **限额属性默认值**（`README.property`）：`persist.logd.logpersistd.buffer` 默认 `all`；`persist.logd.logpersistd.size` 默认 **256**（MB）；`persist.logd.logpersistd.count` 默认 **256**；`persist.logd.logpersistd.rotate_kbytes` 默认 **1024**（**注意**：`logcatd.rc` 内的兜底默认写作 **2048**，两处不一致）；`persist.logd.size` / `ro.logd.size` 全局默认 **256K**，范围限 64K–256M，`ro.config.low_ram=true` 时为 64K；单 buffer 覆盖用 `persist.logd.size.<buffer>`。
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/logd/README.property]
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/logcat/logcatd.rc]
- **buffer 类型没有 "persistent" 这一项**：AOSP 的 buffer id 全集为 `main / radio / events / system / crash / stats / security / kernel`。所有 buffer 都是内存 ring buffer；**kernel buffer 的来源是 `/dev/kmsg`**（logd `main.cpp`：`static const char dev_kmsg[] = "/dev/kmsg";`），所以重启后内核日志即丢失，只能靠 pstore。`logcat -b` 可选值原文 `main system radio events crash default all`，**默认是 `main,system,crash,kernel`**。
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/liblog/logger_name.cpp]
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/logd/main.cpp]
  [来源: https://android.googlesource.com/platform/system/logging/+/refs/heads/main/logcat/logcat.cpp]
- **崩溃后怎么取**：`logcat -b crash`；`logcat -b all`；`logcat -L` 取上次重启前的（来自 pstore）。bugreport 侧 dumpstate 分别抓 `logcat -b kernel` / `-b main,system,crash` / `-b radio` / `-b events` / `-b stats`。`/sys/fs/pstore/console-ramoops` 的读者**不是 logd**，而是 dumpstate 与 system_server 的 BootReceiver。
  [来源: https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/cmds/dumpstate/dumpstate.cpp]

#### 2.3 tombstone 与 ANR

- 官方描述：崩溃会「a basic crash dump to be written to logcat and a more detailed **tombstone** file to be written to `/data/tombstones/`」，tombstone 含崩溃进程**所有线程**的 stack trace、完整 memory map、所有打开的 fd 列表。Android 8.0 之前由常驻 `debuggerd`/`debuggerd64` 处理，新版本改为按需拉起 `crash_dump32`/`crash_dump64`。
  [来源: https://source.android.com/docs/core/tests/debug]
- 落盘由常驻服务 `tombstoned` 完成（`socket tombstoned_crash seqpacket 0666 system system`）。
  [来源: https://android.googlesource.com/platform/system/core/+/refs/heads/main/debuggerd/tombstoned/tombstoned.rc]
- **命名与格式**：文本 `tombstone_%02d`，proto 变体 `tombstone_%02d.pb`，两者可同时生成；proto schema 在 `debuggerd/proto/tombstone.proto`，并有 `pbtombstone` 做转换。
  [来源: https://android.googlesource.com/platform/system/core/+/refs/heads/main/debuggerd/tombstoned/tombstoned.cpp]
  [来源: https://android.googlesource.com/platform/system/core/+/refs/heads/main/debuggerd/proto/tombstone.proto]
- **限额**（源码原文）：tombstone 队列 `CrashQueue("/data/tombstones", "tombstone_", GetIntProperty("tombstoned.max_tombstone_count", 32), ...)`；ANR 队列 `CrashQueue("/data/anr", "trace_", GetIntProperty("tombstoned.max_anr_count", 64), ...)`。**默认分别是 32 和 64**（流传的「默认 10」与当前 main 分支源码不符）。清理方式是环形复用文件名（`next_artifact_ = (next_artifact_ + 1) % max_artifacts_`），写出前先 `unlink` 旧文件再 `link` 新文件。
  [来源: https://android.googlesource.com/platform/system/core/+/refs/heads/main/debuggerd/tombstoned/tombstoned.cpp]
- **tombstone → DropBox**：system_server 的 `NativeTombstoneManager` 用 `FileObserver.CREATE | FileObserver.MOVED_TO` 监视 `/data/tombstones`，只处理 `tombstone_` 开头的文件（`.pb` 为 proto），解析进程名后调用 `BootReceiver.addTombstoneToDropBox(...)`。
  [来源: https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/services/core/java/com/android/server/os/NativeTombstoneManager.java]
- DropBox tag 常量：`SYSTEM_TOMBSTONE`、`SYSTEM_TOMBSTONE_PROTO`、`SYSTEM_TOMBSTONE_PROTO_WITH_HEADERS`；若 tombstone 文本含 `>>> system_server <<<`，额外再写一条 tag 为 `system_server_native_crash` 的记录。
  [来源: https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/services/core/java/com/android/server/BootReceiver.java]
- **DropBox 存储细节**：目录 `/data/system/dropbox`；文件名 `<urlencode(tag)>@<timestampMillis><ext>`，扩展名 `.txt`/`.dat`/`.gz`/`.lost`；保留默认 `DEFAULT_AGE_SECONDS = 3 * 86400`（3 天），配额 `DEFAULT_QUOTA_KB = Build.IS_USERDEBUG ? 20*1024 : 10*1024`、`DEFAULT_QUOTA_PERCENT = 10`。取用接口 `dumpsys dropbox`（`-p/--print`、`-f/--file`、`--proto`）。
  [来源: https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/services/core/java/com/android/server/DropBoxManagerService.java]
- **ANR trace 落盘**：AMS 的 `StackTracesDumpHelper` 用 `ANR_TRACE_DIR = "/data/anr"`、`ANR_FILE_PREFIX = "anr_"`、`ANR_TEMP_FILE_PREFIX = "temp_anr_"`，文件名 `anr_<格式化时间>`，权限 `0600`；裁剪依据 `tombstoned.max_anr_count`（默认 64）以及 1 天时限。因此 `/data/anr/` 下有两类前缀：AMS 的 `anr_*` 与 tombstoned 的 `trace_*`。
  [来源: https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/services/core/java/com/android/server/am/StackTracesDumpHelper.java]
  [来源: https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/cmds/dumpstate/dumpstate.cpp]

#### 2.4 重启后的交接链路（Android 的答案）

- **BootReceiver 是 system_server 侧的汇聚点**。在 `ACTION_BOOT_COMPLETED` 时 `BootReceiver.logBootEvents()` 执行：
  - 常量 `LAST_KMSG_FILES = { "/sys/fs/pstore/console-ramoops", "/proc/last_kmsg" }`，实际对三个路径调用 `addLastkToDropBox(..., "SYSTEM_LAST_KMSG")`：`/proc/last_kmsg`、`/sys/fs/pstore/console-ramoops`、`/sys/fs/pstore/console-ramoops-0`；
  - 其它交接 tag：`/cache/recovery/log` → `SYSTEM_RECOVERY_LOG`；`/cache/recovery/last_kmsg` → `SYSTEM_RECOVERY_KMSG`；audit 失败 → `SYSTEM_AUDIT`；fsck → `SYSTEM_FSCK`；首次启动 → `SYSTEM_BOOT`，否则 `SYSTEM_RESTART`；
  - **截断上限**：`LOG_SIZE` 默认 **65536**（`ro.debuggable==1` 时 98304），`LASTK_LOG_SIZE` 默认 **65536**（`ro.debuggable==1` 时 196608）。
  [来源: https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/services/core/java/com/android/server/BootReceiver.java]
- **bugreport 是最终聚合**：`adb bugreport` 产出 `bugreport-BUILD_ID-DATE.zip`（含 `.txt` 主文件 + `version.txt`）；dumpstate 会把设备文件系统的文件以 **`FS/` 前缀**拷进 zip（源码 `ZIP_ROOT_DIR = "FS"`），并收集 `TOMBSTONE_DIR = "/data/tombstones/"`、`ANR_DIR = "/data/anr/"`、`DROPBOX_DIR = "/data/system/dropbox"`。文本内另有两个专门段落：**`LAST KMSG`**（`PSTORE_LAST_KMSG = "/sys/fs/pstore/console-ramoops"`，备用 `console-ramoops-0`）与 **`LAST LOGCAT`**（`logcat -L -b all`）。
  [来源: https://developer.android.com/studio/debug/bug-report]
  [来源: https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/cmds/dumpstate/dumpstate.cpp]
- **端到端链路**：内核 panic → ramoops 把 console 与 pmsg 写入保留内存 → 重启后 init 挂载 `/sys/fs/pstore` 并授权 `system:log` → (a) BootReceiver 把 `console-ramoops` 读成 DropBox `SYSTEM_LAST_KMSG`，(b) `tombstoned`/`NativeTombstoneManager` 把新 tombstone 交给 BootReceiver 写 DropBox `SYSTEM_TOMBSTONE`，(c) dumpstate 把 pstore、tombstone、anr、dropbox、logcat 各 buffer 打成一个 `FS/...` 结构的 bugreport zip。
  [来源: https://android.googlesource.com/platform/system/core/+/refs/heads/main/rootdir/init.rc]
  [来源: https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/services/core/java/com/android/server/BootReceiver.java]
  [来源: https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/cmds/dumpstate/dumpstate.cpp]

---

### 3. systemd-journald 的持久化

> 取证说明：`freedesktop.org` 的 man 页对自动抓取返回 403，因此 man 页内容取自生成该页面的权威源文件 `man/journald.conf.xml`、`man/systemd-journald.service.xml`（systemd 主仓 main 分支）。

#### 3.1 存储模式（Storage=）

- `volatile`：仅内存，位于 `/run/log/journal` 层级（需要时创建）。
- `persistent`：优先磁盘 `/var/log/journal`（需要时创建），**早期启动或磁盘不可写时回退到 `/run/log/journal`**。
- `auto`：**判定条件就是 `/var/log/journal` 目录是否存在**——原文 "behaves like `persistent` if the `/var/log/journal` directory exists, and `volatile` otherwise (the existence of the directory controls the storage mode)"。
- `none`：关闭全部存储、丢弃所有日志，但转发到 console / kmsg / syslog socket 仍然工作。
- 默认值：默认 namespace 由**编译期**决定（meson 选项 `default-journal-storage`，发行版通常配 `auto`）；其余 namespace 默认 `persistent`。实际落盘目录 `/var/log/journal/<machine-id>/` 或 `/run/log/journal/<machine-id>/`，后缀 `.journal`。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/systemd-journald.service.xml]
- **官方明说的坑**：journald 启动时**先**用 volatile，直到 `journalctl --flush`（或 SIGUSR1）才切到持久化；这一步在启动时由 `systemd-journal-flush.service` 自动完成。`Storage=` 的设置与 `/var/log/journal/` 是否存在无关。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/systemd-journald.service.xml]
- 运行时基线文件名：`system.journal`、`user-<uid>.journal`、运行时 `runtime`。
  [来源: https://github.com/systemd/systemd/blob/main/src/journal/journald-manager.c]

#### 3.2 落盘方式与崩溃一致性：不是 rename，是 header state + fsync

- **「先写 `system@*.journal~` 临时文件再 rename」的说法经查证不成立**。`journal_file_open()` 中 `O_CREAT` 时要求文件名以 `.journal` 结尾，然后 `openat_report_new(AT_FDCWD, fname, ...)` **直接创建最终文件**，失败时 `(void) unlink(fname)`。`journal-file.c` 中**不存在** `O_TMPFILE`/`mkostemp`/`linkat` 或临时名 + rename 的提交路径。在 systemd 主仓检索 `rsync` 只命中无关内容。0pointer 的 "The Journal" 一文也未涉及文件格式或原子性。
  [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-file.c]
  [来源: https://github.com/systemd/systemd/search?q=rsync]
  [来源: https://0pointer.de/blog/projects/journal-submit.html]
- **真正的原子性来自 header 的 `state` 字段 + 有序 fsync**：
  - 枚举 `STATE_OFFLINE = 0, STATE_ONLINE = 1, STATE_ARCHIVED = 2`；
  - 格式文档规定：打开写入时置 `STATE_ONLINE`，写完关闭置 `STATE_OFFLINE`，轮转后置 `STATE_ARCHIVED`；**"After and before the state field is changed, `fdatasync()` should be executed on the file"**；
  - 实现里实际写的是 `fsync()`：`journal_file_set_offline_internal()` 的顺序是 **fsync(fd) → `f->header->state = f->archive ? STATE_ARCHIVED : STATE_OFFLINE` → fsync(fd)**。即**状态翻转前后各一次 fsync**，是典型的 write-ahead-state 而非 atomic-rename。
  [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-def.h]
  [来源: https://github.com/systemd/systemd/blob/main/docs/JOURNAL_FILE_FORMAT.md]
  [来源: https://github.com/systemd/systemd/blob/main/src/shared/journal-file-util.c]
- **崩溃判定**：`journal_file_verify_header()` 只在以可写方式打开既有文件时校验 state —— `STATE_ARCHIVED` 返回 `-ESHUTDOWN`；`STATE_ONLINE` 返回 `-EBUSY` 并打印 `"Journal file %s is already online. Assuming unclean closing."`；其它值返回 `-EBUSY` 并打印 `"Journal file %s has unknown state %i."`。**「干净关闭」的唯一判据就是 state 字段**。
  [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-file.c]
- **启动时 `.journal~` 的处理是「重命名产出」而不是「消费」**：journald 遍历已有 `.journal` 文件，能读开的走 `journal_file_archive()` 改名为 `原名@<seqnum_id>-<head_entry_seqnum>-<head_entry_realtime>.journal`（随后 `fsync_directory_of_file()`）；**读不开的**走 `journal_file_dispose()` 改名为 `原名@<realtime十六进制>-<random十六进制>.journal~`，日志 `"Failed to read journal file %s for rotation, trying to move it out of the way"`。
  [来源: https://github.com/systemd/systemd/blob/main/src/journal/journald-manager.c]
- `journal_file_open_reliably()` 对 `-EBADMSG`(损坏)/`-EADDRNOTAVAIL`/`-ENODATA`(截断)/`-EHOSTDOWN`/`-EPROTONOSUPPORT`/`-EBUSY`(非干净关闭)/`-ESHUTDOWN`(已归档)/`-EIO`/`-EIDRM` 这些错误，打印 `"File %s corrupted or uncleanly shut down, renaming and replacing"`，改名加 `~` 后**只重试一次**。
  [来源: https://github.com/systemd/systemd/blob/main/src/shared/journal-file-util.c]
- **历史上的一个真实缺陷**：早先 `journal_file_rotate()` 直接把旧文件置 `STATE_ARCHIVED`，而 `journal_file_set_offline()` 在 state ≠ `STATE_ONLINE` 时会短路，导致**被轮转的 journal 从不被 fsync**。现在改为置 `f->archive = true`、保持 `STATE_ONLINE`，由 offline 流程完成落盘后再提交 `STATE_ARCHIVED`。
  [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-file.c]
- `journalctl --verify` 定义为 "Check the journal file for internal consistency"，配合 `--verify-key=` 校验 FSS 真实性；失败时返回非零。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journalctl.xml]
- **`SyncIntervalSec=` 默认 5 分钟**；CRIT/ALERT/EMERG 级别的消息**无条件立即同步**，其余（ERR/WARNING/NOTICE/INFO/DEBUG）才受该超时约束。发行版配置写作 `#SyncIntervalSec=5m`。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]
  [来源: https://github.com/systemd/systemd/blob/main/src/journal/journald.conf.in]

#### 3.3 轮转与限额

- `SystemMaxUse=` / `RuntimeMaxUse=` 默认 **10%** 对应文件系统大小；`SystemKeepFree=` / `RuntimeKeepFree=` 默认 **15%**，**各自 cap 到 4G**；两者同时生效时取**较小**者。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]
  - **文档与代码不一致（实测发现）**：当前 main 分支源码算的是 `MIN(PAGE_ALIGN_U64(fs_size / 20), KEEP_FREE_UPPER)`（即 **5%**，cap 4GiB），与 man 页写的 15% 不符。历史：2013 年 commit `8621b110` 把默认从 5% 提到 15%（理由是 SSD 到 85% 占用后性能下降）；逐 tag 核对显示 v238 是 15%，**v243 起变回 5%**，而 man 页从 v241 到 main 一直写 15%。可判定为**文档滞后**。
    [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-file.c]
    [来源: https://lists.freedesktop.org/archives/systemd-devel/2013-May/010842.html]
- `SystemMaxFileSize=` / `RuntimeMaxFileSize=` 默认 = `SystemMaxUse` 的 **1/8**，**cap 到 128M**，故「通常保留约 7 个轮转文件作为历史」；启用 compact 模式（默认开）时单文件上限为 4G。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]
- `SystemMaxFiles=` / `RuntimeMaxFiles=` 默认 **100**；`MaxFileSec=` 默认 **1 month**（设 0 关闭）；`MaxRetentionSec=` 默认 **0**（关闭）。
  [来源: https://github.com/systemd/systemd/blob/main/src/journal/journald.conf.in]
- **限额是随文件增长同步强制的**："size limits are enforced synchronously when journal files are extended"，无需按时间触发轮转。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]
- **但轮转并不只看 `SystemMaxFileSize`**。`journal_file_rotate_suggested()` 的触发条件还包括：header 尺寸小于当前 `sizeof(Header)`（旧格式）、data/field hash table 填充率超过 **75%**、hash 链深度超过 `HASH_CHAIN_DEPTH_MAX`（**100**）、data 对象未被 field 对象索引、以及 `MaxFileSec` 超期。另外**系统时间倒退**会立即强制轮转。
  [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-file.c]
  [来源: https://github.com/systemd/systemd/blob/main/src/journal/journald-manager.c]
- **超限时删最老的**：`journal_directory_vacuum()` 按 (seqnum, realtime) 升序 `typesafe_qsort` 后从索引 0 开始删，日志 `"Deleted archived journal %s/%s (%s)."`。
  [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-vacuum.c]
- **只删 archived 文件，active 文件永不删**，因此 vacuum 后占用**仍可能超过** `SystemMaxUse=`（man 页明确承认）。「至少保留一个文件」这类规则 man 页**没有给出任何保证**，源码中只是把 active 文件数单独计入 `n_active_files` 而豁免删除。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]
  [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-vacuum.c]

#### 3.4 Rate limiting

- 默认 **`RateLimitIntervalSec=30s`、`RateLimitBurst=10000`**；源码常量 `DEFAULT_RATE_LIMIT_INTERVAL (30*USEC_PER_SEC)`、`DEFAULT_RATE_LIMIT_BURST 10000`。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]
  [来源: https://github.com/systemd/systemd/blob/main/src/journal/journald-config.c]
- **per-service，不是全局**："This rate limiting is applied per-service, so that two services which log do not interfere with each other's limits"；实现上按 `c->unit` 分组并按优先级分池。但**无 unit 归属的消息（如内核消息）不经过该 per-service 限流路径**。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]
  [来源: https://github.com/systemd/systemd/blob/main/src/journal/journald-rate-limit.c]
  [来源: https://github.com/systemd/systemd/blob/main/src/journal/journald-manager.c]
- 超限后**丢弃**该消息，并在恢复时补写一条替代消息：`LOG_MESSAGE("Suppressed %i messages from %s", rl - 1, c->unit)`，带 `N_DROPPED=%i` 字段与 `SD_MESSAGE_JOURNAL_DROPPED` 消息 ID。**这个「补一条说我丢了多少条」的做法值得借鉴。**
  [来源: https://github.com/systemd/systemd/blob/main/src/journal/journald-manager.c]
- **burst 会随可用磁盘空间放大**：`burst_modulate()` 取 `k = log2u64(available)`，`k <= 20`（≤1MB）时不变，否则 `burst = burst * (k-16) / 4`；注释给出对照表：≤1MB ×1、16MB ×2、256MB ×3、4GB ×4、64GB ×5、1TB ×6。
  [来源: https://github.com/systemd/systemd/blob/main/src/journal/journald-rate-limit.c]
- 服务可用 `LogRateLimitIntervalSec=` / `LogRateLimitBurst=` 覆盖全局值（v240 加入）；把任一值设为 0 即关闭限流。
  [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]
  [来源: https://github.com/systemd/systemd/blob/main/NEWS]
- 历史默认值比现在严格得多：2012 年的 `journald.conf` 注释默认是 `#RateLimitInterval=10s` / `#RateLimitBurst=200`。
  [来源: https://lists.freedesktop.org/archives/systemd-devel/2012-October/006801.html]
- **「引入动机是当年被日志风暴打爆」这一说法：未证实**——未找到解释引入动机的一手 commit 说明或设计文档，只能确认该选项 2012 年即已存在。

#### 3.5 已知的坑

- **`Storage=auto` 静默只存内存**：`auto` 的语义就是「`/var/log/journal` 存在才持久化」，而该目录不会被自动创建，因此默认发行版上重启即丢日志。这是 Red Hat 官方 KB 明确指出的根因。
  [来源: https://access.redhat.com/solutions/696893]
- **`/var` 晚挂载竞态**：`/var/log` 位于晚挂载文件系统（如 ZFS）时，`systemd-journald.service` 可能先于挂载启动，于是静默退回内存 journal。
  [来源（社区）: https://discourse.practicalzfs.com/t/psa-systemd-journal-persistence-settings-and-race-condition-between-zfs-mount-service-and-systemd-journald-service/1929]
- **即便配了 `Storage=persistent` 也仍丢**：systemd issue #25712 报告 `/var/log/journal/<machine-id>` 已创建、`journalctl --flush` 也执行了，日志仍进 `/run/log/journal` 且重启丢失。
  [来源: https://github.com/systemd/systemd/issues/25712]
- **崩溃后 `journalctl` 拒绝读取甚至崩溃**：issue #29167 报告非干净重启（BTRFS，systemd 254.3-1）后 `journalctl -b -1` 触发断言 `'!sd_id128_is_null(id)' failed` 并 SIGABRT；用显式 boot ID 反而可读。
  [来源: https://github.com/systemd/systemd/issues/29167]
- **「损坏」未必真损坏**：Ubuntu bug #1696970 中 journald 被 watchdog softlockup 打死后重启，`journalctl --verify` 对所有文件**全部通过**——"renaming and replacing" 只是 unclean shutdown 的**保险动作**，代价是日志被隔离。
  [来源: https://bugs.launchpad.net/ubuntu/+source/systemd/+bug/1696970]
- **损坏并非只来自非干净关闭**：issue #24150 报告用户 journal 文件在没有任何非干净关闭的情况下损坏，`journalctl --verify` 报 `Bad message` 与 `corrupted, ignoring file`，磁盘与文件系统均正常。
  [来源: https://github.com/systemd/systemd/issues/24150]
- **上游加固**：commit `383d915` "journald: harden against forward clock jumps before unclean shutdown" 改为在轮转前先以只读方式把损坏文件当作 template 打开，尽量继承 seqnum 与 ID，改善时钟前跳 + 非干净关闭后的文件选择。
  [来源: https://github.com/systemd/systemd/commit/383d9155a2b5be10e2a14909c234a99e0e0dbba7]
- 早期一手讨论：2013 年 freedesktop bug 64116 "How does one fix journal corruptions?" 记录了「检测到损坏时 journald 把文件改名为 `<something>.journal~`，journalctl 尽力读取」的行为。
  [来源: https://lists.freedesktop.org/archives/systemd-bugs/2013-August/001486.html]

---

### 4. 车规与工业：AUTOSAR DLT、车规 EDR、工业黑匣子

#### 4.1 AUTOSAR DLT（Diagnostic Log and Trace）

**定位与层级**

- 规范为 `AUTOSAR_SWS_DiagnosticLogAndTrace`（Document ID 351，V1.4.0，R4.1 Rev 3）。需求 `SWS_Dlt_00464` 原文："Dlt (Diagnostic Log and Trace) is a basic software module, which handles and stores log and trace messages produced by SW-C it self or the interactions between SW-C and RTE/VFB and by the Basic Software Modules Dem and Det."；`SRS_Dlt_00041`："DLT shall be a central software component in BSW for the log and trace functionality."
  [来源: https://www.autosar.org/fileadmin/standards/R4.1.3/CP/AUTOSAR_SWS_DiagnosticLogAndTrace.pdf]
- 分层文档把 DLT 定义为 "a centralized AUTOSAR service component (Dlt) in the BSW"；BSW 分为 System / Memory / Crypto / Off-board Communication / Communication Services 等组。
  [来源: https://www.autosar.org/fileadmin/standards/R22-11/CP/AUTOSAR_EXP_LayeredSoftwareArchitecture.pdf]
- 注意：**SWS 正文本身未出现 "Service Layer" 字样**，其 Figure 2 只把 Dlt 与 Dem、Det 并列画在 BSW 内；"服务层/系统服务"归类来自 EXP 分层文档。

**协议形态（标准头，大端，`SWS_Dlt_00091`）**

| 字节 | 字段 |
|---|---|
| 0 | HTYP：bit0 UEH / bit1 MSBF / bit2 WEID / bit3 WSID / bit4 WTMS / bit5-7 VERS |
| 1 | MCNT |
| 2-3 | LEN（16 位，含标准头+扩展头+负载） |
| 4-7 | ECU（可选，4 个 ASCII 字符） |
| 8-11 | SEID（可选） |
| 12-15 | TMSP（可选，`SWS_Dlt_00309` 规定分辨率 0.1 ms） |

[来源: https://www.autosar.org/fileadmin/standards/R4.1.3/CP/AUTOSAR_SWS_DiagnosticLogAndTrace.pdf]
- COVESA 头文件 `DLT_HTYP_UEH 0x01 / MSBF 0x02 / WEID 0x04 / WSID 0x08 / WTMS 0x10 / VERS 0xe0` 与之逐位吻合。
  [来源: https://raw.githubusercontent.com/COVESA/dlt-daemon/master/include/dlt/dlt_protocol.h]
- 扩展头共 10 字节：MSIN(1B) / NOAR(1B) / APID(4B) / CTID(4B)；MSIN 内 VERB(bit0)、MSTP(bit1-3)、MTIN(bit4-7)。
- **verbose vs non-verbose**：verbose 下负载自带类型信息；non-verbose 下负载仅 "4 字节 Message ID + 非静态数据"，变量名/单位/源码位置/静态文本等下发给外部文件按 ID 关联，故 NOAR 在 non-verbose 时为 `0x0`。
  [来源: https://www.autosar.org/fileadmin/standards/R4.1.3/CP/AUTOSAR_SWS_DiagnosticLogAndTrace.pdf]

**存储形态（含三处常见误传的纠正）**

- **关键结论：AUTOSAR SWS DLT 本身不规定日志报文落盘。** 其 NVRAM 仅用于配置持久化——"The ECU shall provide enough NVRAM to store persistently the log level table."；`SWS_Dlt_00287` 定义的 `DltNvramBlockId` 存的是 log level / trace status，**不是日志报文**。落盘属实现层扩展。
  [来源: https://www.autosar.org/fileadmin/standards/R4.1.3/CP/AUTOSAR_SWS_DiagnosticLogAndTrace.pdf]
- COVESA `dlt-daemon` 提供两套落盘机制：
  - **旧 Offline Trace**：`OfflineTraceDirectory`（不设即关闭）、`OfflineTraceFileSize`（默认 **1000000** 字节）、`OfflineTraceMaxSize`（默认 **4000000** 字节）、`OfflineTraceFileNameTimestampBased`（1=时间戳，0=索引）。
    [来源: https://raw.githubusercontent.com/COVESA/dlt-daemon/master/src/daemon/dlt.conf]
    [来源: https://covesa.github.io/dlt-daemon/doc/dlt.conf.5.html]
  - **新 Offline Logstorage**（官方称"offline trace 功能的改进"）：独立配置 `dlt_logstorage.conf`，`[FilterN]` 段含 `LogAppName` / `ContextName` / `LogLevel` / `File` / `FileSize` / `NOFiles` / `SyncBehavior` 等；在 `dlt.conf` 中由 `OfflineLogstorageMaxDevices` 启用（默认 off），`OfflineLogstorageDirPath` 默认 off，`OfflineLogstorageCacheSize` 默认 **30000 KB**（内存缓存）；默认搜索路径 `/tmp/dltlogs/dltlogsdevX`，文件名形如 `example_001_20150512_133344.dlt`；控制工具 `dlt-logstorage-ctrl`（`-c 1` 连接 / `-c 0` 断开、`-s` 同步缓存、`-p` 挂载点）。
    [来源: https://covesa.github.io/dlt-daemon/doc/dlt_offline_logstorage.md]
- **纠正一**：离线文件名不是 `dlt_offline_trace`。源码常量 `#define DLT_OFFLINETRACE_FILENAME_BASE "dlt_offlinetrace"`、`#define DLT_OFFLINETRACE_FILENAME_EXT ".dlt"`。
  [来源: https://raw.githubusercontent.com/COVESA/dlt-daemon/master/include/dlt/dlt_offline_trace.h]
- **纠正二**：`dlt-daemon` 的 `-c` 不是缓存开关，而是"加载替代配置文件"（默认 `/etc/dlt.conf`）；端口是 `-p`（默认 **3490**），用户管道目录是 `-t`（默认 `/tmp`）。
  [来源: https://covesa.github.io/dlt-daemon/doc/dlt-daemon.1.md]
- **纠正三**：符号 `DLT_OFFLINE_LOGSTORAGE` 在 COVESA 官方文档与头文件中均未出现 —— **未证实**。

**可靠性：只有序号，没有 CRC**

- MCNT 用途原文："The Message Counter counts Dlt messages received by the Dlt module. With the Message Counter, lost messages can be recognized to a certain level."；8 位无符号 0–255（`SWS_Dlt_00319`），每收到一条 API 消息 +1（`SWS_Dlt_00105`），到 255 后回 0（`SWS_Dlt_00106`）。
  [来源: https://www.autosar.org/fileadmin/standards/R4.1.3/CP/AUTOSAR_SWS_DiagnosticLogAndTrace.pdf]
- **DLT 无 CRC**：SWS DLT 全文无 "CRC"/"checksum" 字样；`DltStandardHeader` 也仅 `htyp`/`mcnt`/`len` 三字段。即 DLT 只有序号级丢包检测，无报文完整性校验。缓冲区溢出是另一套机制（`DLT_MESSAGE_BUFFER_OVERFLOW 0x01` 与 `overflow_counter`）。
  [来源: https://www.autosar.org/fileadmin/standards/R4.1.3/CP/AUTOSAR_SWS_DiagnosticLogAndTrace.pdf]
  [来源: https://raw.githubusercontent.com/COVESA/dlt-daemon/master/include/dlt/dlt_common.h]

**轮转**

- Offline Trace：按 `OfflineTraceFileSize` 切文件，按 `OfflineTraceMaxSize` 超限后**删最旧文件**。
  [来源: https://covesa.github.io/dlt-daemon/doc/dlt.conf.5.html]
- Logstorage：`FileSize`（单文件上限）+ `NOFiles`（文件数上限）+ `OverwriteBehavior`：`DISCARD_OLD`（默认，删最旧）/ `DISCARD_NEW`（停止写入）。
  [来源: https://covesa.github.io/dlt-daemon/doc/dlt_offline_logstorage.md]

#### 4.2 车规 EDR（Event Data Recorder）

**49 CFR Part 563 的结构与时效**

- 结构：§563.1 Scope / .3 Application / .5 Definitions / .7 Data elements / .8 Data format / .9 Data capture / .10 Crash test performance and survivability / .11 Owner's manual / .12 Data retrieval tools。必录元素表是 §563.7 的 Table I 与 Table II，量程/精度表是 §563.8 的 Table III。
  [来源: https://www.law.cornell.edu/cfr/text/49/part-563]
  [来源: https://www.govinfo.gov/content/pkg/CFR-2023-title49-vol6/xml/CFR-2023-title49-vol6-part563.xml]
- **重要时效**：Part 563 已于 2024-12-18 被最终规则（89 FR 102810）实质修订，2026-05-18 再次修订推迟合规日期（91 FR 28432）。截至 2026-09，在售车辆实际仍适用**旧窗（碰撞前 5.0 s @ 2 Hz）**；新窗 **−20.0 s @ 10 Hz** 自 **2028-09-01** 起分阶段、**2031-09-01** 全面生效。
  [来源: https://www.govinfo.gov/content/pkg/FR-2024-12-18/html/2024-29862.htm]
  [来源: https://www.govinfo.gov/metadata/granule/FR-2026-05-18/2026-09849/mods.xml]
- **Table I 核心元素**（区间相对 time zero / 采样率）：
  - Delta-V longitudinal：0–250 ms @ **100 Hz**
  - Maximum delta-V longitudinal、Time, maximum delta-V：0–300 ms
  - Speed, vehicle indicated、Engine throttle % full、Service brake on/off：−5.0–0 s @ **2 Hz**
  - Ignition cycle, crash：−1.0 s；Ignition cycle, download：下载时
  - Safety belt status, driver、Frontal air bag warning lamp：−1.0 s
  - Frontal air bag deployment, time to deploy（driver 与 right front passenger）：Event
  - Multi-event, number of event、Time from event 1 to 2、Complete file recorded
  [来源: https://www.law.cornell.edu/cfr/text/49/563.7]
- **Table II 为 "If recorded" 条件项**（该脚注定义为"记入非易失存储器以便日后下载"）：lateral delta-V、Engine rpm、ABS activity、Stability control、Steering input、Vehicle roll angle（−1.0–5.0 s @10 Hz）、各座位安全带状态、气囊抑制开关、Seat track position、乘员尺寸/位置分类、侧气囊与侧气帘展开时间、Pretensioner time to fire 等。
  [来源: https://www.law.cornell.edu/cfr/text/49/563.7]
- 精度例（§563.8）：delta-V 最小量程 −100~+100 km/h、精度 ±10%、分辨率 1 km/h；最大 delta-V 时间量程 0–300 ms、精度 ±3 ms、分辨率 2.5 ms。
  [来源: https://www.law.cornell.edu/cfr/text/49/563.8]

**触发、冻结、掉电保持**

- **触发阈值**：§563.5 定义 trigger threshold = "a change in vehicle velocity, in the longitudinal direction, that equals or exceeds **8 km/h within a 150 ms interval**"（记横向者纵向或横向皆可）。
  [来源: https://www.law.cornell.edu/cfr/text/49/563.5]
- **time zero** 取以下最先发生者：约束控制算法激活时刻 / 连续算法下纵向累计 delta-V 在 20 ms 内超 0.8 km/h（横向为 5 ms）/ 不可逆约束装置展开。
  [来源: https://www.law.cornell.edu/cfr/text/49/563.5]
- **冻结（锁存）**：§563.9 —— 气囊展开事件 "The memory for the air bag deployment event must be locked to prevent any future overwriting of the data."；其他事件最多记 **2 个**，且已有气囊展开事件数据不得被覆盖。
  [来源: https://www.law.cornell.edu/cfr/text/49/563.9]
- **掉电保持**：靠 §563.5 对非易失存储器的定义（"Data recorded in non-volatile memory is retained after loss of power."）加 §563.10 —— 数据须在碰撞测试后仍存在，并可依 §563.12 方法**读取不少于 10 天**。
  [来源: https://www.law.cornell.edu/cfr/text/49/563.5]
  [来源: https://www.law.cornell.edu/cfr/text/49/563.10]
- **数据取出**：§563.12 只要求厂商保证"商业可得工具"能读取，**不要求任何特定物理接口**（全文无 OBD/DLC 字样）。事实标准是 **Bosch CDR**（可从 DLC 读取）。
  [来源: https://www.law.cornell.edu/cfr/text/49/563.12]
  [来源: http://cdr.boschdiagnostics.com/cdr/]
- **温度要求：未证实。** 已核 Part 563 全部 12 条、2008 年复议答复与 UN R160 Annex 4，均无温度条款；NHTSA 反而明确拒绝了对碰撞后车辆环境条件的保护性要求。另注："73 FR 21808, April 2008" 这一引用**未能证实**。
  [来源: https://www.govinfo.gov/content/pkg/FR-2008-01-14/html/E8-407.htm]

**欧盟与 UN R160**

- Regulation (EU) 2019/2144 Art. 6(1)(g) 强制装备 EDR，Art. 3(13) 定义，Art. 6(4) 要求碰撞前/中/后数据、闭环运行、不可停用、匿名化。
  [来源: https://eur-lex.europa.eu/legal-content/EN/TXT/HTML/?uri=CELEX:32019R2144]
- 技术细则为 Commission Delegated Regulation (EU) 2022/545（编号已证实），Art. 2 要求符合 UN R160（含 01 系列修正案）；Art. 4 要求数据可经**标准 DLC 串口**读取、该口失效时须可直连 EDR，并**禁止经免解锁接口或无线接口读取**。
  [来源: https://eur-lex.europa.eu/eli/reg_del/2022/545/oj]
- UN R160（E/ECE/TRANS/505/Rev.3/Add.159）：
  - 5.3.1 触发同 8 km/h / 150 ms，另加"不可逆约束装置激活"与"VRU 二次安全系统激活"；
  - 5.3.2 锁存条件（含正面无不可逆约束装置时 x 轴 150 ms 内 >25 km/h）；
  - **5.3.4 无空缓冲时按 FIFO 覆盖**（即环形覆盖保新）；
  - **5.3.5 "Data recorded in non-volatile memory is retained after loss of power."**；
  - 5.3 要求"至少容纳 **3 个不同事件**"（对比美国最多 2 个）；5.5 不得停用。
  [来源: https://eur-lex.europa.eu/legal-content/EN/TXT/HTML/?uri=CELEX:42021X1215]
- UN R160 Annex 4 Table 1 与 Part 563 高度重合，但多出 Yaw Rate、TPMS 警告灯、Traction Control、AEBS、巡航/ACC、车道偏离、CSF/ESF/ACSF、VRU 二次安全系统、eCall 状态、Far side impact center airbag 等元素。
  [来源: https://eur-lex.europa.eu/legal-content/EN/TXT/HTML/?uri=CELEX:42021X1215]

#### 4.3 工业黑匣子的常见做法

- **铁路 JRU/OTDR 是"法规驱动的黑匣子"**。IEC 62625-1:2013 定义车载行车数据记录系统，含 Table 1 保护能力参数值与 Table 2 最小记录数据清单，但明确"**数据的保留管理不在本标准范围内**"。
  [来源: https://webstore.iec.ch/en/publication/7273]
  [来源: https://assets.vde-verlag.de/iec-normen/preview-pdf/info_iec62625-1%7Bed1.0%7Db.pdf]
- 49 CFR §229.135：>30 mph 的列车须装事件记录仪，"shall record the most recent **48 hours** of operation"（**即覆盖式环形窗口语义**），并须配符合 Appendix D 的 certified crashworthy ERMM。
  [来源: https://www.law.cornell.edu/cfr/text/49/229.135]
- **Appendix D 抗坠毁数值**（两张互斥表择一）：Table 1 为高温火 **750 °C / 60 min**、低温火 260 °C / 10 h、冲击 **55g / 100 ms** 半正弦、静压 110 kN / 5 min、液体浸泡 48 h、静水压 15 m / 48 h；Table 2 为高温火 1000 °C / 60 min（明火）、静压 111.2 kN + 44.5 kN。合格判据是**测试后全部数据保留**。
  [来源: https://www.law.cornell.edu/cfr/text/49/appendix-D_to_part_229]
- 注：EN 50155 管的是工作条件与试验，**不含**抗坠毁/耐火内存要求；IEEE 1482.1-2013 已于 2024-03-21 失效（inactive）。
  [来源: https://www.evs.ee/en/evs-en-50155-2026]
  [来源: https://standards.ieee.org/ieee/1222/5086/]
- **写路径范式（store-and-forward）**：Ignition 的实现为 内存 buffer → 本地磁盘 cache → 投递，且 "Data is removed from the system only when the write to the database has executed successfully."，"Data is forwarded in the same order that it arrived"，并以 quarantine 隔离反复失败的记录。
  [来源: https://docs.inductiveautomation.com/docs/7.9/database-connections/store-and-forward]
- **Telegraf 的 buffer 设计**：`buffer_strategy` 默认 **memory**，可选 disk；`buffer_disk_sync` 默认 **true**，关闭则 "at the risk of losing metrics buffered during the last flush interval in a power failure"；`metric_buffer_limit` 默认 **10000**，溢出时**覆盖最旧**；每个 output 一个 WAL，遗留 WAL 先排空以保序；**不限制磁盘占用**。
  [来源: https://docs.influxdata.com/telegraf/v1/configuration/agent/]
  [来源: https://raw.githubusercontent.com/influxdata/telegraf/76c56ccb8531c5689fce945a394fbe2e84f767ae/docs/specs/tsd-005-output-buffer-strategy.md]
- **文件系统层的对标：littlefs** —— 原子性 = "redundancy and error detection"，用 **32-bit CRC**，metadata pair 双块 + **revision count**，三条更新路径 append / compaction / split，但明确 "**littlefs by itself does not provide ECC**"。
  [来源: https://raw.githubusercontent.com/littlefs-project/littlefs/master/DESIGN.md]
- **ext4 的对照**：默认 `data=ordered` 下 "file data blocks are not guaranteed to be in any consistent state after a crash"；只有 `data=journal` 才做到 "all data and metadata are written to disk through the journal. This is slower but safest."
  [来源: https://www.kernel.org/doc/Documentation/filesystems/ext4/journal.rst]
- **介质能力边界（对黑匣子最关键）**：SD/microSD 无电容，固件级 SPOR 只保护"静态数据"——"protects data at rest — not data in flight"，"the one write in progress at the instant power is cut is not guaranteed to finish"；厂商建议 "pair SPOR with a system-level clean-shutdown or write-buffering policy"。
  [来源: https://www.atpinc.com/blog/iot-gateway-edge-storage-industrial-sd-card]
- 工业 SSD 的 PLP（Power Loss Protection）则靠**电容**完成在途写入。
  [来源: https://www.innodisk.com/en/products/flash-storage/sata-25-ssd/25-sata-ssd-3ie6-p]
- **缺口检测范式：OPC UA Part 14 §7.2.3 SequenceNumber** —— 单调递增、每报文 +1、**会回绕**；接收端用 `(收到序号 − 1 − 上次已处理序号) mod 2^N` 判定，下界 `2^(N−2)`、上界 `2^N − 2^(N−2)`；低于下界 = 更新、高于上界 = 更旧或重复、其余 = 非法丢弃。**这是"用回绕序号 + 窗口判定"同时解决丢包与重放的成熟规范。**
  [来源: https://reference.opcfoundation.org/Core/Part14/v105/docs/7.2.3]
- **时间基准**：PTP（IEEE 1588）支持 "synchronization in the sub-microsecond range" 并可折算 UTC；NTPv4 "extend the potential accuracy to the tens of microseconds with modern workstations and fast LANs"。
  [来源: https://standards.ieee.org/ieee/1588/11795/]
  [来源: https://www.rfc-editor.org/rfc/rfc5905.txt]
- **反面教训**：systemd journal 的 seqnum 每次启动重置为 0，跨 boot 强行做全序会产生 A<B<C<A 的环。
  [来源（社区讨论）: https://lists.freedesktop.org/archives/systemd-devel/2017-December/039971.html]

---

### 5. 嵌入式黑匣子：ArduPilot DataFlash 与 Betaflight Blackbox

#### 5.1 ArduPilot DataFlash（.BIN）

**介质与后端选择**

- DataFlash 日志存在飞控本机，介质随硬件而定：SD 卡、板载 dataflash 芯片、或通过 MAVLink 遥测口实时流出（典型 921600 baud）。
  [来源: https://ardupilot.org/copter/docs/common-downloading-and-analyzing-data-logs-in-mission-planner.html]
- 由 `LOG_BACKEND_TYPE` 位掩码决定："0" 关闭、"1"（bit0）写 SD 卡文件、"2"（bit1）走 MAVLink 流、"4"（bit2）写板载 dataflash；另有 `file_disarm_rot`、`log_disarmed`、`max_log_files` 等成员。
  [来源: https://ardupilot.org/copter/docs/common-downloading-and-analyzing-data-logs-in-mission-planner.html]
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger.h]
- 参数默认值（源码 `AP_GROUPINFO` 表）：`LOG_BACKEND_TYPE` 默认 `HAL_LOGGING_BACKENDS_DEFAULT`、`LOG_DISARMED`=0、`LOG_REPLAY`=0、`LOG_FILE_DSRMROT`=0、`LOG_MAV_BUFSIZE`=8、`LOG_FILE_TIMEOUT`=5、`LOG_MAX_FILES`=**500**。**注意**：`LOG_MIN_MB_FREE` 这个参数名不存在，实际是 `LOG_FILE_MB_FREE`（"Old logs on the SD card will be deleted to maintain this amount of free space"）。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger.cpp]

**记录格式（自描述，FMT 为核心）**

- **每条记录的头是 3 字节**（不是 2 字节）：`#define LOG_PACKET_HEADER uint8_t head1, head2, msgid;`，`LOG_PACKET_HEADER_LEN 3`，`HEAD_BYTE1 0xA3`、`HEAD_BYTE2 0x95`。长度字段 1 字节，`LOG_PACKET_MAX_LEN (UINT8_MAX)`，单条记录最大 255 字节。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/LogStructure.h]
- **FMT 消息自描述**：`struct PACKED log_Format { LOG_PACKET_HEADER; uint8_t type; uint8_t length; char name[4]; char format[16]; char labels[64]; };`，注册项 `{ LOG_FORMAT_MSG, sizeof(log_Format), "FMT", "BBnNZ", "Type,Length,Name,Format,Columns", ... }`，`LOG_FORMAT_MSG = 128`（源码注释 "this must remain #128"）。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/LogStructure.h]
- 格式串字母表（节选）：`b`=int8_t、`B`=uint8_t、`h`=int16_t、`i`=int32_t、`f`=float、`d`=double、`n`=char[4]、`N`=char[16]、`Z`=char[64]、`Q`=uint64_t、`L`=int32 经纬度（-35.1332423 存为 -351332423）、`M`=uint8_t flight mode。
  [来源: https://ardupilot.org/dev/docs/code-overview-adding-a-new-log-message.html]
- **单位与倍数不在格式串里**，而由独立的 `UNIT` / `MULT` 消息给出（`"UNIT","QbZ","TimeUS,Id,Label"` 与 `"MULT","Qbd","TimeUS,Id,Mult"`），另有 `FMTU` 把 Format ID 关联到 Unit/Mult ID。README 明确警告 "a GCS shouldn't/mustn't infer any scaling from the unit name"。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/README.md]
- 新增消息 API：`void Write(const char *name, const char *labels, const char *units, const char *mults, const char *fmt, ...)`；name ≤4 字符、labels 上限 16 字段/64 字符、fmt ≤16 字符。高频消息用 `WriteBlock()` + PACKED 结构体，结构体首字段以 `LOG_PACKET_HEADER;` 开头，第二字段必须是 `uint64_t time_us;`。
  [来源: https://ardupilot.org/dev/docs/code-overview-adding-a-new-log-message.html]

**扇区化与块头（关键澄清）**

- **文件后端（SD 卡）没有块头、没有扇区计数器、没有记录级 CRC**。写入走 `AP::FS()` 抽象（`open/read/write/lseek/fsync`），文件名 `asprintf(&buf, "%s/%08u.BIN", _log_directory, log_num)`（8 位零填充、大写 .BIN），另有 `LASTLOG.TXT` 记录最后一个日志号。唯一的对齐考虑是"尽量按 512 字节边界写以避免文件系统读放大"（对 littlefs 会跳过）。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_File.cpp]
- **块后端（板载 dataflash）才有块头**：每页开头 `struct PACKED PageHeader { uint32_t FilePage; uint16_t FileNumber; #if BLOCK_LOG_VALIDATE uint32_t crc; #endif };`，`BLOCK_LOG_VALIDATE` **默认 0，所以 CRC 字段默认不存在**；`FilePage==1` 时后面再跟 `struct PACKED FileHeader { uint32_t utc_secs; };`。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_Block.h]
- 排序**不是用递增计数器**，而是 64 位键 `(FileNumber<<32) | df_FilePage`（文件号在高 32 位、页号在低 32 位）；`find_last_page()` 用二分查找找最大值，擦除区（`0xFFFF`）被折扣。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_Block.cpp]
- 最后一块扇区的首 4 字节存格式版本字：`DF_LOGGING_FORMAT 0x1901201B`；`Init()` 执行 `df_NumPages -= df_PagePerBlock;` 以 "reserve space for version in last sector"。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_Block.cpp]
- **结论**：ArduPilot 有"块头含标识（FileNumber + FilePage + utc_secs + 格式版本字）"的做法，但**没有"第一次写该扇区的序号"这类字段**。扇区/块/页尺寸为 `df_PageSize`、`df_PagePerBlock`（一般 64k 块）、`df_PagePerSector`（一般 4k 扇区）。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_Block.h]

**满了怎么处理：两种后端策略不同**

- **块后端是显式环形缓冲**：`bufferspace_available()` 直接返回 `df_NumPages * df_PageSize`，注释 "AP_Logger_Block devices are ring buffers, we *always* have room…"；`_WritePrioritisedBlock()` 注释 "is_critical is ignored - we're a ring buffer and never run out of space."。环形覆盖的终止条件在 `FinishWrite()`：**"are we about to erase a sector with our own headers in it?"**，若是则置 `chip_full` 并停止，每秒打印 "Chip full, logging stopped"。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_Block.cpp]
- **文件后端不做环形，而是删最老的整个文件**：`Prep_MinSpace()` 按 `min_MB_free` 从 `find_oldest_log()` 向后删除，打印 "Removing (%s) for minimum-space requirements"；运行中 `disk_space_avail()` 不足则打印 "Out of space for logging"、调用 `stop_logging()` 并置 `_open_error_ms` 阻止 5 秒内重启日志。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_File.cpp]
- 官方文档补充：板载 flash 板（典型 16MB）"saves log files in a manner like a circular buffer"，"Once the flash is filled, the oldest log file is overwritten"，但 **"If there is only one file on the flash when space runs out, logging is stopped instead"**。
  [来源: https://ardupilot.org/copter/docs/common-downloading-and-analyzing-data-logs-in-mission-planner.html]
- `LOG_DISARMED` 语义：1 = 上电即记录（便于查 pre-arm 故障）；2 = 仅非 USB 供电时记录；3 = 记录但**若该次未进入 armed 状态则在下次启动时删除该日志**。
  [来源: https://ardupilot.org/copter/docs/common-downloading-and-analyzing-data-logs-in-mission-planner.html]
- `LOG_FILE_DSRMROT`：`vehicle_was_disarmed()` 中当它与 `!log_replay` 同时成立时置 `_rotate_pending = true`，真正轮转在 `periodic_1Hz()` 中执行（"handle log rotation once we stop logging" → `stop_logging_async()`）。官方描述为"disarm 后强制新建日志文件"；正常情况下"one file for every power cycle of the autopilot, beginning upon first arm"。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_Backend.cpp]
  [来源: https://ardupilot.org/copter/docs/common-downloading-and-analyzing-data-logs-in-mission-planner.html]

**掉电 / 残缺记录**

- 写入路径：`_WritePrioritisedBlock()` 先塞进 `_writebuf`（大小 `file_bufsize`×1024，失败则 ×0.9 重试），空间不足时 **"if no room for entire message - drop it"** 并累加 `_dropped`；`io_timer()` 再按 `_writebuf_chunk` 分块 `AP::FS().write()`，并用 `bytes_until_fsync()` 裁剪到 "write exactly enough to sync" 后 `fsync()`。写失败时 `_writebuf.advance(nwritten)` 只消费实际写入的字节数；非 ENOSPC 错误持续超过 `LOG_FILE_TIMEOUT` 秒则放弃并关闭文件。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_File.cpp]
- **文件后端本身没有记录级校验/截断/修复逻辑**——没有 CRC，只从 `_write_offset` 续写。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_File.cpp]
- **残缺记录的恢复责任在解析器一侧**。pymavlink `DFReader.py` 定义 `HEAD1 = 0xA3` / `HEAD2 = 0x95`，逐字节扫描，头不匹配则 `ofs += 1` 重同步并打印 `"bad header 0x%02x 0x%02x at %d"`。源码注释包括 `"we can have garbage at the end of an APM2 log"`、`"other corruption; logs transferred via DataFlash_MAVLink may have blocks of 0s in them"`、`"out of data - can often happen half way through a message"`；对块日志尾部空白有专门处理 `"Block based logs are sized in pages which means they can have up to 249 bytes of trailing space."`
  [来源: https://raw.githubusercontent.com/ArduPilot/pymavlink/master/DFReader.py]
- `mavlogdump.py` **不检查魔数**，仅按扩展名分流：`.bin/.BIN/.px4log` 走 DataFlash、`.log/.LOG` 走文本 DataFlash、`.tlog/.TLOG` 走遥测日志；坏数据靠 `BAD_DATA` + `--robust` 跳过。
  [来源: https://raw.githubusercontent.com/ArduPilot/pymavlink/master/tools/mavlogdump.py]

**导出链路（支持分包与续传——这是全篇对我方最可直接借鉴的一条）**

- 协议：`LOG_REQUEST_LIST` → `LOG_ENTRY` → `LOG_REQUEST_DATA` → `LOG_DATA`，另有 `LOG_ERASE`、`LOG_REQUEST_END`。
  [来源: https://ardupilot.org/dev/docs/mavlink-log-download.html]
- **`LOG_REQUEST_DATA` 带 `ofs`（偏移）与 `count`（请求字节数），因此天然支持分包与断点续传**；飞控侧 `handle_log_request_data` 直接 `_log_data_offset = packet.ofs; _log_data_remaining = size - ofs;` 并把剩余长度截断到 `packet.count`。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_MAVLinkLogTransfer.cpp]
- **每个 `LOG_DATA` 最多 90 字节**（`MAVLINK_MSG_LOG_DATA_FIELD_DATA_LEN`）；请求直到收满 `LOG_ENTRY` 报告的 `size` 才算完成——官方明确这是**唯一可靠的完成判据**，不能用 `count == 0` 作为结束信号。
  [来源: https://ardupilot.org/dev/docs/mavlink-log-download.html]
- 同一链路**只能有一个未完成的 `LOG_REQUEST_DATA`**，多余请求被静默忽略（"silently dropping any repeated attempts to start logging"），因此不能流水线化；**但丢失的分片可以事后用另一个 `LOG_REQUEST_DATA` 只请求该偏移区间补回**（Mission Planner 就是这么做的）。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_MAVLinkLogTransfer.cpp]
  [来源: https://ardupilot.org/dev/docs/mavlink-log-download.html]
- 下载前必须解锁（disarm），否则返回 "Disarm for log download" 并忽略请求。
  [来源: https://ardupilot.org/dev/docs/mavlink-log-download.html]
- 实时流式导出走 `REMOTE_LOG_DATA_BLOCK`（每块 200 字节，带 `seqno`），需先发 `REMOTE_LOG_BLOCK_STATUS` + `seqno = MAV_REMOTE_LOG_DATA_BLOCK_START`（2147483646）；**每块可 ACK/NACK，NAK 的块会重发**；`seqno = MAV_REMOTE_LOG_DATA_BLOCK_STOP`（2147483645）停止。
  [来源: https://ardupilot.org/dev/docs/mavlink-log-download.html]
- 官方文档指出 **MAVFTP 更快**（"not limited to the small chunk sizes"），但 "not the recommended method"。
  [来源: https://ardupilot.org/dev/docs/mavlink-log-download.html]
- 离线分析工具：Mission Planner "Review a Log"；**MAVExplorer**（MAVProxy 的一部分，用法 `MAVExplorer.py ~/Desktop/ardupilot/00000013.bin`）；`mavlogdump.py`（pymavlink 仓 `tools/`，可出 CSV，支持 `--types/--nottypes`、`--reduce`、`--robust`、`--no-bad-data`、`--follow`）。大文件随机访问有可选的 Cython 扩展 `dfindexer`（内存映射 + 预计算消息偏移数组做类型索引）。
  [来源: https://ardupilot.org/dev/docs/using-mavexplorer-for-log-analysis.html]
  [来源: https://raw.githubusercontent.com/ArduPilot/pymavlink/master/tools/mavlogdump.py]

#### 5.2 Betaflight Blackbox

> URL 更正：`https://betaflight.com/docs/development/Blackbox` 与 `https://betaflight.com/docs/wiki/guides/current/blackbox` 实测**均 404**。当前有效入口是 `https://betaflight.com/docs/wiki/guides/current/Black-Box-logging-and-usage`（用户指南）与 `https://betaflight.com/docs/development/Blackbox-Internals`（格式内部细节）。

**介质与启用**

- 每个控制循环迭代都把飞控状态流式写出：串口外接 logger、板载 dataflash，或板载 SD 卡座。需先启用 `BLACKBOX` feature；介质用 `blackbox_device` 选择：`SERIAL`/`SPIFLASH`/`SDCARD`。固件枚举 `BLACKBOX_DEVICE_NONE=0, FLASH=1, SDCARD=2, SERIAL=3, VIRTUAL=4`。
  [来源: https://betaflight.com/docs/wiki/guides/current/Black-Box-logging-and-usage]
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.h]
- **`blackbox_logging` 这个参数名未证实存在**；实际是 `blackbox_mode`（`NORMAL`/`MOTOR_TEST`/`ALWAYS`），采样率参数是 `blackbox_sample_rate`（`1/1`、`1/2`、`1/4`、`1/8`、`1/16`，**默认 `1/4`**），老的 `blackbox_rate_num/denom` 已被取代。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/cli/settings.c]

**帧格式：自描述 header + 差分帧**

- 日志起始标记是固定字符串 `"H Product:Blackbox flight data recorder by Nicholas Sherlock\n"` + `"H Data version:2\n"`，**当前 Data version = 2**。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.c]
- header 段是纯 ASCII 行 `H <name>:<value>\n`；**没有显式终止标记**，隐式结束于"遇到第一个不以 H 开头的字节"。`H `（H + 空格）是区分 header 行与二进制 `H`（GPS home）帧的关键。
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-tools/master/src/parser.c]
- 字段定义 header 由 `blackboxFieldHeaderNames[] = { "name", "signed", "predictor", "encoding", "predictor", "encoding" }` 驱动，格式串 `"H Field %c %s:"`，实际写出 `H Field I name:` / `H Field I signed:` / `H Field I predictor:` / `H Field I encoding:` / `H Field P predictor:` / `H Field P encoding:`（以及 H/G/S 帧各自的对应行）。字段列表用**逗号**分隔，带下标时追加 `[n]`，例如 `axisP[0]`。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.c]
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-tools/master/src/parser.c]
- 系统 header 实测包括 `H Firmware type:`、`H Firmware revision:`、`H DeviceUID:`、`H Board information:`、`H Log start datetime:`、`H Craft name:`、`H I interval:`、`H P interval:`、`H P ratio:`、`H gyro_scale:`、`H motorOutput:`、`H vbatref:`、`H looptime:` 等。（`H GPS:` 这种 header 未证实。）
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.c]
- 帧类型字符：`I`(Intra/关键帧，自包含可独立解码)、`P`(Inter/差分帧)、`S`(Slow/低频帧)、`E`(Event)、`H`(GPS home)、`G`(GPS 状态)。**每帧只有 1 个字母 + 字段数据，没有长度字段、没有 checksum、没有 trailer**。
  [来源: https://betaflight.com/docs/development/Blackbox-Internals]
- I 帧插入时机：`if (++blackboxLoopIndex >= blackboxIInterval) { ... }`，注释 "Write a keyframe every blackboxIInterval frames so we can resynchronise upon missing frames"；`blackboxIInterval = (uint16_t)(32 * 1000 / targetPidLooptime)`，1kHz 环时为 32（约 32ms 一个 I 帧）。时间戳字段 `time` 紧跟 `loopIteration`，是本次主循环开始时刻的微秒时间戳。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.c]
  [来源: https://betaflight.com/docs/development/Blackbox-Internals]

**差分编码（predictor）**

- **`Field I predictor` 与 `Field P predictor` 是同一字段在 I 帧与 P 帧各自独立的 predictor 声明**——因为 I 帧必须自包含，几乎一律 `PREDICT(0)`，P 帧可用历史。源码结构体 `{ name, fieldNameIndex, isSigned, Ipredict, Iencode, Ppredict, Pencode, condition }`。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.c]
- 典型字段：`time` 用 `.Ipredict=PREDICT(0)/.Iencode=UNSIGNED_VB` 与 `.Ppredict=PREDICT(STRAIGHT_LINE)/.Pencode=SIGNED_VB`；`gyroADC` 的 P 帧用 `PREDICT(AVERAGE_2)`；`motor[0]` 的 I 帧用 `PREDICT(MINMOTOR)`、`motor[1]` 用 `PREDICT(MOTOR_0)`；`vbatLatest` 的 I 帧用 `PREDICT(VBATREF)` + `NEG_14BIT`；`servo` 的 I 帧用 `PREDICT(1500)`。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.c]
- 全部 predictor（`flightLogFieldPredictor_e`）：`0`=PREDICTOR_0、`1`=PREVIOUS、`2`=STRAIGHT_LINE、`3`=AVERAGE_2、`4`=MINTHROTTLE、`5`=MOTOR_0、`6`=INC、`7`=HOME_COORD、`8`=1500、`9`=VBATREF、`10`=LAST_MAIN_FRAME_TIME、`11`=MINMOTOR。数学定义：`STRAIGHT_LINE = 2*prev - prev2`；`AVERAGE_2 = (prev + prev2)/2`；`PREVIOUS` = 上帧同字段值。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox_fielddefs.h]
  [来源: https://betaflight.com/docs/development/Blackbox-Internals]
- 官方解释为什么要差分：**"The job of the predictor is to bring the value to be encoded as close to zero as possible"**，这样配合可变长编码只需 1 字节甚至更少。官方码率参考：典型 30 个状态变量、平均 28 字节/帧、900Hz ≈ **25,000 字节/秒**。
  [来源: https://betaflight.com/docs/development/Blackbox-Internals]

**编码方式（可变长 + 分组打包）**

- 固件编码枚举：`SIGNED_VB=0`、`UNSIGNED_VB=1`、`NEG_14BIT=3`、`TAG8_8SVB=6`、`TAG2_3S32=7`、`TAG8_4S16=8`、`NULL=9`、`TAG2_3SVARIABLE=10`（**2/4/5 是空号**）。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox_fielddefs.h]
- **可变长编码规则**（高位 bit = 续字节标志，低位 7 bit 为数据，小端）：
  ```c
  while (value > 127) {
      blackboxWrite((uint8_t)(value | 0x80)); // Set the high bit to mean "more bytes follow"
      value >>= 7;
  }
  blackboxWrite(value);
  ```
  官方示例：1→`0x01`，127→`0x7F`，128→`0x80 0x01`，23456→`0xA0 0xB7 0x01`。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox_encoding.c]
  [来源: https://betaflight.com/docs/development/Blackbox-Internals]
- **有符号数走 ZigZag**（不是补码截断）：`blackboxWriteSignedVB()` 先 `zigzagEncode(value)` 再调 unsigned VB。解码侧上限 5 字节（`for (i = 0; i < 5; i++) { ... if (c < 128) return result; shift += 7; }`），`readSignedVB` 再做 zigzagDecode。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox_encoding.c]
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-tools/master/src/stream.c]
- 分组打包编码（P 帧用）：`TAG8_8SVB` = 1 字节 header（bit=0 表示该字段为 0，最多 8 个字段）+ 非零字段的 signed VB；`TAG2_3S32` = 2 bit header + 3 个 2/4/6/32 bit 有符号值；`TAG8_4S16` = 8 bit header + 4 个 0/4/8/16 bit 值；`NEG_14BIT` = `blackboxWriteUnsignedVB((vbatReference - vbatLatest) & 0x3FFF)`。
  [来源: https://betaflight.com/docs/development/Blackbox-Internals]
- Header 写入是**分块限速**的以避免占用主循环：`BLACKBOX_MAX_ACCUMULATED_HEADER_BUDGET 256`、`BLACKBOX_TARGET_HEADER_BUDGET_PER_ITERATION 64`。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox_io.h]

**两种介质**

- **SD 卡（FAT）**：要求 FAT16/FAT32，**SDXC（>32GB）不支持**。文件名 `#define LOGFILE_PREFIX "LOG"`、`#define LOGFILE_SUFFIX "BFL"` → `LOGnnnnn.BFL`（5 位十进制递增）；每次解锁新建文件，不追加旧文件。用 `afatfs_fopen(filename, "as", ...)` 打开，mode 的 `'s'` 表示 `AFATFS_FILE_MODE_CONTIGUOUS`（从空闲空间划连续区）。首次上电会扫描空闲空间收进一个 `FREESPAC.E` 文件，之后从它切块建日志；**不要用电脑编辑它、不要做碎片整理**；该文件上限 4GB。
  [来源: https://betaflight.com/docs/wiki/guides/current/Black-Box-logging-and-usage]
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox_io.c]
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/io/asyncfatfs/asyncfatfs.c]
- **板载 flash（flashfs）**：**源码路径是 `src/main/io/flashfs.c` / `flashfs.h`**（不在 `drivers/`）。写缓冲常量 `FLASHFS_WRITE_BUFFER_SIZE 128`、`FLASHFS_WRITE_BUFFER_AUTO_FLUSH_LEN 64`。擦除粒度来自几何结构 `flashGeometry_t { sectors, pageSize, sectorSize, totalSize, pagesPerSector, ... }`，如 W25Q128FV 报 `pagesPerSector=256`、`pageSize=256` → `sectorSize=65536`（64KB）。官方列出的芯片有 M25P16(2MB)、W25Q64/N25Q064(8MB)、W25Q128/N25Q128(16MB)，"At the default 1/4 logging rate this is typically enough for a few minutes of flight"。（`FLASHFS_SECTOR_SIZE` 这个宏**全仓检索 0 命中，不存在**。）
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/io/flashfs.c]
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/drivers/flash/flash.h]
  [来源: https://betaflight.com/docs/wiki/guides/current/Black-Box-logging-and-usage]

**满了怎么处理：不是环形覆盖，是停止记录**

- `isBlackboxDeviceFull()` 按介质分派：`SERIAL` 永远 false；`FLASH` 返回 `flashfsIsEOF()`；`SDCARD` 返回 `afatfs_isFull()`。主循环检测到满则 `blackboxSetState(BLACKBOX_STATE_STOPPED)`——"Did we run out of room on the device? Stop!"。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox_io.c]
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.c]
- 官方佐证："If you try to start recording a new flight when the dataflash is already full, Blackbox logging will be disabled and nothing will be recorded."
  [来源: https://betaflight.com/docs/wiki/guides/current/Black-Box-logging-and-usage]
- **flashfs 不是环形缓冲**：`bool flashfsIsEOF(void) { return tailAddress >= flashfsSize; }`，写指针单调前进到顶即 EOF。回收只能显式擦除（`blackboxEraseAll()` → `flashfsEraseCompletely()`；App 侧 "erase flash" 按钮）。SD 卡满则删 `FREESPAC.E`（及残留日志）或重新格式化。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/io/flashfs.c]
  [来源: https://betaflight.com/docs/wiki/guides/current/Black-Box-logging-and-usage]

**掉电 / 崩溃：无校验、靠启发式重同步**

- 官方明说："There is no frame length field, checksum, or trailer"，且 "neither a frame length field nor frame trailer is recorded that would allow for the detection of missing bytes"——损坏被假定为"缓冲区溢出丢字节"而非位翻转。
  [来源: https://betaflight.com/docs/development/Blackbox-Internals]
- 解码器用**启发式**校验：读完一帧后看紧跟的字节是否是合法帧类型字符，是→接受，否→整帧弃掉并从该帧起始字节的**后一字节**重新找帧头；**一帧被拒会连累其后所有 inter 帧直到下一个 I 帧**——这正是 I 帧存在的意义。另若 `loopIteration`/`time` 出现不合理跳变或回退也判帧无效。
  [来源: https://betaflight.com/docs/development/Blackbox-Internals]
- blackbox-tools 实现里有 `corruptCount`/`totalCorruptFrames` 统计、`FLIGHT_LOG_MAX_FRAME_LENGTH` 上限校验、`flightLogValidateMainFrameValues()` 与时间戳回绕处理 `flightLogDetectAndApplyTimestampRollover()`。
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-tools/master/src/parser.c]
- 正常 disarm 时会写一条结束事件：`case FLIGHT_LOG_EVENT_LOG_END: blackboxWriteString("End of log"); blackboxWrite(0); break;`（`FLIGHT_LOG_EVENT_LOG_END = 255`），字节流为 `'E', 0xFF, "End of log", 0x00`；**掉电时这条不会被写入**——是一个廉价的"是否干净收尾"标志。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.c]
- **flashfs 侧无任何持久化元数据**：没有 sector header、没有 magic、没有序号、没有 state 字段。上电恢复靠 `flashfsInit()` → `flashfsIdentifyStartOfFreeSpace()`，用**二分查找**定位"最左边的整块已擦除块"（块大小 `FREE_BLOCK_SIZE = 2048`，只抽查 16 字节全为 `0xFFFFFFFF`），注释原文 "an erased block is all bits set to 1, which pretty much never appears in reasonable size substrings of blackbox logs"。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/io/flashfs.c]
- 源码明确解释了**为什么不写卷头**："To do better we might write a volume header instead, which would mark how much free space remains. But keeping a header up to date while logging would incur more writes to the flash, which would consume precious write bandwidth and block more often."——**这是"元数据更新开销 vs 可恢复性"权衡的一手陈述，对我方直接相关。**
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/io/flashfs.c]

**导出与离线解析**

- **`blackbox_decode` 不在 blackbox-log-viewer 仓**，而在独立的 `betaflight/blackbox-tools` 仓。
  [来源: https://github.com/betaflight/blackbox-tools]
- 解析流程：`flightLogCreate()` 用 `mmap` 映射整个文件 → `memmem()` 搜索起始标记，把每个匹配位置记入 `log->logBegin[]`（上限 `FLIGHT_LOG_MAX_LOGS_IN_FILE = 1000`）→ **先跑 header 循环建立字段表**并把 I 帧的名字/符号表 `memcpy` 给 P 帧 → **再跑 payload 循环**，按帧类型字符分派到 `parseIntraframe`/`parseInterframe`/`parseGPSFrame`/`parseGPSHomeFrame`/`parseSlowFrame`/`parseEventFrame`，每帧内按 `encoding[i]` 读取、再 `applyPrediction()` 加回预测值 → `onFrameReady` 回调逐帧写 CSV。**这就是"先建表、再解流"的经典两遍式黑匣子解析。**
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-tools/master/src/parser.c]
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-tools/master/src/blackbox_decode.c]
- 命令行选项（原文节选）：`--index <num>` "Choose the log from the file that should be decoded (or omit to decode all)"、`--limits`、`--stdout`、`--raw` "Don't apply predictions to fields (show raw field deltas)"、`--merge-gps`、`--simulate-imu`、`--unit-rotation <unit>` 等。
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-tools/master/src/blackbox_decode.c]
- **`blackbox_decode` 不支持分包/断点续传**：`--index` 只能在文件内多个完整日志之间选择，不能指定时间或字节范围；选定后一定从 `logBegin[logIndex]` 顺序解到底（P 帧与 predictor 依赖历史）。**无任何 resume/checkpoint 机制。**
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-tools/master/src/blackbox_decode.c]
- **网页版 blackbox-log-viewer 支持任意时间点随机访问**：`buildIntraframeDirectories()` 完整扫描一遍建"I 帧目录"，**每 4 个 I 帧切一个 chunk**，并同时快照 `initialIMU`/`initialSlow`/`initialGPSHome`/`initialGPS` 这些非每帧重写的状态，注释 "To enable seeking to an arbitrary point in the log without re-reading anything that came before"；`getChunksInTimeRange()` 再用二分查找定位 chunk 并增量解码，结果进 `chunkCache`。**这是"用关键帧 + 状态快照建索引实现随机访问"的教科书式做法。**
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-log-viewer/master/src/flightlog_index.js]
- 网页版 CSV 导出用 Web Worker，导出范围由 `getChunksInTimeRange(min, max)` 决定。
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-log-viewer/master/src/csv-exporter.js]
- **工具链变动**：`blackbox-log-viewer` 已功能冻结，并入 Betaflight App 的 Blackbox Viewer 标签页，**2026-12-01 归档**；standalone 版 blackbox.betaflight.com 保持在线但不再维护。
  [来源: https://github.com/betaflight/blackbox-log-viewer]
- 从飞控取日志的两条路：MSP 的 "save flash to file"（官方已不推荐："slow and relatively prone to errors"，MSP 连接本身有 "intrinsic, fundamental limitations that make it unsuitable for file transfers"），以及 **MSC 大容量存储模式**（CLI 输入 `msc` 重启，F4/G4/F7/H7 支持）。
  [来源: https://betaflight.com/docs/wiki/guides/current/Mass-Storage-Device-Support]
- 固件侧 MSP 读接口 `MSP_DATAFLASH_READ` 的应答里先写 `sbufWriteU32(dst, address)` 再写长度，实际读调 `flashfsReadAbs(address, sbufPtr(dst), readLen)`——即**按绝对地址 + 长度读，天然可分块**；`MSP_DATAFLASH_SUMMARY` 返回 flags（`MSP_FLASHFS_FLAG_SUPPORTED`/`_READY`）、sectors、`flashfsGetSize()`、`flashfsGetOffset()`。
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/msp/msp.c]

---

## 五维对照表

| 系统 | ① 触发时机 | ② 暂存介质 | ③ 可靠性手段 | ④ 轮转与限额 | ⑤ 导出与离线解析 |
|---|---|---|---|---|---|
| **Linux pstore 核心** | 由前端决定：`kmsg_dump` 钩子（panic/oops/shutdown）、console 持续写、pmsg 用户态随时写 | 由后端决定（可插拔）；核心本身不存储 | 压缩 zlib deflate（可关）；**同一时刻只允许一个后端**；panic 路径用 trylock，抢不到就 **放弃本次转储** | 无（交给后端）；单次快照上限 `kmsg_bytes`（默认 10240 B，从日志尾部往前取） | `/sys/fs/pstore/` 文件（`<类型>-<后端名>-<序号>[.enc.z]`，0444 只读）；unlink = 调用后端 `erase()` |
| **ramoops（保留 RAM）** | `max_reason` 过滤的 kmsg_dump；console 前端注册为真 console（`CON_PRINTBUFFER\|CON_ENABLED\|CON_ANYTIME`），**不做 loglevel 过滤** | 保留 RAM（`mem_address`/`mem_size`/`mem_type`），区域布局 **dmesg → console → pmsg → ftrace** | **Reed-Solomon ECC**（块 128 B / 校验 16 B / poly 0x11d）；zone 头部 magic `"DBGC"`(0x43474244)；冷启动后可能残留垃圾，靠头部校验跳过 | 环形轮转 `dump_write_cnt = (dump_write_cnt+1) % max_dump_cnt`；**单条超 `record_size` 被截断丢弃**（`record->part != 1 → -ENOSPC`）；**计数器重启清零** | `/sys/fs/pstore/dmesg-ramoops-N`；systemd-pstore 归档到 `/var/lib/systemd/pstore/`（`Storage=`/`Unlink=`） |
| **pstore/blk + mtdpstore（裸 flash）** | 同 pstore 前端；额外提供 `panic_write` 回调 | 块设备 / MTD 裸 flash | **panic 中不能擦除、不能探坏块** → 正常路径预先维护 `badmap` 位图并保证至少一个已擦 zone；保留区 `-ENOMSG` 语义=`试下一个 zone`；块设备 panic 写需 `BLK_FEAT_POLL`+FUA，用静态 bio + `bio_poll()` 轮询（不能 `REQ_PREFLUSH`） | `kmsg_size` 默认 **64 KB**，须为 4 KB 倍数；MTD 要求分区 ≥ 2×record、eraseblock ≥ kmsg_size；多 chunk **覆盖最老** | 同 pstore 文件系统；`dmesg-pstore-blk-N` |
| **Android pmsg/logd/tombstone** | pmsg：liblog 每次写日志**同时**写 `/dev/pmsg0`（非 debuggable 构建仅 EVENTS/SECURITY 放行）；tombstone：进程崩溃即时 | pmsg → pstore 保留内存；logcatd → `/data/misc/logd/`；tombstone → `/data/tombstones/`；ANR → `/data/anr/`；DropBox → `/data/system/dropbox/` | pmsg 复用 ramoops 的 ECC；tombstone 环形复用文件名（`next_artifact_ = (next+1) % max`）；**无记录级 CRC**，`crash_dump` 直写 | tombstone **默认 32**、ANR **默认 64**；logpersistd.size 默认 256 MB、rotate_kbytes 默认 1024（rc 里兜底 2048，**不一致**）；DropBox 3 天 / 10 MB(20 MB userdebug) / 10%；BootReceiver 截断 `LOG_SIZE`=65536（LASTK=65536） | `logcat -L`（读 pstore）、`logcat -b crash/all`、`dumpsys dropbox`、`adb bugreport`（`FS/` 前缀打包 + `LAST KMSG`/`LAST LOGCAT` 段）；recovery-persist 转存 `/data/misc/recovery/last_log`/`last_kmsg` |
| **systemd-journald** | 每次日志写入即追加（不是崩溃触发）；`SyncIntervalSec=` 默认 **5 min**，但 **CRIT/ALERT/EMERG 无条件立即 fsync** | `/var/log/journal/<machine-id>/*.journal`（持久）或 `/run/log/journal/`（volatile）；`auto` = **看 `/var/log/journal` 目录存不存在** | **原地写 + header `state` 字段 + 状态翻转前后各一次 fsync**（`STATE_ONLINE`/`STATE_OFFLINE`/`STATE_ARCHIVED`）；**不是 rename 两阶段**；损坏/非干净关闭的文件改名加 `~` 隔离，只重试一次 | `SystemMaxUse=` 默认 10%、`SystemKeepFree=` man 写 15%（**代码实为 5%**，文档滞后）、`SystemMaxFileSize=` 默认 MaxUse/8（cap 128M）、`SystemMaxFiles=` 100、`MaxFileSec=` 1 month；**只删 archived，active 永不删**；vacuum 后**仍可能超标** | `journalctl`、`journalctl --verify`；归档文件 `journalctl -b -1` |
| **AUTOSAR DLT** | 应用/BSW 主动调用 Dlt API 发消息（**非崩溃触发**）；AUTOSAR 规范**不规定落盘** | 网络（到 DLT Viewer）为主；落盘靠实现层：COVESA offline trace（默认 file 1 MB / max 4 MB）或 offline logstorage（缓存 30000 KB + 文件） | **只有 8 位 MCNT 序号**做丢包检测；**无 CRC / 无 checksum**；溢出另有 `overflow_counter` | offline trace 按 FileSize 切、按 MaxSize 删最旧；logstorage 用 `FileSize`+`NOFiles`+`OverwriteBehavior`(`DISCARD_OLD` 默认 / `DISCARD_NEW`) | DLT Viewer 解析 `.dlt`；non-verbose 需外部 ID 表（变量名/单位/静态文本） |
| **车规 EDR** | **事件触发 + 冻结**：8 km/h / 150 ms 触发；time zero 取"约束算法激活 / 连续 delta-V 超阈 / 不可逆约束装置展开"最先者；**气囊展开事件锁存不可覆盖** | **非易失存储器**（法规明文要求掉电后仍保留） | 锁存（§563.9）+ 非易失（§563.5 定义、"retained after loss of power"）+ 碰撞后可读 ≥10 天（§563.10）；EU UN R160 5.3.5 明文写死掉电保留；铁路 ERMM 有 55g/100ms、750°C/60min 等抗坠毁指标 | US：最多 **2 个**事件（气囊展开事件优先保留）；EU UN R160：至少容纳 **3 个**事件，无空缓冲时 **FIFO 覆盖**；旧窗 5 s @2 Hz，新窗 20 s @10 Hz（2028-09-01 起分阶段，2031-09-01 全面） | US §563.12 只要求"商业可得工具"可读，不指定接口（事实标准 Bosch CDR 走 DLC）；EU 2022/545 Art.4 要求标准 DLC 串口，禁止无线/免解锁接口 |
| **ArduPilot DataFlash** | **周期**（按 loop 写）+ 事件（arm/disarm）；`LOG_DISARMED` 控制上电即记 | SD 卡文件（FAT）**或**板载 dataflash（块后端）**或** MAVLink 实时流 | 块后端每页有 `PageHeader{FilePage, FileNumber}` + `FileHeader{utc_secs}` + 末扇区格式版本字 `0x1901201B`；**`BLOCK_LOG_VALIDATE` 默认 0 → 默认无 CRC**；写缓冲满则**整条丢弃**并计 `_dropped` | 块后端=**环形缓冲**（`FinishWrite()` 检测"是否要擦掉自己的 header 扇区"→`chip_full` 停止）；文件后端=**删最老文件**（`LOG_FILE_MB_FREE`）；`LOG_MAX_FILES`=500 | MAVLink `LOG_REQUEST_LIST`/`LOG_REQUEST_DATA`（**带 ofs+count，90 B/包，天然分包+断点续传**，可按偏移补漏包）；`REMOTE_LOG_DATA_BLOCK` 流式（200 B/块 + seqno + NAK 重发）；离线 `DFReader.py`（0xA3 0x95 魔数 + 逐字节重同步）、`mavlogdump.py`、MAVExplorer |
| **Betaflight Blackbox** | **周期**（每控制循环迭代写一帧，采样率 `blackbox_sample_rate` 默认 1/4） | SD 卡（`LOGnnnnn.BFL`，FAT16/32，不支持 SDXC）**或**板载 flash（`flashfs`，无文件系统，`src/main/io/flashfs.c`） | **无长度字段、无 checksum、无 trailer**；靠 I 帧（约 32 ms 一个）做重同步锚点；解码器启发式校验（下一字节是否为合法帧类型）+ `loopIteration`/`time` 合理性；正常结束写 `'E',0xFF,"End of log",0x00`；flashfs **无任何持久化元数据**，靠二分查找找"最左整块已擦除块" | **不是环形**：写指针单调到顶即 `flashfsIsEOF()` → `BLACKBOX_STATE_STOPPED`，需手动 erase；SD 满同理 | **无文件级索引**；MSP `DATAFLASH_READ`（绝对地址+长度，天然分块，官方不推荐）/ **MSC 大容量存储模式**（推荐）；`blackbox_decode`（mmap + `memmem` 找起始标记 → 先解 header 建字段表 → 再逐帧解 predictor）输出 CSV；网页版每 4 个 I 帧建 chunk 索引支持任意时间点 seek |
| **工业（OPC UA / Telegraf / littlefs）** | 周期遥测 / 事件；store-and-forward 由投递失败触发重试 | 内存 buffer → 本地磁盘 WAL → 远端；littlefs metadata pair | **单调递增且会回绕的 SequenceNumber** + 窗口判定（OPC UA Part 14 §7.2.3）；littlefs 32-bit CRC + revision count（但**不含 ECC**）；Telegraf `buffer_disk_sync` 默认 true | Telegraf `metric_buffer_limit` 默认 10000，**溢出覆盖最旧**，且**不限制磁盘占用**；littlefs append/compaction/split | OPC UA 订阅/历史访问；Telegraf 每 output 一个 WAL，遗留 WAL 先排空**保序**；Ignition 交付成功才删 + quarantine |

---

## 可借鉴 / 应避免

> 我方约束复述：C11 嵌入式框架（Cortex-M4 级 MCU、片内 NOR flash）；器件层 flash 抽象要求写操作必须让出 CPU（**故 panic 上下文不能直接写 flash**）；已有零状态分区表 v2（注册表+句柄）与多后端 log 服务（独立日志线程）。目标：黑匣子语义（环形覆盖保新）、掉电安全、主动拉取导出。

### A. 「后端可插拔 + 保留内存」与我方「转存接缝」的对应关系（最重要）

pstore 的分层是本调研中最贴合我方的一个结构，可以逐层对齐：

| pstore 的层 | 职责 | 我方的对应物 |
|---|---|---|
| **前端（frontend）**：`PSTORE_TYPE_DMESG` / `CONSOLE` / `PMSG` / `FTRACE` | 定义"什么东西需要跨重启存活"，各自独立开关 | 我方的日志类别 / 源（系统日志、panic 现场、用户态事件） |
| **核心（`fs/pstore/platform.c`）** | 记录命名、文件系统、压缩、erase 语义、panic 并发策略 | log 服务的"记录管理层"：命名、CRC、序号分配、erase/回收语义 |
| **后端（backend）：ramoops / pstore-blk / efi-pstore** | 只负责"把一段字节写进某个持久介质并读回/擦除"，通过 `struct pstore_info` 的 `open/close/read/write/erase` 注册 | **我方的 flash 分区后端**（零状态分区表 v2 的句柄）+ **保留 RAM 后端**（panic 现场暂存区） |
| **转存者（systemd-pstore / recovery-persist / BootReceiver）** | 下次启动后把持久区读空、归档、**清空以便下次事件** | **我方的"转存接缝"**：复位后由日志线程把保留 RAM 区读空、写进 flash 分区、然后清空 RAM 区 |

**可直接借鉴的四点：**

1. **后端注册即"填一张函数指针表"**（`struct pstore_info` 的 `open/close/read/write/erase`），而不是让上层知道介质细节。我方的零状态分区表 v2 已经是"注册表 + 句柄"形态，天然可以在这之上再包一层"log 后端 ops"，让「保留 RAM 后端」和「flash 分区后端」实现同一组 ops。pstore 还明示：**若后端不提供 `write`，核心会填一个兼容函数转发到 `write_buf`**——即允许后端只实现最小集合，这对 MCU 上做分级后端很有用。
   [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/platform.c]

2. **记录命名带"类型 + 后端名 + 序号"三元组**（`dmesg-ramoops-0`）。`dmesg-ramoops-0` 里的 `ramoops` 是**后端名**，换后端就换名字——这样上层脚本能区分"这条是 RAM 暂存转存来的"还是"直接写 flash 的"。我方建议在记录头里同样带 `backend_id`，让导出侧能标注数据来源（尤其是复位后转存的那批）。
   [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/inode.c]

3. **"转存接缝"的正确语义 = 读走 → 归档 → 清空（三步）**。内核文档对 systemd-pstore 的三步描述（① 在 `/sys/fs/pstore` 定位；② 读并保存到 `/var/lib/systemd/pstore`；③ **清除 pstore 数据以备下次事件使用**）几乎可以直接翻译成我方的复位后流程。`pstore.conf` 的 `Unlink=` 默认 `true`，其设计目的被明确写成"让 pstore 保持近乎空的状态，以便下次内核错误事件有空间可用"——**清空不是可选项，而是保证"下一次崩溃还能记"的前提**。
   [来源: https://docs.kernel.org/power/shutdown-debugging.html]
   [来源: https://man.archlinux.org/man/pstore.conf.5.en]
   反面参照：ramoops 文档说计数器"重启时被清除，因此重启后的新 dump 会覆盖更早的那些"——**如果转存没来得及做就被新一次崩溃覆盖，现场就永久丢了**。我方转存必须在**尽早**的启动阶段完成（Android 用 BootReceiver 在 `ACTION_BOOT_COMPLETED` 做，recovery 甚至更早），并且要能容忍"上一次崩溃后还没来得及转存又崩了一次"这种情况——建议在保留区里用**多条 slot**（像 ramoops 的 `max_dump_cnt` 那样）而不是单条，让连续两次崩溃都有记录。

4. **ramoops 的 zone 有效性协议值得原样搬到保留 RAM 区**：头部含 magic `"DBGC"`(0x43474244) + `start` + `size`，四条分支（空缓冲 / 无效 / 有效保留 / 全新 zap）逻辑清晰；**头部不带时间戳，"是不是新的一次启动"完全由 magic + size/start 合法性决定**。这正好解决我方"复位后无法确定保留区里是上次现场还是上电随机值"的问题。再叠加 **Reed-Solomon ECC**（128 字节块 / 16 字节校验，可纠正 + 可报"不可纠错块数"）应对看门狗复位后的 RAM 半损坏——ramoops 文档明确 ECC 的适用场景就是"硬件复位（如看门狗）之后 RAM 可能有些损坏，但通常可以恢复"。
   [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/ram_core.c]
   [来源: https://docs.kernel.org/admin-guide/ramoops.html]

### B. 「panic 上下文不能直接写 flash」——内核给出了成文的规则

pstore/blk 文档列出的 6 条硬性约束，几乎逐条命中我方场景，可作为"保留 RAM 暂存区写入代码"的检查清单：
1. **不能分配任何内存**（init 时就分配好）；
2. **必须轮询，不能中断驱动**，延时不得睡眠；
3. **不能拿任何锁**——"你被允许打破所有锁"；
4. 用 CPU 拷贝，避免 DMA；
5. 直接控制寄存器，init 时完成映射；
6. 状态不明时重置设备。
[来源: https://docs.kernel.org/admin-guide/pstore-blk.html]

我方的设计可直接据此定型：**panic 路径只做"CPU 拷贝 + ECC 计算 + 一个 magic/size 头"，完全不碰 flash 驱动**；flash 写入一律交给日志线程在复位后完成。这与 pstore 把 ramoops（panic 路径唯一允许的写）和 pstore-blk（需要驱动物理设备）分开的思路一致。

更进一步，**mtdpstore 在裸 flash 上的三条"panic 不能做"清单**，对我方 flash 后端在"复位后转存"路径（虽是线程上下文，但仍面对掉电）的设计极具参考价值：
- **panic 时不能擦除** → 所以正常路径要**保证至少一个已擦 zone 待命**（`mtdpstore_security()` 的注释 "As there is no erase for panic case, we should ensure at least one zone is writable."）；
- **panic 时不能探坏块** → 所以正常路径**预先维护 `badmap` 位图**；
- **擦除要"懒"**：只有整个 block 都空闲时才擦，否则记入 `rmmap` 等 flush 时"读出有效数据 → 擦块 → 写回"（注释 "Avoiding over erasing, do erase block only when the whole block is unused."）。这条**对片内 NOR 的磨损均匀与写放大控制是直接可用的策略**。
[来源: https://raw.githubusercontent.com/torvalds/linux/refs/heads/master/drivers/mtd/mtdpstore.c]

以及一条对齐我方"写操作必须让出 CPU"的旁证：内核在块设备上加 `panic_write` 时，明确说**通用块写路径不可用，因为"会睡眠"**，只能改用静态预留 bio + `REQ_POLLED|REQ_NOWAIT` + `bio_poll()` 轮询，并且**不能用 `REQ_PREFLUSH`**（flush 请求不提供可轮询的 cookie）。这与我方"flash 写必须让出 CPU → panic 上下文不能写 flash"是同一个物理约束的两种表述。同时注意作者明确说这是"**不是**驱动原生 panic 钩子的替代品"——即内核界也不认为轮询写是理想解，只是 best-effort。
[来源: https://patchew.org/linux/20260819105416.24436-1-knirmal@nvidia.com/]

### C. 记录格式：三种成熟范式，建议选"自描述 + 关键帧"这一支

- **ArduPilot 范式（推荐主结构）**：3 字节记录头（`0xA3 0x95` + `msgid`）+ **FMT 自描述消息**（type / length / name / format / labels），FMT 自身用 `msgid = 128` 且"必须保持 128"。**导出工具不需要知道任何编译期信息就能解出全部字段名与类型**。我方可直接照搬：在 flash 分区开头固定写一条"格式表记录"，列出本固件版本下所有记录类型的字段布局；解析侧先读表再解流。单位/倍数用独立的 `UNIT`/`MULT` 消息（并且官方明确警告"不要从单位名推断缩放"）——我方也应把缩放因子显式写进格式表而不是靠字段名约定。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/LogStructure.h]
  [来源: https://ardupilot.org/dev/docs/code-overview-adding-a-new-log-message.html]
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/README.md]
- **Betaflight 范式（推荐压缩支路）**：**I 帧（自包含关键帧）+ P 帧（差分/预测帧）**，predictor 的作用是"把待编码值拉到尽量接近 0"再走可变长编码（高位 bit 为续字节标志，有符号走 ZigZag），实测约 **28 字节/帧 @900Hz ≈ 25 KB/s**。关键设计是 I 帧的插入注释："Write a keyframe every blackboxIInterval frames so we can resynchronise upon missing frames"——**I 帧的存在理由不是压缩率，而是"丢字节后可重同步"**。我方若在 MCU 上做压缩，必须同时保留这个"重同步锚点"语义，否则一次损坏会污染其后所有记录。
  [来源: https://betaflight.com/docs/development/Blackbox-Internals]
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox_encoding.c]
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.c]
- **两遍式解析（header 先行）**：Betaflight 的 `blackbox_decode` 流程是 `mmap` 全文 → `memmem` 找起始标记 → **先跑 header 循环建立字段表**（并把 I 帧的名字/符号表 `memcpy` 给 P 帧）→ **再跑 payload 循环**，按帧类型字符分派、按 `encoding[i]` 读取、`applyPrediction()` 加回预测值 → 回调逐帧写 CSV。**"先建表、再解流"应作为我方导出/离线解析工具的标准结构。**
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-tools/master/src/parser.c]

### D. 可靠性手段：把"序号"和"校验"分开看

- **序号（检测丢包/乱序）**：OPC UA Part 14 §7.2.3 给出了成熟的**回绕序号窗口判定**公式（`(收到 − 1 − 上次已处理) mod 2^N`，下界 `2^(N−2)`、上界 `2^N − 2^(N−2)`）。我方的记录序号如果会回绕（环形缓冲必然回绕），应照此实现，而不是简单比大小。DLT 的 8 位 MCNT 是简版（"可以一定程度识别丢失的消息"），但**DLT 没有 CRC**——这提醒我们：序号解决的是"连续性"，CRC 解决的是"完整性"，**两者不可互相替代**。
  [来源: https://reference.opcfoundation.org/Core/Part14/v105/docs/7.2.3]
  [来源: https://www.autosar.org/fileadmin/standards/R4.1.3/CP/AUTOSAR_SWS_DiagnosticLogAndTrace.pdf]
- **CRC/ECC（检测与纠正位翻转）**：littlefs 用 32-bit CRC + metadata pair 双块 + **revision count**（"原子性 = redundancy and error detection"），但明确"**littlefs 本身不提供 ECC**"。ramoops 则提供**可纠错的 Reed-Solomon**。对我方的启示是分两级：**记录级 CRC32（必做，检测）→ 保留 RAM 区 ECC（可选，纠正看门狗复位后的半损坏）**。注意 ramoops 只对 RAM 区上 ECC，对 flash 后端的 pstore/blk 反而**明确不建议压缩**（因为头部插在数据第一行），说明"不同介质用不同强度的保护"是有先例的。
  [来源: https://raw.githubusercontent.com/littlefs-project/littlefs/master/DESIGN.md]
  [来源: https://docs.kernel.org/admin-guide/pstore-blk.html]
- **"干燥收尾"标志（低成本高价值）**：Betaflight 正常 disarm 时写 `'E', 0xFF, "End of log", 0x00`，掉电时这条不会被写入——**用一个字节判断"上次是正常结束还是异常掉电"**。我方应在每条记录或每个日志会话末尾写一个廉价的"封口"标记（例如会话头的 `sealed` 计数或末尾哨兵），让启动时能立刻区分"正常关机"和"掉电"，从而决定是否需要走恢复/转存流程。**这条比 CRC 更便宜，且能直接驱动状态机。**
  [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/blackbox/blackbox.c]
- **状态字段（journald 的教训）**：journald 判定"文件是否干净关闭"**唯一**依据是 header 的 `state` 字段（`STATE_ONLINE`/`STATE_OFFLINE`/`STATE_ARCHIVED`），且**状态翻转前后各做一次 fsync**。它为此踩过的坑值得警惕：早期版本在轮转时直接把文件置 `STATE_ARCHIVED`，而 `set_offline()` 在 state ≠ ONLINE 时会短路，导致**被轮转的文件从不被 fsync**——一个"看起来对"的状态机能静默地让某类文件永远不落盘。我方若引入类似的"分区状态字段"，**必须对所有状态迁移路径逐一验证"该落盘的都落盘了"**。
  [来源: https://github.com/systemd/systemd/blob/main/src/shared/journal-file-util.c]
  [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-file.c]

### E. 轮转与限额：三种"满了怎么办"，我方的"环形覆盖保新"有法规与工程双重支持

三种策略在本调研中都出现了，且各有明确适用面：

1. **环形覆盖最老（= 我方目标语义）**——ramoops 的 `dump_write_cnt` 轮转、pstore/blk 的 chunk 覆盖、ArduPilot 块后端（注释直说 "we're a ring buffer and never run out of space"）、Telegraf `metric_buffer_limit` 溢出覆盖最旧、**UN R160 5.3.4"无空缓冲时按 FIFO 覆盖"**、49 CFR §229.135 铁路"记录最近 48 小时"（即时间窗覆盖）。
   [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/ram.c]
   [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_Block.cpp]
   [来源: https://eur-lex.europa.eu/legal-content/EN/TXT/HTML/?uri=CELEX:42021X1215]
   [来源: https://www.law.cornell.edu/cfr/text/49/229.135]
2. **停止记录（保旧）**——Betaflight（`flashfsIsEOF()` → `BLACKBOX_STATE_STOPPED`，需手动 erase）、DLT logstorage 的 `DISCARD_NEW`。**这个策略在竞速/飞行场景被证明是"用户可接受"的，因为旧数据比新数据值钱**；但我方的黑匣子语义（保新）与之相反，需要明确记录这个取舍。
   [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/io/flashfs.c]
   [来源: https://covesa.github.io/dlt-daemon/doc/dlt_offline_logstorage.md]
3. **删最老的单位（文件/block）**——ArduPilot 文件后端（`LOG_FILE_MB_FREE`）、journald vacuum（只删 archived）、DLT offline trace（`OfflineTraceMaxSize`）。注意 journald 的教训：**vacuum 只删非活动文件，所以占用仍可能超过限额**（man 页明确承认）。
   [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_File.cpp]
   [来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]

**"保新"必须处理的一个边界**：ArduPilot 官方文档写明"**If there is only one file on the flash when space runs out, logging is stopped instead**"——即连它也不敢把最后一份数据擦掉。我方的环形覆盖必须保留同一条底线（**至少留一条最近记录**），否则"保新"会退化成"什么都留不下"。
[来源: https://ardupilot.org/copter/docs/common-downloading-and-analyzing-data-logs-in-mission-planner.html]

**限额应该"随可用空间自适应"**：journald 的 `burst_modulate()`（`burst = burst * (log2(available)-16) / 4`，≤1MB ×1、16MB ×2、256MB ×3、4GB ×4）以及 `SystemKeepFree`（**保证留出空闲空间，不是占满**）是很有价值的两条思路——**在 NOR flash 上做"预留水位线"能显著降低写放大与擦除阻塞**。
[来源: https://github.com/systemd/systemd/blob/main/src/journal/journald-rate-limit.c]
[来源: https://raw.githubusercontent.com/systemd/systemd/main/man/journald.conf.xml]

### F. 导出与主动拉取：ArduPilot 的协议是全篇最值得照抄的一条

- **`LOG_REQUEST_DATA` 带 `ofs` + `count`，天然分包 + 断点续传**；飞控侧就是 `_log_data_offset = packet.ofs; _log_data_remaining = size - ofs;`，并把剩余长度截断到 `packet.count`。**我方的"主动拉取导出"应直接采用这个形态**：请求里带偏移和长度，设备只需从该偏移读一段返回，**导出的状态全部在主机侧**——这对 MCU 内存极度不友好（设备不需要维护"当前导出位置"）。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_MAVLinkLogTransfer.cpp]
- **"完成"的判据必须是"收满 `LOG_ENTRY` 报告的 `size`"，而不是"某一包返回了 0 字节"**——官方明确这是唯一可靠判据。这条对我方是直接的实现准则。
  [来源: https://ardupilot.org/dev/docs/mavlink-log-download.html]
- **同一时刻只允许一个未完成的下载请求**（多余请求静默忽略，不能流水线化），**但丢包可以事后用另一个请求只补那个偏移区间**。这是一个"低内存 + 可恢复"的良好折中。
  [来源: https://raw.githubusercontent.com/ArduPilot/ardupilot/master/libraries/AP_Logger/AP_Logger_MAVLinkLogTransfer.cpp]
- **小块 + 显式 seqno + NAK 重发**的流式路径（`REMOTE_LOG_DATA_BLOCK`，200 字节/块）。我方的拉取协议若走"推送"模式，应带 seqno 与 NAK。
  [来源: https://ardupilot.org/dev/docs/mavlink-log-download.html]
- **随机访问靠"关键帧索引 + 状态快照"**：Betaflight 网页版的 `buildIntraframeDirectories()` 每 4 个 I 帧切一个 chunk，并快照 `initialIMU`/`initialSlow`/`initialGPSHome`/`initialGPS`，注释 "To enable seeking to an arbitrary point in the log without re-reading anything that came before"。**我方的 flash 分区应在写入时就把"关键帧/会话起始偏移表"持久化**，否则导出时要全分区扫描。
  [来源: https://raw.githubusercontent.com/betaflight/blackbox-log-viewer/master/src/flightlog_index.js]
- **解析侧必须能容忍残缺尾部**：pymavlink `DFReader.py` 逐字节扫描重同步（注释 `"we can have garbage at the end of an APM2 log"`、`"out of data - can often happen half way through a message"`），并对块日志尾部空白做专门处理（`"up to 249 bytes of trailing space"`）。**"尽力解析可解析的部分"应作为我方导出工具的硬性要求**——黑匣子的尾部必然是残缺的。
  [来源: https://raw.githubusercontent.com/ArduPilot/pymavlink/master/DFReader.py]

### G. 应避免 / 需要警惕的坑

1. **不要指望"临时文件 + rename"来保原子性**——journald 恰恰**不是**这么做的（该说法在 systemd 源码中不存在），它用的是"原地写 + state 字段 + 状态前后各一次 fsync"。而且 journald 的 `.journal~` 是**隔离损坏文件**的产物，不是提交机制。我方在 NOR flash 上更没有 rename 语义，应明确采用"**写入 → CRC 校验 → 提交标记（commit word）**"的顺序，且 commit 标记必须是**最后一个**写下的字节。
   [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-file.c]
   [来源: https://github.com/systemd/systemd/blob/main/src/shared/journal-file-util.c]
2. **不要忽略"计数/状态在重启后清零"带来的覆盖**：ramoops 的计数器重启清零，新 dump 覆盖旧 dump。我方若有"本次上电的会话计数"，**复位转存必须抢在任何新写入之前完成**，否则现场会被自己覆盖。
   [来源: https://docs.kernel.org/admin-guide/ramoops.html]
3. **不要硬编码记录名/偏移**：pstore 的文件名含**后端名**（`dmesg-ramoops-0` vs `dmesg-pstore-blk-0`）。我方的导出工具应通过格式表/分区表解析，而不是硬编码。
   [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/inode.c]
4. **不要默认"大 record_size 就能存下完整现场"**：ramoops 的 LKML 讨论指出内核不愿给出 >2MB 的连续分配，4M `record_size` 直接失败、2M 会丢一半 dmesg，且**讨论没有产出解决方案**。我方的现场暂存区大小必须按"最坏情况下的现场长度"实测确定，并**默认接受截断**（ramoops 明确是 `record->part != 1 → -ENOSPC`，即超出即丢，不续写）。
   [来源: https://www.spinics.net/lists/linux-fsdevel/msg209893.html]
5. **不要用"元数据每写必更新"换取可恢复性**：Betaflight 明确拒绝写卷头，理由是 "keeping a header up to date while logging would incur more writes to the flash, which would consume precious write bandwidth and block more often."。我方的分区表 v2 是"零状态"的，**这一取舍方向与 Betaflight 一致，应保持**——恢复靠扫描/魔数，而不是靠每次都更新的头。
   [来源: https://raw.githubusercontent.com/betaflight/betaflight/master/src/main/io/flashfs.c]
6. **不要在 panic 路径上做任何可能失败的重试或加锁**：pstore 的策略是 `raw_spin_trylock_irqsave()` 抢不到就**直接放弃本次转储**并打印原因。宁丢一条，不卡死系统。
   [来源: https://raw.githubusercontent.com/torvalds/linux/master/fs/pstore/platform.c]
7. **不要忘记"介质本身不保证在途写"**：SD 卡的 SPOR 只保护"data at rest — not data in flight"，"the one write in progress at the instant power is cut is not guaranteed to finish"；工业 SSD 靠电容解决。片内 NOR flash 的页写同样可能在掉电时半途而废，**因此每条记录必须有 CRC + 提交标记，且启动时必须能识别"半写"并回退到上一条**。
   [来源: https://www.atpinc.com/blog/iot-gateway-edge-storage-industrial-sd-card]
8. **不要在跨启动的全局序上偷懒**：systemd journal 的 seqnum 每次启动重置为 0，跨 boot 强行做全序会产生 A<B<C<A 的环。我方若用 (boot_id, seq) 二元组，**导出时必须保留 boot_id，"最新记录"的判定也必须以 boot 序为准**。
   [来源（社区讨论）: https://lists.freedesktop.org/archives/systemd-devel/2017-December/039971.html]
9. **限额配置的默认值可能有文档滞后**：journald 的 man 页把 `SystemKeepFree` 默认写成 15%，但自 v243 起代码实为 5%。**涉及默认值的结论必须查源码而非文档。**
   [来源: https://github.com/systemd/systemd/blob/main/src/libsystemd/sd-journal/journal-file.c]
10. **车规 EDR 的"冻结/锁存"语义值得借一层**：§563.9 要求气囊展开事件的内存 "must be locked to prevent any future overwriting"，EU UN R160 5.3.4 则规定无空缓冲时 FIFO 覆盖。**两者结合的正确解读是：默认环形覆盖保新，但"标记为重要"的记录应被豁免覆盖**（我方可加一个 `pinned` 位，或保留一个专用的"最后一次致命故障"slot 不受环形轮转影响）。
    [来源: https://www.law.cornell.edu/cfr/text/49/563.9]
    [来源: https://eur-lex.europa.eu/legal-content/EN/TXT/HTML/?uri=CELEX:42021X1215]

---

## 存疑与未证实

以下内容**未能证实或已被证伪**，不应作为设计依据：

1. **已证伪**：systemd-journald 持久化**不是**"rsync 式落盘 vs 直接写"的二选一，源码中**不存在**"先写 `system@*.journal~` 临时文件再 rename"的提交路径。`.journal~` 是**隔离损坏文件**的产物（`journal_file_dispose()` 改名而来），不是提交机制。systemd 主仓检索 `rsync` 无相关内容。
   [来源: https://github.com/systemd/systemd/search?q=rsync]
2. **已证伪**：不存在 `https://source.android.com/docs/core/architecture/logging` 与 `.../logd` 页面（实测 HTTP 404）。
3. **已证伪**：内核中不存在 `drivers/soc/qcom/pmsg.c`；pmsg 是通用 `fs/pstore/pmsg.c`。
4. **已证伪**：Android 不存在名为 `persistent` 的 log buffer id（全集为 main/radio/events/system/crash/stats/security/kernel）。
5. **已证伪**：Android logd 的落盘文件名**不是** `log.<time>`，而是 `/data/misc/logd/logcat` + `logcat.NNN` + `logcat.id`。
6. **已证伪**：ArduPilot 的记录头是 **3 字节**（0xA3 0x95 + msgid），不是 2 字节。
7. **已证伪**：ArduPilot 与 Betaflight **都没有**"块头含'第一次写该扇区的序号'"这种字段。ArduPilot 块后端最接近的是 `PageHeader{FilePage, FileNumber}` + `utc_secs` + 64 位排序键 `(FileNumber<<32)|FilePage`；Betaflight flashfs **完全没有** sector header / magic / 序号 / state 字段。
8. **已证伪**：`FLASHFS_SECTOR_SIZE` 宏在 Betaflight 全仓检索 0 命中，不存在。
9. **已证伪**：`LOG_MIN_MB_FREE` 参数名不存在，实际是 `LOG_FILE_MB_FREE`。
10. **已证伪**：`blackbox_logging` 参数名未证实存在，实际是 `blackbox_mode`。
11. **已证伪**：Betaflight 官方文档 URL `https://betaflight.com/docs/development/Blackbox` 与 `https://betaflight.com/docs/wiki/guides/current/blackbox` 均 404。正确入口是 `https://betaflight.com/docs/wiki/guides/current/Black-Box-logging-and-usage` 与 `https://betaflight.com/docs/development/Blackbox-Internals`。
12. **未证实**：`blackbox_decode` 在 `betaflight/blackbox-log-viewer` 仓的说法不成立，它在独立的 `betaflight/blackbox-tools` 仓。
13. **未证实**：tombstone「从某一 Android 版本起默认 proto 化」的具体版本分界。
14. **未证实**：`/data/anr/traces.txt` 在现行 AOSP 中仍存在。
15. **未证实**：`pstore.conf(5)` 的 `ProcessFull=` 选项——在 systemd 261.3 的该手册页中**不存在**。
16. **未证实**：systemd journald 引入 rate limiting 的动机是"当年被日志风暴打爆"——未找到一手 commit 说明或设计文档，只能确认该选项 2012 年即已存在。
17. **未证实**：COVESA dlt-daemon 的 `DLT_OFFLINE_LOGSTORAGE` 符号——官方文档与头文件中均未出现。
18. **未证实**：`dlt-daemon` 的 `-c` 是"缓存开关"——实为"加载替代配置文件"。
19. **未证实**：车规 EDR 的**温度要求**——已核 49 CFR Part 563 全部 12 条、2008 年复议答复与 UN R160 Annex 4，均无温度条款；NHTSA 反而明确拒绝了对碰撞后车辆环境条件的保护性要求。另，"73 FR 21808, April 2008" 这一引用未能证实。
20. **未证实**：`pstore-blk` 文档页**没有**给出与 ramoops 的直接对比（ramoops 只出现在站点导航里）。
21. **未证实**：`ramoops` 的 `max_reason` 3–7 的取值范围说明——DT 绑定文件中**没有正式 enum 块**，只有描述；`include/linux/kmsg_dump.h` 的枚举只到 `KMSG_DUMP_MAX = 5`。
22. **未证实**：社区文档称 `console-ramoops` "受 printk level 控制，可能不含全部内容"——**与源码不符**：`fs/pstore/platform.c` 中 `pstore_console_write()` 不做任何 loglevel 过滤，且结构体未设 `.level` 字段。
23. **未证实**：`efi-pstore` 的独立内核文档页——`Documentation/admin-guide/efi-pstore.rst` 在当前 mainline 中返回 404（疑似已移除），`docs.kernel.org/5.15/admin-guide/efi-pstore.html` 亦 404。efi-pstore 的行为只从内核 git 提交史间接得到（变量名格式 `dump-type0-<id>-<count>-<ctime>`，`QueryVariableInfo()` 预检查剩余空间），**未从官方文档证实**。
24. **未证实**：LKML 上 "pstore/ramoops - why only collect a partial dmesg?"（2021-12 ~ 2022-01）的讨论**没有产出补丁或结论**。
25. **已证伪（弱）**：`drivers/soc/qcom/pmsg.c` 不存在；qcom 侧与 pstore 相关的只有尚未进入主线的 `qcom_pstore_minidump` RFC 系列。
26. **未证实**：`reserve_mem` 命令行参数的稳定性——ramoops 文档自己就说它"可能并不总是在同一位置分配内存，不能依赖它"，要求实测并"当作 best-effort"。
27. **未证实**：IEC 62625-1 之外，工业界并无统一强制的"工业黑匣子"标准；本节的工业部分主要来自厂商工程实践（Ignition / Telegraf / littlefs / SD 卡厂商白皮书）与铁路法规，**属工程惯例而非标准**，引用时需注意来源类型。
28. **未证实**：IEEE 1482.1-2013 已于 2024-03-21 失效（inactive），引用该标准需谨慎。
