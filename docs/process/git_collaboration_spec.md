# Git 协作规范

## 1. 目的与基本原则

1. 保持提交历史线性、整洁，降低审查成本，提升 `git bisect`（二分查找） 可用性。
2. 以派生工作流（Forking Workflow）保障官方仓库安全。
3. 当 `feature/*` 或 `fix/*` 落后 `upstream/integration` 时，严禁 `git merge` 同步上游，必须使用 `git rebase`；`hotfix/*` 的同步基线为 `upstream/main`。

## 2. 分支架构与职责

1. `main`：稳定主干，仅允许通过 PR 合入。
2. `integration`：日常集成联调主线，接收 `feature/*`、`fix/*` 合入，并接收 `hotfix/*` 回灌。
3. `feature/*`：功能开发分支，按任务目标拆分。
4. `hotfix/*`：紧急修复分支，从 `main`（或对应发布 Tag）切出，先合入 `main`，再回灌 `integration`。
5. `fix/*`：常规缺陷修复分支，从 `integration` 切出并合入到 `integration`。

## 3. 远端仓库管理与派生工作流（Forking Workflow）

### 3.1 `upstream`（上游中央库）

1. 指向团队官方中央仓库。
2. 对普通开发者默认只读，仅用于"向下同步"。
3. 核心动作：`git fetch upstream`。

### 3.2 `origin`（个人派生库）

1. 指向开发者个人 Fork。
2. 开发者具备读写权限，仅用于"向上备份与提交流转"。
3. 核心动作：`git push origin <feature-branch>`。

### 3.3 工作区根目录 `$WS_ROOT`

> **`$WS_ROOT`** 是本文所有本地路径的锚点（例如 `E:\Oh-My-Robot-Lab` 或 `/home/user/oh-my-robot-lab`）。提供两种设置方式：**地标文件 + 发现脚本**（推荐，可验证）或**手动变量**（最简）。

#### 3.3.1 方式一：地标文件 + 发现脚本（推荐）

**第一步：创建地标文件（一次性）**

在工作区根目录放置空文件 `.oh-my-robot-workspace`，作为发现脚本的查找目标：

```bash
# 1) 创建工作区根目录
mkdir -p /home/user/oh-my-robot-lab      # Linux / macOS
# 或
mkdir E:\Oh-My-Robot-Lab                  # PowerShell

# 2) 放置地标文件（空文件即可）
touch /home/user/oh-my-robot-lab/.oh-my-robot-workspace   # Linux / macOS
New-Item "E:\Oh-My-Robot-Lab\.oh-my-robot-workspace" -ItemType File  # PowerShell
```

**第二步：克隆仓库后设置 `$WS_ROOT`**

克隆完成后，从主仓库目录运行发现脚本 `scripts/ws_root.lua`——它从 `os.curdir()` 向上查找地标文件，输出工作区根目录绝对路径：

```bash
# Bash / Zsh / WSL
cd /home/user/oh-my-robot-lab/worktrees/oh-my-robot/main
export WS_ROOT=$(xmake lua scripts/ws_root.lua)

# PowerShell
cd E:\Oh-My-Robot-Lab\worktrees\oh-my-robot\main
$env:WS_ROOT = xmake lua scripts/ws_root.lua
```

验证：执行 `echo $WS_ROOT`（或 `echo $env:WS_ROOT`），应输出工作区根目录绝对路径。

> 如需每次打开终端自动设置，将上述 `export` / `$env:` 一行加入 Shell 配置文件（`~/.bashrc`、`~/.zshrc` 或 `$PROFILE`）。

#### 3.3.2 方式二：手动变量（最简）

若不需要脚本验证，直接设置绝对路径：

```bash
# Bash / Zsh / WSL
export WS_ROOT=/home/user/oh-my-robot-lab

# PowerShell
$env:WS_ROOT = "E:\Oh-My-Robot-Lab"
```

手动设置同样有效，但更换工作区路径时需手动同步更新。

#### 3.3.3 克隆仓库

1. 先在 GitHub Web 端打开 `https://github.com/oh-my-robot/oh-my-robot-framework`，点击 `Fork`，在个人账号下创建同名派生仓库。
2. 若个人账号下已存在该 Fork，可跳过创建。初次克隆时 `scripts/ws_root.lua` 尚不可用，直接使用目标路径：

```bash
# 用你的实际路径替换以下示例
git clone https://github.com/<your-username>/oh-my-robot-framework.git \
    /home/user/oh-my-robot-lab/worktrees/oh-my-robot/main
cd /home/user/oh-my-robot-lab/worktrees/oh-my-robot/main
git remote add upstream https://github.com/oh-my-robot/oh-my-robot-framework.git
git remote -v
```

