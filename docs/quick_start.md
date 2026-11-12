# OH-MY-ROBOT 快速开始（完全新手）

本指南面向第一次接触本项目的开发者，目标是：
- 先把环境装好并验证。
- 再准备一个可用的项目根目录。
- 然后编写项目级 `om_preset.lua` 完成本机预设。

当前示例环境为 Windows + VSCode。

## 1. 环境搭建（下载 + 验证）

### 1.1 `arm-none-eabi-gcc`（GNU Arm Embedded Toolchain）
下载入口（官方）：
- <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads>

安装建议：
- 安装到不含中文和空格的目录，例如 `D:/Toolchains/gcc-arm-none-eabi-10.3-2021.10`。
- 将 `bin` 目录加入 `PATH`（可选，但推荐）。

验证命令（PowerShell）：
```powershell
arm-none-eabi-gcc --version
arm-none-eabi-gdb --version
```

期望结果：
- 能输出版本号，命令不报 `not recognized`。

### 1.2 `armclang`（Arm Compiler 6）
下载入口（官方）：
- Keil MDK 下载页（内含 Arm Compiler 6）：<https://www.keil.com/download/product/>

安装建议：
- 常见安装路径：`D:/Program Files/ProgramTools/Keil_v5/ARM/ARMCLANG`。

验证命令（PowerShell）：
```powershell
armclang --version
armlink --version
```

期望结果：
- 能输出 Arm Compiler 6 版本信息。
- 建议版本不低于 `6.14`（低于该版本会触发构建门禁）。

### 1.3 XMake
下载入口（官方）：
- <https://xmake.io/#/guide/installation>

验证命令（PowerShell）：
```powershell
xmake --version
```

期望结果：
- 版本号正常输出。
- 版本建议不低于 `3.0.7`（低于该版本会触发构建门禁）。

### 1.4 VSCode 插件：Clangd + XMake
下载入口（官方）：
- Clangd：<https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd>
- XMake：<https://marketplace.visualstudio.com/items?itemName=xmake-io.xmake-vscode>

验证方式：
1. 在 VSCode 扩展页确认插件已安装且启用。
2. 如果本机有 `code` 命令，可用以下命令确认：
```powershell
code --list-extensions | findstr /I "llvm-vs-code-extensions.vscode-clangd"
code --list-extensions | findstr /I "xmake-io.xmake-vscode"
```

### 1.5 J-Link
下载入口（官方）：
- <https://www.segger.com/downloads/jlink/>

安装建议：
- 常见安装路径：
  - `D:/Program Files/ProgramTools/SEGGER/Jlink/JLink.exe`
  - `D:/Program Files/ProgramTools/SEGGER/Jlink/JLinkGDBServerCL.exe`

验证命令（PowerShell）：
```powershell
JLink.exe -?
JLinkGDBServerCL.exe -?
```

期望结果：
- 能输出帮助信息或版本信息。

### 1.6 Cortex-Debug（VSCode 调试插件）
下载入口（官方）：
- <https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug>

安装建议：
- 在 VSCode 扩展市场搜索 `Cortex-Debug` 并安装。
- 安装后建议重启 VSCode。

验证方式：
1. 在 VSCode 扩展页确认 `Cortex-Debug` 状态为已启用。
2. 如果本机有 `code` 命令，可用以下命令确认：
```powershell
code --list-extensions | findstr /I "marus25.cortex-debug"
```
3. 先决条件检查：
```powershell
JLinkGDBServerCL.exe -?
arm-none-eabi-gdb --version
```

说明：
- 当前仓库调试链路支持 J-Link 与 DAPLink (OpenOCD) 两种调试器。

### 1.7 OpenOCD（DAPLink 调试/烧录）
下载入口（官方）：
- <https://openocd.org/pages/getting-openocd.html>

安装建议：
- 下载 Windows 预编译包，解压到不含中文和空格的目录，例如 `D:/openocd-win`。
- 可执行文件路径：`D:/openocd-win/bin/openocd.exe`。

验证命令（PowerShell）：
```powershell
openocd --version
```

期望结果：
- 能输出版本信息（建议 `0.12.0` 及以上）。


