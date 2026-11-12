> ⚠️ **挂起档案（2026-09-09）**：本文件为"自研内核（协作式 v1 → 抢占/协作双模可配）"方向的讨论全档（v0 逐字基线 + v1 修订），**已挂起待重议**。挂起时用户指示：重议方向拟**跳过协作式，直接设计抢占式内核**。当前目标 = Bootloader 裸机 osal-none 适配，见同目录 `osal_none_core_design.md`（v2）。恢复内核讨论时以本文件为输入；**勿据本文档落码**。

# osal-none 核心设计（演进工作文档）

> 修订记录：
> - v0（2026-09-08）：设计讨论首轮逐字基线（一~四，内容存档于会话记录）
> - v1（2026-09-09）：总基调定调 + 三轮讨论结论修订——〇 总基调与演进缝、一 定位改写、二 深潜小节 2.1-2.5、五 Bootloader 消费边界审视
> 性质：工作文档——随讨论演进，修改以讨论结论为准
> 关联：ADR-0022 (osal_none_bare_metal)、`platform/osal/none/`（主工作树与 `_verify_init/omr` 镜像同步）

---

## 〇、总基调与设计原则（2026-09-09 定调）

**本项目不是"为 Bootloader 而做的最小 osal 端口"，而是自研内核的起点**：先通过实现**协作式内核 v1**，深入理解操作系统内核的设计与工程考量；进而演进为**抢占式 / 协作式双模可配内核**。

- **双约束**：v1 允许在必要处简化设计（可接受简化实现、可接受推迟实现）；但**演进策略必须保留**——凡简化处必须留"演进缝"，禁止做出封死演进路径的决策。
- 取舍论证口径：凡"因 bootloader 预算所以 X"的论证一律改写为"v1 形态约束 Y（可配、可演进）"。Bootloader 只是消费方之一（见五），不是设计动机。
- 演进参照系：FreeRTOS（`configUSE_PREEMPTION` 全内核模式开关）、Zephyr（同一内核并存协作线程与可抢占线程）——证明"协作不是异类，是调度策略的一个维度"。

### 演进缝总表

| # | 结构缝 | v1 形态 | 演进去向 |
|---|---|---|---|
| S1 | 调度核心与对象层解耦 | 对象层（sem/queue/mutex/event）只消费 sched 原语 | 双模共享对象层与时间引擎，差异收敛在调度核心 |
| S2 | 时间引擎两入口 | `time_add / time_cancel` + `time_drive` | 结构可换（delta 链 / 时间轮 / tickless 时钟） |
| S3 | ISR 面记账式 | ISR 只记账不切栈、无需 resched 标志 | 抢占模式：ISR 退出切栈 + resched / yield-from-ISR 协议 |
| S4 | 等待者排序器可配 | FIFO（对拍 freertos 契约） | 优先级排序 wait_q + 优先级继承（与抢占同步落地） |
| S5 | 就绪结构集中实现 | 有序单链（取头 O(1)、插入 O(P≤8)） | per-prio 链 + 就绪位图（抢占路径 O(1)） |
| S6 | 堆算法单文件可换 | heap_4 同构（地址序 first-fit + 合并） | TLSF / 多池 / 外部堆 |
| S7 | timer 回调执行器模块化 | 调度器前置就地执行（无 daemon） | daemon 策略 / 直接操作 + 临界区化（抢占） |
| S8 | 对齐参数化 per-arch | x64=16B、ARMv7-M=8B | 随 arch ABI 走（见 2.4） |
| S9 | 超时链暴露 next-deadline | idle 睡眠的输入 | one-shot 定时硬件编程（tickless） |

## 一、坐标系：这个"内核"要回答什么问题

（v0 原文，末段按基调修订）osal-none 是 OMR osal 面（12 头 8 族）在裸机上的一个实现——自研内核的 **v1 协作形态**。第一原则不变：**行为对齐 freertos 端口**（8 套实机语料就是拿 freertos 语义对拍的），一切与 freertos 行为的差异必须显式、有理由、可文档化。

