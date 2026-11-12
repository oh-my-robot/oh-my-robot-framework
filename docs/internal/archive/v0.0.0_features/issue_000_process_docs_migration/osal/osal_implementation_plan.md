# OSAL工程落地规划

> 来源：历史混合文档（按职责拆分：OSAL 机制合同）。


------

## 顶层执行流程（强制）

后续 OSAL+SYNC 相关工作一律按以下顺序执行：

1. **确认需求**：先澄清目标、边界、验收标准与不做事项。
2. **规划**：给出分阶段或分步骤计划，明确每步的变更范围与验证方式。
3. **确认规划**：由你确认计划后再进入实施，不跨步执行重大改动。
4. **开始执行**：按确认后的计划逐步落地，并在每个阶段完成后进行验证与回报。

执行承诺：

- 本规划文档生效期间，后续任务默认严格遵循“确认需求 -> 规划 -> 确认规划 -> 开始执行”流程。
- 若出现不确定事项，必须暂停并向你确认后再继续。

------

## 总体指导思想（非常重要）

> **先锁语义，再谈性能；先锁接口，再谈实现；先可测试，再可加速。**

因此：

- **中断机制**：冻结，沿用现状
- **SYNC 加速**：接口与选择规则先到位，实现可以延后
- **conformance tests**：不是最后才做，而是“每一阶段的验收门槛”

------

## Phase 0：架构冻结与工程骨架（P0，必须先做）

### 目标

让仓库结构、依赖关系、宏体系**不可再随意漂移**。

### 必做事项（P0）

1. **目录结构最终定版**
    - `osal/include/osal`
    - [`platform/osal/freertos`](../../../../../../platform/osal/freertos)
    - `platform/osal/linux`
    - `sync/include/sync`
    - [`lib/sync/src`](../../../../../../lib/sync/src)（reference）
    - [`platform/sync/freertos`](../../../../../../platform/sync/freertos)
    - [`platform/sync/linux`](../../../../../../platform/sync/linux)
2. **公共头文件边界锁死**
    - OSAL / SYNC 公共头文件中：
        - ❌ 不出现 OS 原生类型
        - ❌ 不出现 `#ifdef FREERTOS / LINUX`
    - capability 宏只能来自 `sync/<os>/sync_port_caps.h`
3. **构建系统宏体系定版**
    - xmake 统一定义：
        - `OM_SYNC_ACCEL`
    - OS 后端声明：
        - `OM_SYNC_ACCEL_X`（按原语扩展；completion 当前不启用加速）

### 产出物

- 可编译的空壳工程
- 所有模块 include 关系合法
- CI 能跑空测试

👉 **这一阶段不写“有逻辑的代码”是完全正常的**

------

## Phase 1：OSAL 最小原语（P0，最高优先级）

### 为什么是第一优先级？

- SYNC、driver、system **全部依赖 OSAL**
- OSAL 一旦语义不稳，后面都会返工

------

### Phase 1.1：OSAL 基础设施（P0）

#### 必做原语（严格按规范）

1. **time**
    - `osal_time_now_monotonic()`
    - `osal_sleep_ms()`
    - Linux / FreeRTOS 语义一致
2. **critical**
    - `osal_irq_lock / unlock`
    - ISR-safe，bounded-time
3. **memory**
    - `osal_malloc / free`
    - 可先直接 wrap libc / pvPortMalloc

#### 验收标准

- 单线程下时间/延时正确
- ISR 中调用 time_now 不崩溃
- critical 嵌套行为可预测

------

### Phase 1.2：OSAL 并发原语（P0）

#### 必做（v1.0 必须）

1. **thread create/join**
    - send / recv
    - `queue_send_from_isr`
2. **event_flags**
    - set / clear / wait
    - `event_flags_set_from_isr`
    - 接口固定 `uint32_t`，可用位由 `OSAL_EVENT_FLAGS_USABLE_MASK` 约束
    - 超出可用位掩码统一返回 `OSAL_INVALID`
