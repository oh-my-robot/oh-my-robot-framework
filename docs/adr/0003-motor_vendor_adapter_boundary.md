# ADR-0003 电机统一模型与 Vendor 适配边界

## 背景 (Context)
- 电机链路涉及 `Application`、`MotorCore`、`VendorAdapter`、`TransportAdapter` 多层协作。
- 未明确边界时，容易出现职责越层：适配层接管系统策略、业务层直接处理协议帧。

## 考虑过的方案 (Options)
- 方案 A：核心层只做透传，策略下沉到 vendor 适配层。
- 方案 B：核心层统一语义与策略，适配层仅吸收协议差异。
- 方案 C：按模块自由分工，不做严格边界。

## 最终决策 (Decision)
- 采用方案 B。
- `MotorCore` 负责状态机、命令语义、故障动作、调度超时与统一快照。
- `VendorAdapter` 负责编解码、单位换算、故障码映射、能力声明。
- `TransportAdapter` 负责端口收发、时间戳、路由与丢弃统计。
- `Application` 只消费统一接口，不拼 vendor 私有帧。

## 影响 (Consequences)
- 所有“不支持能力”必须显式失败，不允许伪成功。
- vendor 不得绕过核心状态机直接恢复运行态。
- 字段真相维护层固定，减少双事实源与语义漂移风险。
