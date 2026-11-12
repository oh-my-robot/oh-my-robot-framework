# osal-none：Bootloader 裸机适配（v2 · 当前目标）

> 修订记录：
> - v0/v1（2026-09-08/09）：设计讨论逐字基线 + 协作内核方向（自研内核 → 双模可配）——**已挂起待重议**（重议方向：跳过协作、直接抢占式），全文归档于同目录 `osal_none_core_design_cooperative_archive.md`
> - v2（2026-09-09）：范围重定 = **Bootloader 裸机 osal-none 适配**（ADR-0022 形态）；协作内核非本范围
> 性质：工作文档，随重开讨论演进
> 关联：ADR-0022 (osal_none_bare_metal)、ADR-0023 (osal_minimal_core，修订本文 A3)、ADR-0021 (boot_multi_strategy_skeleton)、reference_design_notes（K-11/K-16/K-19）、D-08（OM_FLASH_SYNC_ONLY）、partition_table_design（下载在应用内）

---

## 1. 定位（前提修正后）

1. **Bootloader 运行时形态 = 裸机直跑**（ADR-0022 Q-07 拍板）：① 被修复者不能依赖被修复者（自举依赖）；② 提交期确定性（秒级擦写/搬移期间无调度）；③ 生态共识——MCUboot/OpenBLT/ST ROM/TI BSL/NXP 六方 loader 全裸机单流（K-16）。
2. **OTA 下载在应用内**（partition_table_design；Zephyr 实证：mcumgr/SMP 下载跑在 app、bootloader 只做 swap/validate、bootloader 内下载仅 serial recovery 且为单流命令循环）→ bootloader 无下载编排需求。"bootloader 编排侧上协作内核"论证作废（错误论证全文留档于归档文件 §五）。
3. **osal-none = 裸机形态的正式 osal 端口**（与 freertos 并列，os=none 合法取值）：bootloader 与 app 共享同一套框架 API，同码双形态 = 裁剪形态的验收场；替代随工程携带的 host_osal.c 桩（技术债，ADR-0022）。
4. 自研内核任务（协作/抢占双模）**挂起待重议**——不构成本文档范围；其全部讨论存档于归档文件，重议时以其为输入。

## 2. 面范围（讨论基线，沿用 ADR-0022 决策；未决项见 §4）

| 面 | 形态 | 要点 |
|---|---|---|
| 时间面 | now / sleep 真实现 | A1：忙等 + 内嵌刷新（无调度器无让出）；A2：时基 = per-arch 编译期绑定 `now()`（CPU 时钟域自由运行计数直读、无 tick ISR）；纯自旋等待；平台先启时钟契约 |
| 中断面 | `irq_lock` 族 / `is_in_isr` | 保留、真关中断；单执行流无任务↔任务并发 → 临界区只服务"任务 ↔ ISR"共享面 |
| 同步面 | mutex = 0/1 互斥开关、sem = 计数、completion 直通 | **保真不塌缩**（K-19：表达资源/事件状态 + 临界区纪律，与抢占无关）；等待由 idle 刷新驱动（XRobot RefreshTimerInIdle）；否决 U-Boot compat.h 空转宏（伪同步） |
| ISR 面 | `*_from_isr` | 保留（真 ISR 上下文存在）；记账置位、主循环轮询消费——无切换语义 |
| 线程面 | thread = **LibXR 式直调占位**（A6/A6d 拍板） | 无真线程：create 立即直调 entry；无栈无调度无并发；self = 唯一执行体单例；语义降级文档化（逐函数语义见 §4-A6d，已确认） |
| async 执行机制 | **统一坍缩直调**（A7 复议二版 2026-09-09） | osal-none = 通用库：无后台执行者 → **一切需执行者的机制（workqueue/FlashDev 域/async）统一 LibXR 式同步直调坍缩**（start 不建线程、执行下沉到提交点），不拒绝；唯一例外 = **ISR 上下文入队**（不能直调 → 显式拒绝 + 文档指引）；坍缩实现分支按 os 轴编译期（`#if OM_OSAL_PORT`，待确认）；`OM_FLASH_SYNC_ONLY` 不加入（A8） |
| 内存 | `osal_malloc/free` = **heap_1 形态静态池**（A4 拍板，归 osal-none 下） | 只分配不释放（bump）；free = no-op（design-by-contract 文档）；耗尽 → NULL；对齐 = arch 属性；host 与 target 同一实现只差 cfg；`OM_OSAL_NONE_HEAP_SIZE` 沿用（0 = 禁用堆 → malloc 恒 NULL） |