DAPLink 配置文件准备：
- 模板文件位于 `tools/openocd/daplink.cfg.example`。
- 复制到项目根 `.vscode/daplink.cfg` 并按以下步骤修改：
  1. 查找探针序列号：
     ```powershell
     openocd -c "adapter driver cmsis-dap; init; exit" 2>&1 | findstr /I "serial"
     ```
     或直接启动 openocd 后观察启动日志中的 `Info : CMSIS-DAP: ... (serial: 5627220232)` 行。
  2. 将序列号填入 `adapter serial` 行（去掉行首的 `#` 注释）。
  3. 若芯片不是 STM32F4 系列，修改 `source [find target/stm32f4x.cfg]` 为对应目标配置。
- 单探针环境可省略 `adapter serial` 行。
DAPLink 探针要求：
- 支持 CMSIS-DAP 协议的调试探针（如 DAP-WL-HS、Segger J-Link CMSIS-DAP 模式等）。
- 若有多探针，需在 OpenOCD 配置文件中通过 `adapter serial` 指定目标探针序列号。

DAPLink 配置文件准备：
- 模板文件位于 `tools/openocd/daplink.cfg.example`。
- 复制到项目根 `.vscode/daplink.cfg` 并按以下步骤修改：
  1. 查找探针序列号：
     ```powershell
     openocd -c "adapter driver cmsis-dap; init; exit" 2>&1 | findstr /I "serial"
     ```
     或直接启动 openocd 后观察启动日志中的 `Info : CMSIS-DAP: ... (serial: 5627220232)` 行。
  2. 将序列号填入 `adapter serial` 行（去掉行首的 `#` 注释）。
  3. 若芯片不是 STM32F4 系列，修改 `source [find target/stm32f4x.cfg]` 为对应目标配置。
- 单探针环境可省略 `adapter serial` 行。

## 2. 在项目根目录编写 `xmake.lua`

文件位置：
- `xmake.lua`（项目根目录）

作用：
- 定义工程入口、构建模式、OM 规则与最终可执行目标。

说明：
- 本章只讨论“项目根目录里应该放什么、怎么完成首次构建”。

### 2.1 如果你没用过 XMake，先理解这件事
`xmake.lua` 就是“构建配置脚本”。
你可以把它理解成：告诉构建系统“项目名是什么、要生成什么目标、主文件在哪、要挂哪些规则”。

### 2.2 按步骤写最小可用 `xmake.lua`
步骤 1：在项目根目录创建 `xmake.lua`。
步骤 2：粘贴下面模板（可直接用）。

```lua
set_project("oh-my-robot")
set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release")
set_policy("build.optimization.lto", false)
add_rules("plugin.compile_commands.autoupdate", {outputdir = os.projectdir()})

includes("oh-my-robot")

target("robot_project")
    set_kind("binary")
    set_filename("robot_project.elf")
    add_deps("tar_oh_my_robot")
    add_rules("oh_my_robot.context", "oh_my_robot.board_assets", "oh_my_robot.image_convert", "oh_my_robot.project_cfg", "oh_my_robot.selfreg")
    add_files(path.join("oh-my-robot", "samples", "osal", "osal_thread", "main.c")) -- 示例文件
target_end()
```

### 2.3 每行都在做什么（新手解释）
- `set_project("oh-my-robot")`：设置项目名。
- `set_xmakever("3.0.7")`：声明最低 XMake 版本。
- `add_rules("mode.debug", "mode.release")`：声明 Debug/Release 两种模式。
- `set_policy("build.optimization.lto", false)`：默认关闭顶层 LTO，由 OM 上下文规则按工具链决定是否启用。
- `add_rules("plugin.compile_commands.autoupdate", ...)`：自动生成 `compile_commands.json`（给 Clangd 用）。
- `includes("oh-my-robot")`：加载 OM 构建入口。
- `target("robot_project")`：定义最终编译目标名（也是 `.vscode/launch.json` 中 `executable` 的文件名前缀）。
- `set_kind("binary")`：目标是可执行镜像。
- `set_filename("robot_project.elf")`：输出文件名（供调试配置中的 `executable` 路径使用）。
- `add_deps("tar_oh_my_robot")`：依赖 OM 的聚合静态库目标。
- `add_rules(...)` 五条规则（binary 目标标准集，事实源见 `oh-my-robot/xmake/modules/binary_rules.lua`）：
  - `oh_my_robot.context`：注入构建上下文并决定工具链/编译参数。
  - `oh_my_robot.board_assets`：注入板级覆盖源与公共头（board 数据驱动）。
  - `oh_my_robot.image_convert`：生成 `.hex`/`.bin` 镜像。
  - `oh_my_robot.project_cfg`：注入工程配置（构建期配置/裁剪宏）。
  - `oh_my_robot.selfreg`：自注册源（`OM_INIT_*`）保活并注入框架弱 `main`。
