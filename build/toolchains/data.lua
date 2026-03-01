--- @file oh_my_robot/build/toolchains/data.lua
--- @brief OM 工具链数据
--- @details 仅存放工具链配置数据，供描述域与脚本域复用。

--- 工具链条目定义
---@class ToolchainEntry
---@field kind string 工具链类型（builtin/custom）
---@field plat string|nil 平台名称
---@field linker_flag string|nil 链接脚本参数前缀
---@field linker_accepts_arch_flags boolean|nil 链接器是否接受 arch ldflags
---@field sdk string|nil SDK 根目录
---@field bin string|nil 工具链可执行文件目录
---@field arch string|nil 默认架构
---@field flags table<string, table>|nil 工具链参数映射（common/cflags/cxflags/asflags/ldflags）
---@field toolset table<string, string>|nil 工具链工具集（cc/cxx/as/ld/ar/ranlib/strip/objcopy）
---@field image table<string, table>|nil 镜像转换工具配置（hex/bin）

--- 工具链配置定义
---@class ToolchainsConfig
---@field default string 默认工具链名称
---@field toolchains table<string, ToolchainEntry> 工具链映射表

--- 工具链配置数据
---@type ToolchainsConfig
local toolchain_config = {
    default = "gnu-rm",
    toolchains = {
        ["gnu-rm"] = {
            kind = "builtin",
            plat = "cross",
            arch = "arm",
            linker_flag = "-T",
            linker_accepts_arch_flags = true,
            toolset = {
                cc = "arm-none-eabi-gcc",
                cxx = "arm-none-eabi-g++",
                as = "arm-none-eabi-gcc",
                ld = "arm-none-eabi-gcc",
                ar = "arm-none-eabi-gcc-ar",
                ranlib = "arm-none-eabi-gcc-ranlib",
                strip = "arm-none-eabi-strip",
                objcopy = "arm-none-eabi-objcopy",
            },
            image = {
                hex = {kind = "objcopy", tool = "arm-none-eabi-objcopy", format = "ihex"},
                bin = {kind = "objcopy", tool = "arm-none-eabi-objcopy", format = "binary"},
            },
        },
        ["armclang"] = {
            kind = "builtin",
            plat = "cross",
            linker_flag = "--scatter=",
            linker_accepts_arch_flags = false,
            toolset = {
                cc = "armclang",
                cxx = "armclang",
                as = "armclang",
                ld = "armclang",
                ar = "armar",
                ranlib = "armar",
                objcopy = "fromelf",
            },
            image = {
                hex = {kind = "fromelf", tool = "fromelf", format = "i32"},
                bin = {kind = "fromelf", tool = "fromelf", format = "bin"},
            },
        },
    },
}

--- 描述域读取的全局数据
---@type ToolchainsConfig
om_toolchains_data = toolchain_config

--- 返回工具链配置（脚本域使用）
---@return ToolchainsConfig
function get()
    return toolchain_config
end
