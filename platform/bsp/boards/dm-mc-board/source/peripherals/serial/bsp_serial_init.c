/**
 * @file bsp_serial_init.c
 * @brief dm-mc-board UART hardware resource description.
 */

#include "bsp_pinmap.h"
#include "bsp_serial.h"

static SerialCfg dm_mc_board_uart_default_cfg(void)
{
    SerialCfg cfg = {
        .baudrate = 115200,
        .dataBits = DATA_BITS_8,
        .stopBits = STOP_BITS_1,
        .parity = PARITY_NONE,
        .flowCtrl = FLOW_CTRL_NONE,
        .overSampling = OVERSAMPLING_16,
        .txBufSize = 256,
        .rxBufSize = 256,
    };
    return cfg;
}

static void dm_mc_board_gpio_default(GPIO_InitTypeDef* gpio)
{
    gpio->Mode = GPIO_MODE_AF_PP;
    gpio->Pull = GPIO_NOPULL;
    gpio->Speed = GPIO_SPEED_FREQ_VERY_HIGH;
}

static void bsp_usart1_pre_init(bsp_serial_t bsp_serial)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    bsp_serial->parent.cfg = dm_mc_board_uart_default_cfg();

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    dm_mc_board_gpio_default(&GPIO_InitStruct);
    GPIO_InitStruct.Pin = DM_MC_BOARD_USART1_TX_PIN | DM_MC_BOARD_USART1_RX_PIN;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static void bsp_uart7_pre_init(bsp_serial_t bsp_serial)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    bsp_serial->parent.cfg = dm_mc_board_uart_default_cfg();

    __HAL_RCC_UART7_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    dm_mc_board_gpio_default(&GPIO_InitStruct);
    GPIO_InitStruct.Pin = DM_MC_BOARD_UART7_TX_PIN | DM_MC_BOARD_UART7_RX_PIN;
    GPIO_InitStruct.Alternate = GPIO_AF7_UART7;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(UART7_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(UART7_IRQn);
}

static void bsp_uart5_pre_init(bsp_serial_t bsp_serial)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    bsp_serial->parent.cfg = dm_mc_board_uart_default_cfg();

    __HAL_RCC_UART5_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    dm_mc_board_gpio_default(&GPIO_InitStruct);
    GPIO_InitStruct.Pin = DM_MC_BOARD_UART5_RX_PIN;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART5;
    HAL_GPIO_Init(DM_MC_BOARD_UART5_RX_PORT, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(UART5_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(UART5_IRQn);
}

static void bsp_uart8_pre_init(bsp_serial_t bsp_serial)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    bsp_serial->parent.cfg = dm_mc_board_uart_default_cfg();

    __HAL_RCC_UART8_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    dm_mc_board_gpio_default(&GPIO_InitStruct);
    GPIO_InitStruct.Pin = DM_MC_BOARD_UART8_TX_PIN | DM_MC_BOARD_UART8_RX_PIN;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART8;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(UART8_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(UART8_IRQn);
}

static void bsp_uart9_pre_init(bsp_serial_t bsp_serial)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    bsp_serial->parent.cfg = dm_mc_board_uart_default_cfg();

    __HAL_RCC_UART9_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    dm_mc_board_gpio_default(&GPIO_InitStruct);
    GPIO_InitStruct.Pin = DM_MC_BOARD_UART9_TX_PIN | DM_MC_BOARD_UART9_RX_PIN;
    GPIO_InitStruct.Alternate = GPIO_AF11_UART9;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(UART9_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(UART9_IRQn);
}

static void bsp_usart10_pre_init(bsp_serial_t bsp_serial)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    bsp_serial->parent.cfg = dm_mc_board_uart_default_cfg();

    __HAL_RCC_USART10_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    dm_mc_board_gpio_default(&GPIO_InitStruct);
    GPIO_InitStruct.Pin = DM_MC_BOARD_USART10_RX_PIN;
    GPIO_InitStruct.Alternate = GPIO_AF4_USART10;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = DM_MC_BOARD_USART10_TX_PIN;
    GPIO_InitStruct.Alternate = GPIO_AF11_USART10;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(USART10_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART10_IRQn);
}

void bsp_serial_pre_init(bsp_serial_t bsp_serial)
{
    if (bsp_serial == NULL)
        return;

    if (bsp_serial->handle.Instance == USART1)
        bsp_usart1_pre_init(bsp_serial);
    else if (bsp_serial->handle.Instance == UART5)
        bsp_uart5_pre_init(bsp_serial);
    else if (bsp_serial->handle.Instance == UART7)
        bsp_uart7_pre_init(bsp_serial);
    else if (bsp_serial->handle.Instance == UART8)
        bsp_uart8_pre_init(bsp_serial);
    else if (bsp_serial->handle.Instance == UART9)
        bsp_uart9_pre_init(bsp_serial);
    else if (bsp_serial->handle.Instance == USART10)
        bsp_usart10_pre_init(bsp_serial);
}
