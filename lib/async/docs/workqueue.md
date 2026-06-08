# Workqueue 设计文档

## 概述

Workqueue（工作队列）是一个**中断安全的延迟工作调度器**，用于将耗时操作从 ISR/高优先级线程上下文卸载到专用的 worker 线程中执行。

### 设计目标

| 目标        | 说明                                                              |
| --------- | --------------------------------------------------------------- |
| **中断安全**  | ISR 可直接调用 `workqueue_enqueue` / `workqueue_cancel`，无需额外的锁或上下文判断 |
| **去重**    | 同一 Work 重复入队返回 BUSY，防止意外堆积                                      |
| **可取消**   | 支持从 pending 队列中 O(1) 取消未执行的工作项                                  |
| **简单**    | 仅关中断保护，无 CAS 自旋、互斥量、优先级继承等复杂机制                                  |
| **极小临界区** | irq\_lock 内仅做指针赋值操作，关中断时间可忽略                                    |

## 并发模型

### 核心原理：开关中断 + 分层同步策略

单核 + RTOS 下，所有并发竞争源自 ISR/线程 打断线程。

- **irq\_lock 内**：临界区提供互斥 + 编译器屏障，flags 使用普通 volatile 读写，无需原子操作
- **irq\_lock 外**：使用原子操作建立 happens-before（enqueue CAS、worker STORE\_REL、work\_is\_busy LOAD\_ACQ）

### 保护范围

| 共享状态                      | 保护方式                                                                      |
| ------------------------- | ------------------------------------------------------------------------- |
| `Workqueue.pending` 链表    | `osal_irq_lock`                                                           |
| `Work.flags`（irq\_lock 内） | 临界区互斥，普通 volatile 读写                                                      |
| `Work.flags`（irq\_lock 外） | `CAS_RLX`（enqueue 去重）、`STORE_REL`（worker 发布完成）、`LOAD_ACQ`（work\_is\_busy） |
| `Workqueue.state`         | `CAS_AR`（生命周期切换）、`LOAD_ACQ`（状态检查）                                         |

## 标志位状态机

```
Work.flags:
                         ┌───────────────────────┐
                         │                       │
                         ▼                       │
┌──────┐  enqueue   ┌─────────┐   worker    ┌─────────┐   func结束   ┌──────┐
│ IDLE │ ────────►  │ PENDING │ ──────────► │ RUNNING │ ──────────►  │ IDLE │
└──────┘   CAS      └─────────┘   flags=    └─────────┘   STORE_REL  └──────┘
   ▲                    │        RUNNING                     ▲
   │                    │ cancel                             │
   │                    ▼                                    │
   │              ┌──────────────┐                           │
   │              │ PENDING|     │                           │
   │              │ CANCELLED    │─── worker 检测到 ──────────┘
   │              └──────┬───────┘    跳过 func, STORE_REL IDLE
   │                     │
   └── cancel list_del ──┘

Workqueue.state:
  ┌────────┐  init    ┌──────┐  start    ┌─────────┐  stop     ┌──────────┐
  │ UNINIT │ ──────►  │ IDLE │ ────────► │ RUNNING │ ────────► │ STOPPING │
  └────────┘          └──────┘   CAS     └─────────┘   CAS      └─────┬────┘
       ▲                 ▲                                              │
       │                 │              drain完成                       │
       │                 └──────────────────────────────────────────────┘
       └── deinit
```

## API 参考

| API                           | ISR 安全   | 说明                   |
| ----------------------------- | -------- | -------------------- |
| `workqueue_init(wq, cfg)`     | -        | 初始化，分配信号量/completion |
| `workqueue_start(wq)`         | -        | 创建 worker 线程         |
| `workqueue_stop(wq)`          | -        | 排空 + 等待 worker 退出    |
| `workqueue_enqueue(wq, work)` | ✓        | 入队，去重（重复返回 BUSY）     |
| `workqueue_cancel(work)`      | ✓        | 取消 pending 工作项       |
| `workqueue_flush(wq)`         | ✗ (阻塞调用) | 排空所有 pending 工作并等待   |
| `workqueue_is_empty(wq)`      | ✓        | 查询队列是否为空             |
| `work_is_busy(work)`          | ✓        | 查询工作项是否忙碌            |

## 演进方向：多 Worker 支持

当前设计为单 Worker 模型（一个 Workqueue 内嵌一个 Worker 线程）。未来可扩展为多 Worker，演进路径：

1. 在 `WorkqueueConfig` 中增加 `worker_count` 字段
2. Workqueue 内部维护一个 worker 线程数组，所有 worker 共享同一个 pending list + semaphore
3. 保持现有 API 不变，cancel/flush 语义通过多 worker 协调自然扩展

**暂不实现的原因**：单核 Cortex-M 上多 worker 无并行收益，仅增加栈内存消耗和上下文切换开销。待多核场景或明确并发瓶颈时再引入。

