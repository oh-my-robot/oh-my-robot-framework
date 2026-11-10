# 存储子系统差距分析（对照成熟开源框架）

> 版本：v1（2026-09-14）
> 状态：分析稿——对照业界成熟存储子系统评估本工程 store 子系统与 flash 驱动现状，产出差距清单与优先级建议。本文不构成决策，收敛项另行提纯 ADR。
> 范围：器件访问层（FlashDev）+ 分区表（partition）+ 后端适配（BSP flash）+ 存储上层服务现状
> 基准：Zephyr（`drivers/flash` + NVS/FCB/settings + LittleFS/FatFs）、ESP-IDF（`esp_flash` + partition + NVS + wear_levelling）、RT-Thread（SFUD + FAL + DFS）、Linux（MTD + UBI/JFFS2）、LittleFS
> 关联：`docs/boot_ota/storage_landscape.md`（存储形态锚点与 ER-xx 留口）、`lib/drivers/docs/partition_table_design.md`、`lib/drivers/docs/log_persist_design.md`、`docs/boot_ota/flash_dev_design.md`
> 行号口径：本文行号对应 `logger_store` 分支 rebase 至 `upstream/integration@8b9fde2` 后的状态（2026-09-14）

---

## 1. 口径与范围

1. 本文字义上的"差距"= **能力与结构差异**，不含对已有实现质量的评价——已达标与优于业界之处单列 §4，避免只列缺点造成的误判。
2. 对照基准取**成熟框架的收敛形态**，非某一具体项目：同一结论尽量给出两个以上项目的同构佐证。
3. 证据来源 = **本仓代码实测**（文件 + 行号锚定），不采信设计文档的宣称；设计文档已声明"未落地"的部分计入差距，不重复计数。
4. 差距项编号 `G-xx`（Gap），优势项 `S-xx`（Strength），与既有 P-xx / K-xx / ER-xx / Q-xx 编号体系不重叠。

---

## 2. 现状快照

### 2.1 代码清单（实测行数）

| 层 | 文件 | 行数 | 状态 |
|---|---|---|---|
| 器件访问 | `lib/drivers/include/drivers/peripheral/flash/pal_flash_dev.h` | 294 | 接口定稿 |
| 器件访问 | `lib/drivers/src/peripheral/flash/hal_flash.c` | 589 | 实现落地 |
| 器件访问 | `lib/drivers/src/peripheral/flash/flash_domain.c` | 35 | 实现落地 |
| 分区语义 | `lib/drivers/include/drivers/storage/partition.h` | 68 | 接口定稿 |
| 分区语义 | `lib/drivers/src/storage/partition.c` | 234 | 实现落地 |
| 后端适配 | `platform/bsp/vendor/STM32/STM32F4/adapters/flash/bsp_flash_f4.c` | 336 | **唯一真实后端** |
| host 验证 | `samples/host/flash_dev_test/`（含 `flash_sim.c` 仿真后端） | ~1350 | 未接入 CI |
| host 验证 | `samples/host/partition_test/partition_test.c` | 248 | 未接入 CI |
| 存储上层服务 | — | **0** | **空缺** |

### 2.2 分层形态

```
消费面服务    配置 KV / 日志持久化 / 文件系统        [ 空缺 ]
──────────────────────────────────────────────────────────
管理层        磨损均衡 / 环形轮转 / 事务化            [ 空缺 ]  ← ER-4 后半
──────────────────────────────────────────────────────────
分区语义      partition（name-only 查询 + 便捷读写擦）  ✅
──────────────────────────────────────────────────────────
器件访问      FlashDev（几何校验 + 执行域 + 请求槽）    ✅
──────────────────────────────────────────────────────────
后端适配      bsp_flash_f4（STM32F4 片内，唯一）
```

**形态判读**：本工程已建立**器件抽象层与分区语义层**（等价于 Linux 的 MTD 核心 + 分区），但**管理层与消费面整体空缺**。即：具备"能正确读写 flash 器件"的能力，尚不具备"能可靠存放数据"的能力。

---

## 3. 差距清单

