# OM XMake 内置任务手册

## 1. 目标与范围
本手册用于说明 OM 内置任务的职责、参数与使用方式。当前仅包含 `xmake flash`（J-Link 烧录）。

## 2. 参数优先级
任务参数的优先级统一为：
1) 命令行参数
2) 配置缓存（`xmake f` 持久化后的 config）
3) `om_preset.lua` 中的预设
4) 任务内置默认值

## 3. 任务列表
### 3.1 `xmake flash`
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
