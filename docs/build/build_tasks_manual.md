# OM XMake 内置任务手册

## 1. 目标与范围
本手册用于说明 OM 内置任务的职责、参数与使用方式。当前包含：
- `xmake init_workspace`：为当前 `oh-my-robot` checkout 生成轻量项目壳。
- `xmake flash`：通过 J-Link Commander 烧录目标固件。

## 2. 参数优先级
任务参数的优先级统一为：
1) 命令行参数
2) 配置缓存（`xmake f` 持久化后的 config）
3) 项目根 `om_preset.lua`
4) 任务内置默认值

## 3. 任务列表
### 3.1 `xmake init_workspace`
**职责**：为当前 `oh-my-robot` worktree 或 checkout 生成一个最小可用的顶层工程目录。

**解决的问题**：
- worktree 里只有框架源码，没有顶层 `xmake.lua`。
- 不希望为每个 worktree 手工复制完整项目环境。
- 需要一个可独立编译完整应用镜像的轻量“项目壳”。

**命令示例**：
```sh
xmake init_workspace --output="../workspaces/oh-my-robot/16-build-env"
```

**参数说明**：
| 参数 | 说明 | 默认值 | 备注 |
| --- | --- | --- | --- |
| `--output` | 输出目录 | 无 | 必填，表示生成项目壳的位置 |
| `--framework` | 框架根目录 | 当前 `oh-my-robot` checkout | 通常不用改 |
| `--preset` | 要拷贝到项目壳的 preset 源文件 | `oh-my-robot/om_preset.example.lua` | 支持绝对路径和相对当前项目目录的路径 |
| `--project_name` | 顶层项目名 | 输出目录名 | 仅影响 `set_project(...)` |
| `--target` | 顶层 XMake 目标名 | `robot_project` | 需与后续烧录目标保持一致 |
| `--entry` | 相对框架根的入口文件 | `samples/motor/p1010b/main.c` | 用于生成默认顶层目标 |
| `--force` | 是否允许覆盖已存在文件 | `false` | `true`/`false` |

**生成内容**：
- `oh-my-robot/`：指向当前框架 checkout 的本地目录链接/junction。
- `xmake.lua`：最小顶层工程壳，通过固定别名 `oh-my-robot` 加载框架。
- `om_preset.lua`：项目级 preset 模板，默认从框架根的 [`oh-my-robot/om_preset.example.lua`](../../om_preset.example.lua) 复制而来。
- `README.md`：说明项目根 preset 的推荐用法。
- `.gitignore`：忽略本地构建产物。

**预设建议**：
1) 不传 `--preset` 时，任务会从 [`oh-my-robot/om_preset.example.lua`](../../om_preset.example.lua) 复制模板。
2) 传 `--preset=<path>` 时，任务会把该文件复制到项目壳根目录并命名为 `om_preset.lua`。
3) 若目标 `om_preset.lua` 已存在，只有在 `--force=true` 时才会覆盖。

### 3.2 `xmake flash`
**职责**：使用 J-Link Commander 对目标固件进行烧录。

**前置条件**：
- 工程已构建，且目标产物存在（默认使用 `target:targetfile()` 或生成的 `.hex`）。
- 已安装 J-Link 软件包，并配置可执行路径。

**命令示例**：
```sh
xmake flash --device=STM32F407IG --interface=swd --speed=4000 --program="D:/Program Files/ProgramTools/SEGGER/Jlink/JLink.exe"
```

**参数说明**：
| 参数 | 说明 | 默认值 | 预设字段 | 备注 |
| --- | --- | --- | --- | --- |
| `--device` | 目标芯片名称（J-Link 设备名） | `STM32F407IG` | `flash.jlink.device` | 必须为 J-Link 支持的设备名 |
| `--interface` | 调试接口类型 | `swd` | `flash.jlink.interface` | 可选：`swd` / `jtag` |
| `--speed` | 接口速度（kHz） | `4000` | `flash.jlink.speed` | 可使用 `auto`（若 J-Link 支持） |
| `--program` | J-Link Commander 可执行文件路径 | 无 | `flash.jlink.program` | 必填，否则自动查找失败时会报错 |
| `--target` | XMake 目标名称 | `robot_project` | `flash.jlink.target` | 用于自动推导产物路径 |
| `--firmware` | 固件文件路径（覆盖自动推导） | 无 | `flash.jlink.firmware` | 必须显式包含 `.hex` / `.elf` / `.bin` |
| `--prefer_hex` | 自动推导时优先使用 HEX | `true` | `flash.jlink.prefer_hex` | 仅在未指定 `--firmware` 时生效 |
| `--reset` | 烧录前后是否复位 | `true` | `flash.jlink.reset` | `true`/`false` |
| `--run` | 烧录完成后是否运行 | `true` | `flash.jlink.run` | `true`/`false` |
| `--native_output` | 是否透传烧录器原生输出 | `false` | `flash.jlink.native_output` | `true`/`false` |

**产物选择策略**：
1) 若指定 `--firmware` 或 `flash.jlink.firmware`（或兼容字段 `flash.jlink.file`），直接使用该文件（必须带后缀）。
2) 否则根据 `--target`/`flash.jlink.target` 推导产物。
3) 当 `prefer_hex=true` 时，若存在 `.hex` 优先使用；否则回退到 `.elf`。

**输出与行为**：
- 任务会在 `build/<profile>/oh-my-robot/flash/` 下生成临时的 J-Link 命令文件。
- 烧录结束后 J-Link 自动退出。
- 若预设中的 toolchain 与配置不一致，会在烧录前输出警告并以配置为准。
- 成功执行后会输出实际使用的可执行文件路径、固件路径与 toolchain。
- 当 `native_output=true` 时，终端会透传 J-Link Commander 的原生输出。
- 当前已知限制：使用 `--firmware` 指向 `.elf` 时可能无法实际烧录，请优先使用 `.hex`。

## 4. `om_preset.lua` 预设示例
可直接参考 [`oh-my-robot/om_preset.example.lua`](../../om_preset.example.lua)。

```lua
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
}
```