### 3.1 第一梯队：整层缺失（结构性）

#### G-01 管理层 / 磨损均衡层不存在

- **现象**：`storage_landscape.md` 自标 ER-4「磨损环形仍待存储/日志层（步骤④）」。全仓无任何磨损均衡、环形轮转、坏块管理代码。
- **业界对照**：成熟框架的工程投入主要集中在这一层——UBI 的 PEB 管理、Zephyr NVS 的条目重定位、LittleFS 的元数据 copy-on-write、EasyFlash 的扇区轮转。
- **影响**：任何需要在有限擦写寿命介质上反复写同一逻辑位置的需求（配置项、计数器、状态字）目前**无正确解法**——只能由业务层自行实现轮转，重复劳动且易错。
- **备注**：日志型负载的顺序环形写入天然摊薄磨损（`log_persist_design.md` 裁决 8 已论证），但**非顺序写场景不适用**。

#### G-02 消费面服务全缺

- **现象**：无文件系统、无 KV/配置存储、无序列号/标定存储。日志持久化仅有设计稿（`lib/drivers/docs/log_persist_design.md`，状态"讨论稿"，裁决 A/B/C/D 已确认、E/F 挂起于 log 服务 per-backend 模块过滤前置）。
- **业界对照**：Zephyr 提供 NVS + FCB + settings + LittleFS/FatFs 一组服务；ESP-IDF 提供 NVS + wear_levelling + 三种 FS；RT-Thread 提供 FAL + DFS + EasyFlash。
- **影响**：存储能力无法被业务直接消费，每个新需求都要从零搭一层。

#### G-03 掉电事务化原语缺失（且为主动外推）

- **现象**：`pal_flash_dev.h`（掉电语义段）明确声明「擦/写中途掉电目标区状态未定义……本层不提供恢复原语；恢复由上层按自身事务规律处理」。
- **业界对照**：该外推方向**与 Linux MTD 一致、是正确的分层**；但成熟框架把义务接收方一并实现——NVS 每条记录带长度 + CRC、LittleFS 元数据 CoW、UBI 原子卷更新。
- **影响**：义务已正确归属，但**接收方不存在**，故当前净缺口为实。

### 3.2 第二梯队：器件层能力缺口

#### G-04 后端覆盖单一，SPI NOR 缺席

- **现象**：唯一真实后端为 `bsp_flash_f4.c`（STM32F4 片内）。全仓无外部 SPI NOR 后端（`find *nor*/*w25*` 仅命中 STM32 HAL 自带文件）。几何由 `#if defined(STM32F427xx)` 编译期硬编码（`bsp_flash_f4.c:29-52`）——对片内 flash 合理，**对外部芯片无法规模复制**。
- **业界对照**：SFUD 以 **JEDEC ID 运行时探测**支持任意 SPI NOR 免驱接入；Zephyr 有 `jedec,spi-nor` 通用驱动 + devicetree 几何；ESP-IDF 支持片内/片外统一抽象。
- **影响**：MCU 上最常见的外部存储形态（SPI NOR）完全无法接入，多器件场景不可验证。
- **另**：ER-2（NAND 独立面）、ER-3（随机器件族 EEPROM/FRAM）、ER-5（块设备族 SD/eMMC）均为留口未实现——此三项**有明确留口设计，按触发条件驱动，不计为失控差距**。

#### G-05 ops 能力面仅三项

- **现象**：`FlashOps = {read, write, erase}`（`pal_flash_dev.h` FlashOps 段）。全仓实测缺失：`sync`/`flush`、`is_erased`/blank check、`chip_erase`、写保护/区域锁、page layout 查询、erase suspend/resume、电源管理（sleep/DPD）、统计/健康。
- **业界对照**：

  | 能力 | Zephyr | ESP-IDF | 本工程 |
  |---|---|---|---|
  | chip erase | `flash_chip_erase` | ✅ | ❌ |
  | 写保护 | `write_protection_set` | region protect | ❌ |
  | page layout 查询 | `get_page_layout` | ✅ | ❌（`pageSize` 字段定义了但**无任何消费点**，F4 后端置 0） |
  | erase suspend | 部分后端 | — | ❌ |
  | 统计/健康 | `flash_stats` op | — | ❌ |