克隆完成后按第 3.3.1 节设置 `$WS_ROOT`，后续命令统一使用 `$WS_ROOT/...` 写法。

3. Fork 关系的强制校验以第 `4.1.2` 节为准。

### 3.4 本地主仓库（Main Checkout）

1. `$WS_ROOT/worktrees/oh-my-robot/main/` 是**本地唯一的主仓库 checkout**，拥有完整的 `.git` 目录，是所有 worktree 共享的 Git 对象源。
2. 主仓本身**不进行日常功能开发**——不在其上 `commit`、`checkout` 或 `merge` 开发内容。
3. 主仓仅用于以下管理操作：
   - `git fetch upstream / origin` —— 同步远端。
   - `git worktree add` —— 从主仓创建任务 worktree。
   - `git worktree remove` —— 清理已完成任务的 worktree。
   - `git worktree list` —— 查看当前所有 worktree。
   - `git worktree prune` —— 清理过期的 worktree 记录。
4. **严禁**在主仓中直接执行 `git rebase`、`git commit`、`git merge` 等开发操作。所有开发工作在通过 `git worktree add` 创建的子 worktree 中进行。
5. 既存历史修复：如果你已经从旧规范（非 worktree 方式）遗留了独立的 Git clone，推荐以下迁移步骤：
   - 执行第 3.3 节创建新的主仓。
   - 将遗留 clone 中的远端分支推送到 origin，然后删除该遗留 clone。
   - 在主仓中执行 `git fetch origin` 拉取所有分支，后续通过 `git worktree add` 重建任务 worktree。

## 4. 分支同步与变基（Rebase）标准规范

### 4.1 提交前预检（Fork 与上游落后检查）

#### 4.1.1 远端拓扑检查（必须通过）

1. `upstream` 必须指向官方仓库 `oh-my-robot/oh-my-robot-framework`。
2. `origin` 必须指向个人 Fork，且 URL 不得与 `upstream` 相同。
3. 未通过该检查前，禁止进入后续 `rebase`、`push` 与 PR 流程。

```bash
git remote -v
git remote get-url origin
git remote get-url upstream
```

若远端配置错误，先修正后再继续：

```bash
git remote rename origin legacy
git remote add origin https://github.com/<your-username>/oh-my-robot-framework.git
git remote set-url upstream https://github.com/oh-my-robot/oh-my-robot-framework.git
```

#### 4.1.2 Fork 关系检查（必须通过）

1. `origin` 对应仓库必须是 `upstream` 的 Fork 网络成员。
2. 在 GitHub 仓库页面确认存在 `forked from oh-my-robot/oh-my-robot-framework` 标识。
3. 若当前 `origin` 不是 Fork，必须先重新创建正确 Fork，再更新 `origin`。

#### 4.1.3 上游落后检查（必须通过）

1. `feature/*` 与 `fix/*` 的检查基线为 `upstream/integration`。
2. `hotfix/*` 的检查基线为 `upstream/main`。
3. 预检时先同步远端追踪信息，再检查当前分支相对基线的落后状态。

```bash
git fetch upstream --prune
git branch --show-current

# feature/* 与 fix/*
git rev-list --left-right --count upstream/integration...HEAD

# hotfix/*
git rev-list --left-right --count upstream/main...HEAD
```

`git rev-list --left-right --count` 输出格式为 `<behind> <ahead>`：

1. `behind > 0`：当前分支落后基线，必须先执行 `rebase`。
2. `behind = 0`：可继续后续流程。

#### 4.1.4 预检后的动作决策

1. 远端拓扑或 Fork 关系不通过：先修正远端与 Fork 关系，再执行后续流程。
2. 若 `behind > 0`：对对应基线执行 `git rebase <base>`。
3. 冲突处理中仅允许 `git add` + `git rebase --continue`，禁止 `git commit`。
4. 变基完成后推送必须使用 `git push origin <branch> --force-with-lease`。
5. 全流程禁止使用 `git merge upstream/*` 同步上游。

### 4.2 强制规定

1. `feature/*` 与 `fix/*` 落后 `upstream/integration` 时，必须 `rebase`；`hotfix/*` 落后 `upstream/main` 时，必须 `rebase`；统一禁止 `merge` 同步上游。
2. 冲突恢复流程中禁止执行 `git commit`。
3. 变基重写历史后，推送必须使用 `--force-with-lease`。

