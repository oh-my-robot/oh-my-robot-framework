# Completion 设计文档

## 概述

Completion 是一次性 ISR<->线程同步原语，用于"等待某个事件发生一次"的场景。它保证严格 one-shot 语义：一次 done 恰好唤醒一个 waiter，重复 done 返回 BUSY。

## 最终设计

### 双后端架构

| 层级              | 后端                           | 状态机      | 阻塞/唤醒             | 临界区      |
| --------------- | ---------------------------- | -------- | ----------------- | -------- |
| 通用端 (reference) | CAS + OSAL Semaphore         | 4 状态 CAS | Semaphore         | 无（纯 CAS） |
| FreeRTOS 加速端    | CAS + Task Notification idx1 | 4 状态 CAS | Task Notification | 无（纯 CAS） |

- **默认启用加速端**：当 `sync_accel=auto` 且 OS 为 FreeRTOS 时，自动注入 `completion_accel_cas_tn.c`
- **回退路径**：`sync_accel=none` 或无加速文件时，使用通用端 CAS + Semaphore

### 原子操作库

本模块使用项目内置的 `lib/include/atomic/` 原子操作库，而非编译器内建函数：

| 操作            | API                            | 展开（armclang/GCC）                                                                    |
| ------------- | ------------------------------ | ----------------------------------------------------------------------------------- |
| Acquire Load  | `OM_LOAD_ACQ(ptr)`             | `__atomic_load_n(ptr, __ATOMIC_ACQUIRE)`                                            |
| Release Store | `OM_STORE_REL(ptr, val)`       | `__atomic_store_n(ptr, val, __ATOMIC_RELEASE)`                                      |
| CAS (strong)  | `OM_CAS_AR(ptr, exp_ptr, des)` | `__atomic_compare_exchange_n(ptr, exp, des, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)` |
| Release Fence | `OM_FENCE_REL()`               | `__atomic_thread_fence(__ATOMIC_RELEASE)`                                           |
| 原子类型          | `OM_ATOMIC_T(type)`            | `volatile type`（fallback）/ `_Atomic(type)`（C11）                                     |

`Completion.status` 声明为 `OM_ATOMIC_T(CompStatus)`，确保跨编译器一致且语义明确。

### 四状态 CAS 协议

```
INIT ──[done CAS]──→ DONE ──[wait 消费]──→ INIT
INIT ──[wait CAS]──→ WAIT ──[wait CAS]──→ WAITING ──[done CAS]──→ DONE
                        WAIT ──[done CAS]──→ DONE
```

四种状态：

- `COMP_INIT` (0)：初始态，等待 wait 或 done
- `COMP_WAIT` (1)：waiter 已注册 waitThread，准备进入阻塞
- `COMP_DONE` (2)：done 已完成，待 waiter 消费
- `COMP_WAITING` (3)：waiter 已进入阻塞，等待 done 唤醒

## 引入 COMP\_WAITING 的竞态分析

### 问题：为什么 3 状态不够？

如果只有 INIT / WAIT / DONE 三种状态，wait 路径在"注册 waitThread"和"进入阻塞"之间存在一段**无法被 CAS 保护**的窗口：

```
时间线（3 状态，有竞态）——
Waiter 线程                           ISR / 另一线程
─────────────────────────────────    ───────────────────
status == INIT
waitThread = self        // 注册
CAS(INIT → WAIT) ✓       // 状态变为 WAIT
                            done():
                              expected = INIT
                              CAS 失败（当前 WAIT）
                              expected = WAIT
                              CAS(WAIT → DONE) ✓
                              sem_post(sem)  ← 唤醒信号已发出！
                              // 但 waiter 还没开始阻塞……
sem_wait(sem)            // 进入阻塞
                          // ↑ 永远等不到下一个 sem_post！
                          // 信号量计数被"过早"消费，
                          // 但消费它的 waiter 还没就位。
```

**根本原因**：`sem_post`（或 `xTaskNotifyGive`）在 waiter 进入 `sem_wait` 之前被调用，唤醒信号丢失。这是经典的 **lost wake-up** 竞态。

### 思考：为什么 irq\_lock 版本没有这个问题？

