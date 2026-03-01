--- @file oh_my_robot/platform/bsp/data/boards/rm-c-board.lua
--- @brief rm-c-board 板级数据
--- @details 描述板级源文件、头文件、组件选择与构建资源位置。

--- 板级数据结构
---@class BspBoard
---@field name string 板级名称
---@field chip string 芯片名称
---@field vendor string 供应商名称
---@field defines string[] 预处理宏
---@field includedirs string[] 头文件目录
---@field sources string[] 源文件路径
---@field override_sources string[] 强覆盖源文件（直接并入 binary）
---@field osal table<string, string> OS 配置路径映射
---@field startup table<string, string> 启动文件映射
---@field linkerscript table<string, string> 链接脚本映射
---@field components string[] 组件白名单
---@field component_overrides table<string, table> 组件覆盖配置
local board = {
    name = "rm-c-board",
    chip = "stm32f407xx",
    vendor = "stm32",
    defines = {},
    includedirs = {
        "boards/rm-c-board/include",
    },
    sources = {
        "boards/rm-c-board/source/core/bsp_cpu.c",
        "boards/rm-c-board/source/core/bsp_dwt.c",
        "boards/rm-c-board/source/peripherals/can/bsp_can_impl.c",
        "boards/rm-c-board/source/peripherals/serial/bsp_serial_impl.c",
        "boards/rm-c-board/source/peripherals/serial/bsp_serial_init.c",
        "boards/rm-c-board/source/port/om_port_hw.c",
    },
    override_sources = {
        -- 这些文件用于覆盖启动文件中的 weak ISR，必须直连最终 binary。
        "boards/rm-c-board/source/peripherals/can/bsp_can_it.c",
        "boards/rm-c-board/source/peripherals/serial/serial_it.c",
    },
    osal = {
        freertos = "boards/rm-c-board/osal/freertos",
    },
    startup = {
        ["gnu-rm"] = "boards/rm-c-board/startup/gcc/startup_stm32f407xx.s",
        ["armclang"] = "boards/rm-c-board/startup/arm/startup_stm32f407xx.s",
    },
    linkerscript = {
        ["gnu-rm"] = "boards/rm-c-board/linker/gcc/stm32f407ighx.ld",
        ["armclang"] = "boards/rm-c-board/linker/arm/stm32f407ighx.sct",
    },
    components = {
        "cmsis",
        "device",
        "hal",
        "svd",
    },
    component_overrides = {},
}

--- 获取板级数据
---@return BspBoard
function get()
    return board
end