### 4.3 标准变基工作流

#### 4.3.1 更新上游追踪分支

```bash
git fetch upstream
```

#### 4.3.2 执行变基

```bash
git rebase upstream/integration
```

`hotfix/*` 分支执行变基时，将基线替换为 `upstream/main`。

#### 4.3.3 冲突处理

```bash
git add <冲突文件路径>
git rebase --continue
```

重复执行"解决冲突 -> `git add` -> `git rebase --continue`"直到变基完成。

#### 4.3.4 推送变更到个人派生库

```bash
git push origin <当前特性分支名> --force-with-lease
```

## 5. 标准任务流转 SOP（从 Issue 到 PR）

### 阶段一：云端任务发布与认领（Web 端）

1. 所有开发任务必须始于官方仓库 `upstream` 的 Issue，严禁"无 Issue 开发"。
2. Issue 标题规范以 `.github/ISSUE_TEMPLATE/feature_request.yml` 的 `title` 字段为唯一事实来源，本文不再重复定义格式。
3. 重大架构改动可先进行讨论，但进入开发前必须补开 Issue。
4. 任务开始前必须设置 `Assignees` 为自己，并补齐对应 `Label`。

### 阶段二：分支命名与 worktree 创建工作区

1. 严禁直接在本地已有分支上开工，必须基于 `upstream/integration` 创建专属分支。
2. 分支命名格式：`feature/<issue编号>-<简短英文描述>`，必须包含 Issue 编号。
3. 缺陷修复任务的 `fix/*` 与 `hotfix/*` 命名和流转规则，见第 6.1 节。

```bash
# 从主仓库目录执行以下命令
cd $WS_ROOT/worktrees/oh-my-robot/main

# 同步上游
git fetch upstream

# 创建任务 worktree 与专属分支
# 注意：若分支名曾创建过（例如中断重试），需先 git branch -D <分支名> 再执行
git worktree add $WS_ROOT/worktrees/oh-my-robot/<task> \
    -b feature/<issue编号>-<简短描述> upstream/integration

# 进入任务 worktree 开始开发
cd $WS_ROOT/worktrees/oh-my-robot/<task>
```

#### 阶段二补充：worktree 与项目根推荐组织

1. 本规范采用 Git Worktree 机制，将**源码编辑**与**构建配置**物理隔离，互不干扰。
2. 推荐工作分层：
   - 源码编辑、提交、rebase、push：在 `$WS_ROOT/worktrees/oh-my-robot/<task>/` 中完成。
   - 配置、构建、调试：在 `$WS_ROOT/workspaces/oh-my-robot/<task>/` 中完成。
3. 源码 worktree 中**不得**放置 `om_preset.lua` 或构建产物（`.xmake/`、`build/`、`compile_commands.json` 等）。
4. `om_preset.lua` 是项目级构建配置，**仅**存在于构建 workspace 中，与顶层 `xmake.lua` 同级；不要在源码 worktree 中维护它，更不要把它作为用户全局配置。
5. 若构建 workspace 尚未准备好，可在源码 worktree 中执行以下命令生成项目壳（`om_preset.lua` 不会在 worktree 中生成，而是写入指定的 `--output` 目录）：

```bash
cd $WS_ROOT/worktrees/oh-my-robot/<task>
xmake init_workspace --output=$WS_ROOT/workspaces/oh-my-robot/<task>
```

随后在生成的 workspace 目录中维护 `om_preset.lua`、执行 `xmake f` / `xmake` / 调试器配置；源码提交仍在 worktree 中完成。`xmake init_workspace` 的具体参数与用法见 `docs/build/build_tasks_manual.md`。

6. 如果你的项目包含多个仓库（例如 robot 主仓 + 框架子仓），框架源码的修改应始终在 `$WS_ROOT/worktrees/oh-my-robot/<task>/` 中进行，不要直接修改集成目录中的框架副本。

推荐目录关系示意：

```text
$WS_ROOT/
├── worktrees/
│   └── oh-my-robot/
│       ├── main/                      # 主仓库 checkout（仅管理用，不开发）
│       └── <task>/                    # 任务 worktree
└── workspaces/
    └── oh-my-robot/
        └── <task>/                    # 独立构建项目根（含 om_preset.lua）
```

### 阶段三：开发与原子提交（worktree 分支）

1. 在任务 worktree 中开发，遵循 `.clang-format` 与 `.clang-tidy` 规范。
2. 过程推演文档按文档治理规范放入：
   `docs/internal/active/issue_015_osal_mutex/`