v1 形态约束（**全部可配，非永久预算**）：静态 TCB 槽默认 ≤8（`OM_OSAL_NONE_MAX_TASKS`）、堆池默认 4096B（`OM_OSAL_NONE_HEAP_SIZE`，0=禁用堆）、协作调度（无抢占中断切换）、x64 host 先行、ARM 端口后续。消费形态：引导下载编排侧 / 主机测试 / 内核教学；**引导决策核不消费内核**（见五）。

## 二、成熟框架调研结论

（v0 表 + v1 行修订；深潜小节 2.1-2.5）

| 机制 | 成熟方案 | 关键事实 | 对我们的取舍 |
|---|---|---|---|
| 协作模式全景 | **FreeRTOS** `configUSE_PREEMPTION=0` | 切换只发生在：任务主动 yield / 阻塞 / exit；**tick ISR 只做 `vTaskIncrementTick`，不做 `vTaskSwitchContext`**——不切栈，只把到期任务转就绪。tickless idle 与协作模式冲突（官方已知限制） | 全盘采纳为 v1 调度语义；双模演进见 〇 |
| 协作下的优先级 | 同上 | 协作模式下**不主动让出的任务会饿死他人，包括更高优先级**——"priority loses its meaning"，优先级只在让出点生效 | 语义如实继承并写入端口文档 |
| **idle 任务的关键作用** | 同上 | 协作模式下**idle 任务每轮 `taskYIELD()`**——这是"tick 到期唤醒的睡任务能真正获得 CPU"的唯一通道 | idle 由 `osal_kernel_start()` 调用栈承担，不建 idle TCB（缝 S9 关联：idle 的"睡到下个 deadline" = next-deadline 接口的消费点） |
| 统一超时子系统 | **Zephyr** | 睡眠、对象等待超时、`k_timer` 共用同一超时队列，全部操作收敛到 `_add_timeout()` / `z_clock_announce()` 两入口 | 同构采纳为时间引擎（**软定时器 = 时间引擎的用户**）；与 wait_q 正交双挂见 2.1；绝对 vs 增量见 2.2；两入口 + next-deadline（缝 S2/S9） |
| 等待队列排序 | Zephyr 按优先级 / FreeRTOS FIFO（尾插头取） | 唤醒顺序是**可观察语义** | v1 = FIFO（对拍基线一致）；等待者排序 = **调度策略**而非对象语义，随模式演进（2.1、缝 S4） |
| 就绪结构 | FreeRTOS per-prio 链 + 就绪位图 O(1)；Zephyr dlist/位图/红黑树可配 | 高位扫描选最高优先级 | v1 = 有序单链（活跃优先级 ≤ 任务 ≤ 8）；谱系与"抢占为何要 O(1)"见 2.3（缝 S5） |
| 静态堆 | **FreeRTOS heap_4** | 块头内嵌 `BlockLink_t`（size + next，MSB=占用标志）；free list **按地址排序** → free 时相邻合并；first-fit + 大块分裂；尾端 sentinel | 大小 = 编译期可配默认（非固定）；对齐 = **arch ABI 属性**（x64 16B / ARMv7-M 8B），参数化不硬编码（2.4、缝 S6/S8） |
| 任务自删回收 | FreeRTOS `vTaskDelete(NULL)` | 自删任务挂 `xTasksWaitingTermination`，**由 idle 任务代回收**（自身栈正在用，不能自己释放） | 同思路不同载体：切换完成后由"新 current"回收前一 EXIT（切换返回点即回收点）；`terminate(他人)` 即时回收 |
| 软件定时器执行模型 | FreeRTOS：daemon 任务 + 命令队列；Zephyr：回调在 ISR 上下文 | daemon 是独立任务，start/stop 经命令队列与 daemon 通信——`timeout_ms` 参数由此而来 | v1 = 调度器前置就地执行（无 daemon）；执行模型与 `timeout_ms` 参数语义完整辨析见 2.5（缝 S7） |
| 无栈协程 | Contiki protothread | 无每任务栈、局部变量不跨阻塞点、阻塞只能出现在进程顶层函数 | **拒绝**——OSAL 面是阻塞 API 形态，sync/async 层消费栈式线程；记档为反面参照 |

