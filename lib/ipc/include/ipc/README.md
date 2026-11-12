# ipc

跨上下文数据传输层（Inter-Context Communication）。提供任务间、ISR 与任务间的字节流/消息传输通道，与 `sync/`（纯同步信号）及 `services/comm/`（结构化消息）互补。

## 层定位

| 层 | 职责 | 数据载荷 |
|---|---|---|
| `sync/` | 纯同步信号（completion、event、barrier...） | 无 |
| **`ipc/`** | **跨上下文数据通道（pipe、channel...）** | **有（字节流/类型化消息）** |
| `services/comm/` | 结构化消息通信（帧格式、路由、发布订阅） | 有（带协议头） |

## 设计约束

- 依赖：core + osal（通过信号量/队列等 OSAL 原语组合实现）
- 不依赖：services / drivers / systems
- 支持通信方：Task ↔ Task、ISR → Task、Task → ISR（通过 `_from_isr` 变体）
- 核间通信为 future scope

## 模块

| 模块 | 文件 | 说明 |
|---|---|---|
| pipe | `include/ipc/pipe.h` `src/pipe.c` | SPSC 字节流管道（Ringbuf + 门铃信号量） |
