#include "bsp_serial.h"
#include "core/om_cpu.h"

static OmRet bsp_serial_configure(HalSerial* serial, SerialCfg* cfg)
{
    bsp_serial_t bsp_serial;
    UART_HandleTypeDef* huart;

    if (!serial || !cfg)
        return OM_ERROR_PARAM;

    bsp_serial = (bsp_serial_t)serial->parent.handle;
    huart = &bsp_serial->handle;

    huart->Init.BaudRate = cfg->baudrate;
    huart->Init.WordLength = (cfg->parity != PARITY_NONE) ? UART_WORDLENGTH_9B : UART_WORDLENGTH_8B;
    huart->Init.Mode = UART_MODE_TX_RX;
    huart->Init.StopBits = (cfg->stopBits == STOP_BITS_2) ? UART_STOPBITS_2 : UART_STOPBITS_1;

    switch (cfg->parity)
    {
    case PARITY_ODD:
        huart->Init.Parity = UART_PARITY_ODD;
        break;
    case PARITY_EVEN:
        huart->Init.Parity = UART_PARITY_EVEN;
        break;
    case PARITY_NONE:
    default:
        huart->Init.Parity = UART_PARITY_NONE;
        break;
    }

    switch (cfg->flowCtrl)
    {
    case FLOW_CTRL_RTS:
        huart->Init.HwFlowCtl = UART_HWCONTROL_RTS;
        break;
    case FLOW_CTRL_CTS:
        huart->Init.HwFlowCtl = UART_HWCONTROL_CTS;
        break;
    case FLOW_CTRL_CTS_RTS:
        huart->Init.HwFlowCtl = UART_HWCONTROL_RTS_CTS;
        break;
    case FLOW_CTRL_NONE:
    default:
        huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
        break;
    }

    huart->Init.OverSampling = (cfg->overSampling == OVERSAMPLING_8) ? UART_OVERSAMPLING_8 : UART_OVERSAMPLING_16;
    huart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(huart) != HAL_OK)
        return OM_ERROR;

    (void)HAL_UARTEx_SetTxFifoThreshold(huart, UART_TXFIFO_THRESHOLD_1_8);
    (void)HAL_UARTEx_SetRxFifoThreshold(huart, UART_RXFIFO_THRESHOLD_1_8);
    (void)HAL_UARTEx_DisableFifoMode(huart);
    return OM_OK;
}

static OmRet bsp_serial_control(HalSerial* serial, uint32_t cmd, void* arg)
{
    bsp_serial_t bsp_serial;
    uint32_t io_type;

    if (!serial)
        return OM_ERROR_PARAM;

    bsp_serial = (bsp_serial_t)serial->parent.handle;

    switch (cmd)
    {
    case SERIAL_CMD_SET_IOTPYE:
        io_type = (uint32_t)arg;
        if (io_type == SERIAL_REG_INT_RX)
            __HAL_UART_ENABLE_IT(&bsp_serial->handle, UART_IT_RXNE);
        else if (io_type == SERIAL_REG_DMA_RX || io_type == SERIAL_REG_DMA_TX)
            return OM_ERROR_NOT_SUPPORT;
        break;

    case SERIAL_CMD_SUSPEND:
        HAL_UART_Abort_IT(&bsp_serial->handle);
        break;

    case SERIAL_CMD_RESUME:
        if (arg != NULL)
            return bsp_serial_configure(serial, (SerialCfg*)arg);
        break;

    default:
        break;
    }

    return OM_OK;
}

static OmRet bsp_serial_getByte(HalSerial* const serial, uint8_t* buf)
{
    bsp_serial_t bsp_serial;
    if (!serial || !buf)
        return OM_ERROR_PARAM;

    bsp_serial = (bsp_serial_t)serial->parent.handle;
    return (HAL_UART_Receive(&bsp_serial->handle, buf, 1, 10) == HAL_OK) ? OM_OK : OM_ERROR_TIMEOUT;
}

static OmRet bsp_serial_putByte(HalSerial* const serial, uint8_t data)
{
    bsp_serial_t bsp_serial;
    if (!serial)
        return OM_ERROR_PARAM;

    bsp_serial = (bsp_serial_t)serial->parent.handle;
    return (HAL_UART_Transmit(&bsp_serial->handle, &data, 1, 10) == HAL_OK) ? OM_OK : OM_ERROR_TIMEOUT;
}

static size_t bsp_serial_transmit(HalSerial* serial, const uint8_t* data, size_t length)
{
    bsp_serial_t bsp_serial;
    uint32_t regparams;
    HAL_StatusTypeDef ret;

    if (!serial || !data || length == 0U)
        return 0U;

    bsp_serial = (bsp_serial_t)serial->parent.handle;
    regparams = device_get_regparams(&serial->parent) & DEVICE_REG_TXTYPE_MASK;

    if (regparams == SERIAL_REG_INT_TX)
        ret = HAL_UART_Transmit_IT(&bsp_serial->handle, data, (uint16_t)length);
    else
        ret = HAL_UART_Transmit(&bsp_serial->handle, data, (uint16_t)length, 10U);

    return (ret == HAL_OK) ? length : 0U;
}

static SerialInterface bsp_serial_interface = {
    .configure = bsp_serial_configure,
    .control = bsp_serial_control,
    .getByte = bsp_serial_getByte,
    .putByte = bsp_serial_putByte,
    .transmit = bsp_serial_transmit,
};

bsp_serial_s g_bsp_serial[] = {
#ifdef USE_SERIAL_1
    BSP_SERIAL_STATIC_INIT(USART1, "usart1", SERIAL_1_REG_PARAMS),
#endif
#ifdef USE_SERIAL_5
    BSP_SERIAL_STATIC_INIT(UART5, "uart5", SERIAL_5_REG_PARAMS),
#endif
#ifdef USE_SERIAL_7
    BSP_SERIAL_STATIC_INIT(UART7, "uart7", SERIAL_7_REG_PARAMS),
#endif
#ifdef USE_SERIAL_8
    BSP_SERIAL_STATIC_INIT(UART8, "uart8", SERIAL_8_REG_PARAMS),
#endif
#ifdef USE_SERIAL_9
    BSP_SERIAL_STATIC_INIT(UART9, "uart9", SERIAL_9_REG_PARAMS),
#endif
#ifdef USE_SERIAL_10
    BSP_SERIAL_STATIC_INIT(USART10, "usart10", SERIAL_10_REG_PARAMS),
#endif
};

void bsp_serial_register(void)
{
    uint8_t cnt = sizeof(g_bsp_serial) / sizeof(g_bsp_serial[0]);
    for (uint8_t i = 0; i < cnt; i++)
    {
        g_bsp_serial[i].parent.interface = &bsp_serial_interface;
        serial_register(&g_bsp_serial[i].parent, g_bsp_serial[i].name, &g_bsp_serial[i], g_bsp_serial[i].regparams);
        bsp_serial_pre_init(&g_bsp_serial[i]);
    }
}
