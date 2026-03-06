/**
 * @file    bsp_serial_init.c
 * @brief   serial的硬件配置相关文件，存放serial的若干资源描述
 * @note    # init行为包括：
 *              1、cfg赋值（仅赋值，具体配置行为交由应用层调用）
 *              2、开启串口、DMA、GPIO时钟
 *              3、NVIC初始化
 *              4、FIFO初始化
 *          # deinit行为包括：
 *              1、关闭串口、DMA、GPIO时钟
 *              2、NVIC解初始化
 *              3、FIFO解初始化
 * @copyright (c) 2025/7/22, 余浩
 */

#include "bsp_serial.h"

#ifdef USE_SERIAL_1
/* 变量定义 */
#ifdef USE_SERIAL1_DMA_TX
static DMA_HandleTypeDef hdma_serial1_tx;
#endif /* USE_SERIAL1_DMA_TX */

#ifdef USE_SERIAL1_DMA_RX
static DMA_HandleTypeDef hdma_serial1_rx;
static uint8_t serial1_container0[SERIAL_1_RX_MULTIBUF_SIZE];
#ifdef USE_SERIAL1_CONTAINER1
static uint8_t serial1_container1[SERIAL_1_RX_MULTIBUF_SIZE];
BSP_SERIAL_MULTIBUF_DEF(serial1_rx_multibuf, serial1_container0, serial1_container1, SERIAL_1_RX_MULTIBUF_SIZE);
#else
BSP_SERIAL_MULTIBUF_DEF(serial1_rx_multibuf, serial1_container0, NULL, SERIAL_1_RX_MULTIBUF_SIZE);
#endif /* USE_SERIAL1_CONTAINER1 */
#endif /* USE_SERIAL1_DMA_RX */
static void _bsp_serial1_pre_init(bsp_serial_t bsp_serial, uint8_t is_enalbe_int)
{
    UART_HandleTypeDef* huart = &bsp_serial->handle;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    SerialCfg cfg = {
        .baudrate = 1000000,
        .dataBits = DATA_BITS_8,
        .stopBits = STOP_BITS_1,
        .parity = PARITY_NONE,
        .flowCtrl = FLOW_CTRL_NONE,
        .overSampling = OVERSAMPLING_16,
        .txBufSize = 128,
        .rxBufSize = 1024,
    };
    // 赋值串口参数
    bsp_serial->parent.cfg = cfg;
    // 开时钟
    __HAL_RCC_USART1_CLK_ENABLE();         // 串口时钟
    if (__HAL_RCC_GPIOA_IS_CLK_DISABLED()) // GPIO时钟
        __HAL_RCC_GPIOA_CLK_ENABLE();
    if (__HAL_RCC_GPIOB_IS_CLK_DISABLED()) // GPIO时钟
        __HAL_RCC_GPIOB_CLK_ENABLE();
#if defined(USE_SERIAL1_DMA_TX) || defined(USE_SERIAL1_DMA_RX)
    if (__HAL_RCC_DMA2_IS_CLK_DISABLED()) // DMA时钟
        __HAL_RCC_DMA2_CLK_ENABLE();
#endif
    // 配置GPIO
    GPIO_InitStruct.Pin = GPIO_PIN_9; // PA9     ------> USART1_TX
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = GPIO_PIN_7; // PB7     ------> USART1_RX
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

// 配置TXDMA
#ifdef USE_SERIAL1_DMA_TX
    __HAL_LINKDMA(huart, hdmatx, hdma_serial1_tx);
    hdma_serial1_tx.Instance = SERIAL_1_DMA_TX_DMA_STREAM;
    hdma_serial1_tx.Init.Channel = SERIAL_1_DMA_TX_DMA_CHANNEL;

    // 配置NVIC
    HAL_NVIC_SetPriority(SERIAL_1_DMA_TX_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(SERIAL_1_DMA_TX_IRQn);
#endif
// 配置RXDMA
#ifdef USE_SERIAL1_DMA_RX
    __HAL_LINKDMA(huart, hdmarx, hdma_serial1_rx);
    hdma_serial1_rx.Instance = SERIAL_1_DMA_RX_DMA_STREAM;
    hdma_serial1_rx.Init.Channel = SERIAL_1_DMA_RX_DMA_CHANNEL;
    bsp_serial->rx_multibuf = &serial1_rx_multibuf;
    // 配置NVIC
    HAL_NVIC_SetPriority(SERIAL_1_DMA_RX_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SERIAL_1_DMA_RX_IRQn);
#endif /* USE_SERIAL1_DMA_RX */
    // 配置NVIC
    if (is_enalbe_int)
    {
        HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
}
#endif

#ifdef USE_SERIAL_3
/* 变量定义 */
#ifdef USE_SERIAL3_DMA_TX
static DMA_HandleTypeDef hdma_serial3_tx;
#endif /* USE_SERIAL3_DMA_TX */

#ifdef USE_SERIAL3_DMA_RX
static DMA_HandleTypeDef hdma_serial3_rx;
static uint8_t serial3_container0[SERIAL_3_RX_MULTIBUF_SIZE];
#ifdef USE_SERIAL3_CONTAINER1
static uint8_t serial3_container1[SERIAL_3_RX_MULTIBUF_SIZE];
BSP_SERIAL_MULTIBUF_DEF(serial3_rx_multibuf, serial3_container0, serial3_container1, SERIAL_3_RX_MULTIBUF_SIZE);
#else
BSP_SERIAL_MULTIBUF_DEF(serial3_rx_multibuf, serial3_container0, NULL, SERIAL_3_RX_MULTIBUF_SIZE);
#endif /* USE_SERIAL3_CONTAINER1 */
#endif /* USE_SERIAL3_DMA_RX */
static void _bsp_serial3_pre_init(bsp_serial_t bsp_serial, uint8_t is_enalbe_int)
{
    UART_HandleTypeDef* huart = &bsp_serial->handle;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    SerialCfg cfg = {
        .baudrate = 921600,
        .dataBits = DATA_BITS_8,
        .stopBits = STOP_BITS_1,
        .parity = PARITY_NONE,
        .flowCtrl = FLOW_CTRL_NONE,
        .overSampling = OVERSAMPLING_16,
        .txBufSize = 1024,
        .rxBufSize = 128,
    };
    // 赋值串口参数
    bsp_serial->parent.cfg = cfg;
    // 开时钟
    __HAL_RCC_USART3_CLK_ENABLE();         // 串口时钟
    if (__HAL_RCC_GPIOD_IS_CLK_DISABLED()) // GPIO时钟
        __HAL_RCC_GPIOD_CLK_ENABLE();
#if defined(USE_SERIAL3_DMA_TX) || defined(USE_SERIAL3_DMA_RX)
    if (__HAL_RCC_DMA1_IS_CLK_DISABLED()) // DMA时钟
        __HAL_RCC_DMA1_CLK_ENABLE();
#endif
    // 配置GPIO
    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_8; // PD9     ------> USART3_RX
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;        // PD8     ------> USART3_TX
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

// 配置TXDMA
#ifdef USE_SERIAL3_DMA_TX
    __HAL_LINKDMA(huart, hdmatx, hdma_serial3_tx);
    hdma_serial3_tx.Instance = SERIAL_3_DMA_TX_DMA_STREAM;
    hdma_serial3_tx.Init.Channel = SERIAL_3_DMA_TX_DMA_CHANNEL;

    // 配置NVIC
    HAL_NVIC_SetPriority(SERIAL_3_DMA_TX_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(SERIAL_3_DMA_TX_IRQn);
#endif
// 配置RXDMA
#ifdef USE_SERIAL3_DMA_RX
    __HAL_LINKDMA(huart, hdmarx, hdma_serial3_rx);
    hdma_serial3_rx.Instance = SERIAL_3_DMA_RX_DMA_STREAM;
    hdma_serial3_rx.Init.Channel = SERIAL_3_DMA_RX_DMA_CHANNEL;
    bsp_serial->rx_multibuf = &serial3_rx_multibuf;
    // 配置NVIC
    HAL_NVIC_SetPriority(SERIAL_3_DMA_RX_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SERIAL_3_DMA_RX_IRQn);
#endif /* USE_SERIAL3_DMA_RX */
    // 配置NVIC
    if (is_enalbe_int)
    {
        HAL_NVIC_SetPriority(USART3_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART3_IRQn);
    }
}
#endif

#ifdef USE_SERIAL_6
/* 变量定义 */
#ifdef USE_SERIAL6_DMA_TX
static DMA_HandleTypeDef hdma_serial6_tx;
#endif /* USE_SERIAL6_DMA_TX */

#ifdef USE_SERIAL6_DMA_RX
static DMA_HandleTypeDef hdma_serial6_rx;
static uint8_t serial6_container0[SERIAL_6_RX_MULTIBUF_SIZE];
#ifdef USE_SERIAL6_CONTAINER1
static uint8_t serial6_container1[SERIAL_6_RX_MULTIBUF_SIZE];
BSP_SERIAL_MULTIBUF_DEF(serial6_rx_multibuf, serial6_container0, serial6_container1, SERIAL_6_RX_MULTIBUF_SIZE);
#else
BSP_SERIAL_MULTIBUF_DEF(serial6_rx_multibuf, serial6_container0, NULL, SERIAL_6_RX_MULTIBUF_SIZE);
#endif /* USE_SERIAL6_CONTAINER1 */
#endif /* USE_SERIAL6_DMA_RX */
static void _bsp_serial6_pre_init(bsp_serial_t bsp_serial, uint8_t is_enalbe_int)
{
    UART_HandleTypeDef* huart = &bsp_serial->handle;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    SerialCfg cfg = {
        .baudrate = 115200u,
        .dataBits = DATA_BITS_8,
        .stopBits = STOP_BITS_1,
        .parity = PARITY_NONE,
        .flowCtrl = FLOW_CTRL_NONE,
        .overSampling = OVERSAMPLING_16,
        .txBufSize = 1024,
        .rxBufSize = 128,
    };
    // 赋值串口参数
    bsp_serial->parent.cfg = cfg;
    // 开时钟
    __HAL_RCC_USART6_CLK_ENABLE();         // 串口时钟
    if (__HAL_RCC_GPIOG_IS_CLK_DISABLED()) // GPIO时钟
        __HAL_RCC_GPIOG_CLK_ENABLE();
#if defined(USE_SERIAL6_DMA_TX) || defined(USE_SERIAL6_DMA_RX)
    if (__HAL_RCC_DMA2_IS_CLK_DISABLED()) // DMA时钟
        __HAL_RCC_DMA2_CLK_ENABLE();
#endif
    // 配置GPIO
    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_14; // PG9     ------> USART6_RX
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;         // PG14     ------> USART6_TX
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

// 配置TXDMA
#ifdef USE_SERIAL6_DMA_TX
    __HAL_LINKDMA(huart, hdmatx, hdma_serial6_tx);
    hdma_serial6_tx.Instance = SERIAL_6_DMA_TX_DMA_STREAM;
    hdma_serial6_tx.Init.Channel = SERIAL_6_DMA_TX_DMA_CHANNEL;
    __HAL_DMA_DISABLE_IT(&hdma_serial6_tx, DMA_IT_HT); // 不知道为啥没用，还是会触发半满中断，暂时没研究

    // 配置NVIC
    HAL_NVIC_SetPriority(SERIAL_6_DMA_TX_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(SERIAL_6_DMA_TX_IRQn);
#endif
// 配置RXDMA
#ifdef USE_SERIAL6_DMA_RX
    __HAL_LINKDMA(huart, hdmarx, hdma_serial6_rx);
    hdma_serial6_rx.Instance = SERIAL_6_DMA_RX_DMA_STREAM;
    hdma_serial6_rx.Init.Channel = SERIAL_6_DMA_RX_DMA_CHANNEL;
    bsp_serial->rx_multibuf = &serial6_rx_multibuf;
    // 配置NVIC
    HAL_NVIC_SetPriority(SERIAL_6_DMA_RX_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SERIAL_6_DMA_RX_IRQn);
#endif /* USE_SERIAL3_DMA_RX */
    // 配置NVIC
    if (is_enalbe_int)
    {
        HAL_NVIC_SetPriority(USART6_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART6_IRQn);
    }
}
#endif

#ifdef USE_SERIAL_7
static void _bsp_serial7_pre_init(bsp_serial_t bsp_serial, uint8_t is_enalbe_int)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    SerialCfg cfg = {
        .baudrate = 115200u,
        .dataBits = DATA_BITS_8,
        .stopBits = STOP_BITS_1,
        .parity = PARITY_NONE,
        .flowCtrl = FLOW_CTRL_NONE,
        .overSampling = OVERSAMPLING_16,
        .txBufSize = 1024,
        .rxBufSize = 128,
    };
    bsp_serial->parent.cfg = cfg;
    __HAL_RCC_UART7_CLK_ENABLE();
    if (__HAL_RCC_GPIOE_IS_CLK_DISABLED())
        __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_7; // PE8 ------> UART7_TX, PE7 ------> UART7_RX
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART7;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    if (is_enalbe_int)
    {
        HAL_NVIC_SetPriority(UART7_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(UART7_IRQn);
    }
}
#endif

#ifdef USE_SERIAL_8
static void _bsp_serial8_pre_init(bsp_serial_t bsp_serial, uint8_t is_enalbe_int)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    SerialCfg cfg = {
        .baudrate = 115200u,
        .dataBits = DATA_BITS_8,
        .stopBits = STOP_BITS_1,
        .parity = PARITY_NONE,
        .flowCtrl = FLOW_CTRL_NONE,
        .overSampling = OVERSAMPLING_16,
        .txBufSize = 1024,
        .rxBufSize = 128,
    };
    bsp_serial->parent.cfg = cfg;
    __HAL_RCC_UART8_CLK_ENABLE();
    if (__HAL_RCC_GPIOE_IS_CLK_DISABLED())
        __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_0; // PE1 ------> UART8_TX, PE0 ------> UART8_RX
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART8;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    if (is_enalbe_int)
    {
        HAL_NVIC_SetPriority(UART8_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(UART8_IRQn);
    }
}
#endif

// note: 在注册后调用该函数
void bsp_serial_pre_init(bsp_serial_t bsp_serial)
{
    uint8_t is_enable_int;
    is_enable_int =
        device_get_regparams(&bsp_serial->parent.parent) & (SERIAL_REG_DMA_RX | SERIAL_REG_DMA_TX | SERIAL_REG_INT_RX | SERIAL_REG_INT_TX);
    switch ((uint32_t)bsp_serial->handle.Instance)
    {
#ifdef USE_SERIAL_1
    case USART1_BASE:
        _bsp_serial1_pre_init(bsp_serial, is_enable_int);
        break;
#endif

#ifdef USE_SERIAL_3
    case USART3_BASE:
        _bsp_serial3_pre_init(bsp_serial, is_enable_int);
        break;
#endif

#ifdef USE_SERIAL_6
    case USART6_BASE:
        _bsp_serial6_pre_init(bsp_serial, is_enable_int);
        break;
#endif
#ifdef USE_SERIAL_7
    case UART7_BASE:
        _bsp_serial7_pre_init(bsp_serial, is_enable_int);
        break;
#endif
#ifdef USE_SERIAL_8
    case UART8_BASE:
        _bsp_serial8_pre_init(bsp_serial, is_enable_int);
        break;
#endif
    }
}