### 2.1 等待队列与超时队列：正交双挂（v1 补）

```
对象侧（回答"谁在等这个对象"）          时间侧（回答"下一个该醒谁"）
┌────────────────────────┐      全局唯一超时队列（按绝对时刻升序）
│ sem: count=0            │      ┌──────────────────────────────┐
│ wait_q: ──► [A] ──► [B] │      │ [B @103ms] ──► [A @150ms]     │
└────────────────────────┘      └──────────────────────────────┘
    A 同时挂在两处：sem 的等待链 + 超时链（FreeRTOS TCB 的
    xStateListItem/xEventListItem 双节点即为此双重身份）
```

- **等待队列（wait_q）**：每个同步对象内嵌一条，装着因"该对象条件不满足"而阻塞的任务，语义按对象（sem 没计数 / 队列空 / 锁被占）。
- **超时队列**：全局一条，装着睡眠任务 + 带超时等待者 + 软件定时器（非任务节点），按唤醒时刻排序。
- **职责边界**：超时子系统只做登记/取消"绝对唤醒时刻"与到期分发，**不知对象语义**；到期只负责"摘对象链、写 TIMEOUT、转就绪"，post 唤醒反向摘除超时节点。两条唤醒路径（post 先到 / 时间先到）互斥于临界区 → 无双重唤醒、无竞态。
- **等待者排序 = 调度策略，不是对象语义**：FreeRTOS FIFO；μC/OS 系按优先级排列（压缩无继承机制下的反转窗口）；**协作模式无抢占即无反转，FIFO 即正确**。→ v1 固定 FIFO；双模阶段把"优先级排序 wait_q + 优先级继承"作为调度策略项同步落地（缝 S4）——那时才有完整的语境（继承需锁持有者提权）。

### 2.2 绝对 vs 增量排序（v1 补）

| | 绝对（每节点存截止时刻值） | 增量（每节点存与前驱的差值） |
|---|---|---|
| 到期判断 | `now` 与头节点时刻比较 | 头节点差值递减到 0 |
| 插入/删除 | 插入 O(n)；**删除任意节点 O(1)**，无级联 | 插入 O(n)；**删除中间节点需把差值并入后继**（级联维护，正确性陷阱） |
| 回绕 | 需窗口安全比较（osal 合同：差值 <2³¹ms 有定义） | 差值小，天然不怕回绕 |
| 适合 | 周期 tick 驱动（SysTick / 轮询） | one-shot 定时硬件（tickless）——头节点差值 = 可直接编程硬件的间隔 |
| 内存 | 每节点 4B 时刻 | 每节点差值 |

FreeRTOS = 绝对时刻 + 双链乒乓处理 tick 回绕；Zephyr 新版 = delta（配合 tickless 时钟驱动）。

**"≤16 节点直接存绝对时刻"的含义**：规模估计 = 阻塞任务（≤8 静态槽）+ 活跃定时器（v1 量级小）≈ 十几节点。此量级下插入 O(16) 只在登记阻塞时发生、4B/节点可忽略；换来增量编码的级联维护逻辑整体消失，且调试期节点值 = 可直读的绝对时刻。v1 tick 源为周期 SysTick / host 轮询，不需要头差值去编程 one-shot 硬件 → 绝对时刻是最简正确解。规模上到上百或上 tickless 时换 delta/时间轮，**接口不变**（缝 S2/S9）。

### 2.3 就绪结构谱系（v1 补）

| 结构 | 选下一个 | 插入 | 额外内存 | 同优/公平 | 代表 |
|---|---|---|---|---|---|
| 无序 FIFO 单链 | O(1) 取头 | O(1) | 2 指针 | 无优先级 | 极简内核 |
| **有序单链（prio 降序，同优尾插）** | **O(1) 取头** | **O(P)** | **2 指针** | 同优 FIFO | **v1 选型** |
| 每优先级链数组 + 就绪位图 + CLZ | O(1) | O(1) | ~P×8B（32 级≈256B 静态） | 同优链内轮转 | FreeRTOS / μC/OS / ChibiOS / 多数商用 |
| 红黑树 | O(log n) | O(log n) | 每节点 2-3 指针 + 色位 | 动态优先级/公平调度 | Zephyr（可配）、Linux CFS |
| SMP per-CPU 队列 + 负载均衡 | — | — | — | — | 多核范畴，超出本项目 |

