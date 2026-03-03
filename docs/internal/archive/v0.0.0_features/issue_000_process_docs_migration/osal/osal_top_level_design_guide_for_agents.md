# OSAL顶层设计指南ForAGENTS

> 来源：历史混合文档（按职责拆分：OSAL 机制合同）。


## 0. 文档目的与范围

- 本指南用于指导 OSAL（Operating System Abstraction Layer）与 SYNC（Synchronization Semantics Layer）的顶层设计、实现与演进。

    本文不是 API 参考手册，而是一份**设计方法论与工程约束文档**，用于确保：

    - 抽象层长期稳定
    - 语义跨平台一致
    - 系统可演进而不腐化

本规范关注：接口、语义、返回码、超时规则、并发与 ISR 约束、模块依赖与演进路径。
本规范不涵盖：文件系统/网络/进程等 Linux 特有能力（建议放入 service 层适配）。
分层总依赖矩阵以 [`oh-my-robot/docs/architecture/layer_dependency_spec.md`](../../../../../architecture/layer_dependency_spec.md) 为准，本文只定义 OSAL+SYNC 专项约束。

------

## 1. 背景与设计目标

### 1.1 背景

- 在同时支持 MCU 实时系统（如 FreeRTOS）与 Linux 的工程中，直接依赖 OS 原生 API 会导致：

    - 业务代码被 OS 细节污染
    - 语义在不同平台下不一致
    - 后期迁移与优化成本极高

    因此本系统采用两层抽象模型：

    - **OSAL**：提供跨平台稳定的并发与时间“机制合同”
    - **SYNC**：在 OSAL 合同之上，提供跨平台一致的“协作语义合同”

    二者共同构成系统并发与同步的基础设施。
- 未来需要支持多 OS：FreeRTOS（当前重点）、Linux（仿真/测试），并具备扩展至 RT-Thread / Zephyr / ThreadX 的能力

### 1.2 设计目标

1. **可移植性**：上层业务逻辑跨 OS 尽量不改或少改
2. **语义一致性**：等待/完成/超时/唤醒等行为在各 OS 上一致
3. **实时友好**：明确 ISR-safe API 集合与约束（不阻塞、不 malloc、bounded-time）
4. **职责清晰**：OSAL 做最小原语，SYNC 做稳定语义；避免 OSAL 语义膨胀
5. **可测试**：提供 conformance tests 锁定语义
6. **渐进演进**：v1.0 可先稳住 OSAL+SYNC，允许中断入口机制沿用现状；后续可局部引入统一 IRQ 架构增强

------

## 2. 核心设计原则（必须遵守）

本章节定义 OSAL 与 SYNC 设计中必须长期遵守的**抽象质量约束**。这些原则用于防止抽象层退化为能力清单，并控制跨平台差异对上层的影响。

### 2.1 两层合同模型

- **OSAL = Mechanism Contract**
    - 关注“能否做到、如何调用”
    - 提供最小且稳定的系统机制
- **SYNC = Semantic Contract**
    - 关注“这件事意味着什么”
    - 定义明确状态机与协作语义

### 2.2 无中间态原则（强制）

对外暴露的每一个 API 行为必须被明确归类为以下两种之一：

1. **Stable / Contracted**：
    - 行为被明确承诺
    - 跨平台必须成立
    - 上层可以安全依赖
2. **Explicitly Unspecified**：
    - 行为明确不被承诺
    - 上层禁止依赖
    - 平台差异不视为 bug

**不存在第三种“半稳定”状态。**

### 2.3 不反向驱动原则

- OSAL 的稳定合同一旦发布，**不得因 SYNC 的迭代而修改其已承诺语义**
- SYNC 的设计必须受限于 OSAL 合同
- 若某语义无法在 OSAL 合同内实现：
    - 不进入 SYNC v1.x
    - 或作为 SYNC 的可选能力

### 2.4 差异上抛门槛

- **MUST**：仅当差异影响**可观察行为**（正确性、实时性上界、资源安全性），且无法在当前层内部吸收时，才允许将其上抛为显式选择或 capability。
- **MUST NOT**：不得将仅影响实现细节或轻微性能差异的因素上抛给上层。
- **SHOULD**：可通过实现策略吸收的差异，应保留在 OSAL port 或 SYNC port 内部。

> 判据：若某差异上抛后，会迫使大多数（≈80%）用例增加条件分支，则该差异应优先被吸收，而非暴露。

### 2.5 默认统一路径原则

- **MUST**：对上层提供一个无需显式选择即可使用的**默认统一路径**。
- **MUST**：默认路径必须是可移植、可测试、语义稳定的 reference 实现。
- **MAY**：仅在必要时提供显式分支，用于启用额外语义或显著优化。