bootloader 实际消费面（预期，ADR-0022 决策）：FlashDev 同步等待原语 + 日志裁剪三件套；驱动/服务代码与 app 同一 API（同码双形态验收）。

## 3. 挂起交接：协作内核设计中对本形态仍有价值的结论

1. **时间回绕合同**（osal_time.h：32 位 ms + `after/before`，窗口 <2³¹ms）——原生合同，两形态共用。
2. **"ISR 只记账不切换"论断**——无调度器形态下退化为：ISR 置位/计数 → 主循环轮询点消费；"唤醒"（条件满足）延迟 = 主循环轮询粒度。
3. **绝对 deadline 思想**退化为单点"最近 deadline"轮询（idle 刷新同款）——结构简化、概念一致。
4. 已否决项跨形态有效：U-Boot 空转宏；daemon/命令队列式 timer（单执行流 timer 若需要 = 到期轮询 + 主循环回调，非本范围先行项）。

## 4. 讨论问题集（状态追踪）

- **A1 等待语义模型 —— 已拍板（2026-09-09）**：单执行流等待 = **忙等 + 内嵌刷新**（LibXR None 后端实证）。`wait(timeout)` = 轮询 {条件 → 时基 → 刷新周期回调}；`WAIT_FOREVER` = 无 deadline 忙等（仅 ISR post 置位/计数解除）；ISR 面 = 短临界区置位/计数，post 唤醒延迟 = 轮询粒度。**实证修正 K-19 表述**：LibXR 刷新点**内嵌在每次等待/Sleep 调用内部**（不是 idle 循环主动刷新）→ 主流程纯计算不等待时周期回调不触发；周期函数工作属于 Timer 体系，不叫 Thread。来源：LibXR None（Mutex/Semaphore/Thread/Timer 官方文档）。
- **A6 线程面形态 —— 方向已拍板 = LibXR 式（2026-09-09）**：同 API 保形、语义降级为直调占位（非运行时 `NOT_SUPPORTED`）。风险记录：entry 死循环则 create 调用不返回、吞调用者后续流程（LibXR 同款，文档明示）。逐函数语义草案 = §4-A6d，待确认后录。
- **A6d 逐函数 —— 已确认（2026-09-09）**：create 直调占位（attr 忽略；entry 返回后 `*t` = 静态单例句柄、返回 OK；entry 死循环则调用不返回——文档明示）/ self 单例 / yield no-op / exit fail-loud 断言陷阱（禁调）/ terminate 恒 INVALID / join NOT_SUPPORTED（对齐 freertos）/ **kernel_start = `OSAL_OK` no-op**（单执行流无"启动调度器"动作，文档写明）。
- **A3 面清单 —— 已拍板（2026-09-09）；2026-09-14 经 ADR-0023 修订**：原决策"全族以统一语义规则提供"由 ADR-0023 (osal_minimal_core) 修订——**osal 契约收敛为最小原语集（thread/sem/mutex/time/core + priority 约定）**，queue/event 从架构删除（队列/事件语义 = 纯数据结构 + sem + 临界区的组合规则；在树 log 消息环 / ipc pipe / workqueue 三例为证，均未使用 osal_queue）。本端口面清单随之收敛：保真族（thread 占位/sem/mutex/time/irq）不变，queue/event 两族删除。workqueue 坍缩 os 轴分支形态 = `#if OM_OSAL_PORT == OSAL_PORT_NONE`（已落码）。
- **A2 时间源 —— 已拍板（2026-09-09，按推荐组合）**：① 时基注入 = **per-arch 编译期绑定 `osal_none_time_now_ms()`**（与 arch 钩子同机制；链接期保证存在；host = `osal_none_arch_x64.c` QPC，target = 板级文件）；② 计数域 = **CPU 时钟域自由运行计数器直读**（无 tick ISR、中断全关可计时；擦写冻结 = 时间暂停，语义 = "软件执行时间"；真实时间兜底 = 硬件 WDT；独立时钟域记备选）；③ 等待 = **纯自旋忙等**（读 now + wrap-safe deadline + 条件 + 内嵌刷新到期软定时器；sleep(0) = 立即返回；WAIT_FOREVER = 纯条件轮询；不用 WFI——通用库不能假设事件中断必然唤醒，省电扩展点记未来）；④ osal-none 不 init 时钟，平台先启契约（bootloader 自有 main 顺序）；⑤ `delay_until` 移植 freertos 端口回退路径逻辑。
- **A4 内存形态 —— 已拍板（2026-09-09，按推荐组合）**：`osal_malloc/free` = **heap_1 形态静态池，归 osal-none 下**（`osal_none_cfg.h` 的 `OM_OSAL_NONE_HEAP_SIZE` 沿用；0 = 禁用堆 → malloc 恒 NULL 的纯静态形态）。只分配不释放（bump allocator，实现 ~30 行）；**free = no-op + design-by-contract 文档**（FreeRTOS heap_1 / LibXR"仅初始化分配永不释放"同款）；耗尽 → NULL（调用方走 NO_RESOURCE 路径）；对齐 = arch 属性（S8）；无锁（单流唯一活动 + ISR 禁入，osal 契约断言）；host 与 target 同一实现只差 cfg（host 测试调大）。依据：serial 驱动真实消费 `osal_malloc/free`（同码双形态要求真堆，非静态改造）；bootloader 单程生命周期 = 无释放模式。演进取向：需真 free 时换 heap_4 同构算法（单文件可换）。
- **A5 host 验证 —— 已拍板（2026-09-09）+ 用户指令"host 测试务必尽可能真实模拟裸机行为"**：工程形态 = `samples/host/osal_none_test/`（osal-none 源 + arch_x64 直接编入；`OM_OSAL_PORT = OSAL_PORT_NONE`，依赖 osal_port.h 枚举扩展）。语料 = **单流顺序语义**（实机 8 套并发语料不可移植——thread 直调下无第二执行体）。真实模拟四机制：**R1 同一实现只差 cfg**（host/target 共享全部内核代码，差异收敛 arch 钩子）；**R2 ISR 模拟上下文**（arch 层 `is_in_isr` 读可注入标志 + host-only `isr_enter/exit` 测试钩子——裸机模型下 ISR 面 = 记账 + 临界区，无打断语义，模拟等价 → from_isr 正路径/拒绝路径/workqueue ISR-enqueue 拒绝分支全部 host 可达）；**R3 host 临界区 = 真互斥**（spinlock 模拟关中断；任务线程 + 模拟 ISR 辅助线程真并发下内核操作安全，竞态可被暴露）；**R4 wait_pause 微钩子**（host = Sleep(0) 让出，target = 空；语义不变）。ISR 面最终确认仍归实机（_verify_init 语料）。
- **A7 执行者机制 —— 拍板后复议撤销，二版定稿（2026-09-09）**：原案"刷新钩子泛化 + 等待点排空器 + workqueue 执行者缝"**撤销**；一版"需执行者机制显式 `NOT_SUPPORTED`"**同撤**（用户纠偏：workqueue 也应等效直调）。定稿：osal-none = 通用库，无后台执行者 → **统一同步直调坍缩**（LibXR ASync None 同款"轻量函数调用包装"——None 实现不建线程，执行下沉到提交点）。workqueue 坍缩逐函数语义（lib/async 共享代码经 os 轴编译期分支实现）：`init` OK；`start` **no-op**（不建 worker，state=RUNNING = 可用）；`enqueue`（任务上下文）**直调 work->func**（func 返回 = 已执行 → flags 归 IDLE；func 内重入 enqueue 同 work → BUSY 检测）；`enqueue`（ISR 上下文）**显式拒绝**（`OM_ERR_NOT_SUPPORTED` + 文档；根据 = 坍缩下执行点即提交点，ISR 不是任意用户 func 的合法执行上下文（短/非阻塞不可假设，长 func 摧毁中断延迟）；若改为"只入队稍后执行"则回到不可预测执行/排空语义（已砍）→ ISR 交接走同步面 `*_from_isr`（真实现）+ 任务上下文提交）。**实证（2026-09-09）**：在树 workqueue 消费者仅 hal_spi.c / hal_flash.c，enqueue 全在任务上下文 API 提交点；SPI 的 ISR 路径（hal_spi_isr ← BSP DMA/中断回调）只做 `completion_done(&bus->transferDone)`（同步面 ISR 原语），不投递 Work；workqueue 文档"中断安全延迟调度器"的 ISR-enqueue 是设计能力面、无在树消费者依赖 → os=none 丢失该能力 = 文档化差异（替代 = 同步面 from_isr 交接），对在树消费者零影响；`cancel` / `flush` / `stop` / `deinit` 按无 pending 退化；`work_wait_idle` 立即 OK。连带影响：FlashDev 域随之坍缩（async API = 同步执行 + done 同步回调于调用者上下文；D-08 的"no-OSAL async → NOT_SUPPORTED"行将来随实现修订，A8 自洽）。A1（忙等等待语义）不受影响。**落码补充（2026-09-09 P2）**：坍缩契约增补 = **enqueue 禁止在 `irq_lock` 临界区内调用**（执行点语义：长活/回调/内部等待不得进关中断区）——`hal_flash.c flash_submit` 原持锁入队（OS 形态无害）在坍缩下 = 关中断跑后端/等 EOP → 死锁，已按 os 轴分支（none = 无锁直提；单流无并发提交者，锁非必需）。**FlashDev 核对结论**：同步 API（submit + work_wait_idle）坍缩下自洽（入队即执行 → wait 立即满足）；同域自锁拒绝（`thread_self()==wq.thread`）坍缩下失效但无害（wq.thread=NULL、self=单例 → 不拒绝；回调内同步等待 = 递归执行而非 OS 形态 BUSY——差异文档化）；done 回调上下文 = 调用者（既有记录）。hal_flash/flash_domain 已入 host 语料工程编译验证 os 轴分支（运行需器件后端，归实机）。
- **A8 OM_FLASH_SYNC_ONLY 处置 —— 已拍板（2026-09-09）**：暂不加入代码（现仅设计态头注释，全库无 define 无消费）；**裁剪判据 = os 轴（有无 OS：os=none / os=freertos），不是模块级宏**；等实机测试暴露需求后再定。D-08 相关文档行将来随实现修订（先不动）。
- 问题集 A1–A8/A6d **全部收口**（P1/P2 已落码，见 §5 与 A7 落码补充）。

