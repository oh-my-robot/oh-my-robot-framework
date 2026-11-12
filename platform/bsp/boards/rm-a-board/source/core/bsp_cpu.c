
#include "bsp.h"
#include "core/om_interrupt.h"
#include "core/om_init.h"

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
static void Error_Handler(void)
{
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
}

static void Board_DelayMs(float ms)
{
    DWT_Delay(ms / 1000.0f);
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Configure the main internal regulator output voltage
     */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 6;
    RCC_OscInitStruct.PLL.PLLN = 180;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 8;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* 与旧工程对齐到 180MHz SYSCLK。
     * STM32F427 在 Scale1 下跑 180MHz 需要打开 OverDrive。
     */
    if (HAL_PWREx_EnableOverDrive() != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
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

void om_board_init(void)
{
    // 开发板初始化，对于STM32来说，在CubeMX上配置时钟树，然后直接复制过来用就好
    // 仅保留 HAL 自举核（时钟/CPU 注册/DWT）；外设注册由 OM_INIT_BOARD 自注册
    // （见 bsp_selfreg.c），在 BOARD 级 om_board_init 之后执行。
    HAL_Init();
    SystemClock_Config();
    NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4); // 任务调度前，需要设置中断优先级分组
    // cpu注册
    om_cpu_register(__OM_CPU_FREQ_MHZ, &g_om_board_interface);
    // DWT
    DWT_Init(__OM_CPU_FREQ_MHZ);
}

/* 板级 HAL 自举作为 BOARD 级 prio 0 自注册：在所有 BOARD 级外设注册回调之前执行。 */
static OmRet om_board_self_init(void)
{
    om_board_init();
    return OM_OK;
}

OM_INIT(om_board_self_init, OM_INIT_LEVEL_BOARD, 0);

/* HardFault_Handler 已上移为架构共享实现（arch/cortex-m/om_hardfault.c → om_fatal_error，
 * 见 ADR-0014），板级不再自写。 */

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
  void NMI_Handler(void)
  {
  }
  
  
  
  /**
    * @brief  This function handles Memory Manage exception.
    * @param  None
    * @retval None
    */
  void MemManage_Handler(void)
  {
    /* Go to infinite loop when Memory Manage exception occurs */
    while (1)
    {
    }
  }
  
  /**
    * @brief  This function handles Bus Fault exception.
    * @param  None
    * @retval None
    */
  void BusFault_Handler(void)
  {
    /* Go to infinite loop when Bus Fault exception occurs */
    while (1)
    {
    }
  }
  
  /**
    * @brief  This function handles Usage Fault exception.
    * @param  None
    * @retval None
    */
  void UsageFault_Handler(void)
  {
    /* Go to infinite loop when Usage Fault exception occurs */
    while (1)
    {
    }
  }
  /**
    * @brief  This function handles Debug Monitor exception.
    * @param  None
    * @retval None
    */
  void DebugMon_Handler(void)
  {
  }
