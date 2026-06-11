--- @file oh_my_robot/platform/bsp/data/boards/dm-mc-board.lua
--- @brief dm-mc-board 板级数据
--- @details 达妙 DM-MC02 / DM-MC-Board02，STM32H723VGTx。

local board = {
    name = "dm-mc-board",
    chip = "stm32h723xx",
    vendor = "stm32",
    defines = {},
    includedirs = {
        "boards/dm-mc-board/include",
    },
    sources = {
        "boards/dm-mc-board/source/core/bsp_cpu.c",
        "boards/dm-mc-board/source/core/bsp_dwt.c",
        "boards/dm-mc-board/source/core/bsp_mpu.c",
        "boards/dm-mc-board/source/peripherals/can/bsp_can_impl.c",
        "boards/dm-mc-board/source/peripherals/serial/bsp_serial_impl.c",
        "boards/dm-mc-board/source/peripherals/serial/bsp_serial_init.c",
        "boards/dm-mc-board/source/port/om_port_hw.c",
    },
    override_sources = {
        "boards/dm-mc-board/source/peripherals/serial/serial_it.c",
    },
    osal = {
        freertos = "boards/dm-mc-board/osal/freertos",
    },
    startup = {
        ["gnu-rm"] = "boards/dm-mc-board/startup/gcc/startup_stm32h723xx.s",
        ["armclang"] = "boards/dm-mc-board/startup/arm/startup_stm32h723xx.s",
    },
    linkerscript = {
        ["gnu-rm"] = "boards/dm-mc-board/linker/gcc/stm32h723vg.ld",
        ["armclang"] = "boards/dm-mc-board/linker/arm/stm32h723vg.sct",
    },
    components = {
        "cmsis",
        "device",
        "hal",
        "svd",
    },
    component_overrides = {},
}

function get()
    return board
end
