# ADR-0009 IPC 层创建与 Pipe 迁移

## 背景 (Context)

- `lib/sync/` 当前包含两类本质不同的模块：纯同步信号原语（`completion`）和带数据载荷的传输通道（`pipe`）。
- `pipe` 基于 `Ringbuf + 双二值信号量` 实现，核心价值是**跨上下文数据传输**（Task ↔ Task、ISR → Task、Task → ISR），而非同步通知。
- `completion` 回答"某事件是否已发生"，无数据载荷；`pipe` 回答"有没有数据？给我/拿走"，有数据载荷。两者语义不匹配。
- 若继续将 `pipe` 留在 `sync/`，未来加入类型化通道（`spsc_channel`）、广播通道等机制时，`sync/` 会变成无区分度的大杂烩。
- 按通信方（任务间、ISR→Task、Task→ISR、核间）建子目录会导致代码笛卡尔积膨胀；按通信机制建目录（pipe/channel/broadcast）并用 `_from_isr` 变体覆盖通信方，是更合理的分类方式。

## 考虑过的方案 (Options)

- **方案 A**：保持 `sync/`，扩展其语义定义为"并发协作层"。零重构，但 `sync/` 语义模糊，未来扩展方向混乱。
- **方案 B**：新建 `ipc/` 层，将 Pipe 迁移至 `lib/ipc/`。`sync/` 回归纯同步信号（completion、future event、barrier），`ipc/` 承载跨上下文数据通道。
- **方案 C**：Pipe 回退到 `osal/` 层，与 `osal_queue` 并列。但 Pipe 是组合产物（ringbuf + sem），不符合 OSAL"最小可移植原语"定位。

## 最终决策 (Decision)

- 采用**方案 B**：创建 `lib/ipc/` 跨上下文数据传输层，将 Pipe 从 `lib/sync/` 迁移至此。
- 层级定位（自底向上）：
  ```
  sync     → 纯同步信号（completion, future event, barrier...），无数据载荷
  ipc      → 跨上下文数据通道（pipe, future channel, broadcast...），有数据载荷
  services/comm → 结构化消息通信（帧格式、路由、发布订阅），构建在 ipc 之上
  ```
- 组织方式：按通信机制建目录，通信方作为 API 变体（`_from_isr` 后缀）。
- 依赖约束：`ipc/` 依赖 `core + osal`，不依赖 `services/drivers/systems`。
- Pipe 头文件守卫从 `OM_SYNC_PIPE_H` 更新为 `OM_IPC_PIPE_H`。
- 构建对象：新增 `tar_awapi_ipc`（headeronly 接口目标）和 `tar_ipc`（静态库目标）。

## 影响 (Consequences)

- `sync/` 目录仅保留 `completion`，语义明确回归"纯同步信号"。
- 所有依赖 Pipe 的上层模块需更新 include 路径：`sync/pipe.h` → `ipc/pipe.h`。
- `tar_awapi_ipc` 作为 headeronly 目标被 `tar_awapi_async` 和 `tar_awapi_driver` 传递依赖，保证 IPC 头文件在构建系统中可见。
- 未来新增通道机制（如 `spsc_channel`、`broadcast`）直接在 `lib/ipc/` 下扩展，不污染 `sync/`。
- 核间 IPC 抽象为 future scope，届时在 `ipc/` 下追加 `ipc_core.h` 或 `inter_core/` 子目录。
- 相关规范更新：`layer_dependency_spec.md`、`build/xmake.lua`、`lib/xmake.lua`、`sync/README.md`。