原有 `irq_lock + Semaphore` 实现中，关中断临界区将"状态检查→写 waitThread→状态转移→开中断→sem\_wait"保护为一个整体。ISR 的 done 在临界区外排队，不会插入到 waitThread 注册和 sem\_wait 之间。

但当我们用 CAS 去掉关中断后，waiter 和 ISR 可以**在任意指令边界并发**。CAS 本身只能保护单个变量的原子转移，无法保护"写 waitThread + 状态转移 + 进入阻塞"这个多步序列。

### 方案评估

**方案 1：先进入阻塞，再设状态（不可行）**

```
sem_wait(sem)  // 先阻塞
CAS(INIT → WAIT) // 再设状态
```

问题：如果 done 在 sem\_wait 之前发生（done-before-wait），sem\_post 已经发出，waiter 随后进入 sem\_wait 将永远阻塞。

**方案 2：自旋等待 + CAS 重试（不可行）**

done 端用 CAS 自旋直到 waiter 进入阻塞。问题：ISR 上下文不能自旋（死锁风险 + 延迟不可控）。

**方案 3：引入中间状态 COMP\_WAITING（采用）**

将"waitThread 已注册"和"已进入阻塞"拆分为两个可区分的状态：

- `COMP_WAIT`：waiter 已注册 waitThread，done 可以在此状态唤醒（此时 sem 信号由 waitThread 上的后续 sem\_wait 消费）
- `COMP_WAITING`：waiter 已进入阻塞，done 在此状态唤醒最安全

done 端**不区分** WAIT 和 WAITING，两种状态都发送唤醒信号。区别仅在于 wait 端的内部逻辑。

### 解决方案：WAIT → WAITING 两步 CAS

```
Waiter 线程                           ISR / 另一线程
─────────────────────────────────    ───────────────────
status == INIT
waitThread = self
OM_FENCE_REL()            // 确保 waitThread 先于 status 可见
CAS(INIT → WAIT) ✓        // 阶段 2：状态变为 WAIT
                            done():
                              CAS(INIT → DONE) 失败（当前 WAIT）
                              CAS(WAIT → DONE) ✓
                              sem_post(sem)     ← 在此处唤醒也安全：
                              //              ↑ sem_post 被缓冲在信号量计数中
CAS(WAIT → WAITING)       // 阶段 3：尝试转入 WAITING
  → 失败！expected 已被 done 改为 DONE
  → 进入"done 已到达"分支：
    CAS(DONE → INIT)
    sem_drain(sem)         // 消费 done 发出的 post
    return OM_OK           // wait 成功返回，不进入阻塞
```

**关键保证**：done 端看到 COMP\_WAIT 或 COMP\_WAITING 时均发送唤醒信号。waiter 在 CAS(WAIT→WAITING) 失败时检查是否 done 已完成，若已完成则消费信号后直接返回，不再进入阻塞。信号量则作为"保险"——万一 WAIT 状态下 sem\_post 已发出，WAITING 状态下的 sem\_wait 会立即消费它。

**超时路径同样安全**：waiter 在超时后 CAS(WAITING→INIT)，若失败（done 恰在此时到达），同样走"done 已到达"分支消费信号并返回 OK。

### 状态转换总结

```
INIT ──[done CAS fast-path]──→ DONE → wait 端 CAS(DONE→INIT) 消费后回到 INIT

INIT ──[wait CAS step1]──→ WAIT ──[done CAS]──→ DONE
                              │                    ↑ waiter 在 CAS(WAIT→WAITING) 失败后
                              │                      检测到 DONE，不走阻塞直接返回
                              │
                              └──[wait CAS step2]──→ WAITING ──[done CAS]──→ DONE
                                                       │
                                                       └──[timeout CAS]──→ INIT
```

## 关键实现细节

**wait 路径（四阶段）：**

1. **快路径检查**：若 status==DONE，CAS(DONE→INIT) 消费后直接返回 OK
2. **注册等待**：写 waitThread，Release Fence，CAS(INIT→WAIT)
3. **消竞态**：CAS(WAIT→WAITING)，若失败说明 ISR 在步骤 2-3 之间已 done，走 done 消费路径
4. **阻塞等待**：进入 sem\_wait / ulTaskNotifyTakeIndexed，被唤醒或超时

