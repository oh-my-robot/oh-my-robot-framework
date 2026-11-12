# MPSC 环形缓冲区设计文档

## 1. 概述

`MpscRingbuf` 是一个基于纯原子操作的多生产者单消费者（MPSC）定长元素环形队列，位于 `lib/data_struct` 层。它不依赖 OSAL，可在任意上下文（线程 / ISR）中使用。

核心机制：**CAS 原子预留 + Per-slot Ready Flag**。

## 2. 设计动机

### 2.1 为什么需要 MPSC

现有的 `Ringbuf` 是 SPSC（单生产者单消费者）环形缓冲区，`writePos` 由单一生产者独占更新。在实际嵌入式场景中，多个 ISR 或多个 Task 需要向同一个消费者投递消息——例如多个 CAN 接收中断向同一个处理任务汇聚报文。此时需要 MPSC 语义。

### 2.2 为什么独立于 OSAL

data_struct 层的定位是**纯数据结构**，不引入上下文感知的 OS 原语。MPSC 的并发控制通过原子操作（CAS、Load/Store）实现，不依赖关中断、信号量等 OSAL 概念。这使得：

- 可在任意优先级的 ISR 中使用（不受 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 限制）
- 跨平台、跨 OS 可移植
- 可在裸机环境中使用

### 2.3 为什么选择 CAS + Ready Flag 而非关中断

经过横向对比，两种方案各有适用场景：

| 维度 | 关中断 | CAS + Ready Flag |
|---|---|---|
| 中断延迟 | 有（= memcpy 时间） | 无 |
| ISR 优先级约束 | 受 RTOS 限制 | 无限制 |
| 内存开销 | 零额外开销 | 1 字节/slot |
| 依赖 | OSAL（关中断原语） | 仅原子操作 |
| 多核可扩展 | 不可（关中断只影响本地核） | 天然支持 |
| API 统一性 | 需区分 ISR/Task 变体 | 统一 API |

CAS + Ready Flag 在**不关中断**的前提下保证了正确性，且为未来多核扩展保留了空间。代价是 per-slot ready flag 的额外内存和消费者可能遇到"延迟可见"（队首 slot 已预留但数据尚未写入完毕）。

## 3. 核心设计

### 3.1 数据结构

```c
typedef struct MpscRingbuf
{
    unsigned char* buf;       /* 数据缓冲区 */
    OmAtomicUint  writePos;   /* 写位置（单调递增，多生产者 CAS 竞争） */
    OmAtomicUint  readPos;    /* 读位置（单调递增，单消费者独占） */
    OmAtomicU8*   ready;      /* per-slot 就绪标志（1 字节/slot） */
    unsigned int  mask;       /* capacity - 1 */
    unsigned int  esize;      /* 元素大小（字节） */
} MpscRingbuf;
```

关键设计决策：

- **`writePos` / `readPos` 单调递增**：不回绕，通过 `& mask` 映射到实际缓冲区位置。无符号减法自然处理溢出。
- **`ready` 使用 `OmAtomicU8`（1 字节）**：比 `OmAtomicUint`（4 字节）节省 75% 内存。ARMv7-M 上 byte 级原子 load/store 与 word 级同样高效。
- **容量必须为 2 的幂**：`& mask` 代替 `% capacity`，单条位运算指令。

### 3.2 生产者写入流程（`mpscrb_in`）

```
1. CAS 循环预留 slot：
   do {
       pos = LOAD_ACQ(writePos);      // 看到其他生产者的最新预留
       LOAD_ACQ(readPos);             // 看到消费者的最新进度
       if (满) return false;
   } while (!CAS_ACQ_REL(writePos, pos, pos+1));

2. 写入数据：
   memcpy(buf[pos & mask], data, esize);

3. 标记就绪：
   STORE_REL(ready[pos & mask], READY); // Release 确保 memcpy 可见
```

**CAS 而非 FAA 的原因**：FAA（Fetch-and-Add）先递增 `writePos` 再检查容量，如果队列已满则无法安全回退。CAS 可以在预留前原子地检查容量，避免无效预留导致的 slot 重叠。

**单核上的 CAS 重试**：ARMv7-M 上 CAS（LDREX/STREX）在 ISR 打断且对同一地址执行 RMW 时失败。单核上重试次数 ≤ 嵌套中断深度，不会无限重试。

### 3.3 消费者读取流程（`mpscrb_out`）

```
1. pos = LOAD_RLX(readPos);                // 只有自己写
2. if (pos >= LOAD_ACQ(writePos)) return false;  // 空
3. if (LOAD_ACQ(ready[pos & mask]) != READY) return false;  // 未就绪
4. copy_out(buf, pos & mask);
5. STORE_RLX(ready[pos & mask], NOT_READY); // 生产者不读此标志
6. STORE_REL(readPos, pos + 1);            // 发布消费进度
```

**ready flag 的必要性**：即使 `writePos` 前进了，也不代表数据已经写入完毕。生产者在 CAS 成功后可能被抢占，此时消费者看到 `writePos` 增加但 slot 数据未完成。ready flag 正是解决这个时序问题。

### 3.4 内存序总结

