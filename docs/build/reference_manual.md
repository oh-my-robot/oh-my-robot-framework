# OM XMake 新手开发者手册

## 1. 适用范围
本手册面向首次使用 OM 的开发者，目标是让你在最短时间内完成可运行的构建，并理解最常用的配置入口。内容完全基于当前仓库代码实现。

## 2. 术语与约定
- board：板级名称，对应 `oh-my-robot/platform/bsp/data/boards/<board>.lua`。
- os：OS/RTOS 名称，对应 `oh-my-robot/platform/osal/<os>/` 目录。
- toolchain：工具链名称，来自 `oh-my-robot/build/toolchains/data.lua`。
- arch traits：芯片提供的架构数据（CPU/FPU/float-abi），由工具链映射为编译参数。
- build profile：输出目录结构，由 XMake `plat/arch/mode` 决定，形如 `build/<plat>/<arch>/<mode>`。

## 3. 环境准备
1) 安装 XMake，并验证：
```sh
xmake --version
```
2) 准备交叉编译工具链（例如 gnu-rm 或 armclang）。
3) 确认工具链路径：
- `sdk`：工具链根目录
- `bin`：可执行文件目录（包含 `arm-none-eabi-gcc` 或 `armclang` 等）

## 4. 项目结构速览
- `xmake.lua`：项目顶层构建入口。
- `oh-my-robot/xmake.lua`：OM 构建入口转发（实际入口在 `oh-my-robot/build/xmake.lua`）。
- `oh-my-robot/build/xmake.lua`：OM 构建入口。
- `oh-my-robot/build/config/`：构建选项定义与默认值。
- `oh-my-robot/build/rules/`：构建规则（上下文注入/板级资源/镜像转换）。
- `oh-my-robot/build/toolchains/`：工具链数据与脚本逻辑。
- `oh-my-robot/build/tasks/`：自定义任务（如 flash 烧录）。
- `oh-my-robot/platform/bsp/`：BSP 数据与板级构建脚本。
- `oh-my-robot/platform/osal/`：OS 抽象层与 OS 端口。
- `oh-my-robot/platform/sync/`：同步原语与加速后端。

## 5. 第一次构建（完整示例）
### 5.1 配置
```sh
xmake f -c --board=rm-c-board --os=freertos --toolchain=gnu-rm \
  --sdk="D:/Toolchains/gcc-arm-none-eabi" --bin="D:/Toolchains/gcc-arm-none-eabi/bin" \
  -m debug
```
说明：
- `-c`：清理旧配置并重新生成。
- `-m debug|release`：构建模式。
- `--board/--os/--toolchain`：必须指定。
- `--sdk/--bin`：必须提供（除非已在 `om_preset.lua` 中配置）。
- 已启用 `plugin.compile_commands.autoupdate`，构建后会自动生成 `compile_commands.json` 到项目根目录。

### 5.2 构建
```sh
xmake
```
说明：
- 首次构建会触发工具链检测与配置校验；若工具链不可用会在构建阶段报错。

## 6. 根 `xmake.lua` 最小示例
```lua
set_project("om_app")
add_rules("mode.debug", "mode.release")

includes("oh-my-robot")

target("app")
    set_kind("binary")
    add_deps("tar_oh_my_robot")
    add_rules("oh_my_robot.context", "oh_my_robot.board_assets", "oh_my_robotimage_convert")
    add_files("src/main.c")
    set_policy("check.auto_ignore_flags", false)
target_end()
```

## 7. 构建选项与默认值
### 7.1 选项说明
| 选项 | 说明 | 来源/可选值 |
| --- | --- | --- |
| `--board` | 板级名称 | `oh-my-robot/platform/bsp/data/boards/index.lua` |
| `--os` | OS/RTOS 名称 | `oh-my-robot/platform/osal/index.lua` |
| `--sync_accel` | 同步加速模式 | `auto` / `none` |
| `--semihosting` | semihosting 模式 | `off` / `on` |
| `--toolchain` | 工具链名称 | `oh-my-robot/build/toolchains/data.lua` |
| `--sdk` | 工具链 SDK 目录 | 命令行或 preset 提供 |
| `--bin` | 工具链可执行目录 | 命令行或 preset 提供 |
| `-m` | 构建模式 | `debug` / `release` |