## 5. 文件形态（A1-A8/A6d 收口后定稿；P1/P2 已落码；移植面 2026-09-14 落位）

```
platform/osal/none/（注：oh-my-robot-framework 与 _verify_init/omr 为同一物理目录的两个路径——无"镜像"概念）
  README.md               # 移植面声明（新芯片/架构只实现 portable/<arch>/osal_none_arch.c）
  om_osal_portdef.h       # portdef 义务：PRIO_MAX 32 / NAME 16 / WORD 4 / SYNC_ACCEL_CAP_*=0
  osal_none_cfg.h         # OM_OSAL_NONE_HEAP_SIZE（0=禁用）；协作时代 MAX_TASKS 项移除
  osal_none_internal.h    # 端口私有：对象结构 / 忙等等待原语 / 时间驱动 / 堆 / arch 钩子声明
  osal_core_none.c        # is_in_isr / irq_lock 族 + malloc(heap_1 静态池) / free(no-op)
  osal_time_none.c        # now_monotonic(委托 arch) / sleep_ms(忙等) / delay_until(回退路径)
  osal_sem_none.c         # 计数信号量（忙等；任务面 + from_isr 面）
  osal_mutex_none.c       # 0/1 互斥开关（owner = 单例句柄）
  osal_timer_none.c       # 软件定时器（到期链 + 等待点内嵌刷新回调；非阻塞契约）
  osal_thread_none.c      # LibXR 式占位族（A6d 逐函数已确认）
  portable/<arch>/osal_none_arch.c
                          # 移植面（每 arch 一份）：时基(SysTick 自由运行懒启动) / PRIMASK 嵌套
                          # 临界区 / IPSR 判断 / 让步点；cortex-m4 已落（真机验证）
  （无 .S：v2 无上下文切换，直调占位 = 纯 C；协作时代 osal_none_switch_x64.S 已于 2026-09-09 清理）
lib/osal/include/osal/osal_port.h   # +OSAL_PORT_NONE 枚举与校验（公共头契约扩展，已落码）
samples/host/osal_none_test/        # host 语料（R1-R4 真实模拟）+ osal_none_arch_x64.c
                                    # （host arch = 测试基础设施，不入 platform）
lib/async/src/workqueue.c           # 坍缩分支 #if OM_OSAL_PORT == OSAL_PORT_NONE（P2 已落码）
lib/drivers/src/peripheral/flash/hal_flash.c
                                    # flash_submit os 轴分支（P2 核对修复；坍缩 enqueue 禁锁内调用）
platform/osal/index.lua              # om_os_index + "none"（P3 已落码）
platform/osal/none/xmake.lua         # tar_awapi_osal 端口注入（OM_OSAL_PORT=3 / portdef 头路径，
                                     # public 传播）+ tar_os 静态库（osal_*_none.c + on_load 按
                                     # context.arch 解析 portable/<arch>/，缺失即 raise——freertos 同款）
platform/bsp/data/boards/rm-c-board.lua
                                     # osal 映射 + "none"（P3 验证板）
platform/bsp/boards/rm-c-board/osal/none/
                                     # 板级 os=none 目录（无 FreeRTOSConfig 等价物）
```

