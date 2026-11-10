# OM XMake 维护者手册

## 1. 目标与维护边界
本手册面向维护者，目标是说明 OM 构建体系的模块边界、依赖关系、数据流与扩展方式。文档内容完全基于当前仓库实现。

相关文档索引：
- [`oh-my-robot/docs/build/build_tasks_manual.md`](build_tasks_manual.md)：内置任务与参数说明。
- [`oh-my-robot/docs/build/build_system_best_practices.md`](build_system_best_practices.md)：构建系统最佳工程实践。
- [`oh-my-robot/docs/build/upstream_xmake_armlink_sourcefile_nil.md`](upstream_xmake_armlink_sourcefile_nil.md)：XMake `armlink.lua` 缺陷的上游提交材料（复现、根因、建议 patch）。
- [`oh-my-robot/docs/quick_start.md`](../quick_start.md)：新手快速开始与项目根首次构建说明。

## 2. 构建系统设计方法论
- **分层原则**：构建系统只在 [`oh-my-robot/xmake/`](../../xmake/) 集中管理“选项、规则、工具链、共享模块”，模块级构建脚本就地维护，避免全局脚本侵入模块。
- **单一入口**：顶层 `xmake.lua` 仅负责入口转发与目标聚合，不直接承载工具链或板级逻辑。
- **数据/行为分离**：板级/芯片/厂商数据放在 [`oh-my-robot/platform/bsp/data`](../../platform/bsp/data)，构建规则与工具链行为放在 [`oh-my-robot/xmake`](../../xmake)。
- **依赖方向**：模块构建脚本可以调用构建层 API（如 `oh-my-robot`、`bsp`），构建层不得硬编码模块路径或板级细节。
- **口径边界**：构建期依赖用于约束“可见与可链接”关系，不等同于运行时调用链；运行时依赖需结合接口调用/注册路径单独审计。
- **上移准则**：当逻辑被多个模块复用、与具体板级无关、且不依赖运行时代码时，才上移到 [`oh-my-robot/xmake`](../../xmake)；否则保持模块内聚。
- **脚本域约束**：仅在脚本域使用 `import/try/raise/task.run` 等 API，描述域只做目标/规则/选项声明，避免配置期卡死或无效调用。

## 3. 构建链路全景（从配置到产物）
1) 配置阶段：`xmake f` 读取 `board/os/toolchain/sync_accel/semihosting` 等选项。  
2) 选项回调：[`oh-my-robot/xmake/config/options.lua`](../../xmake/config/options.lua) 在 `after_check` 中写入上下文。  
3) 上下文持久化：`oh-my-robot/xmake/modules/oh_my_robotlua` 将关键字段保存到配置缓存。  
4) 预设解析：只读取项目根 `om_preset.lua`。  
5) 编译数据库：通过 `plugin.compile_commands.autoupdate` 在构建后生成 `compile_commands.json` 至项目根目录。  
6) 目标加载：`oh_my_robot.context` 注入架构与工具链参数。  
7) 板级资源：`oh_my_robot.board_assets` 注入启动文件与链接脚本。  
8) 可选镜像：`oh_my_robot.image_convert` 生成 `hex/bin` 并在清理时移除。
9) 编译与链接：`tar_board`、`tar_os`、`tar_sync` 等目标完成编译，最终由 `robot_project` 输出可执行镜像。  

## 4. 构建入口与目录职责
- `xmake.lua`：顶层入口，定义 `robot_project` 并挂载规则；设置官方 LTO 策略默认关闭，由 `oh_my_robot.context` 按工具链动态启用（debug/release 同口径）。
- [`oh-my-robot/xmake.lua`](../../xmake.lua)：OM 构建入口转发（实际入口在 [`oh-my-robot/xmake/xmake.lua`](../../xmake/xmake.lua)）。
- [`oh-my-robot/xmake/xmake.lua`](../../xmake/xmake.lua)：OM 构建入口，加载配置/规则/子模块，并定义聚合目标 `tar_oh_my_robot`。

核心目录职责：
- [`oh-my-robot/xmake/config/`](../../xmake/config/)：构建选项与默认值。
- [`oh-my-robot/xmake/rules/`](../../xmake/rules/)：规则定义（上下文注入、板级资源、镜像转换）。
- [`oh-my-robot/xmake/toolchains/`](../../xmake/toolchains/)：工具链数据与脚本逻辑。
- [`oh-my-robot/xmake/tasks/`](../../xmake/tasks/)：自定义任务（如工作区生成）。
- [`oh-my-robot/platform/bsp/`](../../platform/bsp/)：BSP 数据加载、合并、板级构建脚本。
- [`oh-my-robot/platform/osal/`](../../platform/osal/)：OS 抽象层与 OS 端口。
- [`oh-my-robot/platform/sync/`](../../platform/sync/)：同步原语与可选加速后端。
自定义任务：
- `xmake init_workspace`：为当前 `oh-my-robot` checkout 生成轻量项目壳，并创建本地 `oh-my-robot` 目录链接/junction，解决单独 worktree 缺少顶层 `xmake.lua` 的问题。
- 烧录/调试不在 XMake 内承担：构建只产出 `.elf` / `.hex` / `.bin` 与 `compile_commands.json`；下载与调试交给外部工具（J-Link Commander、OpenOCD、pyOCD、Cortex-Debug 等）。
- 内存报告与链接契约校验通过 `tools/omcheck/` 独立脚本执行；XMake 只负责在构建后调用。