**关键工程考量**：抢占模式下"选下一个"发生在 **ISR 退出路径**——每次 tick/中断唤醒都可能触发切换，调度选择是中断关键路径热代码，必须短且**时间有界**；32 级优先级恰好一个 `uint32`，CLZ 数条指令定位最高就绪级 → 位图方案统治抢占 RTOS 的原因。**协作模式**下"选下一个"只在任务主动让出时发生，不在中断路径、延迟不敏感 → O(n≤8) 插入微不足道，2 指针的有序单链是正确简化。同优策略亦随模式：协作 = FIFO（自觉让出形成轮转），抢占 = 时间片 RR（FreeRTOS `configUSE_TIME_SLICING`）。就绪操作收敛在调度核心 3-4 个函数内，双模阶段换实现零波及（缝 S5）。

### 2.4 堆谱系与对齐（v1 补）

**谱系**：

| 算法 | 合并 | 时间有界 | 碎片 | 一句话 | 适用 |
|---|---|---|---|---|---|
| heap_1 | —（只分配不释放） | ✓ | 无（不释放） | 单向递增指针 | 启动期/单程分配 |
| heap_2 | ✗（best-fit 不合并） | ✗ | 严重 | 官方弃用 | — |
| heap_3 | 交给 libc | — | — | 包一层 malloc+锁 | 有 libc 堆时 |
| **heap_4** | **✓ 地址序 + first-fit + 相邻合并** | ✗（扫链） | 低 | 合并抑制外碎片 | **v1 同构** |
| heap_5 | 同 heap_4 | ✗ | 低 | 多段不连续内存 | 分散 RAM 芯片 |
| TLSF | ✓（边界标记双向合并） | **O(1)** | 低 | 两级位图 + 分级桶 | 硬实时首选（μC/OS-III） |
| buddy | ✓（2 幂伙伴合并） | O(log n) | 内碎片最坏 ~50% | 按 2 幂分块 | 内核页分配 |
| 定长池/slab | — | O(1) | 无外碎片 | 每对象定长 | 驱动对象专用 |

v1 选 heap_4 同构的理由：单池 4KB 规模下地址序 + first-fit + **相邻合并**（free list 永无相邻空闲块）已把外碎片抑制到可用水平；实现约百行、教学清晰；时间无界在协作模式无人在意（不在中断路径）。TLSF 的 O(1) 是给抢占硬实时的——属双模阶段（抢占后 malloc 还要禁调度/加锁保护，FreeRTOS `pvPortMalloc` 即挂起调度器——对象层共享、保护语义随模式变的又一例）。堆 = 单文件 + 内部 API 稳定（`osal_none_malloc/free`），换算法不动内核（缝 S6）。

**大小与对齐**：池大小编译期可配（`#ifndef` 默认 4096，0=禁用堆 → 动态对象 create 返 `OSAL_NO_RESOURCE`）；"对齐"指**块首地址（以及池基、块尺寸步进）是 N 的倍数**，头尺寸取 N 的倍数 → 用户指针自然 N 对齐。N 由 **arch ABI** 决定而非设计偏好：x64（Windows/System V）call 边界 `rsp` 16 对齐（汇编切换直接换 rsp，任务栈顶不对齐首次切入即崩）；ARMv7-M AAPCS 8 字节双字对齐；ARMv8/RISC-V 16。→ 对齐值参数化 per-arch，通用内核不硬编码（缝 S8）；当前 x64 落地 16。

### 2.5 定时器执行模型与 timeout 参数语义（v1 重写）

