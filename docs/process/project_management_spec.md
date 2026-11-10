# OM 项目管理规范

> 状态：草案 v0.1
> 适用项目：Jira `OMR` / GitHub `oh-my-robot/oh-my-robot-framework`
> Owner：Bamboo
> 最后评审：2026-11-10

## 1. 目的与适用范围

本规范定义 Oh My Robot（OM）框架的开发管理方式，回答四个问题：

1. 工作如何分组？——工作流
2. 什么算一个可交付成果？——Epic
3. 日常任务如何创建和验收？——Issue / DoR / DoD
4. 代码、版本、PR 如何对齐？——字段、版本与命名规范

适用范围：所有参与 OM 框架开发的内部成员与外部贡献者。

本规范的修改必须走 PR，并在文档头部更新“最后评审”日期。

## 2. 组织模型

### 2.1 四层模型

| 层 | Jira 对象 | 回答的问题 | 生命周期 |
|---|---|---|---|
| 工作流 | `工作流` 字段 | 为什么做、长期方向 | 长期 |
| Epic | Epic | 交付什么、何时算完成 | 1–3 个月 |
| Issue | Story / Task / Bug / Subtask | 具体做什么 | 1–3 天 |
| 模块与版本 | `模块` 字段 / Fix Version | 代码在哪、何时交付 | 长期 / 时间点 |

原则：**工作流长期存在，Epic 有边界，Issue 可执行，模块表达架构层，版本表达时间。**

### 2.2 工作流

工作流是一类长期工作的主线，用 Jira 单选字段 `工作流` 表示。工作流不是 Epic；它不会因为某个版本发布而关闭。

当前工作流：

1. 框架拆分
2. 启动与升级
3. 通信设施
4. 存储系统
5. 平台支持

工作流可以暂时没有活跃 Epic。**没有启动的工作流只在本文档和字段里存在，不要在 Jira 建占位 Epic。**

### 2.3 Epic

Epic 是一个有边界、可验收的交付物，必须满足：

- 有明确目标与成功标准
- 有目标版本（Fix Version）
- 有至少一个可执行的首个 Issue
- 能在 1–3 个月内收口
- 不是单纯的“目录名”或“长期方向”

架构层不作为 Epic 划分依据；架构层用 `模块` 字段表达。

### 2.4 Issue

| 类型 | 用途 | 粒度 |
|---|---|---|
| Story（故事） | 可交付功能 / 能力 | 1–5 天 |
| Task（任务） | 工程工作：重构、构建、CI、文档 | 1–3 天 |
| Bug（缺陷） | 可复现缺陷 | 0.5–3 天 |
| Subtask（子任务） | 实现步骤 | ≤ 1 天 |

规则：

- 一个 Issue 控制在 1–3 天；超过则拆 Subtask 或拆成多个 Issue。
- 不是每个 Issue 都必须挂 Epic。Bug、独立小任务可以没有 Epic，但必须填 `工作流`、`模块` 等字段。
- 不建“占位 Issue”。

### 2.5 字段与版本

- `工作流`：长期分组。
- `模块`：架构层 / 代码位置。
- `芯片平台`、`运行环境`：目标硬件与运行环境。
- `修复版本`：目标发布版本。
- `优先级`：排期依据。
- 标签：跨切面属性，见第 8 节。

## 3. 工作流定义

### 3.1 框架拆分

| 项 | 内容 |
|---|---|
| 目标 | 将框架拆分为框架本体、器件库、平台支持包三个仓库，依赖清晰、可独立构建与发布 |
| 范围 | 仓库边界、依赖规则、构建 / 包管理、CI、版本矩阵、迁移 |
| 非目标 | 具体器件驱动功能、BSP 新平台适配（属于存储系统 / 平台支持） |
| 成功指标 | 三仓库可独立构建；跨仓库版本矩阵可追溯；迁移指南完成 |
| 典型 Epic | 三仓库边界与依赖规则、器件库拆分、平台支持包拆分、跨仓库工程化 |

### 3.2 启动与升级

| 项 | 内容 |
|---|---|
| 目标 | 建立 Bootloader 与 OTA 框架，做到可升级、可回滚、可验证 |
| 范围 | 镜像格式、分区、启动流程、升级服务、传输接入、校验、恢复、工具 |
| 非目标 | 通信中间件本身（属于通信设施）；存储器件驱动（属于存储系统） |
| 成功指标 | 目标板可完成一次 OTA；失败可回滚；工具有 CI 验证 |
| 典型 Epic | Boot/OTA 镜像契约与基础设施、OTA 升级框架、Boot/OTA 工具链与验证 |