## 5. 目录职责与依赖方向简图
**依赖方向原则**
- 构建系统层（[`oh-my-robot/xmake`](../../xmake)）提供配置、规则、工具链与共享 API，供模块构建脚本调用。
- 模块构建脚本（[`oh-my-robot/lib`](../../lib)、[`oh-my-robot/platform`](../../platform) 下的 `xmake.lua`）只描述本模块目标与依赖，不反向依赖构建系统实现细节。
- 板级/芯片/厂商数据（[`oh-my-robot/platform/bsp/data`](../../platform/bsp/data)）仅被 BSP 装配层读取，不直接参与构建规则。

```mermaid
flowchart TB
  build[oh-my-robot/xmake: 配置/规则/工具链/共享模块] --> mod[模块构建脚本<br/>oh-my-robot/lib、oh-my-robot/platform/*/xmake.lua]
  data[oh-my-robot/platform/bsp/data: board/chip/vendor] --> mod
  mod --> code[运行时代码<br/>oh-my-robot/lib 与 oh-my-robot/platform]
```

## 6. 目标依赖关系图
```mermaid
graph TD
  robot_project --> tar_oh_my_robot
  tar_oh_my_robot --> tar_awcore
  tar_oh_my_robot --> tar_awalgo
  tar_oh_my_robot --> tar_awdrivers
  tar_oh_my_robot --> tar_awsystems
  tar_oh_my_robot --> tar_bsp
  tar_oh_my_robot --> tar_osal
  tar_oh_my_robot --> tar_sync
  tar_bsp --> tar_board
  tar_osal --> tar_os
  tar_sync --> tar_osal
```

## 7. 模块关系与数据流
### 7.1 配置与上下文流向
```mermaid
flowchart LR
  defaults[config/defaults.lua] --> options[config/options.lua]
  options --> context[oh_my_robotlua: set_context_extra]
  context --> rules_context[rules/context.lua]
  context --> rules_assets[rules/board_assets.lua]
  context --> osal[platform/osal/xmake.lua]
  context --> sync[platform/sync/xmake.lua]
```

### 7.2 BSP 数据装配关系
```mermaid
flowchart LR
  vendor[data/vendors] --> loader[data_loader]
  chip[data/chips] --> loader
  board[data/boards] --> loader
  loader --> profile[profile]
  profile --> arch_traits[arch traits]
  arch_traits --> toolchain_map[toolchain arch map]
  toolchain_map --> arch_flags[arch flags]
  profile --> components[components merge]
  profile --> inputs[build inputs]
  profile --> assets[build assets]
  inputs --> board_target[boards/<board>/xmake.lua]
  assets --> rule_assets[oh_my_robot.board_assets]
```

## 8. 模块职责细节
### 8.1 配置模块
- [`oh-my-robot/xmake/config/defaults.lua`](../../xmake/config/defaults.lua)：定义 `board/os/sync_accel/semihosting` 默认值。
- [`oh-my-robot/xmake/config/options.lua`](../../xmake/config/options.lua)：
  - 基于 [`oh-my-robot/platform/bsp/data/boards/index.lua`](../../platform/bsp/data/boards/index.lua) 与 [`oh-my-robot/platform/osal/index.lua`](../../platform/osal/index.lua) 枚举可选 board/os。
  - 将 board/os/toolchain/arch/flags 写入上下文。
  - `semihosting=off` 时为 armclang 可执行目标注入非 semihosting syscall 覆盖桩，避免运行期触发 semihost BKPT。
  - 对默认值进行合法性校验。
  - `config.arch` 作为平台架构标识：优先取工具链数据中的 `arch`，缺失时使用板级 CPU 架构。
    - `context.arch` 表示 CPU 架构（来自 BSP 芯片数据），芯片仅提供 `arch_traits`，由工具链映射生成 `arch_flags`。

### 8.2 规则模块
- `oh_my_robot.context`：
  - 注入 `arch_flags` 与 `toolchain_flags`。
  - 当 LTO 计划开启时，按工具链收敛注入显式参数（`gnu-rm`: `-flto`；`armclang`: `-flto/--lto`），确保参数落地一致。
  - 配置阶段校验硬浮点支持，并确保工具链已完成检测。
  - 构建后输出 `build mode` 与内存用量：
    - `build mode` 通过目标模式接口判定（`debug`/`release`）。
    - 内存用量由 `tools/omcheck/mem_report.py` 调用工具链 `size` / `fromelf` 输出 `FLASH/RAM` 已用量；不解析链接脚本。
  - 构建后通过 `tools/omcheck/link_guard.py` 执行链接契约校验（`tar_os`/`tar_sync` 关键强符号与 `.om_init` 段边界检查），失败即终止构建。