**困境**：到期检测必然发生在时间驱动路径（target = SysTick ISR），但用户回调**绝不能跑在 ISR 里**（栈小、优先级倒挂、易违反 ISR 面契约）→ "到期"与"执行回调"必须拆开：ISR 只记账，回调挪到某任务上下文。

| 方案 | 执行上下文 | 资源代价 | 回调约束 | 与协作模式的关系 |
|---|---|---|---|---|
| A. FreeRTOS daemon | 独立定时器服务任务 | 1 TCB + 1 栈 + 1 命令队列 + 双链 | 不得阻塞（阻塞 = 全部定时器停摆）；回调内调 timer API 须防命令队列满 | daemon 是普通任务：协作模式下只能在别人让出后跑 |
| B. Zephyr `k_timer` | 系统时钟 ISR | 零 | 最严（通常转 k_work 到线程） | ISR 天然"任意时刻打断" |
| **C. v1 选型** | **调度器入口（让出任务栈上）+ idle 兜底** | **零** | 不得阻塞（`in_cb` + `OSAL_ASSERT`）；可自由调非阻塞 API | 与 A 等价但更早：让出后、选任务前 |

**方案 C 时间线**：

```
t=0    任务 A：osal_timer_create(T, 10ms, periodic) + start
        → T 挂超时链 @now+10                          （直接操作，无 daemon）
t=5    任务 A 阻塞等 sem → schedule() → 无 fired → 选 B 切走
t=10   SysTick ISR → time_drive(now)：T 到期
        → 摘 T → 挂 fired（周期 → 重挂 @now+10）       ← ISR 只记账
t=15   任务 B 调 sleep(2) → schedule()
        → 前置步：fired 非空 → 在 B 的栈上执行 T 回调   ← 让出任务上下文
        → 清 fired → 选下一个任务切走
```

回调延迟上界 = 系统"让出节律"；全阻塞时 idle 循环兜底。回调执行期置 `in_cb`，阻塞类 API（wait/sleep/lock）入口检测即断言——配合 `osal_timer.h` 头契约"非阻塞与短执行"。回调内可自由调非阻塞 API（post/set/start/stop/delete 自己），任务上下文唯一活动 → 安全，比 FreeRTOS 命令队列约束宽松。

**`timeout_ms` 参数语义辨析（2026-09-09）**——它是**命令通道架构的指纹**，不是定时器语义：

- 来源：FreeRTOS daemon 架构。任务调 `xTimerStart` → 构造命令消息 → `xQueueSend(xTimerQueue, …, xTicksToWait)`；队列满 + `timeout=0` → 立即失败（freertos 端口映射 `WOULD_BLOCK`），满 + `timeout>0` → 阻塞等空位、到期失败（`TIMEOUT`，见 `osal_timer_freertos.c` 的 `osal_timer_queue_cmd_result_to_status`）。`pdPASS` 只表示**命令已入队**，定时器实际生效在 daemon 处理命令的时刻（存在不可观察的排队窗口）。
- 生态对照：FreeRTOS（daemon → 有控制超时参数）；Zephyr `k_timer` / ChibiOS vt / Contiki etimer（直接操作 → **无**该参数）。**有该参数 = daemon 化异步命令的标记**。
- 我们（直接操作）与 freertos 端口的可观察差异：
  1. **WOULD_BLOCK/TIMEOUT 永不存在**（无通道容量概念）——`start/stop/reset/delete` 除校验外恒 `OSAL_OK`；freertos 端口在 daemon 积压时可返回二者 → 跨端口行为差异，端口文档写明。
  2. **生效时刻同步确定**：返回 OK = 已入链、到期从此刻计；FreeRTOS 返回后存在排队窗口。
  3. **回调上下文无陷阱**：FreeRTOS 回调内调 timer API 有"daemon 正执行你而无法消费新命令 → 队列满自锁"陷阱，只能 block=0/FromISR 且失败静默；我们直接操作不依赖 daemon 消费，回调内调 start/stop/reset/delete 安全，**自删合法**（fire 循环先摘节点再回调）。