3. 每条 Commit 必须包含 Issue 编号锚点 `(#15)`，以保证 Git 历史可追溯。

```bash
git commit -m "feat(osal): 新增互斥锁抽象接口与基础数据结构 (#15)"
git commit -m "docs(osal): 补充互斥锁使用场景推演草稿 (#15)"
```

### 阶段四：变基与推送到个人派生库（CLI）

1. 进入本阶段前，必须先通过第 `4.1` 节提交前预检（远端拓扑、Fork 关系、上游落后状态）。
2. 同步上游时严禁 `git merge upstream/integration`，必须使用 `git rebase upstream/integration`。
3. 变基冲突处理中严禁执行 `git commit`，仅允许 `git add` + `git rebase --continue`。

```bash
# 0) 执行第 4.1 节预检（示例为 feature/fix）
git fetch upstream --prune
git rev-list --left-right --count upstream/integration...HEAD

# 1) 变基防冲突，保持线性历史
git rebase upstream/integration

# 2) 推送到个人派生库（origin）
git push origin feature/15-osal-mutex --force-with-lease
```

`hotfix/*` 分支将基线替换为 `upstream/main`，其余规则不变。

### 阶段五：跨库 PR 与自动闭环（Web 端）

1. PR 来源：`origin/feature/15-osal-mutex`。
2. PR 目标：`upstream/integration`。
3. 若目标为 `integration`，PR 描述必须使用关联指令：`Refs #15`（不关闭 Issue）。
4. 仅在合并到 `main` 的 PR 中使用关闭指令：`Fixes #15`，由平台自动关闭对应 Issue。

### SOP 执行检查清单

1. 是否先在 `upstream` 创建并认领了 Issue。
2. 是否已执行第 `4.1` 节预检并确认 `origin` 是 `upstream` 的 Fork。
3. 是否已执行 `git rev-list --left-right --count <base>...HEAD` 并按规则完成落后处理。
4. 分支名是否为 `feature/<id>-<slug>` 且包含 Issue 编号。
5. 是否所有提交都带 `(#<id>)` 锚点。
6. 提交 PR 前是否已 `rebase upstream/integration`。
7. PR 描述是否按目标分支使用正确关键字：`Refs #<id>`（到 `integration`）/`Fixes #<id>`（到 `main`）。

## 6. 合并策略

1. `feature/* -> integration`：建议 `Squash and merge`，压缩开发噪音提交。
2. `fix/* -> integration`：缺陷修复默认只合入 `integration`，随计划版本发布。
3. `integration -> main`：建议 `Create a merge commit`，保留发布汇合节点。
4. `hotfix/* -> main` 后必须回灌 `integration`，保持修复提交可追溯。

### 6.1 缺陷修复工作流（Fix vs Hotfix）

1. 场景界定：
   - `Fix`：日常开发/测试阶段发现的问题，不影响已发布稳定版本可用性。
   - `Hotfix`：已发布到 `main`（或已交付标签版本）中的阻断性缺陷。
2. 分支切出与命名：
   - `Fix`：从 `integration` 切出，命名 `fix/<issue编号>-<简短描述>`。
   - `Hotfix`：从 `main`（或对应发布 Tag）切出，命名 `hotfix/<issue编号>-<简短描述>`。
3. 合并终点：
   - `Fix`：仅合入 `integration`。
   - `Hotfix`：必须先合入 `main`，随后回灌 `integration`。
4. 开发纪律：
   - `Fix` 允许必要的小范围优化，但不得跨主题扩改。
   - `Hotfix` 仅允许最小化修复，严禁夹带新功能或无关重构。
5. 发布节奏：
   - `Fix`：合入 `integration` 后，随下一个计划版本统一发布。
   - `Hotfix`：合入 `main` 后按 Patch 紧急发布，并回灌 `integration`（详见 `docs/process/git_version_release_spec.md`）。

## 7. 强制推送与纪律红线

1. 仅允许在个人开发分支（`feature/*`、`fix/*`、`hotfix/*`）且未合并前执行强推。
2. 严禁对 `integration` 与 `main` 执行任何形式的强推。
3. 默认禁止向 `upstream` 推送；仅发布管理员按 `docs/process/git_version_release_spec.md` 推送 Release Tag 时，允许执行 `git push upstream <tag>`。
4. 严禁向 `upstream` 推送任何分支（包括 `main`、`integration`、`feature/*`、`fix/*`、`hotfix/*`）。
5. 本地 `integration` 和 `main` 视为只读镜像，禁止在其上 `commit` 或 `merge` 开发内容。

