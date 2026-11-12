# Pipe 设计决策与正确性分析

本文档记录 Pipe 的核心设计约束、关键决策理由及边界场景分析，供维护者理解"为什么这样设计"而非仅仅"怎样使用"。

Pipe 位于 `lib/ipc/`（跨上下文数据传输层），不属于 `lib/sync/`（纯同步信号层）。详见 [ADR-0009](../../../../docs/adr/0009-ipc_layer_and_pipe_migration.md)。

---

## 1. 架构概览

Pipe 是基于 **Ringbuf + 双二值信号量（门铃模式）** 的单向字节流管道，严格遵循 **SPSC** 并发模型。

```
┌──────────────────────────────────────────────────┐
│                      Pipe                        │
│  ┌──────────────┐    ┌──────────┐    ┌─────────┐│
│  │  write_sem   │    │ Ringbuf  │    │read_sem ││
│  │ max=1 init=1 │    │          │    │max=1    ││
│  │              │    │ buf[]    │    │init=0   ││
│  └──────┬───────┘    └────┬─────┘    └────┬────┘│
│         │                 │               │      │
│    写者 wait 被唤醒   数据进出环        读者 wait │
│    读者 post 通知       buf            写者 post │
└──────────────────────────────────────────────────┘
```

- **write_sem**（`max=1, init=1`）：初始时 pipe 为空，空间可用，写者可直接写入。读者在满→非满转换点 post 通知写者"空间可用"。
- **read_sem**（`max=1, init=0`）：初始时 pipe 为空，无数据可读。写者在空→非空转换点 post 通知读者"数据到达"。

## 2. 设计决策

### 2.1 为什么选信号量而非 Completion

Completion 底层基于二值信号量 + 一次性状态机（INIT→WAIT→DONE→INIT），其设计目标是"完成通知"语义。Pipe 需要"流式通知"语义，两者不匹配：

- **DONE 状态残留**：写者 done 后，若读者通过 `pipe_read` 的非阻塞路径读到数据（未走 wait 路径），DONE 状态残留，下次 wait 立即返回但 pipe 可能已空——虚假唤醒。
- **连续 done 返回 BUSY**：写者连续写入间读者未 wait，第二次 done 返回 BUSY，调用方必须静默忽略，违背 Completion 的设计契约。
- **状态机临界区冗余**：Completion 的 wait/done 内部有 irq_lock 临界区 + 状态判断，SPSC Pipe 下 ringbuf 已通过 Acquire/Release 内存序保证一致性，无需额外临界区。
- **单等待者检查冗余**：Completion 的 waitThread 字段在 SPSC 下完全多余。

直接使用信号量的优势：

- 无状态机开销，post/wait 直接映射到 OSAL（`osal_sem_post` / `osal_sem_wait`）。
- 天然支持反复 wait/post 循环，无需重置。
- 门铃模式天然适配流式场景（见 2.3）。

### 2.2 为什么用双信号量而非单信号量

单信号量只能通知读端"数据到达"，写端满时无法被唤醒。Pipe 存在两个独立的等待条件：

- **写者需要"空间可用"通知**（`write_sem`）：pipe 从满→非满时，由读者 post。
- **读者需要"数据到达"通知**（`read_sem`）：pipe 从空→非空时，由写者 post。

一个信号量无法同时服务两个方向的阻塞。初始计数值的设计也经过考量：

- `write_sem` init=1：pipe 初始为空，写者可以直接写入（空间可用），无需等待。
- `read_sem` init=0：pipe 初始为空，读者必须等待数据到达。

这样避免了初始化后写者的首次写入就被迫阻塞。

### 2.3 为什么是门铃模式而非计数模式

门铃模式：仅在**状态转换点** post（空→非空 post `read_sem`；满→非满 post `write_sem`），而非每次操作都 post。

如果每次写都 post `read_sem`，信号量计数会与实际数据量脱节：

- 二值信号量 `max_count=1`，写者写 3 次但读者还没来，信号量最多 post 1 次就满，后续 post 被丢弃（`osal_sem_post` 在计数已满时返回 `OSAL_NO_RESOURCE`）。
- ringbuf 的原子读写指针才是数据量的唯一真实来源。
- 信号量只负责"敲门"，不负责"数数"。

