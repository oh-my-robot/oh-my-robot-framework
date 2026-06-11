#include "bsp.h"
#include "bsp_mpu.h"
#include "core/om_interrupt.h"

static uint8_t g_hal_tick_use_dwt = 0U;

static void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

static void Board_DelayMs(float ms)
{
    DWT_Delay(ms / 1000.0f);
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 2;
    RCC_OscInitStruct.PLL.PLLN = 40;
    RCC_OscInitStruct.PLL.PLLP = 1;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
        Error_Handler();
    }

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART16910 | RCC_PERIPHCLK_USART234578 |
                                               RCC_PERIPHCLK_SPI6 | RCC_PERIPHCLK_FDCAN;
    PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16910CLKSOURCE_D2PCLK2;
    PeriphClkInitStruct.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    PeriphClkInitStruct.Spi6ClockSelection = RCC_SPI6CLKSOURCE_HSE;
    /* Keep the existing system clock tree intact and source FDCAN from PLL1Q
     * so CAN FD data phase timing can use an exact 120 MHz kernel clock. */
    PeriphClkInitStruct.FdcanClockSelection = RCC_FDCANCLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }
}

static OmBoardInterface g_om_board_interface = {
    .errhandler = Error_Handler,
    .reset = HAL_NVIC_SystemReset,
    .getCpuTimeS = DWT_GetTimeline_s,
    .getCpuTimeMs = DWT_GetTimeline_ms,
    .getCpuTimeUs = DWT_GetTimeline_us,
    .getDeltaCpuTimeS = DWT_GetDeltaT,
    .delayMs = Board_DelayMs,
};

uint32_t HAL_GetTick(void)
{
    if (g_hal_tick_use_dwt != 0U)
    {
        return (uint32_t)(DWT_GetTimeline_us() / 1000ULL);
    }

    return uwTick;
}

void om_board_init(void)
{
    bsp_mpu_config();
    SCB_EnableICache();
    SCB_EnableDCache();

    HAL_Init();
    SystemClock_Config();
    NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);

    om_cpu_register(__OM_CPU_FREQ_MHZ, &g_om_board_interface);
    DWT_Init(__OM_CPU_FREQ_MHZ);
    g_hal_tick_use_dwt = 1U;

    bsp_serial_register();
    bsp_can_register();
}

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1)
    {
    }
}

void MemManage_Handler(void)
{
    while (1)
    {
    }
}

void BusFault_Handler(void)
{
    while (1)
    {
    }
}

void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

void DebugMon_Handler(void)
{
}