- **影响**：
  1. **erase suspend 是硬能力**——外部 NOR 擦除耗时 ms~s 级，无 suspend 则擦除期间读全阻塞（与 G-09 叠加）；
  2. 无 blank check → 只能靠"读回比较 `erasedValue`"自建；
  3. `pageSize` 定义了却无消费点，说明**无页聚合写入路径**——SPI NOR 的 256B page program 是主要性能杠杆。

#### G-06 无句柄化 area API，且分区模块为单例

- **现象**：
  1. `om_partition_read` 每次 I/O 的完整路径为 `om_partition_lookup`（strcmp 线性查找）→ `flash_find` → `device_find`（链表遍历 + 名字比较）→ `flash_read`（`partition.c` 便捷层三段）——**热路径每次读写两次名字查找**；
  2. `static const OmPartitionEntry *s_table` / `s_count`（`partition.c:20-21`）为模块全局唯一，**只能注册一张表**；
  3. 无枚举 API。
- **业界对照**：Zephyr `flash_area_open()` 一次解析出 `{dev, offset, size}` 句柄，之后零查找；MTD 以 `mtd_info` 句柄承载。多实例在 Zephyr（DT 多实例）/ESP（`esp_partition_register_external`）均支持。
- **影响**：日志写入等热路径承担无谓查找开销；boot+app 双栈共存、多板型并存、可插拔介质等场景被架构性锁死。

#### G-07 分区表非单一事实源（表在代码里）

- **现象**：分区表为 C 数组（`partition.h` 的 `OmPartitionEntry`），由 `om_partition_register()` 运行期交入。bootloader 与 app **各持一份副本**。表本身无版本/魔数/条目数字段，无 on-media 表示。
- **业界对照**：三条成熟路径——
  1. **ESP-IDF：分区表烧进 flash**（0x8000），引导程序/应用/主机工具读同一份介质事实源；
  2. **Zephyr：devicetree 构建期生成** + 链接脚本联动；
  3. **Linux：DT / `mtdparts` 命令行**。
- **影响**：`partition_table_design.md` §2.1 自陈的动机是"消除消费者错位踩踏"，但当前解法（共享表常量）仅**缓解**而非消除——两份副本仍需人工同步，漂移是结构性的。

### 3.3 第三梯队：执行模型与业界主流的分歧

#### G-08 器件层异步优先 vs 业界同步优先

> **✅ 已关闭（2026-09-20，ADR-0027）**：异步执行域（`FlashDomain` / worker 线程 / 请求队列 / `OM_FLASH_QUEUE_DEPTH` / 三个 async API）已整体删除，器件层改为"调用者上下文同步 + 每设备睡眠互斥量"，后端让出 CPU 升为**承重条款**。以下现象描述保留为**修复前的取证记录**；真机证据（心跳活性、并访读等锁）见 ADR-0027 的验证状态段。

- **现象**：本工程器件层以异步为默认（`flash_write_async` / `flash_erase_async`），异步执行体为**每设备一个 worker 线程**（`flash_domain.c`），同步路径 `flash_write`/`flash_erase` 亦经队列提交后阻塞等待。相关成本：
  - 每设备 3072 B 线程栈（`hal_flash.c`，`flash_domain_init(..., 3072u)`），**随设备数线性增长**；
  - 固定 2 槽队列（`pal_flash_dev.h:54-55`，`OM_FLASH_QUEUE_DEPTH 2u`）——第 3 个并发提交者收 `OM_ERR_BUSY`；
  - 特殊规则：同域同步等待 = 自锁拒绝（`flash_sync_wait` 段）。