**P3 构建接线验证（2026-09-09）**：`--os=none` 经索引校验并路由到 `platform/osal/none/xmake.lua`；rm-c-board os=none 全量配置 + 构建通过（tar_os 编入 8 个族文件、lib 树在 `-DOM_OSAL_PORT=3` 下编译归档——含 workqueue 坍缩分支与 flash os 轴分支的 ARM 交叉编译）。**边界（归 bootloader 工程 S2-3）**：binary 链接需板级/工程提供 arch 钩子（now/crit/in_isr，编译期绑定契约）与自有 main/裁剪；本框架构建只产静态库，无 binary 目标故未触达。os=freertos 配置回归正常。

## 6. 实机验证（rm-a/F427 os=none，2026-09-09 独立闭环）

- **工程**：`_verify_init/osal_none/`（xmake 壳：board rm-a / os=none / gnu-rm；自有强 main，initcalls `[EARLIEST, SERVICE)` 窗口 + 手动串口后端注册 + SysTick 1ms 真实 ISR 源；appcfg：OM_LOG_ASYNC=0 + OM_LOG_RTT=1 双通道）。
- **arch 钩子实装示例**（编译期绑定契约落点）：`_verify_init/osal_none/osal_none_arch.c`——DWT CYCCNT 时基（懒使能 + 32 位回绕高位计数 + SystemCoreClock 换算 ms）、PRIMASK 嵌套临界区（深度计数）、IPSR 判断；寄存器直映射、零 vendor 头依赖（gnu-rm 内联汇编）。bootloader 工程可复用/迁移。
- **结果**：RTT 通道 **81/81 ×3 轮全绿复现**（0.796s 完成；证据 `_verify_init/rtt_osn{2,3}.txt`）；时基单调（时间戳 0→796ms）。覆盖：T0 时间（sleep 时长/delay_until/catch-up）、T1 sem + **真实 SysTick ISR post 唤醒 FOREVER 忙等**、T2 mutex/timer、T4 周期定时器（等待点内嵌刷新推进）、T5 thread 占位族 + heap。（原文含 event/queue 项，2026-09-14 随 ADR-0023 删除后语料已同步更新。）
- **过程发现（非端口缺陷）**：event ISR 唤醒用例初版用 timeout=0 非阻塞探针与 1ms ISR 竞态 → 改 FOREVER（测试缺陷；sem 用例 FOREVER 一直稳定）。该用例随 ADR-0023 删除。
- **串口疑点结案（2026-09-09 驱动侧排查）——三重干扰 + 一个真缺陷**：
  1. **波特率口径**：板级默认 `BSP_LOG_BAUD=115200`（#ifndef 守卫），姊妹工程经 boardcfg 覆写 460800，本工程初始缺失 → 早期 460800 采集全空。修复：补 `cfg/boards/rm-a-board/om_boardcfg.h`（覆写 460800）。
  2. **CH340 驱动缓冲回放**：打开端口吐历史片段，制造"2 行/片段"假象（方法论已知）。
  3. **真缺陷——时基依赖调试域**：首版 arch 时基用 DWT CYCCNT——实测 **无调试器连接时不计数**（attach 前冻结于 21ms、attach 后精确走满 1.002s；PC 定格在 `__udivmoddi4` = now_ms 的 64 位除法 → 卡死在第一个 `sleep(50)` 自旋）。此前"attach 在场才通/全量"之谜由此解释（attach 使能 CYCCNT；且 attach 周期性 halt 暂停产出、DMA 抽干 ring，恰好规避突发截断契约 → 见到"全量"）。**修复：时基改 SysTick 自由运行计数**（CPU 时钟域、24 位相位模差累加零除法、不依赖调试单元——bootloader 语义本就不得依赖调试器；SysTick 由平台先启）。
  4. **TX 管线零缺陷**（探针实证）：TXDONE 回调全程连续（每消息 2 段）、BUSY_TX 无残留、PRIMASK 恒 0、TxXferSize 正常；freertos 参考固件同板同口 35s 心跳零丢。
  5. **修复后**：无 attach 下 **hb 连续 10765 条 / 100 分钟无缺口**（cap_systick.txt + rtt_final.txt），corpus 正常推进——长时间稳定性闭环。
  6. 既定契约如实记录：非阻塞日志 txFifo 满 ⇒ **静默截断 + err_cb(overflow)**——无 attach 突发 corpus（6.4KB/0.8s）超出带宽时部分行按契约丢弃（稀疏心跳期零丢）；需完整突发日志的工程应增大 txBuf 或流控（后续项）。