- `oh_my_robot.board_assets`：
  - 注入启动文件与链接脚本。
  - 依赖 `toolchain_linker_flag`。
- `oh_my_robot.image_convert`：
  - 构建后生成 `hex/bin`。
  - 清理阶段删除生成文件。

### 8.3 工具链模块
- [`oh-my-robot/xmake/toolchains/data.lua`](../../xmake/toolchains/data.lua)：工具链数据（`toolset/image/linker_flag` 等）。
- [`oh-my-robot/xmake/toolchains/xmake.lua`](../../xmake/toolchains/xmake.lua)：注册 `kind=custom` 工具链。
- [`oh-my-robot/xmake/modules/toolchain_lib.lua`](../../xmake/modules/toolchain_lib.lua)：脚本侧 API，供规则与配置使用。
- [`oh-my-robot/xmake/toolchains/toolchain_bootstrap.lua`](../../xmake/toolchains/toolchain_bootstrap.lua)：在构建前补全工具链配置，保证首轮构建可用。

### 8.4 BSP 模块
- `bsp/data_loader.lua`：加载 board/chip/vendor 数据并生成 profile。
- `bsp/arch.lua`：解析架构与 arch traits。
- `bsp/components.lua`：合并组件数据（board 覆盖优先）。
- `bsp/inputs.lua`：输出 `defines/includedirs/sources/headerfiles/extrafiles`。
- `bsp/assets.lua`：解析启动文件/链接脚本与板级 OS 配置目录。
- rm-c-board 的 armclang scatter 不再将 FreeRTOS 相关对象固定放入 `RW_IRAM2`，避免 CCM/DMA 风险。

### 8.5 armclang 运行时与 semihosting 维护约束
- 运行时决策入口：[`oh-my-robot/xmake/toolchains/toolchain_runtime.lua`](../../xmake/toolchains/toolchain_runtime.lua)。
  - `resolve_semihosting_mode()` 只接受 `off/on`。
  - `resolve_runtime_payload("armclang", "off")` 返回 [`oh-my-robot/platform/runtime/armclang/semihost_stub.c`](../../platform/runtime/armclang/semihost_stub.c)。
  - `resolve_runtime_payload("armclang", "on")` 返回空注入。
- 注入生效点：[`oh-my-robot/xmake/rules/context.lua`](../../xmake/rules/context.lua) 的 `oh_my_robot.context` 规则。
  - 仅对 `target:kind() == "binary"` 注入运行时 payload。
  - 静态库目标不应注入 semihost stub，避免污染库边界。
- stub 语义约束（[`oh-my-robot/platform/runtime/armclang/semihost_stub.c`](../../platform/runtime/armclang/semihost_stub.c)）：
  - 目标是“去 semihost 依赖”，不是“模拟完整主机文件系统”。
  - `_sys_open` 返回伪句柄用于保障 C 运行库初始化继续执行。
  - `_sys_exit` 保持死循环，避免落入未定义宿主行为。
- 维护原则：
  - 修改 stub 返回值前，必须评估对启动阶段、日志路径与异常路径的影响。
  - 不要在业务代码中直接依赖 semihosting 行为；semihosting 仅视为调试能力。

### 8.6 Cortex-Debug 下载策略约束
- 推荐策略：`.elf` 作为符号载体，`.hex` 作为下载载体（`restore xxx.hex`）。
- `gnu-rm` 可使用 `load`；`armclang` 在部分调试链路下使用 `load` 可能出现段装载语义不一致，应优先 `restore .hex`。
- 禁止将 `restore xxx.bin binary` 作为通用模板默认值：该命令缺省从 `0x00000000` 写入，强依赖目标地址别名，不具备稳定迁移性。

### 8.7 weak/strong 覆盖链接治理约束
根因定义：
- 问题属于“静态库成员抽取机制”范畴，不是 `weak` 语法错误。
- 当 `weak` 符号已满足未定义引用时，链接器不会继续抽取库内同名 `strong` 成员。
- 仅调整库顺序通常无法根治该类问题。

当前实现（必须保持）：
- [`platform/bsp/inputs.lua`](../../platform/bsp/inputs.lua) 负责收集 `override_sources`（vendor/chip/board）并从 `inputs.sources` 中排除。
- [`build/rules/board_assets.lua`](../../xmake/rules/board_assets.lua) 仅对 `binary` 目标注入 `override_sources`，并带入板级 `includedirs/defines`。
- 覆盖源通过“直连最终 `binary`”参与链接，不依赖静态库抽取偶然性。