---

## 3. API 设计

### 3.1 生命周期管理

Pipe 提供两种构造方式以适应不同场景：

| 函数 | 缓冲区来源 | 适用场景 |
|---|---|---|
| `pipe_init` | 外部提供静态缓冲区 | 无动态内存分配的固件、裸机 |
| `pipe_alloc` | 内部动态分配 | 需要灵活容量、有 `malloc` 支持 |

对应的析构函数 `pipe_deinit` / `pipe_free` 回收信号量及缓冲区，调用前必须确保无并发访问和无等待者。

### 3.2 阻塞超时与 ISR 安全

读写操作按上下文分为两组：

| 上下文 | 写操作 | 消费读 | 窥视读 | 跳过 |
|---|---|---|---|---|
| **Task（可阻塞）** | `pipe_write` | `pipe_read` | `pipe_peek` | `pipe_skip` |
| **ISR（非阻塞）** | `pipe_write_from_isr` | `pipe_read_from_isr` | `pipe_peek_from_isr` | `pipe_skip_from_isr` |

**阻塞机制**：

- 第一次尝试直接操作 ringbuf，成功则立即返回（快速路径）。
- 若 ringbuf 满/空（写入/读取 0 字节），进入等待路径：
  - `timeout_ms == 0`：非阻塞模式，立即返回 `OM_ERROR_WOULD_BLOCK`。
  - `timeout_ms == OSAL_WAIT_FOREVER`：无限等待直到被对端 post 唤醒。
  - 其他值：阻塞等待指定毫秒数，超时返回 `OM_ERROR_TIMEOUT`。
- 被唤醒后**重新检查 ringbuf 状态并再次操作**（防御性：门铃模式下 post 只保证"曾经"有状态变化）。

**ISR 安全检查**：阻塞函数内部调用 `osal_is_in_isr()`，若检测到在 ISR 上下文调用则直接返回 `OM_ERROR_PARAM`，防止硬实时破坏。

### 3.3 peek + skip 组合

`pipe_peek` / `pipe_peek_from_isr` 窥视数据但不移动读指针（使用 `ringbuf_out_peek`），数据保留在 pipe 中。`pipe_skip` / `pipe_skip_from_isr` 移动读指针但不拷贝数据（使用 `ringbuf_update_out`）。

典型协议解析场景：

```c
// 1. 先 peek 帧头判断帧长度
pipe_peek(&p, header, 4, timeout);
uint16_t frame_len = decode_length(header);

// 2. 再 peek 整帧数据
pipe_peek(&p, frame_buf, frame_len, timeout);

// 3. skip 消费整帧（仅移动指针，不拷贝）
pipe_skip(&p, frame_len);
```

**设计要点**：

- `peek` **不 post `write_sem`**：peek 不消费数据，pipe 的空间状态不变，无需通知写端。
- `skip` 在 pipe 从满→非满时 **post `write_sem`**：skip 消费数据后空间被释放，需要通知写端。
- 相比 `read` + 外部缓冲区回退方案，peek+skip 减少了一次数据拷贝。

### 3.4 状态查询（inline）

`pipe_len`、`pipe_avail`、`pipe_cap`、`pipe_is_empty`、`pipe_is_full` 均为 inline 函数，直接读取 ringbuf 的原子变量，零系统调用开销。在热路径上（如写者每次写前检查 `pipe_avail`）避免函数调用开销。

---

## 4. 并发正确性

### 4.1 ISR 写 + Task 读仍是 SPSC

并发模型看的是"角色数量"而非"上下文数量"。ISR 是唯一的写者，Task 是唯一的读者，各自只扮演一个角色，因此是 SPSC。

同理，ISR 读 + Task 写也是 SPSC。`pipe_read_from_isr` / `pipe_peek_from_isr` / `pipe_skip_from_isr` 的引入使得 Pipe 可以用于 Task 写→ISR 收的数据流向。

### 4.2 门铃模式不会导致死锁

写者流程：`ringbuf_in()` → 检查 `was_empty` → `osal_sem_post(read_sem)`
读者流程：`ringbuf_out()` 返回 0 → `osal_sem_wait(read_sem)`