- **业界对照**：**Zephyr / ESP-IDF / SFUD / LittleFS / Linux MTD 的设备层 API 全部为同步阻塞 + 内部锁，零线程**；异步责任上推到 FS/服务层。
- **影响**：`log_persist_design.md` 裁决 6 另设专用写线程——**同一件事由器件层 worker 与服务层写线程两处承担**；槽深 2 使批量写必须靠 `BUSY` 重试，而裁决 6 自陈「3 次后丢弃计数」——**竞争下日志存在被丢弃路径**。
- **备注**：该分歧**有正当动机**（擦除 ms~s 级不可阻塞控制环），且上游 #72 已为其补 OSAL_PORT_NONE 坍缩分支（单执行流下退化为当场执行）。故此项记为"**需重新评估**"而非"错误"。

#### G-09 读写互斥粒度粗

- **现象**：`flash_read` 全程持设备 `busy` 标志（`hal_flash.c` busy 协议段）→ **任何读期间写被拒**；擦除期间读被拒；worker 中以 `while (dev->busy) osal_sleep_ms(1);` 等待（1 ms 粒度轮询）。
- **业界对照**：Zephyr 以设备互斥锁 + 驱动内部细粒度处理；SPI NOR 驱动普遍支持 **erase suspend / program suspend** 以在长操作中服务读。
- **影响**：片内 XIP 读走 `memcpy` 直访绕过（`bsp_flash_f4.c` read 实现），**当前形态自洽**；一旦接入外部 SPI NOR（G-04），擦除期间读阻塞将是硬伤。

### 3.4 第四梯队：验证与运维成熟度

#### G-10 无故障注入 / 掉电仿真

- **现象**：`flash_sim.c` 为**正常行为**仿真器，无擦除失败、位翻转、ECC 错误注入；无掉电中断仿真。
- **业界对照**：LittleFS 自带 power-loss 穷举测试台（断电点遍历 + 状态机验证）；Zephyr `flash_simulator` 支持 ECC/擦失败注入；UBI 有 torture 测试。
- **影响**：掉电一致性是存储子系统头号难点（G-03），**当前无任何自动化手段验证**，只能靠真机拔电。

#### G-11 无统计与健康面

- **现象**：无擦写计数、错误计数、分区健康度、最后错误查询 API。仅 `bsp_flash_f4.c` 内一个调试符号 `gBspFlash4DbgSr`（最近一次错误 SR 原值）。
- **业界对照**：UBI 提供 bad PEB 统计与磨损度；MTD 提供 `ecc_stats`；NVS 提供条目统计。
- **影响**：黑匣子/长期运行场景无健康度可观测，寿命预算只能纸面推算。

#### G-12 无 on-media 格式版本化政策

- **现象**：分区表无版本/魔数字段；日志记录头预留了 `version` 字段（`log_persist_design.md` 裁决 10）——**直觉正确**，但无兼容矩阵与迁移规则。
- **业界对照**：每种 on-media 结构都带 magic + version + 兼容策略（NVS 的 ATE 版本、UBI 的 version header）。
- **影响**：一旦量产后再改格式，无升级路径可用。

#### G-13 存储测试未接入 CI，且已因该缺口发生静默回归（实证）

- **现象**：`.github/workflows/ci.yml` 的 host-test 作业**只运行 `om_core_test` 与 `om_log_test`**；`partition_test` / `flash_dev_test` 存在但**从未接入 CI**（全 workflow grep 无命中）。
- **实证一：上游 #73 已使测试静默损坏**（2026-09-14，MinGW/GCC 15.1 工具链实跑）。二分定位：

  | 提交 | partition_test 结果 |
  |---|---|
  | `dc92281`（合并上游前） | 32 passed / 0 failed |
  | `7183544`（仅 #72） | 32 passed / 0 failed |
  | `8b9fde2`（含 #73） | **29 passed / 3 failed** |

  **根因** = #73 新增的坍缩守卫 `#if (OM_OSAL_PORT == OSAL_PORT_NONE)`：存储测试的 include 路径不含 `lib/osal/include`，两宏均未定义 → 预处理器按 `0 == 0` **判定为真**，静默走无 OS 坍缩路径（不建 worker 线程）→ `wq->thread` 为 NULL，而宿主桩 `osal_thread_self()` 在主线程亦返回 NULL → `flash_sync_wait` 的自锁判定 `NULL == NULL` 命中 → **全部同步操作返回 `OM_ERR_FLASH_BUSY`**；异步路径因"入队即执行"当场回调而虚假通过。注入 `-DOM_OSAL_PORT=1` 后恢复 32/0。

  **该守卫写法本身即缺陷**：宏未定义时静默取坍缩分支，任何未注入该宏的构建都会中招。已上游立项：`oh-my-robot/oh-my-robot-framework` **Issue #74**（含二分证据与 7 处修复点）。本地已按该方案加固，加固后 `partition_test 32/0`、`flash_dev_test 89/0`，`om_log_test` 四件套无回归。
