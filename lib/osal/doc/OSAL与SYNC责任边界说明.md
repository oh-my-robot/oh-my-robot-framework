# OSAL 与 SYNC 的职责边界说明（最终版）

> 分层总依赖矩阵以 `oh-my-robot/document/architecture/分层与依赖规范.md` 为唯一规范源；本文只描述 OSAL 与 SYNC 的语义职责边界。

## 1. 核心结论（先给结论）

> **OSAL 与 SYNC 的根本区别不在于“抽象层级高低”，
> 而在于它们对外提供的“稳定性承诺类型”不同。**

- **OSAL 提供的是：稳定的「机制语义合同（Mechanism Contract）」**
- **SYNC 提供的是：稳定的「协作语义合同（Semantic Contract）」**

二者都是**稳定抽象层**，都不允许存在“语义模糊、半稳定”的中间态，但它们**稳定的对象不同**。

------

## 2. OSAL 的职责重新定义（重要修正）

### 2.1 OSAL 是什么

**OSAL 是一个面向全系统的、跨平台稳定的机制抽象层。**

它的职责不是：

- “尽可能屏蔽 OS 差异”
- “让所有行为在所有平台上一样”

而是：

> **定义一组跨平台可依赖的、稳定的并发与时间机制合同，
> 并明确指出哪些行为是被承诺的，哪些行为是明确不被承诺的。**

### 2.2 OSAL 的稳定性定义（关键）

在本架构中，**OSAL 不存在“部分稳定 / 部分不稳定”的中间态**。

对外暴露的每一个 OSAL API，其语义必须被明确划入以下两类之一：

1. **稳定承诺（Stable Contract）**
    - 跨平台必须成立
    - 所有依赖者（driver / service / system / sync）均可安全依赖
2. **明确不承诺（Explicitly Unspecified）**
    - 明确写入规范
    - 禁止任何上层依赖
    - 行为差异不得被视为 bug

**不存在第三种状态。**

> 如果一个行为没有被明确承诺，那么它在 OSAL 的语义中等价于“不存在”。

### 2.3 OSAL 稳定的是什么（而不是什么）

OSAL 稳定的是：

- API 的存在性
- 返回码与超时规则
- 阻塞/非阻塞语义
- 并发安全与可见性规则
- ISR-safe 能力与约束
- 时间的 monotonic 语义

OSAL **不稳定、也不试图稳定**的是：

- 公平性
- 唤醒顺序
- 性能特征
- OS 特有的调度/优化行为

这些要么被明确标为“不承诺”，要么通过 capability 显式表达是否存在，但**不改变核心合同**。

------

## 3. SYNC 的职责重新定义

### 3.1 SYNC 是什么

**SYNC 是一个在 OSAL 稳定机制合同之上构建的、语义稳定的协作层。**

它的目标是解决 OSAL 无法、也不应该解决的问题：

- “等待/完成到底意味着什么？”
- “一次性完成 vs 可重复触发”
- “多等待者如何被唤醒？”
- “超时与竞争如何解释？”

### 3.2 SYNC 的稳定性定义

SYNC 对外承诺的是：

> **只要 OSAL 的稳定机制合同成立，
> 那么 SYNC 对外暴露的行为在所有平台上必须一致。**

因此：

- SYNC 必须有 reference（common）实现
- SYNC 的语义必须通过 conformance tests 锁死
- SYNC 的 per-OS 优化只能改变“怎么做”，不能改变“意味着什么”

------

## 4. OSAL 与 SYNC 的关系

### 4.1 SYNC 依赖 OSAL，但不反向定义 OSAL

一个关键原则是：

> **OSAL 的设计不以 SYNC 的当前实现为依据，
> SYNC 的设计必须受限于 OSAL 的稳定机制合同。**

这意味着：

- SYNC 的迭代不能要求 OSAL 改变已发布的稳定语义
- 如果 SYNC 想要的语义无法在 OSAL 合同内实现：
    - 要么该语义不进入 SYNC
    - 要么作为 SYNC 的可选能力
    - 要么通过 SYNC port 加速实现，但仍不突破 OSAL 合同

### 4.2 OSAL 不是“为 SYNC 服务”的

OSAL 的稳定合同是**面向所有消费者的**：

- driver：依赖 OSAL 的 ISR-safe 机制
- service：依赖 OSAL 的并发与时间能力
- system：在必要时直接使用 OSAL
- sync：只是其中一个、且是“最严格”的消费者

因此：

> **SYNC 不是 OSAL 存在的理由，
> SYNC 只是 OSAL 稳定性最敏感的验证者。**

------

## 5. 原语级职责划分（总结版）

### 5.1 OSAL 原语的职责边界

OSAL 提供的原语必须满足：

- 机制层
- 无复杂状态机
- 稳定合同清晰

典型 OSAL 原语：

- time / sleep
- critical
- mutex
- semaphore
- queue
- event_flags
- memory
- thread（其中 `terminate(other)` 若保留，仅为机制能力，不代表语义安全）

其中：

- event_flags 是“条件位集合机制”，而非“通知语义”
- event_flags 对外接口统一使用 `uint32_t` 掩码，但可用业务位由
  `OSAL_EVENT_FLAGS_USABLE_MASK` 合同约束；超出掩码的输入必须返回 `OSAL_INVALID`
- `OSAL_EVENT_FLAGS_USABLE_MASK` 由激活端口在 `tar_awapi_osal` 单点注入，公共头不提供按平台分支的默认值
- 端口实现目标（如 `tar_os`）与业务目标只能通过依赖继承该宏，不得重复定义
- 当上层仅需要“位条件聚合等待（ANY/ALL）”时，可直接使用 OSAL event_flags；
  当需要稳定协作语义（状态机、多等待者语义）时，应优先使用 SYNC 原语

------

### 5.2 SYNC 原语的职责边界

SYNC 提供的原语必须满足：

- 明确状态机
- 明确等待/完成关系
- 明确并发与竞争语义

典型 SYNC 原语：

- completion
- event（manual / auto reset）
    -（未来）channel / closeable / wait-group

------

## 6. ISR 场景下的边界一致性

- OSAL 提供 ISR-safe **机制合同**
    - `*_from_isr` 的合法性与约束是稳定的
- SYNC 提供 ISR-safe **语义通知**
    - `complete_from_isr`
    - `event_set_from_isr`

中断处理方式如何实现（irq glue / dispatch）：

- 不影响 OSAL 或 SYNC 的语义合同
- 属于实现层问题

------

## 7. 一句话边界总结

> **OSAL 是稳定的机制抽象层，
> 它通过清晰的语义合同为整个系统提供可移植的基础能力；
> SYNC 是稳定的语义抽象层，
> 它在 OSAL 合同之上定义跨平台一致的协作语义。**
>
> **二者都必须稳定，但稳定的对象不同，职责不可混淆。**

------

## 8. 为什么这套边界是“可长期演进的”

因为它满足三个条件：

1. **没有中间态**
    - 稳定 or 明确不承诺
2. **没有反向依赖**
    - SYNC 不驱动 OSAL
3. **没有语义泄漏**
    - OS 差异不会渗透到上层

这正是你一开始坚持的那个直觉真正想要的结果。