- 方法纪律执行：flash_one.sh 向量比对一致、独立 r/g、RTT 捕获后 recovery 复位（方法论记忆）。

## 7. 移植面落位与复验（2026-09-14）

- **落位**：目标 arch 实现自验证工程迁入 `portable/cortex-m4/osal_none_arch.c`（迁移适配：SysTick 改为**懒启动**——RVR==0 时按 SystemCoreClock 自配纯计数、不使能 TICKINT，工程需要 tick 中断时自行置位；"一个文件 = 该 arch 完整移植面"）；host arch 归测试目录（`samples/host/osal_none_test/osal_none_arch_x64.c`，platform = 可部署目标的边界）；端口 README 声明移植面契约；`xmake.lua` on_load 按 `context.arch` 解析注入 tar_os、缺失 raise（freertos 端口同款惯例）。
- **验证**：host 语料 **118/118**；rm-c 双轴（os=none / os=freertos）构建通过（构建日志确认 `portable/cortex-m4/osal_none_arch.c` 由 on_load 编入 tar_os）；**rm-a 真机复验**——新构建烧录 + 复位后完整运行 `=== osal-none target corpus: 60 passed, 0 failed ===`（60 项 = ADR-0023 删除 queue/event 后的语料），心跳 hb 1→214 连续零缺口（证据 `_verify_init/cap_new1.txt`，30s 窗口同时覆盖旧进程尾段→复位→新固件全程）。
- **附记（环境）**：本次 CH340 端口号发生重枚举（COM17→COM10），识别法 = hwid LOCATION/驱动描述（方法论已更新）。
