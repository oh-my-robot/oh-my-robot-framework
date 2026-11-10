# OM 项目管理原则

> 状态：v0.1
> 适用项目：Jira `OMR` / GitHub `oh-my-robot/oh-my-robot-framework`
> Owner：Bamboo
> 最后评审：2026-11-10
>
> 配套文档：
> - [当前配置快照](project_management_config.md)
> - [新人速查](jira_quick_start.md)

## 1. 目的与适用范围

本规范定义 OM 框架开发中**稳定的原则与规则**，不固化具体配置。

具体的工作流名称、状态、字段选项、必填规则、WIP 限制、自动化规则，以 `project_management_config.md` 和 Jira 实际配置为准。配置可以变化，原则应保持稳定。

适用范围：所有参与 OM 框架开发的内部成员与外部贡献者。

## 2. 术语

| 术语 | 英文 | 含义 |
|---|---|---|
| 工作流 | Workstream | 长期战略分组，如“启动与升级” |
| 状态流转 | Workflow | Jira 状态机，如“待办 → 进行中 → 完成” |
| Epic | Epic | 有边界、可验收的交付物 |
| Issue | Story / Task / Bug / Subtask | 可执行任务 |
| 模块 | Module | 架构层 / 代码位置，Jira 字段 |
| 版本 | Fix Version | 发布里程碑 |

> “工作流”和“状态流转”不是一回事。前者回答“为什么做”，后者回答“现在到哪一步”。

## 3. 核心模型

```
工作流（长期）
  └── Epic（1–3 个月，有目标版本）
        └── Story / Task / Bug（1–3 天）
              └── Subtask（≤ 1 天）

工作流 / 模块 / 芯片平台 / 运行环境 = 字段
版本 = Fix Version
```

原则：

- 工作流长期存在，不随某个版本关闭。
- Epic 有边界，必须能收口。
- Issue 可执行，有验收标准。
- 架构层用 `模块` 字段表达，不用 Epic 表达。
- 时间用 `修复版本` 表达，不用 Epic 表达。

## 4. 原则

### 4.1 工作流（Workstream）

- 工作流是长期战略分组，不是 Epic，也不是 Jira 状态机。
- 工作流数量应保持少（建议 3–7 个），便于聚焦。
- 每个 Epic 只归属一个工作流。
- 工作流可以暂时没有活跃 Epic；**没有启动的工作流不要建占位 Epic**。
- 当前工作流集合是配置，见 `project_management_config.md`。
- 新增、删除、改名工作流必须走变更流程（见 4.7）。

### 4.2 Epic

Epic 是一个有边界、可验收的交付物。创建前必须满足：

- [ ] 有明确 owner
- [ ] 有目标版本（Fix Version）
- [ ] 有可验收的成功标准
- [ ] 有至少一个可执行的首个 Issue
- [ ] 预计 1–3 个月内能收口
- [ ] 不是“目录名”“架构层”或“长期方向”

规则：

- 架构层不作为 Epic 划分依据；架构层用 `模块` 字段。
- 不建占位 Epic；未来想法放 Backlog 或文档，不建 Epic。
- Epic 完成后关闭；每月 review，合并或关闭过时 Epic。
- Epic 命名见 5.1。

### 4.3 Issue

| 类型 | 用途 | 粒度 |
|---|---|---|
| Story（故事） | 可交付功能 / 能力 | 1–5 天 |
| Task（任务） | 工程工作：重构、构建、CI、文档 | 1–3 天 |
| Bug（缺陷） | 可复现缺陷 | 0.5–3 天 |
| Subtask（子任务） | 实现步骤 | ≤ 1 天 |

规则：

- 一个 Issue 控制在 1–3 天；超过则拆 Subtask 或拆成多个 Issue。
- 不是每个 Issue 都必须挂 Epic。Bug、独立小任务可以没有 Epic，但必须填结构化字段。
- 不建“占位 Issue”。

### 4.4 字段与标签

| 类型 | 放哪里 | 例子 |
|---|---|---|
| 结构化维度 | 字段 | 工作流、模块、芯片平台、运行环境、版本 |
| 跨切面 / 临时属性 | 标签 | `tech-debt`、`needs-hw`、`toolchain:gcc` |

规则：

- 能进字段的，不进标签。
- 只出现一次的概念，写进描述，不建标签。
- 标签使用统一前缀、小写、连字符；当前白名单见配置文档。
- 不设默认标签；每季度清理一次。

### 4.5 版本与发布

- 版本格式：`v<major>.<minor>.<patch>[-suffix]`，遵循 SemVer。
- Jira `Fix Version` 与 GitHub Tag 完全一致。
- 只创建当前版本 + 下一个版本，不预建版本坟场。
- 不兼容变更加 `breaking-change` 标签，并在发布说明中单列。
- 发布流程以 `docs/process/git_version_release_spec.md` 为准。

### 4.6 单一事实源

| 内容 | 事实源 |
|---|---|
| 原则 / 规则 | 本仓库 `docs/process/` |
| 当前配置 | Jira 项目设置 + `project_management_config.md` 快照 |
| 实时状态 | Jira |
| 代码与发布 | GitHub |

规则：

- **链接，不复制。** Jira Docs、项目说明只放摘要和链接。
- 不在两个地方维护同一份规则。
- 配置变化时，更新 Jira 和配置快照；原则变化时，走 PR。

### 4.7 变更管理

#### 原则变更

