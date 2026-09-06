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
    defines = {"OM_BOARD_RM_C"}, -- 板身份宏（编译级；boardcfg 板守卫/多板条件逻辑的事实源，见 ADR-0017）
    includedirs = {
        "boards/rm-c-board/include",
        "arch/cortex-m",  -- 内核架构级共享头（bsp_dwt.h）
    },
    sources = {
        "boards/rm-c-board/source/core/bsp_cpu.c",
        "arch/cortex-m/bsp_dwt.c",     -- 内核架构级共享（DWT 周期计数器）
        "arch/cortex-m/om_port_hw.c",  -- 内核架构级共享（Cortex-M 临界区）
    },
    override_sources = {
        -- 这些文件用于覆盖启动文件中的 weak ISR，必须直连最终 binary。
        -- CAN 的 ISR 已上移为共享适配层（板瘦身），路径指向 vendor adapters。
        "vendor/STM32/STM32F4/adapters/can/bsp_can_f4_it.c",
        "vendor/STM32/STM32F4/adapters/gpio/bsp_gpio_f4_it.c",
        "vendor/STM32/STM32F4/adapters/serial/bsp_serial_f4_it.c",
    },
    selfreg_sources = {
        -- OM_INIT 自注册入口：直连 binary，保证 .om_init 回调存活。
        -- bsp_cpu.c 含 om_board_self_init（BOARD prio0），且其强 HardFault_Handler
        -- 需覆盖 startup.s 的 weak 版——故必须直连，不能留 in tar_board 任由抽取。
        -- 板级外设源（source/peripherals/**.c）由 inputs.lua 自动 glob 发现，无需在此列举。
        -- 共享适配层实现（含 OM_INIT 自注册）不在自动 glob 范围，须显式引用（opt-in 铁律）。
        "boards/rm-c-board/source/core/bsp_cpu.c",
        "arch/cortex-m/om_hardfault.c", -- 架构共享强 HardFault_Handler（覆盖 startup weak，须直连），ADR-0014
        "vendor/STM32/STM32F4/adapters/can/bsp_can_f4.c",
        "vendor/STM32/STM32F4/adapters/gpio/bsp_gpio_f4.c",
        "vendor/STM32/STM32F4/adapters/serial/bsp_serial_f4.c",
        "vendor/STM32/STM32F4/adapters/serial/bsp_serial_f4_init.c",
        "vendor/STM32/STM32F4/adapters/pwm/bsp_pwm_f4.c",
        "vendor/STM32/STM32F4/adapters/flash/bsp_flash_f4.c",
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
