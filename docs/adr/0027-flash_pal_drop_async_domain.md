# ADR-0027：Flash PAL 移除异步执行域——调用者上下文同步 + 每设备互斥

- 状态：已决策（2026-09-20）
- 日期：2026-09-20
- 参考：`docs/boot_ota/flash_dev_impl_design.md`（D-06 后端契约 / D-07 完成源 / 执行模型章节，**本文对其作修订**）；`docs/boot_ota/reference_design_notes.md` 的 **K-14**（本文对其结论重新解读）；调研取证 `docs/internal/active/issue_000_partition_completion/raw/research_workqueue_in_storage.md`（15 个对象，逐条文件行号）；ADR-0022 (osal_none_bare_metal)；ADR-0026 (partition_semantics_completion)
- 编号说明：0025 已被 boot 侧占用（`0025-boot_decision_data_and_jump_contract.md`，另一克隆），故取 0027。

## 背景 (Context)

1. **现状**：`FlashDomain`（每物理片默认一个 worker 线程 + 请求 FIFO，队列深 2）承载写/擦的异步执行；完成经设备级回调或同步等待原语；`read` 在调用者上下文直跑，与写/擦经 `busy` 标志互斥。设备可在注册时绑定一个**共享域**（多片共用一个 worker）。
2. **调研结论（器件驱动层）**：成熟存储栈的 flash 器件驱动层**一律同步在调用者上下文执行，无一家使用 worker**。Linux MTD 更进一步——它把"异步擦除"这个 API 概念**删除**了：`struct erase_info` 在 v6.12 只剩 `{addr, len, fail_addr}`，删除理由载于 commit `884cfd9023ce`（v4.17）：**"None of the mtd->_erase() implementations work in an asynchronous manner"**；配套 commit `e7bfb3fdbde3` 删除了 `erase_info->state`、`MTD_ERASE_XXX` 与 `mtd_erase_callback()` 整个函数。
3. **异步在存储栈中的两个正当理由**（调研归纳）：**上下文不合法**（ISR / 不可睡眠上下文——dm-crypt 的注释原文 "unwise to do decryption in an interrupt context"）与**后台家务**（GC / 磨损均衡 / 回收——UBI 背景线程、JFFS2 GC 线程、jbd2）。**"操作耗时长"不在其中**：那一类问题的业界解法是**就地让出**（spi-nor 与 mtdblock 的 `cond_resched()`；ESP-IDF 的调用者上下文 + 命令级 `vTaskDelay` 让出 + 分段，其 Kconfig 原文 "Prevents starvation of other tasks."）。
4. **本仓实情**：`flash_write_async` / `flash_erase_async` 的**生产消费者为零**（全部调用点位于 `samples/`）；唯一的生产消费者 `partition.c` 完全走同步包装。⇒ 异步接口面当前是**为未来支付的现在成本**。
5. **K-14 的重新解读（本决策的关键前提）**：K-14 记录 v0 的"可睡眠互斥全排他同步"方案经真机实测被废弃，理由是**忙等饿死低优线程**。⇒ **真凶是忙等，不是同步**。当年选择了"把执行搬到 worker"；**本决策选择另一条等价可行的路**：保留同步语义，**把"必须让出"从建议升级为后端硬契约**。两者解决的是同一个问题，但后者与业界形态一致且无线程成本。

## 考虑过的方案 (Options)

- **A（否决）：保留执行域。** 器件驱动层无先例；异步面无消费者；代价为每物理片一个线程与栈、队列满 `BUSY` 这一额外失败模式、以及 osal-none 坍缩分支的复杂度。收益（执行栈与优先级解耦）在当前消费者形态下不构成需求。
- **B（采纳）：移除执行域，改为调用者上下文同步执行 + 每设备睡眠互斥量。**
- **C（否决）：把工作队列上移到本仓内另建一层。** 异步是**消费面时序策略**，"调用者能不能等"只有上层知道；本仓当前不存在需要"提交即返回"的消费者（日志环自带写线程，boot meta 跑在可阻塞上下文）。为不存在的需求预建一层，即是本次要消除的那类过度设计。

## 最终决策 (Decision)

