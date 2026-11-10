# OM 项目管理速查

> 原则：[project_management_principles.md](project_management_principles.md)
> 当前工作主线、状态、字段、版本：**在 Jira `OMR` 中查看**，仓库不复制。

## 30 秒模型

```
工作主线（长期）
  └── Epic（1–3 个月，有目标版本）
        └── Story / Task / Bug（1–3 天）
              └── Subtask（≤ 1 天）
```

- **工作主线（Workstream）**：长期战略分组，回答“为什么做”
- **状态流转（Workflow）**：Jira 状态机，回答“现在到哪一步”
- **Epic**：有边界交付，回答“交付什么”
- **Issue**：可执行任务，回答“怎么做”
- **模块**：代码位置，回答“在哪做”
- **版本**：Fix Version，回答“何时交付”

## 去 Jira 看什么

| 内容 | 位置 |
|---|---|
| 当前工作主线 | `工作主线` 字段的选项 |
| 当前状态流转 | 看板列 / 项目设置 → 工作流 |
| 当前字段与选项 | 项目设置 → 工作类型 → 字段 |
| 当前版本 | Releases 页面 |
| 当前活跃 Epic | 看板 / JQL |

## 三条原则

1. **工作主线长期，Epic 有边界，Issue 可执行。**
2. **不建占位 Epic / Issue。** 未来想法放 Backlog 或文档。
3. **字段管结构，标签管属性。** 当前配置以 Jira 为准。

## 命名

| 对象 | 格式 | 示例 |
|---|---|---|
| Epic | `<成果描述>` 或 `[工作主线] <成果描述>` | `[存储系统] W25Q256JV SPI NOR 与存储器件` |
| Story / Task | `<模块>: <动作> + <对象>` | `OSAL: 实现 FreeRTOS 互斥量后端` |
| Bug | `[模块] 现象（条件）` | `[OSAL] 中断嵌套下消息队列死锁` |
| 分支 | `<type>/OMR-<id>-<slug>` | `feature/OMR-123-osi-mutex` |
| PR | `OMR-<id> <type>(<scope>): <summary>` | `OMR-123 feat(osal): add FreeRTOS mutex backend` |
| 版本 | `v<major>.<minor>.<patch>` | `vX.Y.Z` |

## DoR / DoD

### 开始前（DoR）

- [ ] 工作主线、模块、目标版本已填
- [ ] 目标行为 / 接口契约清楚
- [ ] 验收标准可判断
- [ ] 验证方式明确（host / 目标板 / 静态检查）
- [ ] 依赖已识别

### 完成时（DoD）

- [ ] 代码合入 `integration`
- [ ] CI 绿
- [ ] host 单测通过
- [ ] 目标板验证通过，记录板卡与固件版本
- [ ] 文档 / ADR 更新
- [ ] 不兼容变更加 `breaking-change`
- [ ] 修复版本已填

## 新人 10 分钟

1. 读这份速查。
2. 读 `project_management_principles.md` 第 1–5 节。
3. 打开 Jira `OMR`，看当前工作主线、看板和字段。
4. 获取权限，选一个工作主线，认领一个 `good-first-issue`。
5. 建 Issue，按规范命名分支和 PR。
6. 与 Owner 过一遍 DoR / DoD。

## 常用 JQL

```jql
-- 我的待办
project = OMR AND assignee = currentUser() AND statusCategory != Done ORDER BY priority DESC

-- 当前版本未完成（把 vX.Y.Z 换成 Jira Releases 里的当前版本）
project = OMR AND fixVersion = "vX.Y.Z" AND statusCategory != Done ORDER BY rank ASC

-- 某工作主线（把 <工作主线> 换成字段里的实际值）
project = OMR AND "工作主线" = "<工作主线>" AND statusCategory != Done

-- 未解决缺陷
project = OMR AND issuetype = 缺陷 AND resolution = Unresolved ORDER BY priority DESC
```
