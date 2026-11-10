# OM 项目管理速查

> 详细规范：`docs/process/project_management_spec.md`
> 适用项目：Jira `OMR`

## 30 秒模型

```
工作流（长期）
  └── Epic（1–3 个月，有目标版本）
        └── Story / Task / Bug（1–3 天）
              └── Subtask（≤ 1 天）

模块 / 芯片平台 / 运行环境 = 字段
版本 = Fix Version
```

一句话：**工作流管“为什么做”，Epic 管“交付什么”，Issue 管“怎么做”，模块管“在哪做”，版本管“何时交付”。**

## 5 个工作流

| 工作流 | 范围 |
|---|---|
| 框架拆分 | 框架本体 / 器件库 / 平台支持包三仓库拆分 |
| 启动与升级 | Bootloader、OTA、镜像契约、回滚恢复 |
| 通信设施 | 板级通信、上位机通信、通信中间件 |
| 存储系统 | Flash、分区、存储器件（W25Q256 等） |
| 平台支持 | BSP、芯片平台、板级支持包 |

工作流可以暂时没有 Epic。**没有启动的工作流不要建占位 Epic。**

## 4 个字段

| 字段 | 值 | 填在哪 |
|---|---|---|
| **工作流** | 框架拆分 / 启动与升级 / 通信设施 / 存储系统 / 平台支持 | Epic 必填 |
| **模块** | Core / DataStruct / Algorithm / Osal / Sync / Async / Ipc / Drivers / Services / Systems / Platform / Build / Tools / Samples / Docs / CI / CrossModule | Story / Task / Bug 必填 |
| **芯片平台** | platform-agnostic / STM32F4 / MSPM0G3507 | 涉及硬件时填 |
| **运行环境** | platform-agnostic / FreeRTOS / bare-metal / Windows | 涉及 OS / host 时填 |

另外：

- **修复版本**：目标版本，必填（发布相关）
- **优先级**：排期依据
- **标签**：只放跨切面属性，见下方白名单

## 5 个状态

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

- `待验证` 属于进行中类，不是完成类。
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

## 标签白名单

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
2. 获取 Jira `OMR` 权限。
3. 读 5 个工作流的定义（见规范第 3 节）。
4. 选一个工作流，认领一个 `good-first-issue`。
5. 用模板建 Issue，填好工作流 / 模块 / 版本。
6. 按规范命名分支和 PR。
7. 与 Owner 过一遍 DoR / DoD。

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