维护约束：
- 新增必须覆盖 `weak` 的点时，必须将 `strong` 源文件登记到对应层级 `override_sources`。
- 共享适配层（vendor adapters，见 ADR-0012）的 ISR 文件遵循同一规则：登记到板级 `override_sources` 直连 binary；实现文件（含 `OM_INIT` 自注册）登记到 `selfreg_sources`。
- 不得默认依赖 `whole-archive` 作为跨工具链通用方案。
- 不得将“应用层 weak 扩展”作为推荐模式对外传播；当前版本明确不建议应用层使用 weak 语义承载业务逻辑。
- 中断/启动/端口钩子等关键覆盖点，必须在构建验证阶段执行符号强弱校验。

未来规划（路线要求）：
1. XMake 层：
   - 建立不局限于单一层级的通用强制覆盖规则，统一管理 BSP/OSAL/SYNC/APP 等覆盖源。
2. 用户层：
   - 推进无感开发能力，目标是强定义位置不受文件层级与组织方式约束，仍可稳定正确链接。

### 8.8 OSAL/SYNC 聚合边界与链接契约约束
当前实现（必须保持）：
- `tar_oh_my_robot` 与 `tar_osal` 使用聚合目标（`phony`），不直接产出静态库。
- 应用目标只依赖 `tar_oh_my_robot`，禁止直连 `tar_os`、`tar_sync`。
- `tar_osal -> tar_os` 采用 `public` 依赖传播，确保链路闭包稳定。

维护约束：
- 不得把“应用层直接补 `add_deps("tar_os")`/`add_deps("tar_sync")`”作为修复手段。
- 若出现 OSAL/SYNC 未定义引用，应优先检查聚合层依赖传播与底层模块产物完整性。
- 任何新增 OSAL/SYNC 核心符号都应同步纳入链接契约校验列表。

### 8.9 armclang 链路确定性与诊断约束
当前实现（必须保持）：
- [`oh-my-robot/xmake/toolchains/toolchain_overrides.lua`](../../xmake/toolchains/toolchain_overrides.lua) 对 `armclang` 强制补全 `toolset_as=armclang`，禁止回退到 `armasm`。
- 若用户显式传入 `toolset_as` 且值不是 `armclang`，配置期直接报错，不做隐式兼容。

版本门禁（配置期/构建期）：
- XMake 最低版本：由工程根 `xmake.lua` 的 `set_xmakever("3.0.7")` 统一约束。
- armclang 最低版本：`6.14`（通过 `armclang --version` 解析并强校验）。
- 任一版本不满足门禁时，必须直接失败，不允许降级继续构建。
- 授权前提（配置期基线）：
  - armclang 链路要求 `armlink` 具备非 Lite 授权能力。
  - 维护者在发布/分发前必须执行 `armlink --vsn`，若 `Product` 含 `MDK-ARM Lite`，需标记为不满足 armclang 发布前提。

## 9. 数据结构与合并策略
### 9.1 Toolchain 数据
- 定义位置：[`oh-my-robot/xmake/toolchains/data.lua`](../../xmake/toolchains/data.lua)。
- 关键字段：`default/toolchains`。
- `toolchains[name]` 包含 `kind/plat/arch/linker_flag/toolset/image`。
- `linker_accepts_arch_flags`：控制是否将工具链映射得到的 arch ldflags 注入链接阶段（armclang 为 `false`）。
- 对 `gnu-rm/armclang`，`sdk/bin` 需由 CLI 或 `toolchain_presets` 提供。

### 9.2 BSP 三层数据
- vendor：`oh-my-robot/platform/bsp/data/vendors/<vendor>.lua`
- chip：`oh-my-robot/platform/bsp/data/chips/<chip>.lua`
- board：`oh-my-robot/platform/bsp/data/boards/<board>.lua`
- chip 使用 `arch_traits` 描述 CPU/FPU/float-abi 等架构数据。

### 9.3 组件合并规则
- 字段统一：`defines/includedirs/sources/headerfiles/extrafiles`。
- 合并优先级：board 覆盖优先，其次 chip，再其次 vendor。
- `component_overrides` 允许对单组件进行白名单覆盖。

## 10. 上下文与持久化
- 上下文字段：`board_name/chip_name/os_name/toolchain_name/arch/arch_flags/toolchain_flags/board_os_config_dir`。
- `oh-my-robot/xmake/modules/oh_my_robotlua` 将上下文保存到 `om_ctx_*` 配置项。
- `get_context()` 缺失字段时会提示先执行 `xmake f`。
- `get_preset()` 只读取项目根 `om_preset.lua`，不再接受环境变量或用户目录级 fallback。
- `FRAMEWORK_VERSION` 的 Git 查询固定以框架根目录为准，不再错误跟随项目壳目录。

## 11. 扩展指南（模板）
### 11.1 新增 vendor（示例）
文件：`oh-my-robot/platform/bsp/data/vendors/my-vendor.lua`
```lua
local vendor = {
  name = "MY_VENDOR",
  defines = {},
  includedirs = {},
  components = {
    cmsis = {
      includedirs = {"vendor/MY_VENDOR/CMSIS/Include"},
      headerfiles = {"vendor/MY_VENDOR/CMSIS/Include/**.h"},
    },
  },
}
function get()
  return vendor
end
```

