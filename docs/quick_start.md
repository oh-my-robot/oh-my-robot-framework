# OH-MY-ROBOT 快速开始（完全新手）

本指南面向第一次接触本项目的开发者，目标是：
- 先把环境装好并验证。
- 再在项目根目录先创建 `xmake.lua`。
- 然后编写 `om_preset.lua` 完成本机预设。

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
- 当前仓库调试链路默认使用 J-Link。
- DAPLink 为后续规划，当前版本不提供可用模板。

## 2. 在项目根目录编写 `xmake.lua`

文件位置：
- `xmake.lua`（项目根目录）

作用：
- 定义工程入口、构建模式、OM 规则与最终可执行目标。

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
    add_rules("oh_my_robot.context", "oh_my_robot.board_assets", "oh_my_robot.image_convert")
    add_files(path.join("oh-my-robot", "samples", "motor", "p1010b", "main.c")) -- 示例文件
target_end()
```

### 2.3 每行都在做什么（新手解释）
- `set_project("oh-my-robot")`：设置项目名。
- `set_xmakever("3.0.7")`：声明最低 XMake 版本。
- `add_rules("mode.debug", "mode.release")`：声明 Debug/Release 两种模式。
- `set_policy("build.optimization.lto", false)`：默认关闭顶层 LTO，由 OM 上下文规则按工具链决定是否启用。
- `add_rules("plugin.compile_commands.autoupdate", ...)`：自动生成 `compile_commands.json`（给 Clangd 用）。
- `includes("oh-my-robot")`：加载 OM 构建入口。
- `target("robot_project")`：定义最终编译目标的“目标名”（供 `xmake flash --target=...`、`flash.jlink.target` 使用）。
- `set_kind("binary")`：目标是可执行镜像。
- `set_filename("robot_project.elf")`：输出文件名（供调试配置中的 `executable` 路径使用）。
- `add_deps("tar_oh_my_robot")`：依赖 OM 的聚合静态库目标。
- `add_rules("oh_my_robot.context", "oh_my_robot.board_assets", "oh_my_robot.image_convert")`：注入上下文、板级资源并生成镜像格式。
- `add_files(path.join(...))`：指定入口源文件；当前示例使用 `oh-my-robot/samples/motor/p1010b/main.c`。

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
- `om_preset.lua`（项目根目录）

作用：
- 固定本机的工具链路径、默认板卡、默认 OS、烧录参数。
- 避免每次 `xmake f` 都手工输入 `--sdk` 和 `--bin`。

### 3.1 如果你没写过 Lua，先记住这 4 条
1. `key = value`：这是“配置项 = 配置值”。
2. `{ ... }`：这是一个配置表（可以理解为“配置集合”）。
3. 字符串要加引号：例如 `"gnu-rm"`。
4. `get_preset()` 是固定入口函数：请保留函数名不改。

### 3.2 先按步骤创建一个可用版本
步骤 1：在项目根目录新建 `om_preset.lua`。
步骤 2：粘贴以下模板。
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

  -- 烧录器预设：当前仅支持 jlink
  flash = {
    jlink = {
      device = "STM32F407IG",
      interface = "swd",
      speed = 4000,
      program = "D:/Program Files/ProgramTools/SEGGER/Jlink/JLink.exe", -- 找不到 JLink 时请改
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

### 3.3 重要说明：当前调试器支持范围
- `om_preset.flash` 当前只消费 `flash.jlink`。
- 当前版本不支持 `flash.daplink`。
- 后续计划支持 DAPLink（当前处于规划阶段，尚未接入构建任务）。

### 3.4 每个常用参数是什么意思
参数优先级（非常重要）：
- 命令行参数 > `om_preset.lua` > 构建系统默认值

当前仓库可选值（按现有实现）：
- `board.name`：`rm-c-board`
- `os.name`：`freertos`
- `toolchain_default.name`：`gnu-rm` 或 `armclang`

| 字段 | 含义 | 你是否通常需要修改 |
| --- | --- | --- |
| `board.name` | 选择板卡 | 一般不改（除非新增板卡） |
| `os.name` | 选择 OS/RTOS | 一般不改（当前常用 `freertos`） |
| `toolchain_default.name` | 默认使用哪个工具链 | 可按习惯改 |
| `toolchain_presets.<name>.sdk` | 工具链根目录 | 通常必须改成你本机路径 |
| `toolchain_presets.<name>.bin` | 工具链可执行目录 | 通常必须改成你本机路径 |
| `flash.jlink.device` | 芯片名（J-Link 用） | 芯片变化时改 |
| `flash.jlink.interface` | 调试接口（如 `swd`） | 一般不改 |
| `flash.jlink.speed` | 下载速度（kHz） | 速度不稳时改 |
| `flash.jlink.program` | `JLink.exe` 路径 | 自动发现失败时改 |
| `flash.jlink.target` | 烧录目标名（XMake 目标名） | 必须与 `target("...")` 一致 |
| `flash.jlink.firmware` | 手工指定固件路径 | 一般留空，自动选产物 |
| `flash.jlink.prefer_hex` | 自动选择固件时优先 HEX | 一般保留 `true` |
| `flash.jlink.reset` | 烧录前后是否复位 | 一般保留 `true` |
| `flash.jlink.run` | 烧录后是否运行 | 调试停机时可改 |
| `flash.jlink.native_output` | 是否显示原生 CLI 输出 | 排错时改为 `true` |

兼容项说明：
- `flash.file` 与 `flash.firmware` 都可识别，但新配置建议只用 `flash.jlink.firmware`。

### 3.5 写完后怎么确认没写错
```powershell
xmake f -c --toolchain=gnu-rm -m debug
xmake f -c --toolchain=armclang -m debug --semihosting=off
```

期望结果：
- 不报 `sdk/bin not found`。
- 若报 `armclang toolset_as unsupported`：请不要手工指定 `toolset_as=armasm`，当前版本仅支持 `toolset_as=armclang`。
- 若需要检查烧录器输出：
```powershell
xmake flash --native_output=true
```

### 3.6 `flash.jlink.target` 到底要填什么（高频混淆）
- `flash.jlink.target` 填的是 **XMake 目标名**，不是文件名。
- 在本项目里，它应当与 `xmake.lua` 里的 `target("robot_project")` 保持一致。
- 它不等于 `set_filename("robot_project.elf")` 里的文件名。

## 4. 配置 VSCode 调试（`.vscode/launch.json`）

### 4.1 为什么需要这个文件
- `xmake` 负责“构建产物”。
- `.vscode/launch.json` 负责“如何连接调试器、下载固件、启动调试”。
- 两者配合后，才能在 VSCode 里一键调试。

### 4.2 创建配置文件
步骤 1：在项目根目录创建目录 `.vscode`（如果不存在）。  
步骤 2：创建文件 `.vscode/launch.json`。  
步骤 3：粘贴以下模板并按注释改路径。

### 4.3 双工具链模板（可直接复制）
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
    }
  ]
}
```