- 修改本文件必须走 PR。
- PR 描述需说明：改了什么、为什么改、影响哪些流程。
- 合并后更新文档头部的“最后评审”日期。

#### 配置变更

配置变更不走代码 PR，但必须留痕：

1. 提案：说明变更内容、原因、影响范围
2. 评审：Owner 确认
3. 修改 Jira 配置
4. 迁移现有数据（字段值、状态、Epic 归属）
5. 更新 `project_management_config.md` 快照与变更记录
6. 通知参与者

常见变更：

| 变更 | 额外要求 |
|---|---|
| 新增 / 删除工作流 | 迁移相关 Epic，更新配置快照 |
| 修改状态流转 | 迁移 Issue，检查状态类别（验证类必须属于进行中类） |
| 修改字段选项 | 迁移数据，更新 JQL/仪表盘 |
| 修改必填规则 | 检查存量 Issue，避免阻塞 |

### 4.8 评审节奏

| 频率 | 动作 |
|---|---|
| 每周 15 分钟 | Backlog grooming：分配工作流 / 模块 / 版本，清理阻塞 |
| 每月 | Epic review：完成的关闭，过时的合并，不建占位 Epic |
| 每季度 | 配置 review：工作流、状态、字段、标签；更新配置快照 |

## 5. 命名规范

### 5.1 Epic / Issue

| 对象 | 格式 | 示例 |
|---|---|---|
| Epic | `<成果描述>` 或 `[工作流] <成果描述>` | `[启动与升级] Boot/OTA 镜像契约` |
| Story / Task | `<模块>: <动作> + <对象>` | `OSAL: 实现 FreeRTOS 互斥量后端` |
| Bug | `[模块] <现象>（<条件>）` | `[OSAL] 中断嵌套下消息队列死锁` |
| Subtask | `<动作> + <对象>` | `补充互斥量 host 单测` |

规则：

- 动词开头，对象具体。
- Bug 写现象和条件，不写修复方案。
- 避免“优化”“完善”“重构”这类没有具体对象的标题。

### 5.2 分支

```
feature/OMR-<id>-<slug>
fix/OMR-<id>-<slug>
hotfix/OMR-<id>-<slug>
docs/OMR-<id>-<slug>
```

- `slug` 使用小写英文 + 连字符。
- 分支职责以 `docs/process/git_collaboration_spec.md` 为准。

### 5.3 PR

```
OMR-<id> <type>(<scope>): <summary>
```

示例：

```
OMR-123 feat(osal): add FreeRTOS mutex backend
OMR-456 fix(drivers): correct SPI CS timing
```

### 5.4 提交

- 提交信息包含 Jira Key。
- 推荐 Conventional Commits 风格。
- 不写 `update`、`fix bug` 这类无信息量的消息。

## 6. 角色与治理

| 角色 | 权限 |
|---|---|
| Owner | 创建 / 关闭 Epic，修改字段与配置，维护本规范 |
| Developer | 创建 Story / Task / Bug，更新状态，关联 PR |
| Contributor | 通过 GitHub Issue / PR 参与；由 Developer 转为 Jira Issue |

变更配置和原则的权限归 Owner；其他成员通过提案或 PR 参与。

## 7. 新人 onboarding

1. 阅读 `docs/process/jira_quick_start.md`（10 分钟）。
2. 获取 Jira `OMR` 项目访问权限。
3. 阅读本文件第 3–4 节，理解工作流、Epic、Issue 的区别。
4. 阅读 `project_management_config.md`，了解当前工作流、状态和字段。
5. 选择一个工作流，认领一个 `good-first-issue` 或由 Owner 分配任务。
6. 使用模板创建第一个 Issue。
7. 按第 5 节命名分支和 PR。
8. 与 Owner 过一遍 DoR / DoD。

## 8. 变更记录

| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-11-10 | v0.1 | 初稿：拆分原则与配置 |

## 附录 A：Epic 模板

```markdown
## 目标
<!-- 完成后具备什么能力 -->

## 范围
<!-- 包含什么；明确不做什么 -->

## 非目标
<!-- 明确排除的内容 -->

## 成功标准
- [ ] ...
- [ ] ...

## 关键依赖 / 风险
<!-- 上游 Epic、硬件、ADR、外部仓库 -->

## 目标版本
vX.Y.Z
```

## 附录 B：Story / Task 模板

```markdown
## 背景 / 目标

## 范围

## 接口与行为

## 验收标准
- [ ] ...
- [ ] ...

## 验证方式
- [ ] host 单测
- [ ] 目标板（板卡：___，固件版本：___）
- [ ] 静态检查

## 依赖
```

## 附录 C：Bug 模板

```markdown
## 现象

## 复现步骤
1.
2.
3.

## 期望行为

## 环境
- 板卡：
- MCU / RTOS：
- 工具链：
- 固件版本：

## 日志 / 波形

## 影响版本
```

## 附录 D：DoR / DoD

### DoR

- [ ] 所属工作流、模块
- [ ] 目标版本
- [ ] 目标行为 / 接口契约
- [ ] 验收标准
- [ ] 验证方式（host / 目标板 / 静态检查）
- [ ] 依赖（硬件、ADR、上游 Issue）

### DoD

- [ ] 代码合入 `integration`
- [ ] CI 绿（编译 + 静态检查 + host 测试）
- [ ] 目标板验证通过，记录板卡与固件版本
- [ ] 文档 / ADR 更新
- [ ] 不兼容变更加 `breaking-change` 标签
- [ ] `修复版本` 已填