- **实证二：Windows/MSVC 下二者构建失败**——`device.h` 的 `Device` 成员名 `interface` 与 Windows SDK 的 MIDL 宏 `#define interface struct` 冲突；根因链为 `atomic_base.h` 无条件引入 `<windows.h>`。完整修正还需将 `corelist.h` 的 `container_of` 从 GCC 扩展 `typeof` 改为标准形式。
- **业界对照**：LittleFS/UBI 的测试矩阵覆盖多平台；Zephyr 的 `twister` 在模拟器上跑存储测试。
- **影响**：存储子系统——**恰恰是对正确性要求最高、最难真机复现故障的一层**——其验证资产处于"存在但无人自动执行"状态。本次上游合并已实证其后果：**回归进入分支而无人察觉**。

---

## 4. 已达标与优于业界之处（校准）

> 本节用于避免只列差距造成的误判。以下各项经与业界对照，结论为**达到或优于成熟框架的对应做法**。

| 编号 | 项 | 说明 | 业界对照 |
|---|---|---|---|
| S-01 | **三族分类 + 义务归属推导** | 按"擦除几何 + 单向写"划可擦族、按"字节读写"划随机器件族、受管封装单列第三族，并给出"芯片特性 → 软件义务 → 归属层"的完整推导链 | 与业界真实分野（MTD / nvmem / block）完全一致；**文档质量高于多数开源项目** |
| S-02 | **两级漂移防线** | 注册期扇区友好 fail-fast（`partition.c` 注册校验段）+ 操作期器件层 erase 断言 | Zephyr 仅运行期报错；ESP 靠构建期规则（无独立工具链时不成立）。**本工程的两级折中在 MCU 上更优** |
| S-03 | **几何双模** | 均匀 `sectorSize` / 非均一 `sectorRegions` 区域表双模，正确覆盖 STM32F4 非均一扇区；F42x 的 SNB 编码（bank2 扇区 +4）处理正确（`bsp_flash_f4.c`） | 多数项目直接硬编码，未做非均一几何抽象 |
| S-04 | **program 语义契约** | "写 = program，目标区必须已擦是调用方义务，本层不自动擦" | 与 Zephyr / MTD 一致的业界共识 |
| S-05 | **事件驱动擦除等待** | EOP/ERR 中断主路径 + 睡眠轮询退化路径（`bsp_flash_f4.c` IRQ 段） | 优于纯轮询实现 |
| S-06 | **name-only 信任模型** | 全部操作 API 只接受分区名为唯一可信输入，内部重解析权威表；公共头零数据符号、表 const 只读驻留 | 方向是安全工程正解；对 MCU 而言偏重但无害 |
| S-07 | **演进留口纪律** | ER-1..5 留口、「V1 不做」清单、触发条件驱动演进表 | 工程纪律强于多数开源存储模块 |

---

## 5. 优先级建议