board/os 可选值由以上索引文件维护，新增条目时需同步更新索引。

### 7.2 默认值优先级
- board / os：命令行 > `om_preset.lua` > `oh-my-robot/build/config/defaults.lua` > 数据目录按名称排序的第一个。
- sync_accel：命令行 > `oh-my-robot/build/config/defaults.lua`（未配置时等价于 `auto`）。
- semihosting：命令行 > `oh-my-robot/build/config/defaults.lua`（未配置时等价于 `off`）。
- toolchain：命令行 > `om_preset.lua` 的 `toolchain_default.name` > 已保存配置 > `oh-my-robot/build/toolchains/data.lua` 中的 `default`。
- sdk / bin：命令行 > `om_preset.lua` 的 `toolchain_presets[toolchain]` > 已保存配置 > toolchain 数据默认值（若都没有会报错）。

## 8. `om_preset.lua`（推荐配置方式）
位置：项目根目录（与根 `xmake.lua` 同级）。

用途：提供本机的工具链路径和默认 board/os 选择，避免在命令行重复输入。

示例：
```lua
local preset = {
  toolchain_default = {
    name = "gnu-rm",
  },
  toolchain_presets = {
    ["gnu-rm"] = {
      sdk = "D:/Toolchains/gcc-arm-none-eabi",
      bin = "D:/Toolchains/gcc-arm-none-eabi/bin",
    },
    ["armclang"] = {
      sdk = "D:/Program Files/ProgramTools/Keil_v5/ARM/ARMCLANG",
      bin = "D:/Program Files/ProgramTools/Keil_v5/ARM/ARMCLANG/bin",
    },
  },
  board = {name = "rm-c-board"},
  os = {name = "freertos"},
  flash = {
    jlink = {
      device = "STM32F407IG",
      interface = "swd",
      speed = 4000,
      program = "D:/Program Files/ProgramTools/SEGGER/Jlink/JLink.exe",
      target = "robot_project",
      firmware = nil,
      prefer_hex = true,
      reset = true,
      run = true,
      native_output = false,
    },
  },
}

function get_preset()
  return preset
end
```
约束：
- 仅用于选择与路径配置，不用于新增 board/os/toolchain 数据。
- `toolchain_default.name` 和 `toolchain_presets` 的键必须在内置数据中存在。
- `flash.jlink` 仅在 `xmake flash` 任务中读取，不影响常规构建流程。
- 当前 `om_preset.flash` 仅支持 `jlink` 配置；`daplink` 处于规划阶段，尚未接入任务链路。

### 8.1 名称映射（preset/xmake/launch）
`xmake flash` 内部会解析一个 `target_name`（命令行 `--target` 优先，否则回退到 `flash.jlink.target`），再按该名称查找 XMake 目标。  
因此以下关系必须明确：

| 概念 | 来源 | 示例 | 规则 |
| --- | --- | --- | --- |
| 目标名（Target ID） | `xmake.lua` 的 `target("...")` | `target("robot_project")` | 这是目标唯一标识 |
| 烧录目标名 | `om_preset.lua` 的 `flash.jlink.target` | `target = "robot_project"` | 必须与 `target("...")` 完全一致 |
| 输出文件名 | `xmake.lua` 的 `set_filename("...")` | `set_filename("robot_project.elf")` | 仅决定产物文件名 |
| 调试符号文件 | `.vscode/launch.json` 的 `executable` | `build/cross/arm/debug/robot_project.elf` | 必须指向实际生成的 ELF |

常见错配后果：
- `flash.jlink.target` 与 `target("...")` 不一致：`xmake flash` 无法定位目标。
- `launch.json.executable` 与真实产物不一致：Cortex-Debug 启动失败或符号加载失败。