1. **删除异步执行机制**：`FlashDomain`、`flash_domain_init`、`FlashRequest`、请求槽队列、`OM_FLASH_QUEUE_DEPTH`、`flash_write_async`、`flash_erase_async`、`flash_set_done_cb`、`FlashDoneCb`；源文件 `flash_domain.c` 删除。
2. **`flash_read` / `flash_write` / `flash_erase` 一律在调用者上下文同步执行**，入口取**每设备睡眠互斥量**、出口释放。
3. **后端契约升为承重条款**：`write` / `erase` 为同步实现，内部等待 BSY **必须让出 CPU**（睡眠轮询或阻塞等待硬件完成事件），**禁止忙等**。**本决策成立的前提即此条款**——它单独承担了原本由 worker 承担的"不饿死其它任务"职责。
4. **排他性由互斥量提供**，取代原来的 `busy` 标志与槽队列。读、写、擦**共用同一把设备锁**（原先"读直跑 + busy 仲裁"的三方互斥被一把锁统一）。
5. **擦后校验与重试逻辑不变**（含 `OM_FLASH_ERASE_VERIFY_ATTEMPTS`），只是改在调用者上下文执行。
6. **免擦器件判定点不变**：`flash_geom_has_erase()` 仍是"有无擦除语义"的单一定义点。（2026-10-01 注：该函数已更名为 `flash_geom_needs_erase()` 并改为查能力位 `FLASH_CAP_NO_ERASE_NEEDED`，见 ADR-0028——本条决策语义不变，仅符号随之演进。）
7. **上层归属**：需要"提交即返回"的消费者**在自己的层级开线程**（如日志环的写线程已在做），不在介质访问层预置。

## 影响 (Consequences)

- **正面**：
  - 每物理片省一个线程与一份栈（原 worker 栈 3072 字节）；
  - **队列满 `OM_ERR_BUSY` 这一失败模式消失**——并发调用者改为阻塞在锁上，语义更简单；
  - osal-none 形态不再需要"坍缩"分支：无线程时就是同步调用加一把永不争用的锁；
  - 公开接口面收窄（少一个类型族与三个 API）；
  - 与业界器件驱动层形态一致。
- **代价（显式记账）**：
  - **执行栈转移到调用者**：长操作的轮询深度与后端缓冲从 worker 栈移到调用者栈。消费者是普通线程，但**后端实现时须核对调用者栈预算**；
  - **优先级解耦消失**：flash 工作改在调用者优先级上执行。对 bootloader（单执行流）无差别；对 app 侧，低优调用者不再被提升到域线程的优先级去抢占他人——**这一项实为改善**；
  - **失去"提交即返回"能力**：需要该能力的消费者必须自建线程。当前无此类消费者（见背景 4）。
- **与既有决策的关系**：
  - **K-14** 按"真凶是忙等"重新解读，本条记录在案，避免后人把它读成"同步方案已被实测否决"；
  - **D-06 / D-07**：D-06 的让出契约由"契约"升为**承重条款**；D-07（完成源：事件主路径、轮询退化）仍然有效，只是其调用停靠在调用者上下文；
  - **`OM_FLASH_SYNC_ONLY`** 随之失去意义——该宏此前只存在于设计态头注释（全库无 define、无消费），本决策后不再需要它来区分异步面。
- **本决策的适用边界**：它**不主张**"存储系统不需要异步"。异步仍然在**上层**（日志写线程、未来的 GC/回收）与其正当理由（上下文不合法 / 后台家务）下成立。被移除的是**器件访问层**里的异步执行体。
- **验证状态：已通过（2026-09-20，rm-a/F427 真机）**。三面证据：
  - **host（逻辑面）**：`flash_dev_test` 84/84、`partition_test` 186/186（含同设备互斥与跨句柄排他用例：后端内并发度峰值恒为 1）；`osal_none_test` 118/118（坍缩路径）；`om_core_test` / `om_log_test` 全通过。
  - **真机 Flash（`samples/pal/flash`）**：27/27。三条直接检验决策 3 与决策 4——① 128K 扇区擦除在调用者上下文同步返回（1076 ms）；② **擦除期间 HIGH 优先级心跳线程未被饿死**（心跳计数 16→22，与擦除窗口吻合）＝ 让出条款成立；③ 并访线程的 `flash_read` **不返回 `BUSY` 而是等锁**，其返回时刻与擦除结束时刻相同（t=4196 vs 4196）＝ 每设备互斥成立。
  - **真机 Partition（`samples/pal/partition`）**：53/53，覆盖真实非均匀几何（24 扇区 / 三处尺寸边界）与真实通路下的擦写读。
- **验证中修正的两处期望值（非功能回归）**：真机验证程序的部分断言仍按 v1 的 `INVALID_ARG` 写，而越界语义本次已改为 `OM_ERR_RANGE`——`read/write off==size`、len 跨分区末端、`off+len` 越界、len==0 的域外 off 共 5 条按新契约更新。设备层（Flash）越界同样取 `RANGE`，未对齐取 `INVALID_ARG`，越界先于对齐报出。
- **验证环境的两处坑（与本次改动无关，记录以免重复踩）**：① 串口日志后端是**非阻塞提交**（`SERIAL_O_NBLCK_TX`），`txFifo` 满即**静默截断**——爆发式验证输出会成片丢行，把失败项一起吞掉（曾表现为"53 条 CHECK 只打印 31 条"）；验证程序已按此在 `CHECK` 内逐条让出 20 ms，构建壳亦把验证目标的 `OM_LOG_RING_LEN` 提到 256。② 主机侧 `SerialPort.ReadLine` + 逐行写盘会在爆发时溢出驱动缓冲，改块读脚本才稳定。