**done 路径（纯 CAS，零临界区）：**

1. **快路径**：CAS(INIT→DONE)，成功直接返回 OK（done-before-wait，无 waiter 需唤醒）
2. **等待路径**：CAS(WAIT/WAITING→DONE)，成功后唤醒 waiter（sem\_post / TaskNotify）
3. **重复 done**：expected==DONE 时返回 BUSY

**Task Notification 安全分析：**

- Completion 的 one-shot 特性天然适合 Task Notification
- `COMP_INIT→DONE` 快速路径不发送通知（快路径不经过等待状态，waiter 通过 status 感知）
- `COMP_WAIT/WAITING→DONE` 发送恰好一次通知
- `COMP_DONE` 重复 done 返回 BUSY，不会重复通知
- 因此不存在通知累积/计数溢出风险
- 占用通知 index 1，其他模块不得使用同 index

## 多方案对比

### 候选方案

| 方案 | 临界区                 | 阻塞/唤醒                  | 简称              |
| -- | ------------------- | ---------------------- | --------------- |
| A  | irq\_lock (BASEPRI) | OSAL Semaphore         | irq\_lock + Sem |
| B  | CAS                 | Task Notification idx1 | CAS + TN        |
| C  | CAS                 | OSAL Semaphore         | CAS + Sem       |
| D  | irq\_lock (BASEPRI) | Task Notification idx1 | irq\_lock + TN  |

### 对比矩阵

| 维度     | A (irq+Sem)                | B (CAS+TN)                                                                          | C (CAS+Sem)                                   | D (irq+TN)            |
| ------ | -------------------------- | ----------------------------------------------------------------------------------- | --------------------------------------------- | --------------------- |
| ISR 延迟 | 关中断 140-280 cyc            | 0（纯 CAS）                                                                            | 0（纯 CAS）                                      | 关中断 140-280 cyc       |
| 跨平台    | 是（OSAL 抽象）                 | 否（FreeRTOS 专有）                                                                      | 是（OSAL 抽象）                                    | 否（FreeRTOS 专有）        |
| 通知计数风险 | 无（Sem 天然安全）                | 无（one-shot 协议保证）                                                                    | 无（Sem 天然安全）                                   | 无（one-shot 协议保证）      |
| 内存模型   | 每 Completion: Sem CB \~56B | 每 Completion: 0B；每 Task: Notify 数组 +8B（需 `configTASK_NOTIFICATION_ARRAY_ENTRIES=2`） | 每 Completion: Sem CB \~56B + `OM_ATOMIC_T` 4B | 每 Task: Notify 数组 +8B |

**内存分析详细说明：**

- **Sem 方案 (A/C)**：每个 `Completion` 实例持有一个 OSAL Semaphore 控制块（FreeRTOS 下约 56 字节），外加 `Completion` 结构体自身 12 字节（含 `OM_ATOMIC_T(CompStatus)` 4 字节）。系统总开销 = `(56 + 12) × completion_count`。
- **TN 方案 (B/D)**：每个 `Completion` 实例零额外分配（仅 12 字节结构体）。但 FreeRTOS 需将 `configTASK_NOTIFICATION_ARRAY_ENTRIES` 从 1 提升到 2，使每个 TCB 的通知数组翻倍——约增加 8 字节/任务（`ulNotifiedValue[2]` + `ucNotifyState[2]`）。系统总开销 = `12 × completion_count + 8 × task_count`。
- **判定**：当 `task_count < 7 × completion_count` 时（典型机器人系统），TN 方案内存更优。更重要的是 TN 无需运行时动态分配（无 `xSemaphoreCreateCounting` 调用）。

### 性能测试数据

**测试环境：** STM32F427IIH6 (Cortex-M4), armclang, FreeRTOS V11.1.0, \~16MHz (HSI)

