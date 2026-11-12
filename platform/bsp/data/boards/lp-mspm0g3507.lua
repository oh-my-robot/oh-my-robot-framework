--- @file oh_my_robot/platform/bsp/data/boards/lp-mspm0g3507.lua
--- @brief LP_MSPM0G3507 板级数据
--- @details TI LaunchPad LP_MSPM0G3507 — MSPM0G3507 + XDS110。

local board = {
    name = "lp-mspm0g3507",
    chip = "mspm0g3507",
    vendor = "ti",
    defines = {"OM_BOARD_LP_MSPM0G3507"}, -- 板身份宏（编译级；boardcfg 板守卫/多板条件逻辑的事实源，见 ADR-0017）
    includedirs = {
        "boards/lp-mspm0g3507/include",
        "boards/lp-mspm0g3507/source",
    },
    sources = {
        "boards/lp-mspm0g3507/source/bsp_cpu.c",
        "boards/lp-mspm0g3507/source/port/om_port_hw.c",
        "boards/lp-mspm0g3507/source/port/om_port_ptls_stub.c",
        "boards/lp-mspm0g3507/source/peripherals/gpio/bsp_gpio_impl.c",
        "boards/lp-mspm0g3507/source/peripherals/gpio/bsp_gpio_it.c",
        "boards/lp-mspm0g3507/source/peripherals/serial/bsp_serial_impl.c",
        "boards/lp-mspm0g3507/source/peripherals/serial/bsp_serial_init.c",
        "boards/lp-mspm0g3507/source/peripherals/serial/serial_it.c",
        "boards/lp-mspm0g3507/source/peripherals/spi/bsp_spi_impl.c",
        "boards/lp-mspm0g3507/source/peripherals/spi/bsp_spi_init.c",
        "boards/lp-mspm0g3507/source/peripherals/spi/bsp_spi_it.c",
        "boards/lp-mspm0g3507/source/peripherals/pwm/bsp_pwm_impl.c",
        "boards/lp-mspm0g3507/source/peripherals/pwm/bsp_pwm_init.c",
        "boards/lp-mspm0g3507/source/peripherals/pwm/bsp_tima_pwm_impl.c",
        "boards/lp-mspm0g3507/source/peripherals/pwm/bsp_tima_pwm_init.c",
        "boards/lp-mspm0g3507/source/ti_msp_dl_config.c",
    },
    override_sources = {},
    osal = {
        freertos = "boards/lp-mspm0g3507/osal/freertos",
    },
    startup = {},
    linkerscript = {
        ["gnu-rm"] = "boards/lp-mspm0g3507/linker/gcc/mspm0g3507.ld",
    },
    components = { "device" },
    component_overrides = {},
}

function get()
    return board
end
