# Git 协作规范

## 1. 分支架构与职责

1. `main`：稳定主干，仅允许通过 PR 合入。
2. `integration`：日常集成联调主线，仅接收 `feature/*` 合入。
3. `feature/*`：功能开发分支，按任务目标拆分。
4. `hotfix/*`：紧急修复分支，从 `main` 切出并双向回灌 `integration`。

## 2. 合并策略

1. `feature/* -> integration`：建议 `Squash and merge`，压缩开发噪音提交。
2. `integration -> main`：建议 `Create a merge commit`，保留发布汇合节点。
3. `hotfix/* -> main/integration`：保持修复提交可追溯。

## 3. 任务驱动原则

1. 分支必须绑定具体 Issue 或明确任务目标。
2. 禁止按系统层级拆分“半成品分支”互相阻塞。
3. 单个 `feature/*` 应形成可编译、可验证的纵向切片。

## 4. 标准开发流程（SOP）

1. 认领任务并确认验收标准。
2. 基于最新 `integration` 拉取 `feature/*`。
3. 本地完成开发、构建与验证。
4. 按提交规范提交并推送。
5. 发起到 `integration` 的 PR，并填写完整描述与验证证据。

## 5. 原子提交规范（Atomic Commit）

### 5.1 核心原则

1. 一件事，一个提交包。
2. 每个提交必须是独立、完整、职责单一的逻辑单元。
3. 历史应可用于 `git bisect` 与精确回滚。

### 5.2 三大铁律

1. 职责绝对单一（Single Responsibility）
2. 随时可编译（Always Buildable）
3. 杜绝夹带私货（No Sneaky Changes）

### 5.3 具体约束

1. 严禁“大杂烩”提交：修复 Bug、新增功能、重构、格式化必须物理隔离。
2. 任意历史提交切入后，工程都应可编译通过，不得制造“断层提交”。
3. 若修改了跨层公共接口（例如头文件签名），所有调用方必须在同一提交内同步闭环。
4. 业务逻辑提交不得夹带无关注释、空行或排版调整。
5. 纯格式改动必须独立为 `style` 提交。

### 5.4 示例

1. 不合规：`fix: 修复电机 PID，顺便加 SPI 驱动并格式化 OSAL`
2. 合规拆分：
   1. `style(osal): 统一互斥锁模块代码缩进`
   2. `fix(algo): 修复电机速度环 PID 积分饱和问题`
   3. `feat(bsp): 新增硬件 SPI 底层驱动适配`

## 6. Commit Message 命名规范

格式固定为：

`类型(作用域): 简短描述`

类型约定：

1. `feat`：新增核心机制、驱动或算法
2. `fix`：修复死机、逻辑错误等 Bug
3. `docs`：仅文档或 Doxygen 注释变更
4. `style`：仅格式调整，不改变运行逻辑
5. `refactor`：重构实现，不改变外部行为
6. `chore`：构建系统、CI、脚本与基建配置

## 7. PR 纪律

1. PR 描述必须包含范围、风险、验证结果。
2. 需要关联 Issue 时，使用 `Fixes #<id>`。
3. 评审后若有新增提交，需重新确认通过后再合并。

## 8. 本地质量门禁（提交前）

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