关键时序分析：如果写者 `ringbuf_in` 之后、`osal_sem_post(read_sem)` 之前，读者恰好检查到 ringbuf 为空然后进入 wait，不会死锁：

- `ringbuf_in` 使用 Release 语义（`OM_STORE_REL`）发布 `writePos`。
- `ringbuf_is_empty` 使用 Acquire 语义（`OM_LOAD_ACQ`）读取 `writePos`。
- 若读者看到空，说明写者尚未完成 `in` 的 Release 写入。
- 写者完成 `in` 后一定会 post `read_sem`。
- 时序上：要么读者先看到数据直接读走（不进入 wait），要么读者先进入 wait 然后被写者的 post 唤醒。

写者侧同理：`ringbuf_out` → 检查 `was_full` → `osal_sem_post(write_sem)`，读者进入 wait 的时序窗口同理安全。

### 4.3 阻塞等待被唤醒后必须重新检查 ringbuf

`pipe_write` 满→`osal_sem_wait(write_sem)`→被唤醒→`ringbuf_in`：

被唤醒后必须重新检查 ringbuf 状态并重新执行写入，因为信号量是门铃模式，post 只保证"曾经有过空间"，不保证"此刻还有空间"。SPSC 下不会出现被唤醒后仍无空间的场景（消费者是唯一的读角色，不会抢占我们的写入空间），但代码做防御性编程。

### 4.4 SPSC 无锁的优势

SPSC 模型下，ringbuf 的 Acquire/Release 内存序足以保证数据一致性，不需要任何临界区或互斥锁。这是 Pipe 性能优势的根源。若扩展为 MPSC：

- 写端需要临界区保护（`osal_irq_lock_task` / `osal_irq_lock_from_isr`）。
- 引入中断延迟和优先级反转风险。
- 此时应考虑是否用 `osal_queue` 替代。

---

## 5. 边界场景

### 5.1 capacity 必须为 2 的幂

继承自底层 ringbuf 的约束：ringbuf 用 `pos & mask` 代替 `pos % capacity`，要求 capacity 为 2 的幂以支持位运算取模优化。`pipe_init` 通过 `ringbuf_init` 内部校验，`pipe_alloc` 通过 `pipe_is_power_of_two` 显式校验。

### 5.2 ISR 写满返回 WOULD_BLOCK

`pipe_write_from_isr` 在 pipe 满时立即返回 `OM_ERROR_WOULD_BLOCK`。ISR 中不能阻塞，这不是 Pipe 的缺陷，而是 ISR 场景的设计约束。应对策略由使用者决定：增大缓冲区、提高读者/Task 优先级、使用 DMA 双缓冲等。

### 5.3 信号量 post 失败静默忽略

在门铃模式下，若 `osal_sem_post` 因为信号量计数值已满（`max_count=1`）而返回 `OSAL_NO_RESOURCE`（如连续两次空→非空的 post 之间读者没有 wait），Pipe 静默忽略此返回值。

这是正确的行为：门铃不负责计数，上一个 post 已足够唤醒对端。对端被唤醒后通过 ringbuf 的原子指针获取真实数据量——不会漏掉数据。

### 5.4 写入/消费 0 字节

当 ringbuf_in/out 返回 0 时，代表空/满状态，此时不 post 任何信号量（因为没有发生状态转换）。状态转换的判定依据是操作前的 `was_empty` / `was_full` 标志，而非操作后的实际字节数。

---

## 6. 核心设计约束总结

Pipe 的核心设计约束是 **SPSC + 门铃模式**：

- **SPSC** 让我们不需要锁，ringbuf 的 Acquire/Release 内存序即足够。
- **门铃模式** 让信号量只负责唤醒不负责计数，避免了计数与数据量脱节的问题。
- **数据量的真实来源** 始终是 ringbuf 的原子读写指针，信号量仅做辅助。
- 这两个约束互相支撑：正因为是 SPSC，ringbuf 的 Acquire/Release 内存序就够了，无需临界区；正因为信号量不计数，我们才不需要担心 post 被丢弃导致计数错误。

所有新增功能（ISR 读端操作、peek/skip、生命周期管理）都在此核心约束框架内扩展，保持了设计的一致性和简洁性。
