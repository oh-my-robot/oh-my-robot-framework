# ADR-0007 Serial Comm Adapter 分层边界

## 背景 (Context)
- `services/comm` 与串口驱动层之间需要稳定边界，避免双向耦合。
- 若通信适配与业务协议解析混杂，会导致职责扩散并破坏可维护性。

## 考虑过的方案 (Options)
- 方案 A：`services/comm` 直接依赖串口实现。
- 方案 B：在驱动侧实现 comm adapter，依赖抽象接口与消息模型。
- 方案 C：在中断路径直接做业务语义解析。

## 最终决策 (Decision)
- 采用方案 B，并明确禁止方案 C。
- comm adapter 负责总线消息与 `CommMsg` 抽象映射、注册接入。
- 不负责业务协议解析，不在 ISR 路径执行阻塞逻辑。
- 分层依赖以 [`docs/architecture/layer_dependency_spec.md`](../architecture/layer_dependency_spec.md) 为准。

## 影响 (Consequences)
- `services/comm(core)` 与硬件实现解耦，提升平台迁移能力。
- 串口驱动核心路径避免业务语义污染。
- 后续扩展 UART/RS485 适配时，可复用同一边界约束。
