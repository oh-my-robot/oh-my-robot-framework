--- @file oh_my_robot/platform/bsp/data/chips/mspm0g3507.lua
--- @brief MSPM0G3507 芯片数据
--- @details Texas Instruments MSPM0G3507 — Cortex-M0+, 32MHz, 128KB Flash, 32KB SRAM.

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
    name = "mspm0g3507",
    vendor = "ti",
    arch = "cortex-m0plus",
    defines = {
        "CONFIG_MSPM0G3507",
        "DeviceFamily_MSPM0G350X",
        "__MSPM0G3507__",
    },
    includedirs = {},
    sources = {},
    components = {
        device = {
            includedirs = {
                "vendor/TI/MSPM0/m0p",
                "vendor/TI/MSPM0/m0p/ti/devices/msp/m0p",
                "vendor/TI/MSPM0/m0p/third_party/CMSIS/Core/Include",
            },
            headerfiles = {},
            sources = {
                "vendor/TI/MSPM0/m0p/ti/driverlib/dl_common.c",
                "vendor/TI/MSPM0/m0p/ti/driverlib/dl_gpio.c",
                "vendor/TI/MSPM0/m0p/ti/driverlib/dl_uart.c",
                "vendor/TI/MSPM0/m0p/ti/driverlib/dl_spi.c",
                "vendor/TI/MSPM0/m0p/ti/driverlib/dl_dma.c",
                "vendor/TI/MSPM0/m0p/ti/driverlib/dl_timer.c",
                "vendor/TI/MSPM0/m0p/ti/driverlib/dl_timerg.c",
                "vendor/TI/MSPM0/m0p/ti/driverlib/dl_timera.c",
            },
        },
    },
    startup = {
        ["gnu-rm"] = "vendor/TI/MSPM0/m0p/startup/gcc/startup_mspm0g350x_gcc.c",
        ["armclang"] = "vendor/TI/MSPM0/m0p/startup/keil/startup_mspm0g350x_uvision.s",
        ["tiarmclang"] = "vendor/TI/MSPM0/m0p/startup/ticlang/startup_mspm0g350x_ticlang.c",
    },
    linkerscript = {
        ["armclang"] = "vendor/TI/MSPM0/m0p/linker/keil/mspm0g3507.sct",
    },
    arch_traits = {
        cpu = "cortex-m0plus",
        thumb = true,
    },
}

--- 获取芯片数据
---@return BspChip
function get()
    return chip
end