| 操作 | 内存序 | 原因 |
|---|---|---|
| 生产者 LOAD writePos | Acquire | 看到其他生产者的最新预留 |
| 生产者 LOAD readPos | Acquire | 看到消费者释放的最新进度 |
| 生产者 CAS writePos | Acq_Rel | 获取其他写入 + 发布自己的预留 |
| 生产者 STORE ready | Release | 确保 memcpy 在 ready 之前对消费者可见 |
| 消费者 LOAD writePos | Acquire | 看到生产者的最新预留 |
| 消费者 LOAD ready | Acquire | 配合生产者的 Release，确保看到完整数据 |
| 消费者 STORE ready | Relaxed | 生产者不读 ready flag，无需同步 |
| 消费者 STORE readPos | Release | 发布消费进度给生产者 |

## 4. 安全性分析

### 4.1 死锁不可能

Coffman 四条件中，**持有并等待**和**循环等待**均不成立：
- 生产者不持有任何互斥资源，不等待任何资源（CAS 是非阻塞的）
- 消费者遇到未就绪 slot 时直接返回 false，不阻塞等待

### 4.2 活锁不可能

CAS 重试次数有严格上界：
- ISR → Task：Task 的 CAS 因 ISR 打断而失败，ISR 返回后 Task 重试成功（≤ 嵌套深度）
- Task → Task：同一时刻只有一个 Task 运行，CAS 一次成功
- 消费者不轮询 ready flag

### 4.3 容量安全

CAS 循环在预留前原子地检查 `writePos - readPos < capacity`。只有容量充足时 CAS 才会成功，保证 `pos & mask` 指向的 slot 不与活跃数据重叠。

### 4.4 四种场景分析

| 场景 | 死锁 | 活锁 | CAS 重试上界 | 特有风险 |
|---|---|---|---|---|
| 多 ISR → Task | 不可能 | 不可能 | 中断嵌套深度 | ready flag 延迟可见 |
| 多 Task → ISR | 不可能 | 不可能 | 0（ISR 不修改 writePos） | ISR 可能空手返回 |
| ISR + Task → ISR | 不可能 | 不可能 | 嵌套深度 | ISR 消费者需限制 budget |
| ISR + Task → Task | 不可能 | 不可能 | ≤ 1 | 假满（自愈） |

**延迟可见**：低优先级生产者在 memcpy 期间被抢占，高优先级生产者先完成。消费者看到 writePos 前进但队首 slot 未 ready，返回 false。这不是错误——下次调用时被抢占的生产者已完成。

**假满**：ISR 生产者读到偏旧的 readPos，计算出偏小的 free space，可能认为队列已满。ISR 返回后消费者推进 readPos，下次 ISR 触发时恢复正常。

## 5. Per-slot Ready Flag 的内存开销

每个 slot 额外 1 字节（`OmAtomicU8`）。总内存 = `capacity × (esize + 1)`：

| capacity | esize | 数据区 | Ready 数组 | 总计 |
|---|---|---|---|---|
| 16 | 4B | 64B | 16B | 80B |
| 64 | 8B | 512B | 64B | 576B |
| 256 | 4B | 1024B | 256B | 1280B |

`mpscrb_alloc` 将数据区和 ready 数组合并为一次连续 `malloc`，减少内存碎片和分配次数。

## 6. FIFO 语义保证

消费者按 slot 编号严格递增消费。同一个生产者的多次 `mpscrb_in` 得到的 `pos` 严格递增（CAS 保证），因此同一个生产者的数据在消费者端保持原始顺序。

不同生产者的数据按 CAS 竞争结果交错排列——这是 MPSC 的标准语义。

```
Producer-A: CAS→pos=5, memcpy, ready[5]=1
Producer-B: CAS→pos=6, memcpy, ready[6]=1
Producer-A: CAS→pos=7, memcpy, ready[7]=1

消费者按 5→6→7 顺序消费。A 的数据（slot 5, 7）保持原始顺序。
```

## 7. 与 Ringbuf（SPSC）的关系

`MpscRingbuf` 和 `Ringbuf` 是独立的两个数据结构，但共享相同的设计理念：

| 维度 | Ringbuf (SPSC) | MpscRingbuf (MPSC) |
|---|---|---|
| writePos 更新 | 单生产者独占（Relaxed/Release） | 多生产者 CAS（Acquire/Acq_Rel） |
| 容量检查 | ringbuf_in 内部截断 | CAS 循环内原子检查 |
| 额外内存 | 无 | per-slot ready flag（1B/slot） |
| 环绕 memcpy | copy_in / copy_out | 同（独立实现，单元素版本） |

`MpscRingbuf` 不复用 `Ringbuf` 的代码，因为两者的并发控制机制完全不同。但环绕 memcpy 的逻辑（两段式拷贝处理跨越缓冲区末尾的情况）完全一致。

## 8. 局限性与后续方向

### 当前局限

- **仅支持单元素操作**：每次 `mpscrb_in` / `mpscrb_out` 只操作一个元素
- **无阻塞等待**：消费者需要自行实现轮询或信号量通知机制
- **无超时机制**：纯数据结构不涉及时间概念

### 后续扩展方向

- `mpscrb_in_batch` / `mpscrb_out_batch`：批量操作接口（预留接口位置）
- 在 IPC 层封装带信号量通知的 MPSC Channel（类似 Pipe 的门铃模式）
- 多核场景下的正确性验证（ready flag 的缓存一致性）