3. **mutex**
4. **semaphore**
5. **queue**

#### 验收标准

- timeout = 0 / WAIT_FOREVER 行为正确
- ISR-safe API 不阻塞
- 返回码严格符合规范

👉 **此阶段结束：你已经拥有一个“可用的 OS 抽象层”**

------

## Phase 2：SYNC reference 实现（P0，核心阶段）

### 为什么现在做 SYNC？

- 你已经明确：**SYNC 是系统真正的“语义锚点”**
- reference 实现是未来一切优化的基准

------

### Phase 2.1：completion（P0，必须先做）

#### 实现内容

- `sync_completion_init`
- `sync_completion_complete`
- `sync_completion_wait(timeout)`
- （可选）`reset`
- `complete_from_isr`（在driver驱动中广泛运用）

#### 强制要求

- 仅依赖 OSAL
- 明确状态机（未完成 → 完成）
- 多等待者行为清晰（广播 or 单唤醒）

#### 验收标准（非常重要）

- completion 的 **状态机测试**
- timeout / WOULD_BLOCK 行为
- ISR complete → thread wait 正确

------

### Phase 2.2：event（P0 / P1）

#### 建议

- v1.0 至少实现一种语义（manual-reset 或 auto-reset）
- 提供 `set_from_isr`

#### 验收标准

- event 不丢信号
- 多等待者一致

------

## Phase 3：SYNC selector + 加速框架（P1）

### 注意：**这是“框架先行，性能后补”阶段**

------

### Phase 3.1：selector 机制（P1）

#### 必做

- `sync_selector.c`
- 三段式规则：
    1. `OM_SYNC_ACCEL`
    2. `OM_SYNC_ACCEL_X`
    3. fallback to common

#### 当前可以：

- **所有 selector 先绑定到 reference**
- capability 宏即便定义，也不一定真的有 accel 实现
- completion 当前固定 reference，不走 selector 加速分支

#### 验收标准

- 切换宏不影响行为
- reference 始终可用

------

### Phase 3.2：capability 探测（P1）

#### 必做

- `sync/port/<os>/sync_port_caps.h`
- capability 只能来自 port

------

## Phase 4：Linux / FreeRTOS 双端验证（P1）

### 目标

验证“跨 OS 语义一致性”，而不是性能。

#### 内容

- Linux 下用 pthread + cond 模拟 OSAL
- FreeRTOS 实机/仿真跑 conformance tests

#### 验收标准

- 同一组 SYNC tests 在两个 OS 下全部通过
- 无 `#ifdef OS` 泄漏到业务层

#### 注意

- 若当前系统无Linux支持，则暂时跳过

------

## Phase 5：可选优化与未来阶段（P2）

### 可选项（**明确是 P2，不要提前做**）

- SYNC completion 的 FreeRTOS notify 加速
- event 的 OS 原生 event 加速
- BSP 静态 IRQ 表
- OS irq_glue
- Zephyr / RT-Thread 支持

------

## 优先级总览表（给你一个“一眼决策图”）

| 阶段    | 内容                               | 优先级 |
| ------- | ---------------------------------- | ------ |
| Phase 0 | 架构骨架、依赖、宏体系             | P0     |
| Phase 1 | OSAL 最小原语                      | P0     |
| Phase 2 | SYNC reference（completion/event） | P0     |
| Phase 3 | selector + capability 框架         | P1     |
| Phase 4 | FreeRTOS / Linux 语义一致性        | P1     |
| Phase 5 | 中断重构 / 加速优化                | P2     |

------

## 最重要的一句工程建议

> **当你犹豫“要不要现在就做 X（中断/加速/优化）”时，只问一句：
> “它是否会改变 SYNC 的公共语义或 OSAL 的最小接口？”
> 如果不会 —— 坚决延后。**


## 来源映射

1. 原文件：历史混合文档
2. 拆分策略：保留原信息并以 OSAL 机制视角组织，SYNC 语义见 oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/sync/sync_requirements_volume.md。





