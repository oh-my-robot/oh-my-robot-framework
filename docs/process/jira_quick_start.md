# OM 项目管理速查

> 原则：[project_management_principles.md](project_management_principles.md)
> 当前配置：[project_management_config.md](project_management_config.md)
> 适用项目：Jira `OMR`

## 30 秒模型

```
工作流（长期）
  └── Epic（1–3 个月，有目标版本）
        └── Story / Task / Bug（1–3 天）
              └── Subtask（≤ 1 天）

工作流 / 模块 / 芯片平台 / 运行环境 = 字段
版本 = Fix Version
```

一句话：**工作流管“为什么做”，Epic 管“交付什么”，Issue 管“怎么做”，模块管“在哪做”，版本管“何时交付”。**

术语区分：

- **工作流（Workstream）**：长期战略分组，如“启动与升级”
- **状态流转（Workflow）**：Jira 状态机，如“待办 → 进行中”

## 当前工作流

> 这是当前配置，不是固定规则；以 `project_management_config.md` 为准。

| 工作流 | 范围 |
|---|---|
| 框架拆分 | 框架本体 / 器件库 / 平台支持包三仓库拆分 |
| 启动与升级 | Bootloader、OTA、镜像契约、回滚恢复 |
| 通信设施 | 板级通信、上位机通信、通信中间件 |
| 存储系统 | Flash、分区、存储器件（W25Q256 等） |
| 平台支持 | BSP、芯片平台、板级支持包 |

工作流可以暂时没有活跃 Epic。**没有启动的工作流不要建占位 Epic。**

## 当前字段

> 字段和选项是当前配置，见 `project_management_config.md`。

| 字段 | 作用 | 填在哪 |
|---|---|---|
| **工作流** | 长期分组 | Epic 必填 |
| **模块** | 架构层 / 代码位置 | Story / Task / Bug 必填 |
| **芯片平台** | 目标芯片 | 涉及硬件时填 |
| **运行环境** | OS / RTOS / host | 涉及 OS / host 时填 |
| **修复版本** | 目标发布版本 | 发布相关必填 |
| **优先级** | 排期 | 按需 |
| **标签** | 跨切面属性 | 按需，见白名单 |

## 当前状态流转

> 这是当前配置，不是固定规则。

```
待办 → 进行中 → 评审中 → 待验证 → 完成
```

| 状态 | 含义 |
|---|---|
| 待办 | 已录入、未开始 |
| 进行中 | 正在开发 |
| 评审中 | PR 已开，等待 review / CI |
| 待验证 | 已合入，等待目标板 / 回归验证 |
| 完成 | 验收通过 |

规则：

- 验证类状态必须属于进行中类，不是完成类。
- 验证失败 → 回到 `进行中`。
- 阻塞用标签 `blocked`，不加状态。

## 命名

| 对象 | 格式 | 示例 |
|---|---|---|
| Epic | `<成果描述>` 或 `[工作流] <成果描述>` | `[存储系统] W25Q256JV SPI NOR 与存储器件` |
| Story / Task | `<模块>: <动作> + <对象>` | `OSAL: 实现 FreeRTOS 互斥量后端` |
| Bug | `[模块] 现象（条件）` | `[OSAL] 中断嵌套下消息队列死锁` |
| 分支 | `<type>/OMR-<id>-<slug>` | `feature/OMR-123-osi-mutex` |
| PR | `OMR-<id> <type>(<scope>): <summary>` | `OMR-123 feat(osal): add FreeRTOS mutex backend` |
| 版本 | `v<major>.<minor>.<patch>` | `v0.1.0` |

## DoR / DoD

### 开始前（DoR）

- [ ] 工作流、模块、目标版本已填
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

## 当前标签白名单

> 当前配置，见 `project_management_config.md`。

| 标签 | 用途 |
|---|---|
| `toolchain:gcc` / `toolchain:armclang` | 工具链 |
| `board:rm-a-board` / `board:rm-c-board` | 板卡 |
| `subject:rtos` / `subject:bsp` / `subject:toolchain` | 开发主体 |
| `tech-debt` / `spike` / `breaking-change` / `needs-hw` | 工作性质 |
| `blocked` | 阻塞 |

规则：小写 + 连字符 + 前缀；不设默认标签；每季度清理。

## 新人 10 分钟

1. 读这份速查。
2. 读 `project_management_principles.md` 第 3–4 节。
3. 读 `project_management_config.md`，了解当前工作流、状态和字段。
4. 获取 Jira `OMR` 权限。
5. 选一个工作流，认领一个 `good-first-issue`。
6. 用模板建 Issue，填好工作流 / 模块 / 版本。
7. 按规范命名分支和 PR。
8. 与 Owner 过一遍 DoR / DoD。

## 常用 JQL

```jql
-- 我的待办
project = OMR AND assignee = currentUser() AND statusCategory != Done ORDER BY priority DESC

-- 当前版本未完成
project = OMR AND fixVersion = "v0.1.0" AND statusCategory != Done ORDER BY rank ASC

-- 某工作流
project = OMR AND "工作流" = "框架拆分" AND statusCategory != Done

-- 未解决缺陷
project = OMR AND issuetype = 缺陷 AND resolution = Unresolved ORDER BY priority DESC

-- 需要硬件验证
project = OMR AND labels = "needs-hw" AND statusCategory != Done
```