## 9. 构建产物与清理
- 产物路径：`build/<plat>/<arch>/<mode>/`。
- `arch` 为平台架构标识：优先取工具链数据中的 `arch`，若工具链未提供则使用板级 CPU 架构（例如 `cortex-m4`）。  
  因此 armclang 的输出路径可能为 `build/cross/cortex-m4/<mode>/`，属于正常行为。
- 若目标启用 `oh_my_robotimage_convert` 规则，会生成 `.hex`/`.bin`。
- 清理：
```sh
xmake clean
```

## 10. 常见问题与排查
- **context missing**：未完成 `xmake f` 或配置不完整。重新执行配置命令。
- **toolchain path not set / sdk not found / bin not found**：未提供 `--sdk/--bin` 或 preset 路径错误。
- **board/os not found**：`--board` 或 `--os` 不存在于数据/目录中。
- **startup/linkerscript not found**：板级/芯片/厂商数据中缺少映射。
- **image tool not found**：工具链镜像转换工具未安装或 `bin` 路径不正确。
- **FreeRTOSConfig.h not found**：板级 OS 配置路径缺失或不正确。
- **FreeRTOS 内存布局**：默认不将 FreeRTOS 对象固定到 `RW_IRAM2`，如需使用 CCM，请调整板级链接脚本。
- **`[oh-my-robot] link contract check failed`**：
  - 含义：`tar_os`/`tar_sync` 未按约定提供关键强符号（如 `osal_malloc`、`completion_init`）。
  - 正确修复方向：修复底层模块构建脚本与依赖传播，不要在应用目标直接 `add_deps("tar_os")` 或 `add_deps("tar_sync")`。

## 11. 新手常用调整
### 11.1 构建与烧录
- 烧录：`xmake flash` 通过 J-Link Commander 烧录，默认优先使用 HEX，可通过 `--firmware` 指定 ELF/HEX，并可用 `--native_output=true` 透传原生输出。
- 调试器支持范围：当前仅支持 J-Link；DAPLink 为后续规划项（当前版本不可用）。
- 更换板级/OS/工具链：重新执行 `xmake f -c --board=... --os=... --toolchain=...`。
- 关闭镜像生成：从目标规则中移除 `oh_my_robotimage_convert`。
- 禁用同步加速：`xmake f -c --sync_accel=none ...`。
- 启用 LTO（链接时优化）：
  - 当前工程通过官方接口 `set_policy("build.optimization.lto", ...)` 统一管理 LTO，`debug/release` 都可启用。
  - 当前版本采用“工具链自动收敛”：
    - `gnu-rm`：按编译器版本自动判断；`>= 14.2.0` 自动开启，低于门槛自动关闭并给出原因。
    - `armclang`：自动开启；规则层会显式注入 `-flto/--lto`，确保参数落地一致。
- 构建后摘要输出：
  - `build mode`：本次构建模式（`debug`/`release`，优先基于目标模式接口判定）。
  - `lto`：本次构建的 LTO 生效结果（on/off），并附带 `mode` 与收敛原因。
  - `memory distribution`：本次构建代码的内存分布（`FLASH/RAM` 的 `used/total/percent`）。

### 11.2 armclang semihosting 说明（开发者）
- 配置入口：`xmake f -c --toolchain=armclang --semihosting=off|on ...`。
- 默认值：`off`（定义于 `oh-my-robot/build/config/defaults.lua`）。
- `semihosting=off`：
  - 构建系统会为 `binary` 目标自动注入 `oh-my-robot/build/runtime/armclang/semihost_stub.c`。
  - 目的：避免运行期触发 semihost BKPT，减少调试器 semihost 通道依赖。
  - 代价：`printf/fopen` 等主机 I/O 不会输出到主机，`_sys_exit` 会停在死循环（符合裸机场景预期）。
- `semihosting=on`：
  - 不注入 stub，运行库 syscall 走 semihost 通道。
  - 若调试器未开启 semihost 通道，可能在 `_sys_open` 等位置停住。