## 8. 任务驱动原则

1. 分支必须绑定具体 Issue，严禁"无 Issue 开发"。
2. 禁止按系统层级拆分"半成品分支"互相阻塞。
3. 单个 `feature/*` 应形成可编译、可验证的纵向切片。
4. 分支类型前缀与职责以第 2 章为唯一事实来源；Issue 编号与 slug 的拼接规则按第 5 章与第 6.1 节执行。

## 9. 原子提交规范（Atomic Commit）

### 9.1 核心原则

1. 一件事，一个提交包。
2. 每个提交必须是独立、完整、职责单一的逻辑单元。
3. 历史应可用于 `git bisect` 与精确回滚。

### 9.2 三大铁律

1. 职责绝对单一（Single Responsibility）。
2. 随时可编译（Always Buildable）。
3. 杜绝夹带私货（No Sneaky Changes）。

### 9.3 具体约束

1. 严禁"大杂烩"提交：修复 Bug、新增功能、重构、格式化必须物理隔离。
2. 任意历史提交切入后，工程都应可编译通过，不得制造"断层提交"。
3. 若修改跨层公共接口（例如头文件签名），所有调用方必须在同一提交内同步闭环。
4. 业务逻辑提交不得夹带无关注释、空行或排版调整。
5. 纯格式改动必须独立为 `style` 提交。

### 9.4 示例

1. 不合规：`fix: 修复电机 PID，顺便加 SPI 驱动并格式化 OSAL`。
2. 合规拆分：
   1. `style(osal): 统一互斥锁模块代码缩进`
   2. `fix(algo): 修复电机速度环 PID 积分饱和问题`
   3. `feat(bsp): 新增硬件 SPI 底层驱动适配`

## 10. Commit Message 命名规范

格式固定为：`类型(作用域): 简短描述`

类型约定：

1. `feat`：新增核心机制、驱动或算法。
2. `fix`：修复死机、逻辑错误等 Bug。
3. `docs`：仅文档或 Doxygen 注释变更。
4. `style`：仅格式调整，不改变运行逻辑。
5. `refactor`：重构实现，不改变外部行为。
6. `chore`：构建系统、CI、脚本与基建配置。
7. 任务提交必须追加 Issue 锚点：`(#<issue编号>)`。

## 11. PR 纪律

1. PR 描述必须包含范围、风险、验证结果。
2. 日常开发 PR 目标分支为 `upstream/integration`；发布与热修复 PR 目标分支为 `upstream/main`。
3. 关键字策略统一为：到 `integration` 使用 `Refs #<id>`；到 `main` 使用 `Fixes #<id>`。
4. 全仓库不再使用 `Resolves` 关键字。
5. 评审后若有新增提交，需重新确认通过后再合并。

## 12. 本地质量门禁（提交前）

```bash
xmake format --dry-run --error
xmake check clang.tidy
```

可选自动修复：

```bash
xmake format
xmake check clang.tidy --fix
xmake check clang.tidy --fix_errors
xmake check clang.tidy --fix_notes
```

## 13. worktree 维护

任务开发完成后，须及时清理废弃 worktree，避免遗留僵尸目录。

以下命令均在**主仓库**（`$WS_ROOT/worktrees/oh-my-robot/main/`）中执行：

```bash
# 进入主仓库
cd $WS_ROOT/worktrees/oh-my-robot/main

# 1) 查看当前所有 worktree
git worktree list

# 2) 移除已完成的 worktree（目录和 Git 记录一并清除）
git worktree remove $WS_ROOT/worktrees/oh-my-robot/<task>

# 3) 若对应分支已合并，可删除分支
git branch -d feature/<issue编号>-<简短描述>
# 若分支尚未合并（例如放弃开发），用 -D 强制删除
git branch -D feature/<issue编号>-<简短描述>

# 4) 清理 .git/worktrees 中的过期记录（手动删除 worktree 目录后执行）
git worktree prune
```

注意事项：

- `git worktree remove` 仅能移除**干净**（无未提交修改）的 worktree；若有未提交内容，需先提交或丢弃。
- 切勿手动 `rm -rf` 删除 worktree 目录再 `prune` —— 这会在 `.git/worktrees/<id>/` 中遗留悬挂的 worktree 内部引用文件（`HEAD`、`index` 等）。正确做法是先 `git worktree remove`，它也会同时清理对应的 `.git/worktrees/<id>/` 目录。
- 定期执行 `git worktree list` 检查是否存在遗忘的 worktree。