- `add_files(path.join(...))`：指定入口源文件；当前示例使用 `oh-my-robot/samples/osal/osal_thread/main.c`。
- 需要器件库时，额外 `includes("oh-my-robot-devices")`、`add_deps("tar_oh_my_robot", "tar_awdevices")`，入口改为器件仓库示例；推荐直接用 `xmake init_workspace --devices=<path> --entry_repo=devices` 生成。

### 2.4 完成后验证
```powershell
xmake f -c --toolchain=gnu-rm -m debug
xmake
```

期望结果：
- 成功生成 `build/<profile>/robot_project.elf`，并生成 `compile_commands.json`。

### 2.5 常见新手错误
- `xmake: not found`：XMake 未安装或终端未重启。
- `includes("oh-my-robot")` 报错：当前目录不是项目根目录。
- `toolchain path not set`：继续第 3 章配置 `om_preset.lua` 的 `sdk/bin` 后重试。

## 3. 在项目根目录编写 `om_preset.lua`

文件位置：
- 项目根目录下的 `om_preset.lua`

作用：
- 固定本机的工具链路径、默认板卡、默认 OS。
- 避免每次 `xmake f` 都手工输入 `--sdk` 和 `--bin`。
- 这个文件只对当前项目根生效。

### 3.1 如果你没写过 Lua，先记住这 4 条
1. `key = value`：这是“配置项 = 配置值”。
2. `{ ... }`：这是一个配置表（可以理解为“配置集合”）。
3. 字符串要加引号：例如 `"gnu-rm"`。
4. `get_preset()` 是固定入口函数：请保留函数名不改。

### 3.2 先按步骤创建一个可用版本
步骤 1：如果你拿到的项目根目录里已经有 `om_preset.lua`，直接修改它即可。
步骤 2：如果当前项目根还没有 `om_preset.lua`，复制 [`oh-my-robot/om_preset.example.lua`](../om_preset.example.lua) 到项目根并命名为 `om_preset.lua`。
步骤 3：只改标记为“请改成你本机路径”的字段。

```lua
local preset = {
  board = {name = "rm-c-board"},
  os = {name = "freertos"},

  -- 默认工具链，第一次建议 gnu-rm
  toolchain_default = {
    name = "gnu-rm",
  },

  -- 这里是你本机工具链路径（通常必须修改）
  toolchain_presets = {
    ["gnu-rm"] = {
      sdk = "D:/Toolchains/gcc-arm-none-eabi-10.3-2021.10", -- 请改成你本机路径
      bin = "D:/Toolchains/gcc-arm-none-eabi-10.3-2021.10/bin", -- 请改成你本机路径
    },
    ["armclang"] = {
      sdk = "D:/Program Files/ProgramTools/Keil_v5/ARM/ARMCLANG", -- 选用 armclang 时请改
      bin = "D:/Program Files/ProgramTools/Keil_v5/ARM/ARMCLANG/bin", -- 选用 armclang 时请改
    },
  },

}

function get_preset()
  return preset
end
```

### 3.3 烧录与调试（外部工具）
- `om_preset.lua` 只负责 board/os/toolchain 选择与路径配置，不包含烧录/调试参数。
- 烧录/下载使用外部工具（J-Link Commander、OpenOCD、pyOCD 等）；XMake 只产出 `.elf` / `.hex` / `.bin`。
- 调试使用外部工具（VSCode Cortex-Debug 等）；armclang 推荐 `.elf` 作符号、`.hex` 作下载。

### 3.4 构建参数速查
参数优先级：命令行参数 > 项目根 `om_preset.lua` > 构建系统默认值。

| 字段 | 含义 | 通常是否需要修改 |
| --- | --- | --- |
| `board.name` | 选择板卡 | 一般不改（除非新增板卡） |
| `os.name` | 选择 OS/RTOS | 一般不改（当前常用 `freertos`） |
| `toolchain_default.name` | 默认工具链 | 可按习惯改 |
| `toolchain_presets.<name>.sdk` | 工具链根目录 | 必须改成你本机路径 |
| `toolchain_presets.<name>.bin` | 工具链可执行目录 | 必须改成你本机路径 |