### 11.2 新增 chip（示例）
文件：`oh-my-robot/platform/bsp/data/chips/my-chip.lua`
```lua
local chip = {
  name = "my-chip",
  vendor = "my-vendor",
  arch = "cortex-m4",
  defines = {"MY_CHIP"},
  components = {
    device = {
      includedirs = {"vendor/MY_VENDOR/CHIP/Include"},
      headerfiles = {"vendor/MY_VENDOR/CHIP/Include/my_chip.h"},
      sources = {"vendor/MY_VENDOR/CHIP/Source/system_my_chip.c"},
    },
  },
  startup = { ["gnu-rm"] = "vendor/MY_VENDOR/CHIP/Source/gcc/startup_my_chip.s" },
  linkerscript = { ["gnu-rm"] = "vendor/MY_VENDOR/CHIP/Source/gcc/my_chip.ld" },
  arch_traits = {
    cpu = "cortex-m4",
    thumb = true,
  },
}
function get()
  return chip
end
```

### 11.3 新增 board（示例）
文件：`oh-my-robot/platform/bsp/data/boards/my-board.lua`
```lua
local board = {
  name = "my-board",
  chip = "my-chip",
  vendor = "my-vendor",
  includedirs = {"boards/my-board/include"},
  sources = {"boards/my-board/source/**.c"},
  osal = { freertos = "boards/my-board/osal/freertos" },
  startup = { ["gnu-rm"] = "boards/my-board/startup/gcc/startup_my_chip.s" },
  linkerscript = { ["gnu-rm"] = "boards/my-board/linker/gcc/my_chip.ld" },
  components = {"cmsis", "device"},
  component_overrides = {},
}
function get()
  return board
end
```
新增 board 时需同步更新 [`oh-my-robot/platform/bsp/data/boards/index.lua`](../../platform/bsp/data/boards/index.lua)。

**启用 vendor 共享适配层外设（板瘦身，见 ADR-0012）**：若新板使用已适配 MCU 家族的外设（如 STM32F4 的 CAN），无需复制实现，只需：
1. `boards/<b>/include/bsp_<p>.h`：板配置 shim（`USE_*` 开关 + `BSP_<P>_COUNT` + include 适配层契约头）；
2. `boards/<b>/source/peripherals/<p>/bsp_<p>_data.c`：板数据表（实例/参数/引脚/中断，自动 glob 进 selfreg）；
3. 板 lua：`selfreg_sources` 引用适配层实现（含自注册）、`override_sources` 引用适配层 ISR。
不用该外设的板零改动；板数据缺漏由实现侧 `#error`（缺 `BSP_<P>_COUNT`）与数据文件编译期断言兜底。

> ⚠️ **glob 契约**：`boards/<b>/source/peripherals/` 下的 `*.c` 会被自动 glob 直连 binary（进 selfreg）。
> 若在该目录新增 ISR 文件（`*_it.c`），**必须同步登记到板 lua `override_sources`**——否则会被 glob 当作
> 普通源直连，绕开 opt-in 铁律（inputs.lua 的排除规则以 override_sources 集合为准，未登记即不排除）。
> 同理，板数据/实现文件删除时须同步清理板 lua 中的对应引用。

### 11.4 新增 board 构建脚本
文件：`oh-my-robot/platform/bsp/boards/my-board/xmake.lua`
```lua
target("tar_board")
  set_kind("static")
  add_rules("oh_my_robot.context")
  add_deps("tar_awapi_driver", {public = true})
  on_load(function(target)
    local board_root = os.scriptdir()
    local bsp_root = path.join(board_root, "..", "..")
    local om_root = path.join(bsp_root, "..", "..")
    local oh_my_robot = import("oh-my-robot", {rootdir = om_root})
    local context = oh_my_robot.get_context()
    local bsp = import("bsp", {rootdir = bsp_root})
    local inputs = bsp.get_board_build_inputs(context.board_name)
    target:add("includedirs", inputs.includedirs, {public = false})
    target:add("defines", inputs.defines)
    target:add("files", inputs.sources)
    target:add("headerfiles", inputs.headerfiles)
    target:add("extrafiles", inputs.extrafiles)
  end)
target_end()
```

### 11.5 链接脚本 `.om_init_<N>` 段（自动注册初始化）

`om_init` 是 OM 框架的分散加载自动注册初始化系统（参考 Linux initcall / Zephyr SYS_INIT）：模块通过 `OM_INIT_<LEVEL>(func)` 或 `OM_INIT(func, level, prio)` 将 `OmInitEntry` 注册到**每级一段**的 `.om_init_<N>` 段（N = 级别数值），`om_do_initcalls(lo, hi)` 在启动期逐级遍历、同级按 prio 排序后依次调用。级别按依赖轴镜像分层（`EARLIEST=0 / BOARD=1 / DRIVER=2 / SERVICE=3 / SYSTEM=4 / APPLICATION=5 / LATE=6`），详见 `lib/include/core/om_init.h` 与 ADR-0010。

