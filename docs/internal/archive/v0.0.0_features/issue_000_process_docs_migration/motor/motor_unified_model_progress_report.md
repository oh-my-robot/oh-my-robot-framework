# 电机统一模型进度说明

## 0. 维护规则（强制）

- 每次阶段验收、合同编号调整、边界变更后，必须更新本文档。
- 同一阶段仅原地维护，不新增并行阶段版本。
- 每次更新必须包含：阶段、变更范围、当前状态、验证状态、未解决问题。
- 阶段编号必须与 `motor_unified_model_rnd_plan.md` 一一对应。
- 若规划项未落地，必须显式标注“计划项未完成”。
- 所有结论必须给出仓库路径或可执行验证证据。

## 1. 适用范围与依据

- 适用范围：`oh-my-robot/lib/drivers/include/drivers/motor` 统一模型相关文档与后续实现进度。
- 依据文档：
  - `oh-my-robot/docs/adr/0002-motor_unified_model_top_level_spec.md`
  - `oh-my-robot/docs/adr/0003-motor_vendor_adapter_boundary.md`
  - `oh-my-robot/docs/adr/0004-motor_semantic_contract_index.md`
  - `oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/motor/motor_unified_model_rnd_plan.md`

## 2. 阶段总览（按阶段原地维护）

| 阶段 | 验收结论 | 当前状态 | 证据路径 | 验证状态 | 未解决问题 |
| --- | --- | --- | --- | --- | --- |
| Phase 0（合同与边界冻结） | 进行中 | 已启动 | `oh-my-robot/docs/adr/0002-motor_unified_model_top_level_spec.md`、`oh-my-robot/docs/adr/0003-motor_vendor_adapter_boundary.md`、`oh-my-robot/docs/adr/0004-motor_semantic_contract_index.md` | 文档已重构，待一致性复核 | 合同到实现映射样例尚未落地 |
| Phase 1（统一模型最小壳） | 待验收 | 待开始 | `oh-my-robot/lib/drivers/include/drivers/motor/core/*`、`oh-my-robot/lib/drivers/src/motor/core/*`（待创建） | 尚未执行 | 计划项未完成 |
| Phase 2（DJI + P1010B 适配） | 待验收 | 待开始 | `oh-my-robot/lib/drivers/src/motor/vendors/*`（待适配） | 尚未执行 | 计划项未完成 |
| Phase 3（达妙适配与参数诊断收敛） | 待验收 | 待开始 | `oh-my-robot/lib/drivers/include/drivers/motor/vendors/damiao/*` | 尚未执行 | `damiao.h` 契约缺口 |
| Phase 4（回归收敛与发布留痕） | 待验收 | 待开始 | 回归与合同覆盖率证据（待补） | 尚未执行 | 计划项未完成 |

## 2.1 合同域覆盖追踪

| 合同域 | 当前状态 | 证据路径 | 备注 |
| --- | --- | --- | --- |
| `MM-BUS` | 已定义 | `0004-motor_semantic_contract_index.md` | 待实现映射验证 |
| `MM-PROTO` | 已定义 | `0004-motor_semantic_contract_index.md` | 待样板适配验证 |
| `MM-CORE/CMD/STATE` | 已定义 | `0004-motor_semantic_contract_index.md` | 待 core 壳实现验证 |
| `MM-SCH/SAFE/FLT` | 已定义 | `0004-motor_semantic_contract_index.md` | 待回归验证 |
| `MM-PARAM/DIAG/ENG/CAP` | 已定义 | `0004-motor_semantic_contract_index.md` | 待阶段 3 收敛 |

## 2.2 命名合规检查

| 检查项 | 当前结论 | 证据方式 |
| --- | --- | --- |
| 公共类型名不含版本后缀（如 `...V1`） | 通过（文档层） | `rg -n "Motor.*V[0-9]+" oh-my-robot/docs/adr` |

## 2.3 与研发规划一一对应矩阵（审计）

| 研发规划阶段 | 本文对应条目 | 对齐结论 | 偏差处理结论 |
| --- | --- | --- | --- |
| Phase 0 | 2 / 3.1 | 已对齐 | 持续补充一致性证据 |
| Phase 1 | 2 / 3.2 | 待开始 | 无 |
| Phase 2 | 2 / 3.3 | 待开始 | 无 |
| Phase 3 | 2 / 3.4 | 待开始 | 无 |
| Phase 4 | 2 / 3.5 | 待开始 | 无 |

## 3. 当前阶段详情（按阶段原地维护）

## 3.1 Phase 0（合同与边界冻结）

- 当前进展：
  1. 已完成 5 件套文档重构。
  2. 已扩展合同域至完整架构考虑点。
  3. 已完成类型命名去版本后缀（文档层）。
  4. 已明确当前不纳入 ECAT 路径。
- 当前状态：进行中（待执行跨文档一致性复核）。
- 待办：
  1. 完成合同编号互引一致性检查。
  2. 补充合同到实现映射示例模板。

## 3.2 Phase 1（统一模型最小壳）

- 当前状态：待开始。
- 计划项未完成：
  1. Core 类型壳未创建。
  2. 最小接口壳未创建。
  3. Transport/Adapter 抽象壳未创建。

## 3.3 Phase 2（DJI + P1010B 适配）

- 当前状态：待开始。
- 计划项未完成：
  1. `dji -> unified_model` 映射层未落地。
  2. `p1010b -> unified_model` 映射层未落地。
  3. `compat_note` 未生成。

## 3.4 Phase 3（达妙适配与参数诊断收敛）

- 当前状态：待开始。
- 计划项未完成：
  1. 达妙契约头文件未补齐。
  2. `damiao -> unified_model` 映射层未落地。
  3. 参数与诊断闭环未验证。

## 3.5 Phase 4（回归收敛与发布留痕）

- 当前状态：待开始。
- 计划项未完成：
  1. 合同覆盖率回归未执行。
  2. 跨 vendor 一致性回归未执行。

## 4. 更新模板（后续阶段直接覆盖）

| 阶段 | 功能域 | 当前状态 | 证据路径 | 验证状态 | 未解决问题 |
| --- | --- | --- | --- | --- | --- |
| Phase X | 示例：调度语义 | 示例：进行中 | 示例：`oh-my-robot/lib/drivers/...` | 示例：单测通过/待补 | 示例：边界场景未覆盖 |

## 5. 变更记录

1. `v1.0`：重构进度说明结构，新增合同域覆盖追踪与命名合规检查，并明确当前不纳入 ECAT。