### 4.4 关键参数怎么填（新手版）
| 参数 | 作用 | 你通常需要改什么 |
| --- | --- | --- |
| `name` | 调试配置显示名 | 建议保留，便于区分 gcc/armclang |
| `type` | 调试器插件类型 | 固定 `cortex-debug` |
| `servertype` | GDB Server 类型 | 当前固定 `jlink` |
| `device` | 目标芯片名 | 芯片变化时改 |
| `executable` | 符号 ELF 路径 | 切换工具链或 profile 时改 |
| `serverpath` | `JLinkGDBServerCL.exe` 路径 | 通常按本机安装路径改 |
| `armToolchainPath` | `arm-none-eabi-gdb` 所在目录 | 按本机路径改 |
| `overrideLaunchCommands` | 自定义下载链路 | armclang 推荐 `restore ...hex` |
| `preLaunchCommands` | 调试前命令 | semihosting 场景按需调整 |

### 4.5 常见坑与处理
- `Failed to launch GDB`：
  - 检查 `armToolchainPath` 是否是 `arm-none-eabi-gdb` 所在目录。
- `JLinkGDBServerCL.exe` 找不到：
  - 检查 `serverpath` 是否为本机真实路径。
- `armclang` 下载后异常：
  - 优先使用 `restore ...robot_project.hex`，不要直接套用 `load`。
- 停在 semihosting 相关 BKPT：
  - 若构建使用 `--semihosting=on`，确保保留 `monitor semihosting enable`。
  - 若构建使用 `--semihosting=off`，可移除 semihosting 相关命令，仅保留 `set mem inaccessible-by-default off`。

### 4.6 与 `om_preset.lua` 的关系
- `om_preset.lua`：决定构建与烧录默认参数（例如工具链路径、J-Link 参数）。
- `.vscode/launch.json`：决定 VSCode 调试时如何连接并加载程序。
- 两边路径都要指向你本机真实安装位置。

### 4.7 跨模块 `name` 映射关系（必须一致的部分）
| 概念 | 配置位置 | 示例 | 关系规则 |
| --- | --- | --- | --- |
| XMake 目标名 | `xmake.lua` 的 `target("...")` | `target("robot_project")` | 这是“目标 ID”，给 `xmake flash --target=...` 与 `flash.jlink.target` 使用 |
| 烧录目标名 | `om_preset.lua` 的 `flash.jlink.target` | `target = "robot_project"` | 必须与 `target("...")` 完全一致 |
| 输出文件名 | `xmake.lua` 的 `set_filename("...")` | `set_filename("robot_project.elf")` | 仅决定产物文件名，不参与目标查找 |
| 调试符号文件路径 | `.vscode/launch.json` 的 `executable` | `build/cross/arm/debug/robot_project.elf` | 必须指向 `set_filename` 生成的真实 ELF 路径 |
| 调试配置显示名 | `.vscode/launch.json` 的 `name` | `J-Link | STM32F407IG | gcc` | 仅用于界面显示，与构建/烧录逻辑无绑定 |

快速判断是否填错：
- `xmake flash` 报 “target not found”：先检查 `flash.jlink.target` 与 `target("...")` 是否一致。
- VSCode 报找不到可执行文件：先检查 `launch.json` 的 `executable` 是否对应 `set_filename(...)` 的实际输出路径。

## 5. 新手常见错误
- 工具链已安装但命令不可用：`PATH` 未生效，重开终端或在 `om_preset.lua` 明确配置 `sdk/bin`。
- `xmake` 可以执行但编译失败：先执行 `xmake f -c ...` 重新配置，再执行 `xmake`。
- VSCode 没有代码跳转：先确认 Clangd 已启用，再确认项目根目录存在 `compile_commands.json`。