**用户不写 `main`**（ADR-0013）：框架经 `oh_my_robot.selfreg` 规则向 binary 注入弱符号 `main`（`lib/source/core/om_main.c`，内部调 `om_system_startup()` 进入启动编排）——启动文件调 `main` 即自动执行全部 initcall。用户/测试例程的开发通道是 `OM_INIT_APPLICATION(fn)`（建业务线程等，init 线程调用）；需要自定义启动（host 测试/bootloader/自定义序列）时定义强 `main` 自动覆盖；不需要框架 main 符号时构建期设 `om_framework_main=off` 关闭注入。

新板移植时，链接脚本**必须**包含全部 7 个级别段的定义（**级别顺序由链接脚本按段排列保证**，链接期解析），否则自注册回调会运行期静默丢失（缺失段的边界符号被 C 侧 extern 引用，会直接链接报 undefined reference）。框架通过每级两个外部符号定位段边界：

| 符号 | 说明 |
|------|------|
| `__om_init_<N>_start` | 级别 N 回调表起始地址 |
| `__om_init_<N>_end` | 级别 N 回调表结束地址 |

**GCC LD 模板**（置于 `.isr_vector` 之后、`.text` 之前；每级一节，共 7 节按级别顺序排列）：

```ld
.om_init_0 :
{
  . = ALIGN(4);
  PROVIDE(__om_init_0_start = .);
  KEEP(*(.om_init_0))
  PROVIDE(__om_init_0_end = .);
  . = ALIGN(4);
} >ROM
.om_init_1 :
{
  . = ALIGN(4);
  PROVIDE(__om_init_1_start = .);
  KEEP(*(.om_init_1))
  PROVIDE(__om_init_1_end = .);
  . = ALIGN(4);
} >ROM
/* ... .om_init_2 .. .om_init_6 依此类推，按级别顺序排列 ... */
```

`KEEP` 防止链接器在 `--gc-sections` 下丢弃回调表项。

**armlink scatter 模板**（每级一个执行域，按级别顺序）：

```sct
LR_IROM1 0x08000000 ...  {
  ER_PRE 0x08000000  {            ; 向量表 + C 运行时
   *.o (RESET, +First)
   *(InRoot$$Sections)
  }
  ER_OM_INIT_0 +0  {             ; 0 级（EARLIEST）回调表
   *(.om_init_0)
  }
  ER_OM_INIT_1 +0  {             ; 1 级（BOARD）
   *(.om_init_1)
  }
  /* ... ER_OM_INIT_2 .. ER_OM_INIT_6 依此类推，按级别顺序排列 ... */
  ER_RO +0  { ... }
  ...
}
```

armlink 自动为每个执行域生成 `Image$$ER_OM_INIT_<N>$$Base` / `Image$$ER_OM_INIT_<N>$$Limit` 符号。`om_init.c` 在 `__ARMCC_VERSION` 下直接 `extern` 引用这些符号（armclang 允许 `$` 出现在标识符中）并宏重命名为 `__om_init_<N>_start/end`，**无需板级别名文件**（历史上的 `.set` 别名方案在最终符号表中被省略，已弃用）。GCC ld 的边界符号由链接脚本 `PROVIDE` 直接提供。

**同级排序说明：** 级别顺序由链接器保证；同级内执行顺序由 prio 决定（启动期一次小排序）——armlink 无段内 `SORT_BY_NAME` 等价物，故 OMR 不做链接期 prio 排序（Zephyr 仅 GCC/Clang 系才用链接期排序）。

**自注册模块的链接保障（重要）：** 自注册模块（`lib/systems/src`、`lib/services/src` 下的 `.c`）若仅含 `OM_INIT` 注册、无外部引用，会被静态库按需抽取丢弃。binary 目标须启用 `oh_my_robot.selfreg` 规则（与 `context/board_assets/image_convert/project_cfg` 并列）：该规则把这些源直接编译进 binary，直接 `.o` 先于归档被链接、定义模块符号，归档成员随后不再被抽取，故无需从静态库剔除、也不会重复。这是 Linux/Zephyr"全对象链接"目标在 XMake 静态归档模型下的等价实现（XMake 无干净的 per-dep `--whole-archive` 入口，详见 ADR-0010）。**框架默认 `main`（`lib/source/core/om_main.c`，弱符号）也经本规则注入** binary（`om_framework_main=on` 默认）；它不在 `tar_awcore` 归档内（`remove_files`），用户定义强 `main` 自动覆盖，`om_framework_main=off` 时整体关闭注入（见 ADR-0013）。