该原则在本体系中的体现：

- OSAL：Core Contract 始终可用；optional capability 仅作为加成
- SYNC：默认使用 `sync/common`；是否启用加速由 selector 单点决定

### 2.6 选择复杂度控制原则

- **MUST**：抽象层不得退化为“能力清单”。每新增一个显式选择点，必须证明其显著降低总体复杂度或显著提升关键指标。
- **MUST**：显式选择必须集中在单点（如 selector），禁止在业务层散布 capability 检查或 `#ifdef`。
- **SHOULD**：选择点应尽可能向下收敛：
    - 首选：实现内部自动选择（OSAL port / SYNC port）
    - 其次：SYNC selector
    - 最后：上层模块显式分支（仅极少数场景允许）

> 建议的复杂度预算：system 层 = 0；service 层 = 极少；SYNC selector / OSAL port 可承担选择复杂度。

- OSAL 的稳定合同一旦发布，**不得因 SYNC 的迭代而修改其已承诺语义**
- SYNC 的设计必须受限于 OSAL 合同
- 若某语义无法在 OSAL 合同内实现：
    - 不进入 SYNC v1.x
    - 或作为 SYNC 的可选能力

### 2.7 官方接口优先原则（Official-first, 补足其次）

- **MUST**：能使用官方现有接口实现的语义，优先采用官方现有接口，不得重复发明并行语义实现。
- **MUST**：当官方接口不能完美覆盖目标语义时，先保留官方接口作为主路径，再以最小补足机制完善缺口。
- **MUST**：补足逻辑不得改变或对冲官方接口的既有语义；补足仅能覆盖官方接口未定义或未覆盖部分。
- **SHOULD**：在设计文档与代码注释中明确“官方接口负责边界”与“补足逻辑负责边界”，避免语义重叠。
- **SHOULD**：如存在官方接口回退路径，必须显式声明触发条件与行为等价关系。

本原则用于控制抽象层复杂度，确保实现与平台语义一致，减少维护期分叉行为。

## 3. 系统分层与依赖规范

### 3.1 分层（自下而上）

- **bsp**：板级与芯片相关（时钟、pinmux、NVIC、启动代码、基础外设底层）
- **driver**：设备驱动（电机、串口、CAN…），对上提供抽象接口（vtable/ops）
- **osal**：最小 OS 原语（线程/锁/队列/事件标志/时间/临界区/内存等）
- **sync**：稳定同步语义（completion/event 等）
- **service**：日志、诊断、comm、文件系统抽象等
- **system**：系统级组件与控制逻辑

### 3.2 依赖方向（强制）

- `system` → 依赖 `service` / `sync`
- `service(core)` → 优先依赖 `sync`，必要时依赖 `osal`，不直接依赖 `drivers`
- `comm adapter(impl)` → 依赖 `services/comm` + `drivers`（实现侧接入层）
- `lib\source\sync` → **只依赖 `osal`**
- `sync/<os>` → 可使用 OS 原生机制优化，但不得泄漏到 sync 公共头文件
- `osal` → 不得依赖 `sync`
- `driver(core)` → 可依赖 `osal` 的 ISR-safe 子集（见 8.x），不得依赖 OS 原生 API，也不得依赖 `services`
- `bsp` → 不得依赖 `osal/sync`，保持硬件层纯净（OS 无关为目标）

> 注：driver 是否依赖 sync 取决于你的驱动策略。推荐：driver 内部使用 OSAL ISR-safe 原语做快路径，上层通过 sync 组合出业务语义。

------

## 4. 执行上下文模型

### 4.1 上下文分类

- **Thread context**：允许阻塞、等待、调度
- **ISR context**：不允许阻塞，不允许动态内存分配，必须 bounded-time

### 4.2 ISR 总规则（强制）

ISR 内只能调用标记为 ISR-safe 的接口，且必须满足：

- 不阻塞（无 sleep、无 wait）
- 不 malloc/free（除非明确声明为 ISR-safe 且 bounded）
- bounded-time（O(1) 或明确上界）
- 不调用不确定耗时的日志/IO

------

## 5. 统一返回码与错误处理

### 5.1 返回码集合（OSAL + SYNC 共用）

- `OK`
- `TIMEOUT`
- `WOULD_BLOCK`（timeout=0 未满足条件）
- `NO_RESOURCE`（内存/句柄不足）
- `INVALID`（参数/句柄/上下文非法，如 ISR 调用 thread-only API）
- `NOT_SUPPORTED`（port 不支持该能力）
- `INTERNAL`（不可恢复内部错误，尽量少用）

