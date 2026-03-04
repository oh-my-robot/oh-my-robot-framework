# 📦 发布与版本控制规范

规范框架代码从主干合并、打标签到最终生成 Release 固件的标准流程，确保所有输出版本可追溯、可重现。

## 1. 语义化版本号规则 (SemVer)

版本号统一采用标准格式：`v<major>.<minor>.<patch>[-suffix]`

| 字段 | 触发时机与含义 | 示例 |
| --- | --- | --- |
| **`major` (主版本)** | 发生**不向下兼容**的架构或 API 变更（如 OSAL 接口重构）。 | `v1.0.0` -> `v2.0.0` |
| **`minor` (次版本)** | 新增**向下兼容**的功能（如新增某个外设的 BSP 支持）。 | `v1.1.0` -> `v1.2.0` |
| **`patch` (修订号)** | 进行**向下兼容**的缺陷修复。 | `v1.1.1` -> `v1.1.2` |
| **`suffix` (预发布)** | 用于测试或集成阶段的标识（如 `-alpha`, `-rc.1`）。 | `v1.2.0-rc.1` |

> **注：** 当前框架处于早期孵化阶段，默认使用 `v0.x.x`。在此阶段，API 可能随时发生不兼容变更，次版本号 `minor` 的增加代表重要里程碑的达成。

## 2. 打标签 (Tag) 与发布工作流

在将代码合并至主线并确认稳定后，发版管理员需通过 Git 附注标签记录发布节点。

> **⚠️ 远程仓库别名约定 (Remotes)：**
> 本规范假定管理员的本地环境已正确配置派生工作流的远程仓库：
> * `origin`：指向个人派生仓库 (Fork)。
> * `upstream`：指向团队官方主仓库。
> **所有 Release Tag 必须直接推送到 `upstream`。**
> 
> 

**场景 A：常规发布 (Minor / Major)**
当一个迭代的 Milestone 完成，代码从 `integration` 分支合入主仓库的 `main` 后触发。

在 `integration -> main` 的发布 PR 中，使用 `Fixes #<id>` 批量关闭本次发布包含的 Issue。

```bash
# 1. 切换到本地 main 分支
git checkout main

# 2. 从官方主仓库 (upstream) 拉取最新代码，确保与官方进度绝对一致
git pull upstream main

# 3. 创建附注标签 (必须包含 -a 和 -m)
git tag -a v0.2.0 -m "Release v0.2.0: 完成 OSAL 层基础同步原语封装"

# 4. 【关键】将标签推送到官方主仓库 (upstream)
git push upstream v0.2.0

```

**场景 B：紧急补丁发布 (Patch)**
当线上的严重 Bug 已按 Hotfix 流程修复（`hotfix/* -> main`，并完成回灌 `integration`），且修复代码的 PR 已合入主仓库的 `main` 后触发。Hotfix 流程详见 [`docs/process/git_collaboration_spec.md`](./git_collaboration_spec.md) 的“6.1 缺陷修复工作流（Fix vs Hotfix）”。

`hotfix/* -> main` 的 PR 使用 `Fixes #<id>` 关闭对应缺陷 Issue。

```bash
# 同步官方主仓库最新的修复代码
git checkout main
git pull upstream main

# 打上补丁标签并推送到官方主仓库
git tag -a v0.2.1 -m "Hotfix v0.2.1: 修复消息队列在中断嵌套下的死锁问题"
git push upstream v0.2.1

```

## 3. 固件版本自动注入要求

为确保运行中的固件版本与代码仓库绝对对应，**严禁在代码中手写硬编码版本宏**。版本号必须由 XMake 构建系统在编译时通过 Git 状态自动提取并注入。

1. **标准格式：** `<tag-or-v0.0.0>+g<sha>[-dirty]`（利用 `git describe --tags --always --dirty` 自动生成）。
   - 示例：`v0.2.0+g7a8b9c0`（干净工作区）或 `v0.2.0+g7a8b9c0-dirty`（有未提交修改）。


2. **异常回退：** 若编译环境缺失 Git 仓库信息，默认回退为：`v0.0.0+gunknown-nogit`。
3. **日志输出：** 应用层启动时，必须在初始化 Log 中优先打印 `FRAMEWORK_VERSION` 宏的值，作为排查问题的第一依据。


## 4. GitHub Release 归档与冻结流程

标签推送到官方主仓库 (`upstream`) 后，须经由 GitHub Releases 完成正式归档。此页面为下游业务获取产物的唯一受控入口。因仓库已启用 Release Immutability，发布动作不可逆，须严格执行以下单向流程：

1. **创建发布草稿 (Draft Release)：** 基于目标 Tag 新建 Release，并在整个准备期间保持为 **Draft (草稿)** 状态。利用 "Generate release notes" 自动抓取 PR 历史，经人工校对后，严格按 `✨ Features`、`🐛 Bug Fixes`、`⚠️ Breaking Changes` 三级结构化梳理更新日志。
2. **挂载构建产物 (Attach Assets)：** 必须在 Draft 状态下，完整上传与该版本绝对对应的编译产物（如 `.bin`, `.hex`, `.elf`, `.a` 及说明文档）。
3. **终态发布 (Publish & Lock)：** 复核更新日志与所有附件产物。确认无误后，点击 **Publish release**。**注意：发布即冻结。** 系统将永久锁定该版本的全部资产，禁止任何后续的修改、覆盖或增删。若发现遗漏或错误，唯一合规路径为按序发起新的 Patch 修订版本流程。
4. **追溯性验证 (Verification)：** 发版完成后，抽检该 Release 页面由系统生成的源码压缩包，验证其解压代码树的 Commit SHA 与原始 Tag 指针保持绝对一致。


## 5. 权限与安全红线

为了防止版本混乱，GitHub 仓库启用以下保护规则：

1. **Tag 写入权限：** 仅限仓库管理员具有在 `main` 分支上打 `v*` 格式标签的权限。
2. **Tag 保护规则：** 所有匹配 `v*` 的标签一旦推送到远程，系统将自动锁定。**严禁任何人强制移动 (Force push) 或删除已发布的标签**。若发现已发布版本存在严重问题，必须废弃该版本并发布新的 Patch 版本（例如：发现 `v0.2.0` 有致命错误，应修复后发布 `v0.2.1`，绝不允许删除 `v0.2.0` 重新打标）。