- 不变面（语义等价核心）：reset 语义（未运行 = start、运行 = 重装）；回调单飞互斥、delete 永不与在飞回调并发；协作模式下延迟模型与 daemon 同族（都在让出点后）。

**缝 S7 注记**：抢占模式直接操作需给控制操作加短临界区（Zephyr 路线，已证明可行）；daemon 在抢占模式的优势是 ISR 发命令永不碰链。两策略由"回调执行器模块化"覆盖，v1 不用定夺。

## 三、核心设计（osal_none.c 骨架）

### 3.1 任务与就绪

```
TCB（静态数组槽，MAX_TASKS）：
  sp 保存槽 | state(READY/BLOCKED/EXIT) | prio | name[OSAL_TASK_NAME_MAX](拷贝,防悬垂)
  dlist 节点(就绪链/对象等待链/超时链三场合一) | 唤醒结果槽
```

- 就绪链：单一 dlist，prio 降序插入，同优尾插 → **取头即最高优先级最老任务**（谱系选型依据见 2.3）。
- 对象等待链：每个同步对象内嵌一条 dlist；`block`/`wake_one` 摘最老 → FIFO 公平，与 freertos 一致（排序策略演进见 2.1/缝 S4）。
- **关键简化——协作内核无任务↔任务竞态**：任意时刻只有一个活动任务，任务段操作对象无需互斥；临界区只存在于"任务 ↔ ISR"共享面（就绪链/计数/等待链/超时链），统一短 crit（arch：关中断；host：no-op）。mutex 同理：**协作模式下优先级继承无意义**（持锁者不会被抢，不需要提优先级来尽快放锁）——架构红利，文档写明；继承与抢占同步落地（缝 S4）。

### 3.2 调度原语（内核提供给各族的"挂起"接口）

各同步族（sem/queue/mutex/event）本质 = **对象数据 + 两个挂起原语的组合**——框架"分层原语化"哲学在 osal 内部的复现，P2 加族文件不动内核：

```
osal_none_sched_block(wait_obj, timeout_ms)   // 挂对象链(尾) + timeout>0 时挂超时链 → schedule()
osal_none_sched_wake_one(wait_obj, result)    // 摘最老等待者 → 摘超时链 → 就绪 + 写结果槽
osal_none_sched_wake_all(wait_obj, result)    // reset/delete 场景
```

- `timeout==0` 不挂链直接查，空即返回 `OSAL_WOULD_BLOCK`（不切栈）。
- 超时竞争无竞态：到期由 `time_drive()` 摘**对象链 + 超时链**双摘，post 先到则同理双摘，两路互斥于同一 crit → 无双重唤醒（2.1）。结果映射沿用 freertos 端口规则（`OSAL_TIMEOUT` vs `WOULD_BLOCK` 按 timeout 区分；`WAIT_FOREVER` 不可能到期）。
- `yield` = 自己移到就绪链同优段尾 + schedule()。

### 3.3 时间引擎（P1 核心之一）

```
统一超时链：绝对截止时刻升序 dlist（节点内嵌 TCB；选型依据见 2.2）
时间推进双路、驱动代码单一路径：
  target: SysTick 1ms ISR → 读计数器 → osal_none_time_drive(now)   // 只记账，不切栈
  host:   idle 空转轮询 QPC → 同一 osal_none_time_drive(now)
time_drive(now)：从链头弹出 deadline ≤ now 者 →
  睡眠任务 → 就绪
  等待超时 → 摘对象链 + 记 TIMEOUT + 就绪
  (P2) 定时器 → 挂 fired 链（单次摘除 / 周期按 now+period 重挂）
```

31 位回绕由 osal 时间合同兜底：插入比较用 `osal_time_after/before`，超时链本质是"每节点携带绝对时刻的窗口有序链"，窗口 < 2³¹ms 与 osal 合同同域（单次等待超 24.8 天的语义在 osal 层本就无定义；WAIT_FOREVER 不挂链）。**ISR 只允许 `time_drive` 这类记账**，绝不执行用户回调、绝不切栈。登记/取消/驱动三函数 = 时间引擎两入口 + next-deadline（缝 S2/S9）。