| 优先级 | 对应差距 | 动作 | 理由 |
|---|---|---|---|
| **P0** | G-01 / G-02 / G-03 | 落地磨损环形与日志持久化（解挂 `log_persist_design.md` 的 E/F） | 唯一能把本工程从"器件抽象"变为"存储系统"的一步；同时补上掉电事务化的第一个接收方 |
| **P1** | G-06 | area 句柄化 API（`om_partition_open` 返回 `{dev, offset, size}`）+ 去分区单例 | 不改则热路径每次 I/O 两次名字查找，且多实例场景被架构性锁死 |
| **P2** | G-04 / G-05 | SPI NOR 后端 + JEDEC 运行时探测；评估 erase suspend | 否则外部存储场景为零；`pageSize` 消费点与 erase suspend 应一并设计 |
| **P3** | G-07 | 分区表单一事实源（烧介质 或 构建期生成 + 链接脚本联动） | 消除 boot/app 副本漂移的结构性根因 |
| **P4** | G-08 / G-09 | ~~重新评估器件层"异步优先"；至少消除与服务层写线程的重复~~ **✅ 已完成 —— 见 ADR-0027** | 与业界分歧最大、代价最实在（RAM / 复杂度 / 丢弃路径） |
| **P5** | G-10 / G-11 / G-12 / G-13 | 掉电故障注入仿真 + 统计面 + 格式版本化政策 + **存储测试接入 CI** | 黑匣子语义的可信度最终靠这一层；其中 CI 接入成本最低、收益最直接 |

**建议起手**（2026-09-14 实测后修正——两项的真实成本均高于初判）：

1. **修 `OM_OSAL_PORT` 守卫**（`lib/async/src/workqueue.c`、`lib/drivers/src/peripheral/flash/hal_flash.c`）：改为 `#if defined(OM_OSAL_PORT) && (OM_OSAL_PORT == OSAL_PORT_NONE)`，使宏未定义时安全默认到"有 OS"路径。这是恢复存储测试的前提，且修的是上游守卫设计缺陷本身；
2. **存储测试接入 CI**：前置条件 = 第 1 项修复；两者合并即消除 G-13 的实证缺口；
3. **MSVC 移植**（`Device::interface` 成员名 + `corelist.h` 的 `typeof`）：成本经实测确认为**跨结构体（Device/OmSerial）、跨核心头**的改造，**不再属于"低成本起手项"**，建议单独立项。

---

## 6. 附：本次同步的实证发现（2026-09-14）

来源 = 将 `logger_store` 分支 rebase 至 `upstream/integration@8b9fde2` 过程中的实测，四项均**非本分支引入**：

| # | 发现 | 性质 | 关联差距 |
|---|---|---|---|
| 1 | **上游 #73 的坍缩守卫使存储测试静默损坏**：`partition_test` 32/0 → 29/3，二分定位到 #73；注入 `-DOM_OSAL_PORT=1` 后恢复 32/0 | **上游回归**，因无 CI 覆盖而未被发现 | G-13 |
| 2 | `partition_test` / `flash_dev_test` 未接入 CI；且在 Windows/MSVC 下因 `device.h` 成员名 `interface` 与 Windows SDK 的 `#define interface struct` 冲突而构建失败 | 既有缺陷，CI 无覆盖故长期未现 | G-13 |
| 3 | 上游 #73 删除 `osal_event.h` / `osal_queue.h`，但 `samples/osal/osal_sync/main.c` 仍引用；该样本无 `xmake.lua`、未入构建 | 上游遗留死样本 | — |
| 4 | 本地 MSVC 编译 host 测试需 `-utf-8`（源码含中文字符串/注释，MSVC 默认按本地代码页解析无 BOM 的 UTF-8）；CI 用 linux/gcc 不暴露 | 环境差异 | — |

---

## 7. 参考

1. `docs/boot_ota/storage_landscape.md` —— 存储形态锚点（三族分类 / ER-1..5 留口 / 分层纪律）
2. `docs/boot_ota/flash_dev_design.md`、`flash_dev_impl_design.md` —— 可擦族 NOR 内核设计
3. `lib/drivers/docs/partition_table_design.md` —— 分区表抽象（含 §5 三族分区形态对照）
4. `lib/drivers/docs/log_persist_design.md` —— 日志持久化后端设计稿（讨论记录，E/F 挂起）
5. 业界对照来源：Zephyr `drivers/flash` + `subsys/fs`；ESP-IDF `esp_flash` + `partition` + `nvs_flash` + `wear_levelling`；RT-Thread SFUD + FAL + DFS；Linux `drivers/mtd` + UBI；LittleFS