### 3.3 通信设施

| 项 | 内容 |
|---|---|
| 目标 | 建立服务板级通信与上位机通信的统一中间件 |
| 范围 | 消息模型、传输抽象、路由、分帧、可靠性、CAN / Serial 传输、上位机协议与工具 |
| 非目标 | 片内 IPC（属于框架本体）；具体业务协议 |
| 成功指标 | 业务层不依赖具体总线；板级与上位机通信可复用同一套消息模型 |
| 典型 Epic | 通信中间件核心、板级通信、上位机通信、通信验证与互操作 |

### 3.4 存储系统

| 项 | 内容 |
|---|---|
| 目标 | 建立可移植的存储抽象与器件库，覆盖片内 Flash、外部 NOR / NAND 等 |
| 范围 | Flash PAL、分区、存储器件驱动、W25Q256 等器件适配、真机验证 |
| 非目标 | OTA 升级策略（属于启动与升级）；文件系统（后续评估） |
| 成功指标 | 存储器件可替换；分区 / 句柄语义稳定；真机验证有记录 |
| 典型 Epic | 器件库：W25Q256JV SPI NOR 与存储器件、分区表 v2、Flash PAL |

### 3.5 平台支持

| 项 | 内容 |
|---|---|
| 目标 | 提供可独立发布的平台支持包（BSP / SoC / 板级适配） |
| 范围 | STM32F4、MSPM0G3507、STM32H723、板级初始化、时钟、引脚、外设适配 |
| 非目标 | 通用驱动框架（属于框架本体 / 存储系统）；应用业务 |
| 成功指标 | 新平台接入只需增加平台包；至少两个平台可构建；板级示例可运行 |
| 典型 Epic | DM-MC02 STM32H723 BSP、STM32F4 平台包、MSPM0G3507 平台包 |

> 工作流之间允许有依赖，但每个 Epic 只归属一个工作流；跨工作流依赖用 Issue Link 表达。

## 4. Epic 规则

### 4.1 创建条件

一个 Epic 只有同时满足以下条件才创建：

- [ ] 有明确 owner
- [ ] 有目标版本（Fix Version）
- [ ] 有可验收的成功标准
- [ ] 有至少一个可执行的首个 Issue
- [ ] 预计 1–3 个月内能收口

不满足以上条件的想法，放在 Backlog 或文档里，不建 Epic。

### 4.2 Epic 模板

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

### 4.3 生命周期

```
创建 → 进行中 → 完成 → 关闭
```

- 完成后标记 `完成`，并在评论里写清交付物与版本。
- 每月回顾一次 Epic：完成的关闭，过时的合并或关闭。
- **不保留僵尸 Epic。**

### 4.4 命名

推荐格式：

```
<成果描述>
```

或

```
[<工作流>] <成果描述>
```

示例：

- `Boot/OTA 镜像契约与基础设施`
- `[存储系统] W25Q256JV SPI NOR 与存储器件`
- `[平台支持] DM-MC02 STM32H723 BSP`

避免：

- `OSAL 与运行时`（这是目录 / 层，不是成果）
- `服务层`（同上）
- `优化`、`完善`、`重构`（没有具体对象）

## 5. Issue 规则

### 5.1 类型

| 类型 | 用途 |
|---|---|
| Story（故事） | 可交付功能 / 能力，有验收标准 |
| Task（任务） | 重构、构建、CI、文档、工具链等工程工作 |
| Bug（缺陷） | 可复现缺陷 |
| Subtask（子任务） | 实现步骤，挂在 Story / Task 下 |

### 5.2 命名

| 类型 | 格式 | 示例 |
|---|---|---|
| Story / Task | `<模块>: <动作> + <对象>` | `OSAL: 实现 FreeRTOS 互斥量后端` |
| Bug | `[<模块>] <现象>（<条件>）` | `[OSAL] 中断嵌套下消息队列死锁` |
| Subtask | `<动作> + <对象>` | `补充互斥量 host 单测` |

规则：

- 动词开头，对象具体。
- Bug 写现象和条件，不写修复方案。
- 标题不重复字段已有信息。

### 5.3 Definition of Ready

- [ ] 所属 `工作流`、`模块`
- [ ] 目标版本
- [ ] 目标行为 / 接口契约
- [ ] 验收标准
- [ ] 验证方式（host / 目标板 / 静态检查）
- [ ] 依赖（硬件、ADR、上游 Issue）