### 3.4 调度器与 idle（一处汇合）

```
schedule()：        // 所有让出（block/yield/sleep/exit）唯一入口
  1) (P2) 摘出并执行 fired 定时器回调（in_cb 标志 + 禁阻塞断言；执行模型见 2.5）
  2) 若上一任务 EXIT 且 != 下一任务 → 先不回收（自己栈还在用）
  3) 选就绪链头 → 无 → 返回"空闲"给调用方
  4) arch_switch(...) 切走；被切回后：若"前一个任务"EXIT → 回收其栈 + 清槽
```

- **idle 上下文 = `osal_kernel_start()` 的调用栈**（不建 idle 任务）：`kernel_start` 语义定为"把执行交给调度器"，循环调 `schedule()`；无就绪时：跑 fired 回调 → 睡到下个 deadline（target `WFI`/host 短睡+轮询）→ 再让出。**周期性让出 = FreeRTOS idle 每轮 `taskYIELD()` 的移植**（睡到下个 deadline 的输入即超时链 next-deadline，缝 S9），保证 tick 唤醒的任务必然获得 CPU。
- **host 可终结性**：全部任务退出 → 调度器无处可切 → `kernel_start` 返回 `OSAL_OK`（host 测试可断言收尾；target 上总有常驻任务，永不返回——与 freertos `vTaskStartScheduler` 语义兼容）。**死锁检测**：有活任务但就绪空 + 超时链空 + 定时器空 → 真死锁 → 返回 `OSAL_INTERNAL`（host 语料的免费看门狗；target 合理程序不会走到）。
- 任务首跑：初始栈 = `[ret=trampoline][callee-saved 零槽]`，首次切入 pop 零、ret 进 trampoline → 调 `entry(arg)` → 返回后 trampoline 置 EXIT + 进 schedule()（永不返回）。trampoline 与栈构造在 arch 文件，已落 x64 版。

### 3.5 ISR 面语义（协作内核的核心论断）

**ISR 永不切栈，也无需 resched 标志**：`post_from_isr` 等只是"计数/摘链/置就绪"，被唤醒任务已经在就绪链上——当前上下文（某任务或 idle）继续跑，直到它下一次让出，调度器自然选到被唤醒者。唤醒延迟 ≤ 当前上下文的下次内核调用，这就是协作模式的全部实时性承诺，文档如实写（FreeRTOS 协作模式同款）。`*_from_isr` 在线程上下文调用 → `OSAL_INVALID`（freertos 端口同规则）。抢占模式的 ISR 退出切栈 + resched 协议 = 缝 S3，不在 v1 内。

### 3.6 arch 钩子（换芯片成本 = 一个文件）

| 钩子 | host (x64/mingw) | target (ARM, P3) |
|---|---|---|
| `arch_switch` | ✅ 已落 `.S` | 同款汇编（r4-r11+lr） |
| `arch_stack_init` / trampoline 宿主 | ✅ | C 侧相同 |
| `arch_now_ms` | QPC | SysTick 计数 |
| `arch_crit_enter/exit` | no-op（无 ISR） | PRIMASK 全关（短段；不用 FreeRTOS BASEPRI 分层——无 syscall 级别需求，文档记取舍） |
| `arch_in_isr` | 恒 0 | IPSR≠0 |
| idle 睡 | `Sleep` + QPC 轮询 | `WFI` |

对齐值属 arch 属性（缝 S8）：`arch_stack_init` 按各 ABI 对齐构造初始栈（x64 16B / ARMv7-M 8B）。

## 四、文件与 P1 落码清单

