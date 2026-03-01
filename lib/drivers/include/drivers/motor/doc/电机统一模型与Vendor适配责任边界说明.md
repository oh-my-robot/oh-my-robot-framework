# 电机统一模型与 Vendor 适配责任边界说明（最终版）

> 本文定义 Motor Core、Transport Adapter、Vendor Adapter、Application 的职责与禁止项。  
> 分层总依赖矩阵以 `oh-my-robot/document/architecture/分层与依赖规范.md` 为唯一规范源；本文只描述 motor 统一模型专项边界。

## 1. 核心结论（先给结论）

1. Core 负责稳定语义，Adapter 负责差异映射，Transport 负责收发机制。
2. vendor 细节不得越层泄露到 Application。
3. 不支持能力必须显式失败，不得伪成功。
4. 当前范围不包含 ECAT 接入路径。

------

## 2. 责任矩阵（层级职责）

| 层 | 必须负责 | 明确不负责 | 必须输出 |
| --- | --- | --- | --- |
| Motor Core | 状态机、命令语义、调度、超时、故障动作、快照聚合 | vendor 编解码、物理帧细节 | `MotorState`、`MotorSnapshot`、`MotorFaultInfo`、`reject_reason` |
| Vendor Adapter | 编解码、单位换算、故障码映射、能力声明 | 系统级调度与全局策略裁决 | `MotorCapabilityProfile`、映射函数 |
| Transport Adapter | 端口读写、时间戳、路由、丢弃统计 | 控制模式解释、故障闭锁逻辑 | `RxFrame`、`TxResult`、`drop_counter` |
| Application | 业务策略、模式选择、监控告警处理 | 协议包拼装、vendor 差异处理 | 业务命令、策略配置 |

------

## 3. 字段级边界（谁拥有真相）

| 字段/概念 | 真相维护层 | 其他层使用规则 |
| --- | --- | --- |
| `current_mode` | Core | Adapter 只能上报模式可用性，不能直接写终态 |
| `fault_latch` | Core | Adapter 只能上报 fault 事件 |
| `vendor_rom_code` | Adapter | Core 保留存档，不解释厂商私有细节 |
| `timestamp` | Transport | Core 使用但不重定义 |
| `valid_mask` | Core | Adapter 提供原始字段可用性提示 |
| `capability` | Adapter | Core/Application 只读判断可用性 |

------

## 4. Motor Core 约束

## 4.1 Core MUST

1. 执行统一状态机（见 `MM-STATE-*`）。
2. 执行统一故障动作（见 `MM-FLT-*`、`MM-SAFE-*`）。
3. 维护统一快照语义（见 `MM-CMD-*`、`MM-DIAG-*`）。
4. 统一管理在线判定与超时推进（见 `MM-SCH-*`）。

## 4.2 Core MUST NOT

1. 不得依赖 vendor 私有帧 ID 常量作为核心语义。
2. 不得把 capability 缺失伪装为成功。
3. 不得允许 Adapter 绕过状态机直接恢复运行态。

------

## 5. Vendor Adapter 约束

## 5.1 Adapter MUST

1. 将 vendor 原始反馈映射为统一快照字段。
2. 将 vendor 原始 fault/alarm 映射到统一故障域并保留原始码。
3. 输出能力声明与成熟度（`declared/implemented/validated`）。
4. 显式声明“不支持能力”的返回语义。

## 5.2 Adapter MUST NOT

1. 不得实现系统级调度策略。
2. 不得直接修改 Core 终态。
3. 不得向 Application 暴露 vendor 内部状态结构。

------

## 6. Transport Adapter 约束

## 6.1 Transport MUST

1. 提供统一收发结果语义与时间戳语义。
2. 维护输入帧基础合法性检查与丢弃统计。
3. ISR 回调路径仅触发事件，不做重解析。

## 6.2 Transport MUST NOT

1. 不得解释控制模式与故障含义。
2. 不得执行闭锁、清错、限幅决策。

------

## 7. Application 约束

## 7.1 Application MUST

1. 只通过统一接口进行控制与状态读取。
2. 根据 capability 选择功能，不依赖 vendor 条件分支。
3. 通过统一故障域执行业务安全策略。

## 7.2 Application MUST NOT

1. 不得直接拼 vendor 帧。
2. 不得读取/写入 vendor 私有结构体字段。

------

## 8. 差异能力处理规则

1. 差异能力通过 `MotorCapabilityProfile` 明确声明。
2. 不支持能力调用必须返回 `NOT_SUPPORT` 或同等明确错误。
3. 禁止在 Core 中为不支持能力做隐式模拟。
4. 如需模拟能力，必须作为显式可配置策略并写入合同。

------

## 9. 明确不承诺项（Unspecified）

1. 不承诺不同 vendor 的高级能力完全同构。
2. 不承诺不同 transport 时延与吞吐一致。
3. 不承诺所有 vendor 支持硬件级故障清除。
4. 不承诺当前阶段提供 ECAT 相关路径。

------

## 10. 冲突裁决

1. 优先级：`语义合同索引` > `顶层规范` > `责任边界说明` > `实现注释`。
2. 冲突处理流程：
   - 标注冲突类型（语义/实现/文档）
   - 回链合同编号
   - 形成 `compat_note` 与修订动作

------

## 11. 审计检查表（必须逐项结论化）

1. 公共类型是否泄露 vendor 字段。
2. Adapter 是否越权管理系统策略。
3. 不支持能力是否明确失败。
4. 故障与告警是否混用。
5. 业务层是否存在 vendor 分支。
6. ISR 路径是否仅保留最小逻辑。

------

## 12. 变更记录

- `v1.0`：重构责任边界为字段级与层级双矩阵，并明确不纳入 ECAT。

