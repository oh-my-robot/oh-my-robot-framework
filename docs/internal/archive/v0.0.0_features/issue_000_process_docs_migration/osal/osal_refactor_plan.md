# OSAL重构计划

> 来源：历史混合文档（按职责拆分：OSAL 机制合同）。


  1. Phase 0：架构冻结与工程骨架（P0）

  - 目标：先锁边界，不改复杂语义。
  - 变更范围：
      - 目录与索引骨架对齐文档目标（osal/sync/platform）。
      - 统一宏入口与命名边界（OM_SYNC_ACCEL 及分原语宏）。
      - 公共头文件清理边界：不暴露 OS 私有类型，不出现 OS 条件分支泄漏。
  - 阶段验证：
      - include 关系检查（无循环、无越层依赖）。
      - 最小构建通过（不要求功能完备）。
  - 交付后停下，等你确认。

  2. Phase 1：OSAL 最小原语重构（P0）

  - 目标：锁定 OSAL 的 mechanism contract。
  - 变更范围：
      - 基础设施：time/critical/memory 契约化。
      - 并发原语：thread/mutex/semaphore/queue/event_flags 契约化。
      - 统一超时语义：timeout=0、WAIT_FOREVER、错误码一致性。
      - ISR-safe 子集显式化。
  - 阶段验证：
      - OSAL conformance（超时、ISR 约束、返回码）。
      - 编译与最小运行验证。
  - 交付后停下，等你确认。

  3. Phase 2：SYNC reference 重构（P0）

  - 目标：锁定 semantic contract，以 completion 为先。
  - 变更范围：
      - sync/common reference 实现（仅依赖 OSAL）。
      - completion 状态机明确化（必要时补 event）。
  - 阶段验证：
      - 状态机测试。
      - timeout=0/WAIT_FOREVER 与 *_from_isr 语义验证。
  - 交付后停下，等你确认。

  4. Phase 3：selector + capability 框架（P1）

  - 目标：先框架，后性能。
  - 变更范围：
      - 三段式选择规则落地：
          - 未开全局加速 -> reference
          - 开全局未开原语加速 -> reference
          - 全局+原语加速均开 -> 允许加速后端
      - capability 只允许从 platform/sync/<os>/... 提供。
  - 阶段验证：
      - 宏切换不改变语义。
      - reference 始终可回退。
  - 交付后停下，等你确认。

  5. Phase 4：双端一致性验证（P1）

  - 目标：验证语义一致，不追求性能。
  - 变更范围：
      - FreeRTOS 与 Linux（若 Linux 环境可用）运行同一组 conformance。
  - 阶段验证：
      - 同一语义测试双端通过；若 Linux 暂不可运行，输出阻塞项与替代验证。
  - 交付后停下，等你确认。

  6. Phase 5：可选优化（P2）

  - 目标：仅在前四阶段稳定后进行。
  - 变更范围：
      - completion/event 加速后端、IRQ 架构增强等。
  - 阶段验证：
      - 与 reference 语义等价。
  - 交付后停下，等你确认。

  ———

  执行规则（本次固定）

  1. 每次只做一个 Phase。
  2. 每个 Phase 完成后我会给你：

  - 改动清单（文件级）
  - 验证结果（通过/失败及原因）
  - 风险与回滚点

  3. 你确认后我再进入下一阶段。

  如果你同意，我现在开始执行 Phase 0。

## 来源映射

1. 原文件：历史混合文档
2. 拆分策略：保留原信息并以 OSAL 机制视角组织，SYNC 语义见 oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/sync/sync_requirements_volume.md。