### 5.4 Definition of Done

- [ ] 代码合入 `integration`
- [ ] CI 绿（编译 + 静态检查 + host 测试）
- [ ] 目标板验证通过，记录板卡与固件版本
- [ ] 文档 / ADR 更新
- [ ] 不兼容变更加 `breaking-change` 标签
- [ ] `修复版本` 已填

### 5.5 模板

#### Story / Task

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

#### Bug

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

## 6. 字段字典

### 6.1 `工作流`（单选）

| 值 | 说明 |
|---|---|
| 框架拆分 | 三仓库拆分与框架本体 |
| 启动与升级 | Bootloader / OTA |
| 通信设施 | 板级与上位机通信 |
| 存储系统 | Flash / 分区 / 存储器件 |
| 平台支持 | BSP / 芯片平台 |

必填规则：

- Epic：必填
- Story / Task / Bug：建议填写；无法归类时留空

### 6.2 `模块`（单选）

| 值 | 对应目录 |
|---|---|
| Core | `lib/core` |
| DataStruct | `lib/data_struct` |
| Algorithm | `lib/algorithm` |
| Osal | `lib/osal` |
| Sync | `lib/sync` |
| Async | `lib/async` |
| Ipc | `lib/ipc` |
| Drivers | `lib/drivers` |
| Services | `lib/services` |
| Systems | `lib/systems` |
| Platform | `platform` |
| Build | `xmake/`、`xmake.lua` |
| Tools | `tools/` |
| Samples | `samples/` |
| Docs | `docs/` |
| CI | `.github/` |
| CrossModule | 跨模块 |

必填规则：

- Story / Task：必填
- Bug：必填
- Epic：选填（Epic 可能跨模块）

### 6.3 `芯片平台`（单选）

| 值 | 说明 |
|---|---|
| platform-agnostic | 与芯片无关 |
| STM32F4 | STM32F4 系列 |
| MSPM0G3507 | MSPM0G3507 |

必填规则：涉及硬件 / 驱动的 Issue 建议必填；纯软件可填 `platform-agnostic`。

### 6.4 `运行环境`（单选）

| 值 | 说明 |
|---|---|
| platform-agnostic | 与环境无关 |
| FreeRTOS | FreeRTOS |
| bare-metal | 裸机 |
| Windows | Windows host / 工具链 |

必填规则：涉及 OS / RTOS 或 host 工具的 Issue 建议必填。

### 6.5 内置字段

| 字段 | 用途 |
|---|---|
| 修复版本 / Fix Version | 目标发布版本 |
| 优先级 / Priority | 排期 |
| 经办人 / Assignee | 负责人 |
| 父项 / Parent | 挂到 Epic |
| 标签 / Labels | 跨切面属性 |

## 7. 状态与看板

### 7.1 状态

| 状态 | 类别 | 含义 |
|---|---|---|
| 待办 | To Do | 已录入、未开始 |
| 进行中 | In Progress | 正在开发 |
| 评审中 | In Progress | PR 已开，等待 review / CI |
| 待验证 | In Progress | 已合入，等待目标板 / 回归验证 |
| 完成 | Done | 验收通过 |

规则：

- `待验证` 必须是 In Progress 类，不能是 Done 类。
- `完成` 必须是 Done 类。
- 允许 `待验证 → 进行中`（验证失败）和 `完成 → 待办`（重新打开）。
- 用标签 `blocked` 表示阻塞，不新增状态。

### 7.2 WIP 限制

- 进行中 ≤ 2
- 评审中 ≤ 3
- 待验证 ≤ 3

### 7.3 看板

- 列 = 5 个状态。
- Quick filters：仅我的、仅缺陷、当前版本、需要硬件、阻塞。
- 每周 review backlog；每月 review Epic。

## 8. 标签规范

### 8.1 白名单

| 标签族 | 值 | 用途 |
|---|---|---|
| `toolchain:` | `gcc`、`armclang` | 工具链 |
| `board:` | `rm-a-board`、`rm-c-board` | 板卡 |
| `subject:` | `rtos`、`bsp`、`toolchain` | 开发主体 |
| 工作性质 | `tech-debt`、`spike`、`breaking-change`、`needs-hw` | 跨切面 |
| 阻塞 | `blocked` | 阻塞 |

### 8.2 规则

1. 能进字段的，不进标签。
2. 小写 + 连字符 + 前缀。
3. 一个标签至少用 3 次才保留。
4. 不设默认标签。
5. 每季度清理一次。
6. 标签族超过 3 个或总数超过 20 个，考虑升级为字段。