- 实践建议：
  - 固件开发、联调、回归测试优先 `off`，日志走 UART/RTT。
  - 仅当你明确需要 semihost 主机 I/O 时使用 `on`，并同步配置调试器侧 semihost 支持。

### 11.3 Cortex-Debug 多工具链模板（开发者）
零基础用户可先看：`oh-my-robot/docs/quick_start.md` 的 `4. 配置 VSCode 调试（.vscode/launch.json）`。

建议使用“同一调试器参数 + 分工具链下载策略”的模板：`gnu-rm` 使用 `load`，`armclang` 使用 `restore .hex`。

可先在 `.vscode/settings.json` 放公共参数（便于复用）：
```json
{
  "oh_my_robot.debug.device": "STM32F407IG",
  "oh_my_robot.debug.interface": "swd",
  "oh_my_robot.debug.serverpath": "D:/Program Files/ProgramTools/SEGGER/Jlink/JLinkGDBServerCL.exe",
  "oh_my_robot.debug.gdbpath": "D:/Program Files/ProgramTools/gcc-arm-none-eabi-10.3-2021.10/bin"
}
```

再在 `.vscode/launch.json` 维护 `gnu-rm` 与 `armclang` 两套模板：
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "J-Link | ${config:oh_my_robot.debug.device} | gcc",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "jlink",
      "device": "${config:oh_my_robot.debug.device}",
      "interface": "${config:oh_my_robot.debug.interface}",
      "cwd": "${workspaceFolder}",
      "executable": "build/cross/arm/debug/robot_project.elf",
      "runToEntryPoint": "main",
      "svdFile": "${workspaceFolder}/oh-my-robot/platform/bsp/vendor/STM32/STM32F4/SVD/STM32F407.svd",
      "serverpath": "${config:oh_my_robot.debug.serverpath}",
      "armToolchainPath": "${config:oh_my_robot.debug.gdbpath}",
      "overrideLaunchCommands": [
        "monitor reset",
        "load",
        "monitor reset"
      ]
    },
    {
      "name": "J-Link | ${config:oh_my_robot.debug.device} | armclang",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "jlink",
      "device": "${config:oh_my_robot.debug.device}",
      "interface": "${config:oh_my_robot.debug.interface}",
      "cwd": "${workspaceFolder}",
      "executable": "build/cross/cortex-m4/debug/robot_project.elf",
      "runToEntryPoint": "main",
      "svdFile": "${workspaceFolder}/oh-my-robot/platform/bsp/vendor/STM32/STM32F4/SVD/STM32F407.svd",
      "serverpath": "${config:oh_my_robot.debug.serverpath}",
      "armToolchainPath": "${config:oh_my_robot.debug.gdbpath}",
      "overrideLaunchCommands": [
        "monitor reset",
        "restore build/cross/cortex-m4/debug/robot_project.hex",
        "monitor reset"
      ]
    }
  ]
}
```

模板参数逐项说明（`launch.json`）：
| 参数 | 作用 | 建议 |
| --- | --- | --- |
| `version` | VSCode 调试配置格式版本 | 固定 `0.2.0` |
| `configurations` | 调试配置数组 | 每个工具链一个配置项 |
| `name` | 配置显示名称 | 建议包含探针/芯片/工具链，便于选择 |
| `type` | 调试插件类型 | Cortex-Debug 固定为 `cortex-debug` |
| `request` | 调试请求类型 | 启动并下载通常用 `launch` |
| `servertype` | GDB Server 类型 | J-Link 链路固定 `jlink` |
| `device` | 目标芯片型号 | 必须与探针端识别名称一致 |
| `interface` | 调试接口 | 常用 `swd` |
| `cwd` | 命令执行工作目录 | 固定 `${workspaceFolder}`，避免路径转义问题 |
| `executable` | ELF 符号文件路径 | 使用对应工具链 profile 下的 `.elf` |
| `runToEntryPoint` | 下载后自动运行到入口点 | 建议 `main` |
| `svdFile` | 外设寄存器描述文件 | 指向对应芯片 SVD |
| `serverpath` | GDB Server 可执行路径 | 建议放到 `settings.json` 统一复用 |
| `armToolchainPath` | `arm-none-eabi-gdb` 所在目录 | 两套配置可复用同一路径 |
| `overrideLaunchCommands` | 自定义下载命令链 | `gnu-rm` 用 `load`；`armclang` 用 `restore .hex` |

模板参数逐项说明（`settings.json`）：
| 参数 | 作用 | 建议 |
| --- | --- | --- |
| `oh_my_robot.debug.device` | 统一芯片型号变量 | 多配置共用，减少重复修改 |
| `oh_my_robot.debug.interface` | 统一接口类型变量 | 多配置共用，降低配置漂移 |
| `oh_my_robot.debug.serverpath` | 统一 J-Link GDB Server 路径 | 环境迁移时只改一处 |
| `oh_my_robot.debug.gdbpath` | 统一 GDB 路径 | 避免不同配置指向不同 GDB |

`overrideLaunchCommands` 中每条命令的作用：
| 命令 | 作用 | 适用 |
| --- | --- | --- |
| `monitor reset` | 下载前复位目标，清理运行态干扰 | `gnu-rm`、`armclang` |
| `load` | 按 ELF 可加载段下载到目标 | 推荐 `gnu-rm` |
| `restore ...robot_project.hex` | 按 HEX 地址记录下载到目标 | 推荐 `armclang` |
| `monitor reset` | 下载后再次复位，从新镜像启动 | `gnu-rm`、`armclang` |

同一 `armclang` 模板切换 `semihosting on/off`（不新增模板）：
1. 构建侧切换开关：
   - 开启：`xmake f -c --toolchain=armclang --semihosting=on ...`
   - 关闭：`xmake f -c --toolchain=armclang --semihosting=off ...`
2. 调试侧使用同一条 `armclang` 配置，仅调整 `preLaunchCommands`：
   - `semihosting=on` 时：
     - `monitor semihosting enable`
     - `monitor semihosting breakonerror 0`
     - `set mem inaccessible-by-default off`
   - `semihosting=off` 时：
     - `set mem inaccessible-by-default off`
3. 保持下载链路不变：`overrideLaunchCommands` 仍使用 `restore ...robot_project.hex`。
4. 若开启 `on` 后仍停在 `BKPT 0xAB`：
   - 先确认当前构建配置确实是 `--semihosting=on`。
   - 再确认调试会话已执行 `monitor semihosting enable`。
   - 建议保留 `monitor semihosting breakonerror 0`，避免 semihost I/O 错误直接中断。

常见误区：
- 不要在跨板卡模板中写死 `restore xxx.bin binary,0x08000000`。
- 仅写 `restore xxx.bin binary` 会默认从 `0x00000000` 下载，依赖芯片地址别名，移植性差于 `restore .hex`。

### 11.4 armclang 工具链稳定性约束（开发者）
- 构建系统会将 `armclang` 默认补全为 `armclang[toolset_as=armclang]`，不再允许自动回退到 `armasm`。
- 若你显式写入 `--toolchain=armclang[toolset_as=<other>]`，配置阶段会直接报错（当前版本仅支持 `toolset_as=armclang`）。
- 使用前提（必须满足）：
  - `armlink` 必须是非 Lite 授权版本；`MDK-ARM Lite` 会触发代码大小上限。
  - 快速检查命令：`armlink --vsn`。
  - 若输出 `Product: MDK-ARM Lite ...`，则该环境不满足大工程构建前提。
- 版本门禁：
  - XMake：由工程根 `xmake.lua` 的 `set_xmakever("3.0.7")` 统一约束。
  - armclang：配置期/构建期强校验 `>= 6.14`。
- 典型授权错误：
  - `Fatal error: L6050U: The code size of this image ... exceeds the maximum allowed ...`
  - 该错误表示链接器授权受限，不是 XMake 构建逻辑错误。
- 实践建议：
  - 固定使用 `XMake >= 3.0.7`，避免历史版本 `armlink` 错误格式化缺陷。
  - 团队分发前先执行一次 `armlink --vsn` 基线检查，确保所有开发机授权级别一致。

### 11.5 weak/strong 链接约束（开发者）
问题背景：
- 在静态库构建模式下，`weak` 与 `strong` 同名符号是否同时进入最终链接单元，决定了覆盖是否生效。
- 若 `strong` 实现在静态库成员中且未被抽取，最终可能保留启动文件或默认对象中的 `weak` 实现。

根因要点：
- 该问题本质是“静态库成员抽取机制”问题，不是 C 语言 `weak` 语法本身问题。
- 仅调整库顺序通常不足以根治；当 `weak` 已满足未定义引用时，链接器不会再去抽取库内 `strong` 成员。

当前版本开发约束：
- 不建议在应用层业务代码中采用 `weak` 语义作为扩展机制。
- 应用层请优先使用显式注册（回调表/策略表/配置驱动）实现可替换行为。
- `weak` 仅建议用于 BSP/端口层等底层扩展点，并由构建系统显式托管。

推荐做法（源文件 + 构建）：
1. 源文件组织：
   - 将需要覆盖 `weak` 的 `strong` 实现放入独立源文件（例如中断处理、端口钩子）。
   - 该文件不应仅通过静态库“间接可见”，必须被构建系统明确标记为覆盖源。
2. 板级数据声明：
   - 在板级数据中使用 `override_sources` 声明覆盖源文件。
   - 对于覆盖源，不再依赖库抽取链路，改为直连最终 `binary` 目标。
3. 规则注入：
   - 通过 `oh_my_robot.board_assets` 在 `binary` 目标注入覆盖源，并继承板级 `includedirs/defines`。
4. 构建验证：
   - 每次改动覆盖点后，必须检查最终 ELF 符号类型（应为强符号而非 `W/weak`）。

未来规划（已立项方向）：
1. XMake 层：
   - 实现不局限于单一层级（board/chip/vendor）的通用强制覆盖规则，统一治理 `weak` 覆盖源注入。
2. 用户层：
   - 推进“无感开发”能力，使强定义位置尽可能不受目录层级与文件组织限制，保证可预测的正确链接结果。

### 11.6 OSAL/SYNC 依赖边界与链接契约（开发者）
当前实现约束：
- `tar_oh_my_robot` 与 `tar_osal` 是“聚合目标”（`phony`），只负责依赖传播，不直接产出静态库文件。
- 真实代码产物由 `tar_os`、`tar_sync` 等模块静态库承担。
- 应用目标只依赖 `tar_oh_my_robot`，不直连 OSAL/SYNC 实现层目标。

为什么这样设计：
- 避免“空静态库”导致的依赖错觉（例如历史上的 `libtar_osal.a` 空归档现象）。
- 保持模块职责边界：应用层不感知 OS 端口细节，便于板级与 OS 迁移。
- 降低链接顺序偶然性对结果的影响。

构建护栏（已内置）：
- 二进制构建后会做链接契约校验：
  - `tar_os` 必须提供 `osal_malloc`、`osal_free` 强符号；
  - `tar_sync` 必须提供 `completion_init`、`completion_wait` 强符号。
- 若校验失败，构建会直接报错并提示修复方向。

## 12. 任务文档
- `oh-my-robot/docs/build/build_tasks_manual.md`：内置任务与参数说明（含 `xmake flash`）。

## 13. 工程实践
- `oh-my-robot/docs/build/build_system_best_practices.md`：构建系统最佳工程实践。

## 14. 外部问题追踪
- `oh-my-robot/docs/build/upstream_xmake_armlink_sourcefile_nil.md`：XMake `armlink.lua` 的 `sourcefile=nil` 上游缺陷复现与提交流水。