**校验：** 构建后 linkguard 会按工具链校验最终 ELF 的每级边界符号（GCC 查 `__om_init_<N>_start/end`、armclang 查 `Image$$ER_OM_INIT_<N>$$Base/Limit`，N=0..6）；若缺失，构建期即报错（"该板链接脚本未提供每级 `.om_init_<N>` 段边界符号"），不再等到运行期静默失败。

### 11.6 新增 OS
目录：`oh-my-robot/platform/osal/myos/`，编写 `xmake.lua`：
```lua
target("tar_os")
  set_kind("static")
  add_rules("oh_my_robot.context")
  add_files("*.c")
target_end()
```
并在 board 数据中增加映射：
```lua
osal = { myos = "boards/my-board/osal/myos" }
```
新增 OS 时需同步更新 [`oh-my-robot/platform/osal/index.lua`](../../platform/osal/index.lua)。

### 11.7 新增 Sync 加速后端
目录：`oh-my-robot/platform/sync/myos/`，编写 `sync_accel.lua`：
```lua
function get_accel_info()
  return {
    include_dirs = {"path/to/include"},
    capabilities = {},
  }
end
```

### 11.8 新增 Toolchain
在 [`oh-my-robot/xmake/toolchains/data.lua`](../../xmake/toolchains/data.lua) 中添加：
```lua
["my-gcc"] = {
  kind = "custom",
  plat = "cross",
  arch = "arm",
  linker_flag = "-T",
  toolset = {
    cc = "arm-none-eabi-gcc",
    cxx = "arm-none-eabi-g++",
    as = "arm-none-eabi-gcc",
    ld = "arm-none-eabi-g++",
    ar = "arm-none-eabi-gcc-ar",
    ranlib = "arm-none-eabi-gcc-ranlib",
    strip = "arm-none-eabi-strip",
    objcopy = "arm-none-eabi-objcopy",
  },
  image = {
    hex = {kind = "objcopy", tool = "arm-none-eabi-objcopy", format = "ihex"},
    bin = {kind = "objcopy", tool = "arm-none-eabi-objcopy", format = "binary"},
  },
}
```

## 12. 校验与常见失败点
- `board.components` 缺失：会在组件合并阶段报错。
- 启动文件/链接脚本缺失：`oh_my_robot.board_assets` 注入时失败。
- `toolchain_linker_flag` 缺失：链接脚本无法注入。
- 工具链路径错误：`sdk/bin` 校验失败。
- FreeRTOS 端口文件缺失：`tar_os` 加载时失败。
- `semihosting` 值非法：`options.lua`/`toolchain_runtime.lua` 会在配置期报错（仅允许 `off/on`）。
- `gnu-rm` 下 LTO 未开启：`auto` 策略要求 `gcc >= 14.2.0`；低于门槛会自动关闭并在摘要输出原因（典型旧版问题：`offset out of range`）。
- `armclang semihost stub missing`：[`oh-my-robot/platform/runtime/armclang/semihost_stub.c`](../../platform/runtime/armclang/semihost_stub.c) 缺失或路径变更未同步。
- `armclang toolset_as unsupported`：仅支持 `toolset_as=armclang`；请移除自定义回退配置。
- `armclang version not supported`：当前版本门禁为 `>= 6.14`，请升级 Arm Compiler 6。
- `Fatal error: L6050U`：当前 `armlink` 为 Lite 授权导致代码大小受限；先执行 `armlink --vsn` 确认 `Product`，若为 `MDK-ARM Lite` 则需切换到非 Lite 授权或改用 `gnu-rm`。
- 出现 `attempt to concatenate a nil value (global 'sourcefile')`：请检查本机 XMake 是否低于 `3.0.7`，并升级后重试。
- Cortex-Debug `armclang + load` 初始化异常：优先切换为 `restore <profile>.hex` 下载链路。
- weak 覆盖未生效：检查 `override_sources` 是否登记、是否被 `oh_my_robot.board_assets` 注入到 `binary`，并核对最终 ELF 符号是否仍为 `W/weak`。
- `[oh-my-robot] link contract check failed`：`tar_os`/`tar_sync` 的关键符号未以强符号形态提供；应回查模块构建输入与依赖传播，不要在应用目标补直连依赖。