### 5.3 规则

- Thread-only API 在 ISR 调用：必须 `INVALID`（debug 可断言）
- 所有 wait API：必须支持 `timeout=0` 与 `WAIT_FOREVER`
- 所有 handle：必须支持初始化/反初始化规则并明确生命周期

------

## 6. 时间与超时语义

### 6.1 时间基准

- 所有超时基于 **monotonic time**

### 6.2 超时规则（统一）

- `timeout=0`：不阻塞，条件不满足则返回 `WOULD_BLOCK`
- `timeout=WAIT_FOREVER`：无限等待
- 其他：相对超时，超时返回 `TIMEOUT`

------

## 7. OSAL（最小原语）设计规范

### 7.1 OSAL 的职责

OSAL 是一个**面向全系统的稳定机制抽象层**，其职责是：

- 提供跨平台可依赖的并发与时间机制
- 明确哪些行为被承诺，哪些行为明确不承诺
- 为 driver / service / system / sync 提供统一基础

OSAL **不负责** 定义复杂协作语义或状态机。

### 7.2 必须提供的原语（v1.0）

- **time**：`now_monotonic()`、`sleep_ms()`、（可选）`delay_until()`
- **critical**：`irq_lock/unlock`（极短保护）
- **thread**：create/start/join（join 可选），priority 抽象
- **mutex**：lock/unlock（递归与 PI 作为 capability）
- **semaphore**：wait/post
- **queue**：send/recv（含 ISR-safe send）
- **event_flags**：set/clear/wait（含 ISR-safe set）
- **memory**：malloc/free（可选 pool）

### 7.3 OSAL 原语的合同化写法

每一个 OSAL 原语必须在规范中明确以下内容：

1. **Purpose**：用途
2. **Stable Semantics**：稳定承诺
3. **Explicitly Unspecified**：明确不承诺
4. **ISR Rule**：是否 ISR-safe 及约束
5. **Timeout & Return Codes**
6. **Capability**：可选能力（如递归锁、PI）

未被列入 Stable Semantics 的行为，视为不存在。

### 7.4 ISR-safe 子集（v1.0 必须定义）

至少应包含：

- `queue_send_from_isr`（或等价）
- `event_flags_set_from_isr`
- `critical_enter/exit`（或 `irq_lock/unlock` 的 OSAL 表达）
- `time_now`（只读、bounded）

其余是否支持 ISR-safe 由 capability 明确。

### 7.5 OSAL 头文件规则

- OSAL 公共头文件不得暴露 OS 原生类型
- OSAL 公共句柄必须使用**不透明强类型指针**（`struct xxx_handle*`），禁止使用 `typedef void*`
- OSAL port 内部可使用 OS 原生类型，但必须封装在 `.c` 文件或私有头文件
- OSAL 公共头文件不得出现端口分支条件（如 `OM_OSAL_PORT`、`OSAL_PORT_*`）；
  平台差异只能在 port 实现层或构建注入宏中吸收

### 7.6 event_flags 专项合同（v1.0）

- 对外接口统一为 `uint32_t` 事件掩码（API 容器固定）。
- 可用业务位通过 `OSAL_EVENT_FLAGS_USABLE_MASK` 定义（平台合同）。
- `OSAL_EVENT_FLAGS_USABLE_MASK` 必须由 port/构建系统注入，公共头不得提供平台默认兜底值。
- `set/clear/wait/set_from_isr` 输入位超出 `OSAL_EVENT_FLAGS_USABLE_MASK` 时，必须返回 `OSAL_INVALID`。
- `wait` 为线程上下文接口；ISR 路径仅允许 `set_from_isr`（不阻塞）。
- 当需求是“位条件聚合等待（ANY/ALL）”时，可直接使用 OSAL event_flags；
  当需求是“协作语义状态机（manual/auto-reset、多等待者策略）”时，应优先落在 SYNC。

### 7.7 thread terminate(other) 风险合同（v1.0）

- 允许保留 `terminate(other)` 能力，但必须标注为高风险语义。
- OSAL 不承诺自动回收目标线程持有的业务资源（锁/外设/应用内存）。
- `terminate(self)` 视为非法调用，必须使用 `osal_thread_exit` 自退出。
- 推荐默认路径仍为协作退出；`terminate(other)` 仅用于受控场景。
- `osal_thread_join` 在 v1 明确为延期项，当前 FreeRTOS 端允许保持 `OSAL_NOT_SUPPORTED`。

### 7.8 timer 专项合同（v1.0）

