# OM Jira 配置快照

> **本文是 Jira `OMR` 当前配置的快照，不是规则。**
> Jira 是配置事实源；本文只用于评审、追溯和新人理解。
> 配置变化时，先改 Jira，再更新本文，并记录变更。
>
> 最后同步：2026-11-10
> 配套原则：[project_management_principles.md](project_management_principles.md)
> 新人速查：[jira_quick_start.md](jira_quick_start.md)

## 1. 项目信息

| 项 | 值 |
|---|---|
| Jira 项目 | `OMR` — Oh-My-Robot-Framework |
| 类型 | Team-managed / Kanban |
| 仓库 | `oh-my-robot/oh-my-robot-framework` |
| Lead | Bamboo |

## 2. 当前工作流（Workstream）

> 工作流是长期战略分组。当前集合如下，可按变更流程调整。

| 工作流 | 范围 |
|---|---|
| 框架拆分 | 框架本体 / 器件库 / 平台支持包三仓库拆分；框架本体持续开发 |
| 启动与升级 | Bootloader、OTA、镜像契约、回滚恢复 |
| 通信设施 | 板级通信、上位机通信、通信中间件 |
| 存储系统 | Flash、分区、存储器件（W25Q256 等） |
| 平台支持 | BSP、芯片平台、板级支持包 |

## 3. 当前状态流转（Workflow）

| 状态 | 类别 | 含义 |
|---|---|---|
| 待办 | To Do | 已录入、未开始 |
| 进行中 | In Progress | 正在开发 |
| 评审中 | In Progress | PR 已开，等待 review / CI |
| 待验证 | In Progress | 已合入，等待目标板 / 回归验证 |
| 完成 | Done | 验收通过 |

> 验证类状态必须属于 In Progress 类，不能属于 Done 类。

## 4. 当前字段

### 4.1 `工作流`（单选）

状态：**待创建**

| 值 | 说明 |
|---|---|
| 框架拆分 | 三仓库拆分与框架本体 |
| 启动与升级 | Bootloader / OTA |
| 通信设施 | 板级与上位机通信 |
| 存储系统 | Flash / 分区 / 存储器件 |
| 平台支持 | BSP / 芯片平台 |

建议加到：Epic（必填）。

### 4.2 `模块`（单选）

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

### 4.3 `芯片平台`（单选）

| 值 | 说明 |
|---|---|
| platform-agnostic | 与芯片无关 |
| STM32F4 | STM32F4 系列 |
| MSPM0G3507 | MSPM0G3507 |

### 4.4 `运行环境`（单选）

| 值 | 说明 |
|---|---|
| platform-agnostic | 与环境无关 |
| FreeRTOS | FreeRTOS |
| bare-metal | 裸机 |
| Windows | Windows host / 工具链 |

### 4.5 内置字段

| 字段 | 用途 |
|---|---|
| 修复版本 / Fix Version | 目标发布版本 |
| 优先级 / Priority | 排期 |
| 经办人 / Assignee | 负责人 |
| 父项 / Parent | 挂到 Epic |
| 标签 / Labels | 跨切面属性 |

## 5. 当前必填规则

### 目标配置

| 类型 | 必填 | 建议填写 |
|---|---|---|
| Epic | `工作流` | `模块`、`芯片平台`、`运行环境` |
| Story / Task | `模块` | `芯片平台`、`运行环境`、`修复版本` |
| Bug | `模块` | `芯片平台`、`运行环境`、`影响版本` |

### 当前实际配置（2026-11-10）

- `模块`、`芯片平台` 目前在 Epic 上为**必填**
- `工作流` 尚未创建
- `模块`、`芯片平台`、`运行环境` 尚未加到 Story / Task / Bug

> 待办：按“目标配置”调整，避免 Epic 被迫填不准确的模块/平台。

## 6. 当前 WIP 限制

| 列 | WIP |
|---|---|
| 进行中 | ≤ 2 |
| 评审中 | ≤ 3 |
| 待验证 | ≤ 3 |

## 7. 当前标签白名单

| 标签族 | 值 | 用途 |
|---|---|---|
| `toolchain:` | `gcc`、`armclang` | 工具链 |
| `board:` | `rm-a-board`、`rm-c-board` | 板卡 |
| `subject:` | `rtos`、`bsp`、`toolchain` | 开发主体 |
| 工作性质 | `tech-debt`、`spike`、`breaking-change`、`needs-hw` | 跨切面 |
| 阻塞 | `blocked` | 阻塞 |

## 8. 当前自动化规则

| 规则 | 触发 | 动作 |
|---|---|---|
| PR 打开 | PR opened | Issue → `评审中` |
| PR 合并 | PR merged | Issue → `待验证` |
| 完成但无版本 | Issue → `完成` 且 `修复版本` 为空 | 评论提醒 |
| Epic 缺工作流 | Epic 创建且 `工作流` 为空 | 评论提醒 |
| Bug 缺模块 | Bug 创建且 `模块` 为空 | 评论提醒 |

## 9. 当前版本

| 版本 | 状态 | 主题 |
|---|---|---|
| `v0.1.0` | 未发布 | 内核原语与构建基线 |
| `v0.2.0` | 未发布 | 驱动与服务 |

> 只保留当前版本 + 下一个版本；发布后在 Jira 标记为 Released。

## 10. 当前活跃 Epic

不在本文维护，避免与 Jira 漂移。查看方式：

```jql
project = OMR AND issuetype = 长篇故事 AND statusCategory != Done ORDER BY rank ASC
```

## 11. 配置待办

- [ ] 创建 `工作流` 字段（5 个值）
- [ ] 把 `工作流` 加到 Epic，设为必填
- [ ] 把 `模块`、`芯片平台`、`运行环境` 加到 `故事 / 任务 / 缺陷`
- [ ] 调整 Epic 必填规则：只保留 `工作流` 必填
- [ ] 关闭旧 10 个按层划分的 Epic（加注释，不删除）
- [ ] 新建 3 个成果型 Epic：
  - [ ] `[启动与升级] Boot/OTA 镜像契约与基础设施`
  - [ ] `[存储系统] 器件库：W25Q256JV SPI NOR 与存储器件`
  - [ ] `[平台支持] DM-MC02 STM32H723 BSP`
- [ ] 把 `OMR-1` 从旧 Epic 下移出
- [ ] 配置 Jira 自动化规则
- [ ] 创建 Jira Docs 入口页（摘要 + 链接）

## 12. 变更记录

| 日期 | 变更 | 影响 |
|---|---|---|
| 2026-11-10 | 初稿：记录当前配置与待办 | — |
