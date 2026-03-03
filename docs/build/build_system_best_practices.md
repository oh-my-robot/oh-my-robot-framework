# OM XMake 最佳工程实践

## 1. 适用范围
本手册面向 OM 构建体系维护者与高级开发者，目标是给出可复用的工程实践范式，涵盖语法约束、程序设计、组织结构与可维护性原则。内容以当前仓库实现为基础，可直接落地。

## 2. 核心原则
- **单一职责**：模块只做一件事，规则只注入一种能力，任务只完成一个明确动作。
- **开闭原则**：新增板级/工具链/任务时以新增数据或扩展函数为主，避免改动核心流程。
- **数据驱动**：板级/芯片/厂商信息必须数据化，工具链只负责映射，不在数据层硬编码参数。
- **确定性**：相同输入应得到相同输出；避免根据环境变量或未持久化状态导致不稳定行为。
- **最小状态**：状态只在必要时持久化（如配置上下文），不维护重复事实。
- **边界清晰**：构建系统层（oh-my-robot/build）与模块构建脚本（oh-my-robot/lib、oh-my-robot/platform）严格分层。

## 3. 语法与脚本域边界
### 3.1 描述域与脚本域
- **描述域**：仅用于声明 `target/rule/option/task` 等结构。
- **脚本域**：用于执行 `import/raise/try/task.run/os.runv` 等逻辑接口。

**规范：**
- 描述域不得调用 `import/dofile/raise/error/print/task.run` 等脚本接口。
- 脚本域需通过 `on_load/on_config/on_build/on_run` 等回调进入。

**推荐结构：**
```lua
rule("oh_my_robot.context")
    on_config(function(target)
        local oh_my_robot = import("oh-my-robot")
        local context = oh_my_robot.get_context()
        -- 关键流程说明：根据上下文注入架构/工具链参数
        target:add("cxflags", context.arch_flags)
    end)
rule_end()
```

### 3.2 回调职责划分
- `on_load`：解析输入、计算路径、准备数据。
- `on_config`：配置期校验、持久化、参数注入。
- `on_build`：构建期执行工具链或生成器。
- `on_clean`：清理生成物。

## 4. 配置与上下文一致性
### 4.1 单一事实来源
- CLI、配置缓存、preset、默认值应有明确优先级。
- 只允许一个“最终配置”进入上下文，避免出现“配置 vs 上下文”不一致。

**建议优先级（默认）：**
- CLI > config > preset > default

### 4.2 上下文同步原则
- 在 `on_config` 或配置回调中完成上下文持久化。
- 一旦 CLI 指定 board/os/toolchain，需强制写回配置缓存，确保后续任务一致。

## 5. 组织结构最佳实践
### 5.1 分层结构
- [`oh-my-robot/build/`](../../build/)：配置、规则、工具链、任务与共享模块。
- [`oh-my-robot/platform/`](../../platform/)：BSP 与 OS/Sync 的模块级构建脚本。
- [`oh-my-robot/lib/`](../../lib/)：运行时库与算法模块。

### 5.2 上移准则
仅在以下条件满足时上移到 [`oh-my-robot/build`](../../build)：
- 多模块复用
- 与板级无关
- 不依赖运行时代码

其余逻辑保持在模块内聚位置。

## 6. 数据驱动建模
### 6.1 BSP 三层数据
- `vendor/chip/board` 三层数据，按优先级合并。
- `chip.arch_traits` 仅描述 CPU/FPU/float-abi 等架构数据。

### 6.2 工具链映射
- 工具链根据 `arch_traits` 生成 `arch_flags`。
- 工具链决定哪些参数可注入编译/链接阶段。
- 不在 board/chip 层硬编码编译参数。

## 7. 规则与目标设计
### 7.1 规则最小化
- 每个规则只注入一种能力（上下文、资源、镜像转换）。
- 不在规则中进行“全量构建”或复杂流程。

### 7.2 目标职责
- 目标负责组织文件与依赖。
- 目标不应承担工具链逻辑或板级解析。

**示例：**
```lua
target("tar_os")
    set_kind("static")
    add_rules("oh_my_robot.context")
    add_files("*.c")
target_end()
```

## 8. 工具链设计
### 8.1 内置与自定义
- 内置工具链保持 `kind=builtin`，不私自修改名称或 kind。
- 自定义工具链通过 [`oh-my-robot/build/toolchains/data.lua`](../../build/toolchains/data.lua) 扩展。

### 8.2 参数注入策略
- 编译参数：由工具链映射 `arch_traits` 生成。
- 链接参数：根据 `linker_accepts_arch_flags` 决定是否注入。
- 镜像转换：单独在 `image` 中定义，不与编译逻辑耦合。

## 9. 任务设计（Tasks）
### 9.1 任务职责
- `xmake flash` 等任务只执行一件事：烧录或转换。
- 任务应读取已持久化配置，不自行猜测。

### 9.2 描述域 vs 脚本域
- 描述域仅声明 `task`。
- `on_run` 中执行 `import/os.runv/raise` 等逻辑。

### 9.3 输出与验证
- 任务需输出实际使用的工具链、固件路径、外部工具路径。
- 若参数不完整或路径不存在，必须立即报错。

## 10. 构建产物与命名
- 建议显式输出 `.elf`，避免工具链识别歧义。
- 镜像转换统一在规则中处理（HEX/BIN）。
- 产物目录固定为 `build/<plat>/<arch>/<mode>`。

## 11. 可维护性与可诊断性
- 统一日志前缀（如 `[oh-my-robot]`）。
- 使用 `raise` 终止错误，避免 silent failure。
- 关键流程必须有简洁注释（尤其是 `on_load/on_config`）。

## 12. 文档与变更同步
- 构建流程变更需同步更新：
  - [`oh-my-robot/docs/build/reference_manual.md`](reference_manual.md)
  - [`oh-my-robot/docs/build/maintenance_manual.md`](maintenance_manual.md)
- 新增构建文档需在两份手册中建立索引或链接。
- 重要变更需记录到维护手册“变更记录”。

## 13. 常见反模式
- 在描述域调用 `import/raise/task.run`。
- 在板级数据中硬编码工具链参数。
- 未持久化配置即执行任务。
- 多处维护同一事实（例如工具链路径在多处重复）。
- 用临时状态解决长期问题（导致不可预测的构建行为）。

## 14. 最小可行实践清单
- 配置优先级清晰（CLI > config > preset > default）。
- 上下文持久化在配置期完成。
- 工具链与 BSP 数据分离。
- 规则职责单一、目标职责单一。
- 任务执行前做参数完整性校验。