```
platform/osal/none/
  om_osal_portdef.h       # portdef 义务：EVENT 24bit / PRIO_MAX 32 / NAME 16 / WORD 4 / SYNC_ACCEL_CAP_*=0
  osal_none_cfg.h         # 已落
  osal_none_internal.h    # dlist/TCB/调度原语/时间/堆/arch 原型（族文件共享）
  osal_core_none.c        # malloc/free + irq/isr 面 + kernel_start/idle + schedule + time_drive
  osal_thread_none.c      # create/self/yield/exit/terminate/join(→NOT_SUPPORTED，对齐 freertos)
  osal_sem_none.c         # 首族示范（挂起原语消费方）
  osal_time_none.c        # now_monotonic/sleep_ms/delay_until（delay_until 走 freertos 端口回退路径语义）
  osal_none_arch_x64.c    # 栈构造/QPC/临界区/时间源
  osal_none_switch_x64.S  # 已落
lib/osal/include/osal/osal_port.h   # +OSAL_PORT_NONE 枚举与校验（公共头契约扩展，ADR-0022 已立）
samples/host/osal_none_test/        # P1 冒烟：双任务 sem 握手、sleep 序、超时三态、exit 回收、全灭返回
```

## 五、Bootloader 消费边界审视（2026-09-09）

> ⚠️ 本节结论"下载编排侧可上协作线程"已于 2026-09-09 被推翻：OTA 下载在应用内（partition_table_design；Zephyr 实证 mcumgr 在 app / bootloader 只 swap-validate / serial recovery = 单流命令循环），bootloader 无下载编排需求。**本节原文保留作为错误论证的完整记录**；修正后的范围见 v2 主文档。

**结论：能用，但采纳是分层的——决策核无内核，下载编排侧可上协作线程。**

### 5.1 收益（真实收益）

1. **线性代码替代状态机**：传输分块 → 校验 → 暂存 → 提交写成阻塞式线程，重传/超时直接用 `sem/queue + timeout`。
2. **框架资产复用**：log/comm 服务、sync/async 层消费线程上下文（partition/flash 本就是无 OS 裁剪形态，不必 kernel）。
3. **与 FlashDev async 面咬合**：传输任务 sleep 等待擦写完成事件时其它任务照跑——前提见 5.2①。
4. **可论证性**：无抢占 → 无优先级反转、竞态面最小、行为可复现，引导失败分析友好。

### 5.2 代价与边界（审视产出）

1. **取指冻结（并发收益的硬前提）**：F4 类器件擦/写期间 flash 接口忙，从 flash 取指挂起——"擦写期间别的任务照跑"只在代码位于不受影响的执行域（RAM/CCM，或器件确认支持跨 bank 读-写并行，须按手册核验）时成立。应对：擦写等待写成异步事件 sleep 而非忙等（冻结无法避免，但边界清晰）。
2. **冻结期与硬件看门狗冲突（真问题）**：128K 扇区擦除可达秒级；冻结期软件喂狗停摆，独立时钟硬件 WDT 照常倒计时 → 长擦除会触发复位。对策三选：擦除前扩大/暂停狗窗口、按扇区分段擦除 + 段间喂狗、flash 完成中断唤醒后立即喂——写入 bootloader 时钟/喂狗设计。
3. **唤醒延迟 = 让出节律**：ISR 只记账，协议超时预算须含最坏让出间隔；USB 枚举等硬时限环节逐条预算（或留给双模的抢占线程——Zephyr 式同内核并存）。
4. **无抢占安全网**：任务死循环 = 世界停；但协作下死循环是**可检测故障**（硬件 WDT 复位）而非潜伏竞态——配合"idle 喂狗 + 任务让出即活性证明"结构，故障定位清晰。
5. **动机诚实化**：内核代码 + TCB 槽 + 池 vs 手卷状态机的面积差——选用内核的动机是**复用与演进**（见 〇），不是省面积；引导时间预算含内核初始化（小，但要算）。

### 5.3 落点（与四步序列咬合）

**引导决策核（校验/拷贝/跳转）保持无内核顺序流**——裁剪版 partition/FlashDev 直用，现有设计已保证；**OTA 下载编排侧（传输/暂存/超时/进度/喂狗线程）跑 osal-none 协作线程**。这给"内核第一个真实消费场景"划了边界：是编排层，不是决策核；是消费方之一，不绑架内核设计。

> 注：决策核为何连"裁剪 RTOS 单线程形态"都不采用的原因论证 = 2026-09-09 讨论中，确认后并入本节。
