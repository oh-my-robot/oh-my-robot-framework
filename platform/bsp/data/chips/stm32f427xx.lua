--- @file oh_my_robot/platform/bsp/data/chips/stm32f427xx.lua
--- @brief STM32F427 芯片数据
--- @details 描述芯片级架构、宏定义、组件与构建参数。

--- 架构 traits
---@class BspArchTraits
---@field cpu string CPU 名称
---@field thumb boolean|nil 是否使用 Thumb
---@field fpu string|nil FPU 类型
---@field float_abi string|nil 浮点 ABI

--- 芯片数据结构
---@class BspChip
---@field name string 芯片名称
---@field vendor string 供应商名称
---@field arch string 架构名称
---@field defines string[] 预处理宏
---@field includedirs string[] 头文件目录
---@field sources string[] 源文件路径
---@field components table<string, table> 组件映射表
---@field startup table<string, string> 启动文件映射
---@field linkerscript table<string, string> 链接脚本映射
---@field arch_traits BspArchTraits 架构 traits
local chip = {
    name = "stm32f427xx",
    vendor = "stm32",
    arch = "cortex-m4",
    defines = {
        "STM32F427xx",
    },
    includedirs = {},
    sources = {},
    components = {
        device = {
            includedirs = {
                "vendor/STM32/STM32F4/STM32F4xx/Include",
            },
            headerfiles = {
                "vendor/STM32/STM32F4/STM32F4xx/Include/stm32f4xx.h",
                "vendor/STM32/STM32F4/STM32F4xx/Include/stm32f427xx.h",
                "vendor/STM32/STM32F4/STM32F4xx/Include/system_stm32f4xx.h",
            },
            sources = {
                "vendor/STM32/STM32F4/STM32F4xx/Source/system_stm32f4xx.c",
            },
        },
        hal = {
            defines = {
                "USE_HAL_DRIVER",
            },
            includedirs = {
                "vendor/STM32/STM32F4/STM32F4xx_HAL_Driver/Inc",
                "vendor/STM32/STM32F4/STM32F4xx_HAL_Driver/Inc/Legacy",
            },
            sources = {
                "vendor/STM32/STM32F4/STM32F4xx_HAL_Driver/Src/*.c",
            },
        },
        svd = {
            extrafiles = {
                "vendor/STM32/STM32F4/SVD/STM32F427.svd",
            },
        },
    },
    startup = {
        ["gnu-rm"] = "vendor/STM32/STM32F4/STM32F4xx/Source/gcc/startup_stm32f427xx.s",
        ["armclang"] = "vendor/STM32/STM32F4/STM32F4xx/Source/arm/startup_stm32f427xx.s",
    },
    linkerscript = {},
    arch_traits = {
        cpu = "cortex-m4",
        thumb = true,
        fpu = "fpv4-sp-d16",
        float_abi = "hard",
    },
}

--- 获取芯片数据
---@return BspChip
function get()
    return chip
end