- `osal_timer_create` 采用 `OSAL status + out_handle` 形态，禁止返回“句柄或 NULL”混合语义。
- `timer` v1.0 为线程上下文接口，不提供 `*_from_isr` 版本。
- `osal_timer_reset` 语义固定为“重启/重装（rearm）”：未运行时等价 `start`。
- `osal_timer_delete` 采用严格前置条件模型：调用方必须确保无并发访问、无并发回调；OSAL 不做隐式清理。
- 回调合同为“严格非阻塞”：禁止在回调中执行阻塞等待与长耗时逻辑。
- `start/stop/reset/delete` 在命令未入队时，返回码按等待语义映射：
  `timeout=0 -> OSAL_WOULD_BLOCK`；
  `timeout>0 或 WAIT_FOREVER -> OSAL_TIMEOUT`。

------

## 8. SYNC（稳定语义层）设计规范

### 8.1 SYNC 定位

SYNC 在 OSAL 稳定机制合同之上，提供**跨平台一致的协作语义**。

其关注点包括：

- 等待/完成的意义
- 状态机定义
- 并发与竞争规则
- ISR 到线程的通知语义

### 8.2 实现形态（强制）

SYNC 必须有两类实现路径：

1. **Reference 实现（lib\source\sync）**

- 位置：`lib\source\sync`
- 依赖：只依赖 OSAL
- 作用：语义基准、可移植兜底

1. **Optimized 实现（sync/< os >，可选）**

- 可使用 OS 原生机制优化（如 Linux futex / eventfd、RTOS notify 等）
- 必须通过同一套 conformance tests
- 不得泄漏 OS 类型到 SYNC 公共头文件

### 8.3 v1.0 推荐同步抽象（优先级）

- **completion**（强烈推荐 v1.0 必做）
    - `done()` / `wait(timeout)`（不提供公共 `reset`）
    - `done()` 必须支持 ISR 路径调用
- **event**
    - manual-reset / auto-reset 两种语义（至少一种）
    - `set/reset/wait(timeout)` + `set_from_isr()`（如需）
- 后续 v1.x：channel、periodic gate、wait-group 等

### 8.3.1 completion 专项合同（v1.0）

- completion 是“单等待者 one-shot 完成事件”，不得扩展为多等待者广播模型。
- `done` 先于 `wait` 时，`wait` 必须立即成功并消费完成态。
- `COMP_DONE` 状态下重复 `done` 必须返回 `BUSY`，不得静默幂等吞掉重复触发。
- `wait` 仅允许在线程上下文调用；ISR 路径禁止 `wait`。
- completion 仅承载“完成通知”语义，不承载驱动业务条件（字节阈值/协议判定等）。
  业务条件必须留在驱动或更高层状态机中。
  若多驱动复用同类条件等待，应在 completion 之上新增 helper，而不是修改 completion 语义。

### 8.4 SYNC 原语的规范要求

每一个 SYNC 原语必须明确：

1. **状态机定义**（状态与转移）
2. **wait / set / reset 的精确定义**
3. **多等待者语义**（广播 / 单消费者）
4. **超时与返回码**
5. **from_isr 行为**
6. **生命周期规则**（init/deinit）
7. **一致性测试用例（Conformance Tests）**

`lib\source\sync` 为语义基准实现；`sync/<os>` 仅用于性能优化。

> completion 特别约束（当前冻结）：
>
> - completion 不再提供基于 task notification index 的 notify 专用后端。
> - 原因：任务通知 index 属于任务级共享资源，无法满足“同任务跨模块复用”约束。
> - 结论：不得再引入 `OM_SYNC_ACCEL_COMPLETION` 旧宏路径。
> - 当前能力表达：使用 `OM_SYNC_ACCEL_CAP_COMPLETION` 作为 capability 宏；未声明时必须回退 reference。

### 8.5 构建期策略（Policy）

全局加速开关 ``` OM_SYNC_ACCEL```

- **未定义**：强制使用 `lib\source\sync`

- **已定义**：允许使用加速，但仍需能力声明

### 8.6 后端能力声明（Capability）

后端能力声明（Capability）

``` c
OM_SYNC_ACCEL_EVENT
OM_SYNC_ACCEL_CHANNE
……
```

#### 规则（强制）

- capability **只能** 由 `sync/port/<os>` 声明
- 其他模块 **不得定义/覆盖**

### 8.7 三段式选择规则（强制）

对于任一 SYNC 原语 `X`：

1. 未定义 `OM_SYNC_ACCEL`
     → 使用 reference