### 3.5 写完后怎么确认没写错
```powershell
xmake f -c --toolchain=gnu-rm -m debug
xmake f -c --toolchain=armclang -m debug --semihosting=off
```

期望结果：不报 `sdk/bin not found`；armclang 不支持 `toolset_as=armasm`。

## 4. 配置 VSCode 调试（`.vscode/launch.json`）

### 4.1 为什么需要这个文件
- `xmake` 负责“构建产物”。
- `.vscode/launch.json` 负责“如何连接调试器、下载固件、启动调试”。
- 两者配合后，才能在 VSCode 里一键调试。

### 4.2 创建配置文件
步骤 1：在项目根目录创建目录 `.vscode`（如果不存在）。  
步骤 2：创建文件 `.vscode/launch.json`。  
步骤 3：粘贴以下模板并按注释改路径。

### 4.3 多调试器模板（可直接复制）
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "J-Link | STM32F407IG | gcc",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "jlink",
      "device": "STM32F407IG",
      "interface": "swd",
      "cwd": "${workspaceFolder}",
      "executable": "build/cross/arm/debug/robot_project.elf",
      "runToEntryPoint": "main",
      "svdFile": "${workspaceFolder}/oh-my-robot/platform/bsp/vendor/STM32/STM32F4/SVD/STM32F407.svd",
      "serverpath": "D:/Program Files/ProgramTools/SEGGER/Jlink/JLinkGDBServerCL.exe",
      "armToolchainPath": "D:/Toolchains/gcc-arm-none-eabi-10.3-2021.10/bin",
      "preLaunchCommands": [
        "monitor reset halt"
      ]
    },
    {
      "name": "J-Link | STM32F407IG | armclang",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "jlink",
      "device": "STM32F407IG",
      "interface": "swd",
      "cwd": "${workspaceFolder}",
      "executable": "build/cross/cortex-m4/debug/robot_project.elf",
      "runToEntryPoint": "main",
      "svdFile": "${workspaceFolder}/oh-my-robot/platform/bsp/vendor/STM32/STM32F4/SVD/STM32F407.svd",
      "serverpath": "D:/Program Files/ProgramTools/SEGGER/Jlink/JLinkGDBServerCL.exe",
      "armToolchainPath": "D:/Toolchains/gcc-arm-none-eabi-10.3-2021.10/bin",
      "overrideLaunchCommands": [
        "monitor reset",
        "restore build/cross/cortex-m4/debug/robot_project.hex",
        "monitor reset"
      ],
      "preLaunchCommands": [
        "monitor semihosting enable",
        "monitor semihosting breakonerror 0",
        "set mem inaccessible-by-default off"
      ]
    },
    {
      "name": "Debug (OpenOCD / DAPLink)",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "openocd",
      "cwd": "${workspaceFolder}",
      "executable": "build/cross/cortex-m4/debug/robot_project.elf",
      "serverpath": "D:/openocd-win/bin/openocd.exe",
      "configFiles": [
        "${workspaceFolder}/.vscode/daplink.cfg"
      ],
      "gdbPath": "D:/Toolchains/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gdb",
      "svdFile": "${workspaceFolder}/oh-my-robot/platform/bsp/vendor/STM32/STM32F4/SVD/STM32F407.svd",
      "runToEntryPoint": "main",
      "preLaunchTask": "xmake-build-debug",
      "showDevDebugOutput": "none"
    },
    {
      "name": "Attach (OpenOCD / DAPLink)",
      "type": "cortex-debug",
      "request": "attach",
      "servertype": "openocd",
      "cwd": "${workspaceFolder}",
      "executable": "build/cross/cortex-m4/debug/robot_project.elf",
      "serverpath": "D:/openocd-win/bin/openocd.exe",
      "configFiles": [
        "${workspaceFolder}/.vscode/daplink.cfg"
      ],
      "gdbPath": "D:/Toolchains/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gdb",
      "svdFile": "${workspaceFolder}/oh-my-robot/platform/bsp/vendor/STM32/STM32F4/SVD/STM32F407.svd",
      "showDevDebugOutput": "none"
    }
  ]
}
```

### 4.4 关键参数怎么填（新手版）
| 参数 | 作用 | 你通常需要改什么 |
| --- | --- | --- |
| `name` | 调试配置显示名 | 建议保留，便于区分调试器/工具链 |
| `type` | 调试器插件类型 | 固定 `cortex-debug` |
| `servertype` | GDB Server 类型 | J-Link 用 `jlink`；DAPLink 用 `openocd` |
| `device` | 目标芯片名（J-Link） | 芯片变化时改 |
| `executable` | 符号 ELF 路径 | 切换工具链或 profile 时改 |
| `serverpath` | GDB Server 可执行路径 | J-Link: `JLinkGDBServerCL.exe`；OpenOCD: `openocd.exe` |
| `configFiles` | OpenOCD 配置文件列表 | DAPLink 调试时必填 |
| `gdbPath` | `arm-none-eabi-gdb` 完整路径 | OpenOCD 链路需显式指定 |
| `armToolchainPath` | `arm-none-eabi-gdb` 所在目录 | J-Link 链路使用，按本机路径改 |
| `overrideLaunchCommands` | 自定义下载链路 | armclang 推荐 `restore ...hex` |
| `preLaunchCommands` | 调试前命令 | semihosting 场景按需调整 |
| `preLaunchTask` | 调试前构建任务 | 可选，用于自动编译后启动调试 |

### 4.5 常见坑与处理
- `Failed to launch GDB`：
  - J-Link 链路：检查 `armToolchainPath` 是否是 `arm-none-eabi-gdb` 所在目录。
  - OpenOCD 链路：检查 `gdbPath` 是否为 `arm-none-eabi-gdb` 完整路径。
- `JLinkGDBServerCL.exe` 找不到：
  - 检查 `serverpath` 是否为本机真实路径。
- OpenOCD 启动失败 / 探针无法连接：
  - 检查 `configFiles` 中 OpenOCD 配置文件路径是否正确。
  - 若有多探针，确保在 `.cfg` 文件中配置了 `adapter serial` 指定目标探针。
  - 执行 `openocd -f .vscode/daplink.cfg` 检查原生输出定位问题。
- `armclang` 下载后异常：
  - 优先使用 `restore ...robot_project.hex`，不要直接套用 `load`。
- 停在 semihosting 相关 BKPT：
  - 若构建使用 `--semihosting=on`，确保保留 `monitor semihosting enable`。
  - 若构建使用 `--semihosting=off`，可移除 semihosting 相关命令，仅保留 `set mem inaccessible-by-default off`。

### 4.6 与 `om_preset.lua` 的关系
- `om_preset.lua`：决定构建默认参数（例如工具链路径、board/os）；不包含烧录/调试参数。
- `.vscode/launch.json`：决定 VSCode 调试时如何连接并加载程序（外部工具配置）。
- `.vscode/daplink.cfg`：OpenOCD 配置文件，指定调试探针与目标芯片。
- 各文件路径都要指向你本机真实安装位置。

### 4.7 跨模块 `name` 映射关系（必须一致的部分）
| 概念 | 配置位置 | 示例 | 关系规则 |
| --- | --- | --- | --- |
| XMake 目标名 | `xmake.lua` 的 `target(“...”)` | `target(“robot_project”)` | 这是目标 ID |
| 输出文件名 | `xmake.lua` 的 `set_filename(“...”)` | `set_filename(“robot_project.elf”)` | 仅决定产物文件名 |
| 调试符号文件路径 | `.vscode/launch.json` 的 `executable` | `build/cross/arm/debug/robot_project.elf` | 必须指向 `set_filename` 生成的真实 ELF 路径 |
| OpenOCD 配置 | `.vscode/daplink.cfg` | `adapter serial 5627220232` | 指定 CMSIS-DAP 探针序列号与目标芯片 |
| 调试配置显示名 | `.vscode/launch.json` 的 `name` | `Debug (OpenOCD / DAPLink)` | 仅用于界面显示，与构建逻辑无绑定 |

快速判断是否填错：
- VSCode 报找不到可执行文件：先检查 `launch.json` 的 `executable` 是否对应 `set_filename(...)` 的实际输出路径。

## 5. 新手常见错误
- 工具链已安装但命令不可用：`PATH` 未生效，重开终端或在项目根 `om_preset.lua` 明确配置 `sdk/bin`。
- `xmake` 可以执行但编译失败：先执行 `xmake f -c ...` 重新配置，再执行 `xmake`。
- VSCode 没有代码跳转：先确认 Clangd 已启用，再确认项目根目录存在 `compile_commands.json`。
- 当前目录缺少 `xmake.lua` 或 `om_preset.lua`：检查你是否已经进入正确的项目根目录。