| 指标                  | A (irq+Sem) | B (CAS+TN) | 提升 (B/A)    | C (CAS+Sem) | 提升 (C/A) | D (irq+TN) | 提升 (D/A) |
| ------------------- | ----------- | ---------- | ----------- | ----------- | -------- | ---------- | -------- |
| ISR done 路径 (cyc)   | 462         | 88         | **-81.0%**  | 370         | -19.9%   | 180        | -61.0%   |
| Round-trip (cyc)    | 602         | 215        | **-64.3%**  | 520         | -13.6%   | 328        | -45.5%   |
| 快路径 done+wait (cyc) | \~60        | \~20       | -66.7%      | \~36        | -40.0%   | \~44       | -26.7%   |
| 超时额外开销 (cyc)        | \~280       | \~256      | -8.6%       | \~264       | -5.7%    | \~272      | -2.9%    |
| 吞吐量 (ops/sec)       | \~2260      | \~5540     | **+145.1%** | \~2440      | +8.0%    | \~5100     | +125.7%  |

### 归因分析

```
A (irq+Sem) ──[irq_lock→CAS]──→ C (CAS+Sem) : ISR done 节省 ~92 cyc
                                      │
                                      │ [Sem→TN]
                                      ↓
A (irq+Sem) ──[Sem→TN]──→ D (irq+TN) : ISR done 节省 ~282 cyc
                                      │
                                      │ [irq_lock→CAS]
                                      ↓
                                   B (CAS+TN) : ISR done 节省 ~374 cyc (最优)
```

**关键结论：**

1. **CAS 替换 irq\_lock** 贡献约 -92 cycles（对比 A→C 或 D→B）
2. **Task Notification 替换 Semaphore** 贡献约 -282 cycles（对比 A→D 或 C→B）
3. 两者收益**独立且可叠加**，最优方案为 CAS + Task Notification（B 后端）
4. 通用端使用 CAS + Semaphore（C 后端）作为跨平台参考实现

## 构建系统

### 加速后端选择

```
sync_accel=auto (默认)
  ├── FreeRTOS + 存在加速文件 → 启用 CAS+TN accel
  └── 其他 OS / 无加速文件   → 回退 CAS+Sem reference

sync_accel=none
  └── 始终使用 CAS+Sem reference
```

### 编译宏注入

- `OM_SYNC_ACCEL=1` — 全局加速使能（public）
- `OM_SYNC_ACCEL_CAP_COMPLETION=1` — completion capability 声明（public）
- `OM_COMPLETION_ACCEL_ENABLED` — completion.c 内部后端选择

### 文件清单

| 文件                                                 | 职责                                    |
| -------------------------------------------------- | ------------------------------------- |
| `lib/sync/include/sync/completion.h`               | 公共接口 + CompStatus 枚举（含 COMP\_WAITING） |
| `lib/sync/src/completion.c`                        | 通用端 CAS+Sem 实现 + 加速后端调度               |
| `platform/sync/freertos/completion_accel_cas_tn.c` | FreeRTOS CAS+TN 加速后端                  |
| `platform/sync/freertos/sync_accel.lua`            | capability 声明                         |
| `platform/sync/xmake.lua`                          | 构建脚本（加速文件注入 + FreeRTOS includes）      |

## 功能验证

测试程序位于 `samples/sync/sync_completion/main.c`，覆盖 9 个测试组：

| 组 | 测试内容                       |
| - | -------------------------- |
| 1 | 参数校验（NULL 指针）              |
| 2 | timeout=0 未完成即超时           |
| 3 | done 先于 wait（one-shot 消费）  |
| 4 | 重复 done 返回 BUSY            |
| 5 | 有限超时 wait(20ms)            |
| 6 | 单等待者约束（第二个 waiter 返回 BUSY） |
| 7 | wait 先于 done 异步唤醒          |
| 8 | 双轨压测（功能性固定轮次 + 压力时间窗）      |
| 9 | 线程模拟 ISR 高优先级 done 并发      |

### 已知适配点

- **COMP\_WAITING 状态兼容**：组 6 的状态检查需同时接受 `COMP_WAIT` 和 `COMP_WAITING`（CAS 后端在 WAIT→WAITING 间存在短暂过渡）
- **测试线程优先级**：测试控制线程必须使用最高优先级（`completion_priority_test_ctrl()`），防止在压力场景中被 done 工作线程饿死
- **Task Notification Index 1**：FreeRTOS 加速后端占用通知 index 1，其他模块需避免冲突

