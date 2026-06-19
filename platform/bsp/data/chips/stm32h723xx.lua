--- @file oh_my_robot/platform/bsp/data/chips/stm32h723xx.lua
--- @brief STM32H723 芯片数据

local chip = {
    name = "stm32h723xx",
    vendor = "stm32",
    arch = "cortex-m7",
    defines = {
        "STM32H723xx",
    },
    includedirs = {},
    sources = {},
    components = {
        device = {
            includedirs = {
                "vendor/STM32/STM32H7/STM32H7xx/Include",
            },
            headerfiles = {
                "vendor/STM32/STM32H7/STM32H7xx/Include/stm32h7xx.h",
                "vendor/STM32/STM32H7/STM32H7xx/Include/stm32h723xx.h",
                "vendor/STM32/STM32H7/STM32H7xx/Include/system_stm32h7xx.h",
            },
            sources = {
                "vendor/STM32/STM32H7/STM32H7xx/Source/system_stm32h7xx.c",
            },
        },
        hal = {
            defines = {
                "USE_HAL_DRIVER",
            },
            includedirs = {
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Inc",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Inc/Legacy",
            },
            sources = {
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_adc.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_adc_ex.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_fdcan.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi_ex.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c",
                "vendor/STM32/STM32H7/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c",
            },
        },
        svd = {
            extrafiles = {},
        },
    },
    startup = {},
    linkerscript = {},
    arch_traits = {
        cpu = "cortex-m7",
        thumb = true,
        fpu = "fpv5-d16",
        float_abi = "hard",
    },
}

function get()
    return chip
end