## 13. 变更记录
- 2026-11-10：构建系统瘦身——移除 `xmake flash` 与烧录/调试集成（转外部工具）；移除 armclang ELF LOAD segment 修补（调试统一走 `.hex` 下载）；内存报告与链接契约校验外置到 `tools/omcheck/`；`xmake init_workspace` 的壳生成逻辑外置到 `tools/omworkspace/`；semihosting stub 迁至 `platform/runtime/armclang/`。
- 2026-05-23：`xmake flash` 重构为策略模式，新增 DAPLink (OpenOCD) 烧录器支持。`flash` 预设格式升级：新增 `flash.flasher` 通用字段与 `flash.daplink` 子表，通用字段（`target`/`firmware`/`prefer_hex`/`reset`/`run`/`native_output`）提升至 `flash` 层级对两种烧录器均生效。同步更新全部构建文档与 VSCode 调试模板。
- 2026-03-26：preset 解析链路收敛为”只读取项目根 `om_preset.lua`”；`xmake init_workspace` 会在项目壳中创建本地 `oh-my-robot` 目录链接/junction，并自动落一份项目级 `om_preset.lua` 模板；新增 `--preset=<path>` 后，可直接把指定 preset 源文件复制到项目壳根目录；`docs/process/git_collaboration_spec.md` 已补充“外部 worktree 开发 + 外层项目根独立构建”的推荐用法，`quick_start.md` 收回到项目根首次构建口径。
- 2026-02-19：LTO 在 debug/release 统一纳入策略控制；收敛为“gnu-rm 按版本门槛启用（>=14.2.0）、armclang 默认开启”，并在 `oh_my_robot.context` 按工具链显式注入 LTO 参数。
- 2026-02-19：根 `xmake.lua` 使用官方策略 `set_policy("build.optimization.lto", false)` 作为统一入口，具体目标开关由 `oh_my_robot.context` 依据工具链动态设置，避免模式与工具链行为分叉。
- 2026-02-18：将 `tar_oh_my_robot`、`tar_osal` 收敛为聚合目标（`phony`），并新增二进制构建后的 OSAL/SYNC 关键符号链接契约校验。
- 2026-02-13：`oh_my_robot.context` 新增构建后摘要输出，统一输出 `build mode` 与 `memory distribution`（全工具链可用）。
- 2026-02-13：XMake 门禁提升至 `3.0.7`；移除 `armlink sourcefile=nil` 兼容保护逻辑，统一依赖上游修复版本。
- 2026-02-12：新增 `upstream_xmake_armlink_sourcefile_nil.md`，固化 XMake `armlink.lua` 缺陷的最小复现、根因与上游 patch 建议。
- 2026-02-12：收敛 armclang 汇编链路为 `toolset_as=armclang`，禁止 `armasm` 回退；新增 armclang 版本门禁与 `armlink sourcefile=nil` 错误透传保护。
- 2026-02-08：统一文档口径：`om_preset.flash` 当前仅支持 J-Link，DAPLink 标记为后续规划；同步 QuickStart/开发者手册/维护者手册说明。
- 2026-02-07：新增 weak/strong 覆盖链接治理文档，明确静态库抽取根因、`override_sources` 机制、维护约束与通用化规划目标。
- 2026-02-07：补充 armclang semihosting 维护约束文档（`toolchain_runtime` 决策链路、`oh_my_robot.context` 注入边界、stub 语义约束）。
- 2026-02-07：补充 Cortex-Debug 多工具链下载策略文档：推荐 `.elf` 仅做符号、`.hex` 做下载；armclang 默认采用 `restore .hex`。
- 2026-02-07：新增 `--semihosting=off|on` 构建选项；默认 `off`。当 toolchain=armclang 且 `off` 时，自动注入 syscall 覆盖桩以消除 semihosting BKPT 风险。
- 2026-02-07：`xmake flash` 新增 `--native_output` 参数，用于按需透传烧录器原生输出。
- 2026-02-04：board/os 列表改为静态索引，避免描述域目录扫描。
- 2026-02-01：新增 `build_system_best_practices.md` 最佳工程实践文档。
- 2026-02-01：记录已知限制：`xmake flash` 使用 ELF 可能无法实际烧录，建议使用 HEX。
- 2026-02-01：`robot_project` 输出文件名显式为 `.elf`；`xmake flash` 要求固件路径必须带扩展名。
- 2026-02-01：`xmake flash` 参数优先级调整为 CLI > config > preset > default；当 preset 与配置不一致时烧录前给出提示。
- 2026-02-01：`xmake flash` 支持从 `om_preset.lua` 读取 J-Link 预设；新增内置任务说明文档。
- 2026-02-01：新增 `xmake flash` 任务，使用 J-Link Commander 进行烧录。
- 2026-01-30：根据当前 XMake 实现重写维护手册，补充依赖图与模块关系说明；明确平台 arch 与 CPU arch 解耦；为 armclang 链接阶段跳过 BSP arch ldflags。
- 2026-01-30：BSP 改为仅提供 `arch_traits`，由工具链映射生成 `arch_flags`，实现职责解耦。
- 2026-01-31：rm-c-board 的 armclang 链接脚本移除 FreeRTOS 对象固定分配，默认落入 `RW_IRAM1`。
- 2026-01-31：preset 拆分为 `toolchain_default` 与 `toolchain_presets`，支持多工具链路径预设。
- 2026-01-31：构建脚本入口迁移至 [`oh-my-robot/xmake/xmake.lua`](../../xmake/xmake.lua)，并将 `config/rules/toolchains/modules` 统一归档到 [`oh-my-robot/xmake/`](../../xmake/)。
- 2026-02-01：启用 `plugin.compile_commands.autoupdate`，构建后自动生成 `compile_commands.json` 到项目根目录。

