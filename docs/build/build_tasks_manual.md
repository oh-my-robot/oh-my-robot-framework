# OM XMake 内置任务手册

## 1. 目标与范围
本手册用于说明 OM 内置任务的职责、参数与使用方式。当前包含：
- `xmake init_workspace`：为当前 `oh-my-robot` checkout 生成轻量项目壳。

烧录与调试不在 XMake 内承担：构建只产出 `.elf` / `.hex` / `.bin` 与 `compile_commands.json`，下载/调试交给外部工具（J-Link Commander、OpenOCD、pyOCD、Cortex-Debug 等）。

## 2. 参数优先级
任务参数：命令行参数 > 任务内置默认值。`init_workspace` 的 `--preset` 未指定时默认取框架根 `om_preset.example.lua`。

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

同时接入器件库：
```sh
xmake init_workspace --output="../workspaces/oh-my-robot/16-build-env" \
    --devices="../oh-my-robot-devices" \
    --entry_repo=devices \
    --entry="samples/motor/p1010b/main.c"
```

**参数说明**：
| 参数 | 说明 | 默认值 | 备注 |
| --- | --- | --- | --- |
| `--output` | 输出目录 | 无 | 必填，表示生成项目壳的位置 |
| `--framework` | 框架根目录 | 当前 `oh-my-robot` checkout | 通常不用改 |
| `--devices` | `oh-my-robot-devices` checkout 路径 | 无 | 指定后生成第二个目录链接并链接 `tar_awdevices` |
| `--entry_repo` | 入口所属仓库 | `framework` | `framework` / `devices`；`devices` 需同时给 `--devices` |
| `--preset` | 要拷贝到项目壳的 preset 源文件 | `oh-my-robot/om_preset.example.lua` | 支持绝对路径和相对当前项目目录的路径 |
| `--project_name` | 顶层项目名 | 输出目录名 | 仅影响 `set_project(...)` |
| `--target` | 顶层 XMake 目标名 | `robot_project` | 供调试配置引用 |
| `--entry` | 相对入口仓库的入口文件 | `samples/osal/osal_thread/main.c` | 用于生成默认顶层目标 |
| `--force` | 是否允许覆盖已存在文件 | `false` | `true`/`false` |

**生成内容**：
- `oh-my-robot/`：指向当前框架 checkout 的本地目录链接/junction。
- `oh-my-robot-devices/`：指定 `--devices` 时生成的器件库目录链接/junction。
- `xmake.lua`：最小顶层工程壳，通过固定别名加载框架；接入器件库时同时加载器件库并依赖 `tar_awdevices`。
- `om_preset.lua`：项目级 preset 模板，默认从框架根的 [`oh-my-robot/om_preset.example.lua`](../../om_preset.example.lua) 复制而来。
- `README.md`：说明项目根 preset 的推荐用法。
- `.gitignore`：忽略本地构建产物与目录链接。

**预设建议**：
1) 不传 `--preset` 时，任务会从 [`oh-my-robot/om_preset.example.lua`](../../om_preset.example.lua) 复制模板。
2) 传 `--preset=<path>` 时，任务会把该文件复制到项目壳根目录并命名为 `om_preset.lua`。
3) 若目标 `om_preset.lua` 已存在，只有在 `--force=true` 时才会覆盖。

### 3.2 烧录与调试（外部工具）
XMake 不再内置烧录/调试任务。构建只负责产出 `.elf` / `.hex` / `.bin` 与 `compile_commands.json`；烧录、下载、调试请使用外部工具（J-Link Commander、OpenOCD、pyOCD、Cortex-Debug 等）。

## 4. `om_preset.lua` 预设示例
可直接参考 [`oh-my-robot/om_preset.example.lua`](../../om_preset.example.lua)。

```lua
board = {name = "rm-c-board"}
os = {name = "freertos"}
toolchain_default = {name = "gnu-rm"}
toolchain_presets = {
  ["gnu-rm"] = {
    sdk = "D:/Toolchains/gcc-arm-none-eabi",
    bin = "D:/Toolchains/gcc-arm-none-eabi/bin",
  },
}
```
