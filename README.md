# Oh My Robot
A universal embedded development framework specifically designed for robots

## Documentation
- Quick Start: [`document/QuickStart.md`](document/QuickStart.md)

---

## Git 协作与研发工作流规范

本规范旨在确保框架代码的绝对稳定与可追溯性。所有团队成员在参与开发前，请务必熟练掌握以下核心工作流。

### 一、 分支架构与职责

我们的代码仓库采用“双主干 + 任务分支”的隔离模型，严格控制代码流向。

* **`main` (稳定主干)**：代表随时可烧录、可发布的稳定基线。
* **权限**：严禁直接 Push。只能通过 `integration` 分支或 `hotfix` 分支发起 PR 合并。
* **合并方式**：**Create a merge commit**（保留版本汇合节点）。


* **`integration` (集成联调区)**：日常开发的汇聚地，用于各功能模块合并后的综合测试。
* **权限**：严禁直接 Push。只能接收来自 `feature/*` 分支的 PR。
* **合并方式**：仅限 **Squash and merge**（将开发过程的琐碎提交压缩为一个整洁的功能节点）。


* **`feature/*` (功能任务分支)**：开发者个人的日常工作台。
* **命名规范**：`feat/姓名缩写/任务描述`（例：`feat/zs/mpsc-lockfree-queue`）。


* **`hotfix/*` (紧急修复分支)**：用于修复线上致命 Bug（如引发系统死机的严重缺陷）。
* **流转**：基于 `main` 分支拉取，修复后双向合并至 `main` 和 `integration`。



### 二、 任务驱动开发原则 (纵向切片)

严禁按照“系统层级”（如单独开一个 OSAL 分支或 Driver 分支）来分配任务。

* **唯一原则**：分支必须跟着**“具体任务目标”**走。
* **标准**：一个 `feature` 分支的代码拼在一起，必须是一个**能使用 XMake 跑通编译、可通过测试的独立功能**。
* *反例*：张三负责写 I2C 驱动，李四负责写依赖该 I2C 的温度传感器代码（互相阻塞）。
* *正例*：张三负责“适配并跑通板载温度传感器”，其分支 `feat/zs/temp-sensor-bringup` 内包含该传感器所需的 I2C 配置、Driver 及上层接口。



### 三、 标准开发工作流 (SOP)

#### 第 1 步：认领任务 (Issue)

1. 在 GitHub 的 Issues 面板中查阅看板。
2. 将分配给自己的 Issue 卡片拖拽至 `In Progress`，明确任务的**验收标准**。

#### 第 2 步：拉取分支并开发

```bash
# 务必基于最新的集成区拉取新分支
git checkout integration
git pull origin integration
git checkout -b feat/your_name/issue_name
```

使用 XMake 进行日常编译与本地手动测试。

#### 第 3 步：提交规范 (Commit)

提交信息需清晰表明意图，建议采用分类前缀：

* `feat:` 新增功能（如外设驱动、数据结构）
* `fix:` 修复缺陷（如时序问题、死锁预防）
* `docs:` 文档更新
* `chore:` 构建脚本 (xmake.lua) 或基建配置调整

#### 第 4 步：发起代码审查 (Pull Request)

1. 将本地分支推送到云端：`git push origin feat/your_name/issue_name`
2. 在 GitHub 网页端向 `integration` 分支发起 PR。
3. **关键动作**：
* 仔细填写 PR 模板（勾选涉及的架构维度，附上验证截图或串口日志）。
* 在 PR 描述中写入通关密码：**`Fixes #对应Issue编号`**（如 `Fixes #12`），实现合并后自动关单。


4. **审查纪律**：如果 Reviewer 提出了修改意见并点击了 Approve，开发者在本地进行微调并重新 Push 后，原有的 Approve 会被**自动撤销**，必须等待 Reviewer 再次确认后方可合并。

### 四、 代码风格与命名门禁（XMake 官方接口）

当前仓库代码风格与静态检查暂采用本地执行，使用 XMake 官方命令：

```bash
# 1) 风格检查（不改文件）
xmake format --dry-run --error

# 2) 静态检查（clang-tidy，自动发现目录配置）
xmake check clang.tidy

# 3) 风格自动修复（仅格式，不含命名改写）
xmake format

# 4) clang-tidy 自动修复（官方内置）
xmake check clang.tidy --fix
xmake check clang.tidy --fix_errors
xmake check clang.tidy --fix_notes
```

> 需要缩小范围时，可用 `--files=...` 或直接指定 targets（例如 `xmake check clang.tidy tar_awcore tar_awalgo`）。

官方参考（内置插件 clang-tidy）：
<https://xmake.io/zh/guide/extensions/builtin-plugins.html#%E6%A3%80%E6%B5%8B%E5%B7%A5%E7%A8%8B%E4%BB%A3%E7%A0%81-clang-tidy>

职责边界：

* 根 `.clang-tidy`：负责默认规则与框架层头文件诊断范围（`HeaderFilterRegex`）。
* `platform/.clang-tidy`：通过 `Checks: '-*,clang-diagnostic-*,llvm-twine-local'` 与 `InheritParentConfig: false` 使平台目录仅保留编译诊断（`llvm-twine-local` 仅用于规避 `no checks enabled`）。
* 根 `.clang-format`：负责默认格式规则。
* `platform/.clang-format`：通过 `DisableFormat: true` 截断平台目录格式化。
* `xmake check clang.tidy`：默认扫描当前工程 targets 的源文件；`--files=...` 仅用于本地缩小范围。