## 9. 版本与发布

- 版本格式：`v<major>.<minor>.<patch>[-suffix]`，遵循 SemVer。
- Jira `Fix Version` 与 GitHub Tag 完全一致。
- 只创建当前版本 + 下一个版本。
- 不兼容变更加 `breaking-change` 标签，并在发布说明中单列。
- 发布流程以 `docs/process/git_version_release_spec.md` 为准。
- 每个版本发布后，在 Jira 中标记该版本为 Released。

## 10. 命名规范

### 10.1 分支

```
feature/OMR-<id>-<slug>
fix/OMR-<id>-<slug>
hotfix/OMR-<id>-<slug>
docs/OMR-<id>-<slug>
```

- `slug` 使用小写英文 + 连字符。
- 分支职责以 `docs/process/git_collaboration_spec.md` 为准。

### 10.2 PR

```
OMR-<id> <type>(<scope>): <summary>
```

示例：

```
OMR-123 feat(osal): add FreeRTOS mutex backend
OMR-456 fix(drivers): correct SPI CS timing
```

### 10.3 提交

- 提交信息包含 Jira Key。
- 推荐 Conventional Commits 风格。
- 不写 `update`、`fix bug` 这类无信息量的消息。

### 10.4 版本

- `v0.1.0`
- 预发布：`v0.1.0-rc.1`

## 11. 自动化规则

| 规则 | 触发 | 动作 |
|---|---|---|
| PR 打开 | PR opened | Issue → `评审中` |
| PR 合并 | PR merged | Issue → `待验证` |
| 完成但无版本 | Issue → `完成` 且 `修复版本` 为空 | 评论提醒 |
| Epic 缺工作流 | Epic 创建且 `工作流` 为空 | 评论提醒 |
| Bug 缺模块 | Bug 创建且 `模块` 为空 | 评论提醒 |
| 长期未更新 | 14 天无更新 | 评论提醒（可选） |

> 自动化只做“减少手工动作”的事，不掩盖流程问题。

## 12. 角色与治理

### 12.1 角色

| 角色 | 权限 |
|---|---|
| Owner | 创建 / 关闭 Epic，修改字段与工作流，维护本规范 |
| Developer | 创建 Story / Task / Bug，更新状态，关联 PR |
| Contributor | 通过 GitHub Issue / PR 参与；由 Developer 转为 Jira Issue |

### 12.2 节奏

| 频率 | 动作 |
|---|---|
| 每周 15 分钟 | Backlog grooming：分配工作流 / 模块 / 版本，清理阻塞 |
| 每月 | Epic review：完成的关闭，过时的合并，不建占位 Epic |
| 每季度 | 标签清理、字段选项清理、本规范 review |

### 12.3 变更管理

- 本规范的修改走 PR。
- PR 描述需说明：改了什么、为什么改、影响哪些字段 / 流程。
- 修改后更新文档头部的“最后评审”日期。

## 13. 新人 onboarding

1. 阅读 `docs/process/jira_quick_start.md`（10 分钟）。
2. 获取 Jira `OMR` 项目访问权限。
3. 阅读第 3 节，了解 5 个工作流。
4. 选择一个工作流，认领一个 `good-first-issue` 或由 Owner 分配任务。
5. 创建第一个 Issue，使用第 5.5 节模板。
6. 按第 10 节命名分支和 PR。
7. 与 Owner 过一遍 DoR / DoD。
8. 在 Jira 中把 Issue 从 `待办` 移动到 `进行中`。

## 14. 变更记录

| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-11-10 | v0.1 | 初稿 |

## 附录 A：Epic 模板

```markdown
## 目标

## 范围

## 非目标

## 成功标准
- [ ] ...
- [ ] ...

## 关键依赖 / 风险

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

## 附录 D：常用 JQL

```jql
-- 当前版本未完成
project = OMR AND fixVersion = "v0.1.0" AND statusCategory != Done ORDER BY rank ASC

-- 未解决缺陷
project = OMR AND issuetype = 缺陷 AND resolution = Unresolved ORDER BY priority DESC, created ASC

-- 我的待办
project = OMR AND assignee = currentUser() AND statusCategory != Done ORDER BY priority DESC

-- 某工作流
project = OMR AND "工作流" = "框架拆分" AND statusCategory != Done

-- 某模块
project = OMR AND 模块 = "Osal" AND statusCategory != Done

-- 需要硬件验证
project = OMR AND labels = "needs-hw" AND statusCategory != Done
```
