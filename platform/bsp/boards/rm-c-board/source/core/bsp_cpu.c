
#include "bsp.h"
#include "core/om_interrupt.h"

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
    RCC_OscInitStruct.PLL.PLLN = 168;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
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

static OmBoardInterface_s OmBoardInterface = {
    .errhandler = Error_Handler,
    .reset = HAL_NVIC_SystemReset,
    .getCpuTimeS = DWT_GetTimeline_s,
    .getCpuTimeMs = DWT_GetTimeline_ms,
    .getCpuTimeUs = DWT_GetTimeline_us,
    .getDeltaCpuTimeS = DWT_GetDeltaT,
    .delayMs = DWT_Delay,
};

void om_board_init(void)
{
    // 开发板初始化，对于STM32来说，在CubeMX上配置时钟树，然后直接复制过来用就好
    HAL_Init();
    SystemClock_Config();
    NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4); // 任务调度前，需要设置中断优先级分组
    // cpu注册
    om_cpu_register(__OM_CPU_FREQ_MHZ, &OmBoardInterface);
    // 外设注册、初始化
    DWT_Init(__OM_CPU_FREQ_MHZ);
    bsp_serial_register();
    bsp_can_register();
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
  void HardFault_Handler(void)
  {
    /* Go to infinite loop when Hard Fault exception occurs */
    while (1)
    {
    }
  }

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