默认检查范围：

* 源文件扫描：按工程 targets 参与构建的源文件（含 `lib/**`、`platform/**`、`samples/**`）。
* 头文件诊断：`lib/**`、`samples/**`（由根 `HeaderFilterRegex` 控制）
* 平台目录：参与扫描，但仅触发编译诊断，不做命名/风格规则检查。

本地执行建议：

* 每次提交前执行：`xmake format --dry-run --error` 与 `xmake check clang.tidy`。
* 当前阶段暂不启用 CI 强制门禁，仅保留本地检查规范。

命名规范（统一约定）：

* 结构体实体类型统一使用 `_s` 后缀（示例：`CanCfg_s`、`P1010BDriver_s`）。
* `_t` 后缀仅用于句柄/指针/抽象别名（示例：`CanCfg_t`、`Device_t`、`OsalThread_t`）。
* 枚举与联合后缀分别统一为 `_e`、`_u`。
* 类型名（结构体/枚举/联合）使用 `CamelCase`，成员变量使用 `camelBack`。
* 函数名、普通变量、参数名使用 `snake_case`。
* 宏与枚举常量使用 `UPPER_CASE`。

### 五、 版本发布与防篡改 (Tags)

### 六、 版本管理与发布规范

确保现场设备中烧录的固件与代码历史**绝对对应**是排错的生命线。本框架严格实行**语义化版本控制** 结合 **Git Tag** 的版本追踪机制。

### 1. 版本号命名规则 (vX.Y.Z)

版本号统一采用 `v<主版本号>.<次版本号>.<修订号>[-阶段后缀]` 的格式：

* **主版本号 (Major) `X`**：发生不向下兼容的重大架构重构（如 OSAL 接口传参逻辑全盘推翻）。
* ⚠️ **特别说明**：**当前开发阶段固定为 `0`**（即 `v0.x.x`），这代表框架处于初步构建与孵化期，底层 API 随时可能发生变动。
* 
* **次版本号 (Minor) `Y`**：向下兼容的新功能合入（如新增 P1010B 电机驱动适配、跑通无锁队列模块）。
* **修订号 (Patch) `Z`**：向下兼容的 Bug 修复（如修复由于中断抢占导致的死锁、微调硬件时序）。

### 2. 打标签 (Tag) 触发时机与权限

* **权限控制**：仅限管理员 (Admin) 在 `main` 分支上打标签。所有 `v*` 标签受 GitHub 规则集强制保护，**历史标签绝对禁止被强行移动或删除**。
* **触发时机**：
* **里程碑节点**：当 `integration` 分支完成了一个核心模块的集成（例如：多线程互斥锁可用，或首个完整的电机 PWM 转动闭环跑通），合并入 `main` 时，打出次版本号（如 `v0.2.0`）。
* **紧急救火**：通过 `hotfix` 分支紧急修复了测试板高频死机问题并合并入 `main` 后，打出修订版本号（如 `v0.2.1`）。


### 3. 固件版本号注入机制 (自动化追踪)

为杜绝“玄学排错”，严禁在 C 代码中手动定义版本号宏。本框架已通过 **XMake** 构建系统实现了版本号的自动化注入：

* **编译时行为**：XMake 脚本会在每次编译时，自动抓取当前代码对应的最新 Git Tag 与 Commit Hash，并生成唯一版本宏 `FRAMEWORK_VERSION`。
* **输出格式**：`<tag-or-v0.0.0>+g<sha>[-dirty]`。当仓库无 `v*` 标签时回退到 `v0.0.0+g<sha>`；当工作区存在未提交改动时追加 `-dirty`。
* **无 Git 回退**：若构建环境缺失 `.git` 元信息，则回退为 `v0.0.0+gunknown-nogit`。
* **开发纪律**：在应用层 `main.c` 或系统板级初始化的最前端，**必须首先通过串口打印该版本号**。
```c
// 示例：上电必须打印版本身份，方便追踪该固件对应的准确源码快照
printf("[BOOT] Core Framework Version: %s\r\n", FRAMEWORK_VERSION);
```

### 4. GitHub Release 归档流程

代码层面的 Tag 建立后，必须在 GitHub 云端进行 Release 归档，使其成为一个可追溯的“内部产品”：

1. 进入仓库的 **Releases -> Draft a new release**。
2. 选择最新推送的 Tag（如 `v0.3.0`）。
3. 点击 **Generate release notes**，自动汇总自上个版本以来合并的所有 PR（包含修复的 Issue 编号）。
4. 📦 **强制要求**：必须将基于该版本源码编译出的**标准测试固件（如 `.bin` / `.hex` / `.elf` 格式文件）**拖拽至附件中一并发布。作为该历史版本的物理备份。

* **打标签权限**：仅限管理员（Admin）操作。
* 每次 `integration` 稳定并合入 `main` 后，管理员将使用 `v*` 格式打上版本标签（如 `v1.0.0` 或 `v2.1-motor-ctrl`）。
* **规则限制**：所有 `v*` 标签受 GitHub 规则集严格保护，**禁止任何人更新、移动或删除**，确保现场烧录固件的源码具备绝对溯源能力。

---