2. 定义 `OM_SYNC_ACCEL`，但未定义 `OM_SYNC_ACCEL_X`
     → 使用 reference
3. 同时定义
    - `OM_SYNC_ACCEL`
    - `OM_SYNC_ACCEL_X`
         → 允许使用 port 加速实现

### 8.8 合规性要求

- 加速实现必须通过与 reference 相同的 conformance tests

- `*_from_isr` 行为必须一致

- capability 是“事实声明”，不是“启用指令”

------

## 9. 中断处理（Interrupt Handling）章节（v1.0 决策版）

> 本章同时给出：
>
> - v1.0 的“冻结策略”（允许沿用现有中断机制）
> - 未来增强的“目标架构”（静态表 BSP 分发 + OS irq_glue）
>     以确保接口层留足扩展缝，不导致未来推翻重构。

### 8.1 现状与约束（v1.0）

#### 8.1.1 v1.0 策略（强制说明）

- v1.0 **不强制重构**中断入口/分发机制，可沿用当前 board/driver 中已有方式（例如在 ISR 中直接调用 `hal_xxx_hw_irq(...)`）
- v1.0 的核心目标是：**固定 ISR-safe 语义与接口边界**，确保未来可平滑替换中断入口组织方式

#### 8.1.2 允许沿用现有机制的前提（强制）

即使沿用现有 ISR 入口，也必须满足：

1. ISR 内只调用 OSAL/SYNC 的 ISR-safe API
2. driver 不得直接调用 OS 原生 `FromISR` API（如 `xQueueSendFromISR`），必须通过 OSAL
3. ISR 不得把 OS 差异（need_yield/enter/leave/direct）暴露到上层接口

### 8.2 OS 差异处理原则（强制）

- `need_yield` 必须被视为 OS 差异，封装在 OSAL port 的 `*_from_isr` 实现内部
    - FreeRTOS：内部执行 `portYIELD_FROM_ISR`（如需要）
    - Linux：恒无此概念
- SYNC 对外 API 不暴露 OS 差异；提供 `*_from_isr` 版本满足 ISR 场景

### 8.3 未来增强目标：BSP 静态分发表 + OS irq_glue（规划，不作为 v1.0 交付）

当需要统一多 OS 中断风格时，将引入增强架构：

- **BSP（OS 无关）**：提供静态 `irqn -> handler(ctx)` 映射表（编译期生成）
- **OS irq_glue（osal/port/）**：承接各 OS 中断入口形式并调用 `bsp_irq_dispatch(irqn)`
- **driver trampoline**：将统一签名 `void (*)(void*)` 适配到现有 `hal_uart_hw_irq(...)` / `hal_can_hw_irq(...)` 等不同签名

该增强架构的目标：

- BSP OS 无关
- driver 不写 OS ISR 入口符号
- Zephyr direct / normal 等差异集中在 port glue 层
- 性能接近直连，保留统一插桩与扩展缝

> 注意：该增强架构未来引入时，应做到“新增 port glue / BSP 映射文件”为主，不改 SYNC 语义与 OSAL 上层接口。

------

## 9. 统一测试与验收（Definition of Done）

### 9.1 必须具备的测试

- **OSAL conformance tests**：timeout、queue 满空、event_flags ANY/ALL、ISR-safe 不阻塞等
- **SYNC conformance tests**：completion/event 状态机、多等待者、超时、从 ISR 发信号等

### 9.2 验收标准

- `sync/common` 在 FreeRTOS 与 Linux（仿真）下语义一致并通过测试
- 若启用 `sync/port/<os>`：必须同样通过 conformance tests
- driver/service/system 不直接依赖 OS 原生 API

------

## 10. 关键架构决策摘要（v1.0）

1. OSAL = 最小机制层；SYNC = 稳定语义层
2. 业务层优先依赖 SYNC；driver 仅在必要时使用 OSAL ISR-safe 子集
3. SYNC 必须有 common 实现，并可选 per-OS 优化实现
4. v1.0 暂不重构中断入口，沿用现状；但必须锁定 ISR-safe 语义与禁止项
5. 未来需要统一 IRQ 风格时，引入 BSP 静态表 + OS irq_glue 增强方案

------

## 11. 附：术语

- **Mechanisms**：最小原语能力（OSAL）
- **Semantics**：稳定的等待/完成语义（SYNC）
- **ISR-safe API**：可在中断上下文调用的 API，满足不阻塞、不 malloc、bounded-time


## 来源映射

1. 原文件：历史混合文档
2. 拆分策略：保留原信息并以 OSAL 机制视角组织，SYNC 语义见 oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/sync/sync_requirements_volume.md。






