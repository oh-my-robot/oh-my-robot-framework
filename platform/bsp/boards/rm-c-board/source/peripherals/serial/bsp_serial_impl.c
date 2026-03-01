/**
 * @file      bsp_serial_impl.c
 * @brief     serial的接口实现文件
 * @copyright (c) 2025/7/22, 余浩
 */

#include "bsp_serial.h"
#include "core/om_cpu.h"
/******************************************* SERIAL INTERFACE IMPLEMENTATION
 * *************************************************/
static OmRet_e bsp_serial_configure(HalSerial_t serial, SerialCfg_t cfg)
{
    bsp_serial_t bsp_serial;
    UART_HandleTypeDef* huart;
    bsp_serial = (bsp_serial_t)serial->parent.handle;
    huart = (UART_HandleTypeDef*)bsp_serial;
    huart->Init.BaudRate = cfg->baudrate;
    huart->Init.WordLength = (cfg->parity != PARITY_NONE) ? UART_WORDLENGTH_9B : UART_WORDLENGTH_8B;
    huart->Init.Mode = UART_MODE_TX_RX;

    switch (cfg->stopBits)
    {
    case STOP_BITS_1:
        huart->Init.StopBits = UART_STOPBITS_1;
        break;
    case STOP_BITS_2:
        huart->Init.StopBits = UART_STOPBITS_2;
        break;
    default: // TODO: assert
        break;
    }
    switch (cfg->parity)
    {
    case PARITY_NONE:
        huart->Init.Parity = UART_PARITY_NONE;
        break;
    case PARITY_ODD:
        huart->Init.Parity = UART_PARITY_ODD;
        break;
    case PARITY_EVEN:
        huart->Init.Parity = UART_PARITY_EVEN;
        break;
    }
    switch (cfg->flowCtrl)
    {
    case FLOW_CTRL_NONE:
        huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
        break;
    case FLOW_CTRL_RTS:
        huart->Init.HwFlowCtl = UART_HWCONTROL_RTS;
        break;
    case FLOW_CTRL_CTS:
        huart->Init.HwFlowCtl = UART_HWCONTROL_CTS;
        break;
    case FLOW_CTRL_CTS_RTS:
        huart->Init.HwFlowCtl = UART_HWCONTROL_RTS_CTS;
        break;
    }

    huart->Init.OverSampling = (cfg->overSampling == OVERSAMPLING_8) ? UART_OVERSAMPLING_8 : UART_OVERSAMPLING_16;
    if (HAL_UART_Init(huart) != HAL_OK)
    {
        OM_CPU_ERRHANDLER("HAL_UART_Init failed", OM_LOG_LEVEL_FATAL);
    }
    return OM_OK;
}

static OmRet_e bsp_serial_control(HalSerial_t serial, uint32_t cmd, void* arg)
{
    bsp_serial_t bsp_serial;
    uint32_t regparams;
    uint32_t tx_type;
    uint32_t rx_type;
    bsp_serial = (bsp_serial_t)serial->parent.handle;
    regparams = device_get_regparams(&serial->parent);
    tx_type = regparams & DEVICE_REG_TXTYPE_MASK;
    rx_type = regparams & DEVICE_REG_RXTYPE_MASK;
    switch (cmd)
    {
    case SERIAL_CMD_SET_IOTPYE:
    {
        uint32_t _arg = (uint32_t)arg;
        // 根据注册的参数配置DMA或是中断
        if (_arg == SERIAL_REG_DMA_TX || _arg == SERIAL_REG_DMA_RX)
        {
            bsp_serial_dma_cfg(bsp_serial, _arg);
        }
        else if (_arg == SERIAL_REG_INT_RX)
        {
            __HAL_UART_ENABLE_IT(&bsp_serial->handle, UART_IT_RXNE);
        }
    }
    break;

    case SERIAL_CMD_SUSPEND:
    {
        if ((tx_type == SERIAL_REG_DMA_TX) || (rx_type == SERIAL_REG_DMA_RX))
            HAL_UART_DMAStop(&bsp_serial->handle);
        else if ((tx_type == SERIAL_REG_INT_TX) || (rx_type == SERIAL_REG_INT_RX))
            HAL_UART_Abort_IT(&bsp_serial->handle);
        else
            HAL_UART_Abort(&bsp_serial->handle);
    }
    break;

    case SERIAL_CMD_RESUME:
    {
        if (arg != NULL && bsp_serial_configure(serial, (SerialCfg_t)arg) != OM_OK)
            return OM_ERROR;
    }
    break;
    default:
        break;
    }

    return OM_OK;
}

static OmRet_e bsp_serial_getByte(const HalSerial_t serial, uint8_t* buf)
{
    OmRet_e ret = OM_ERROR;
    bsp_serial_t bsp_serial;
    bsp_serial = (bsp_serial_t)serial->parent.handle;
    if (HAL_UART_Receive(&bsp_serial->handle, buf, 1, 10) != HAL_OK)
        ret = OM_ERROR_TIMEOUT;
    else
        ret = OM_OK;
    return ret;
}

static OmRet_e bsp_serial_putByte(const HalSerial_t serial, uint8_t data)
{
    bsp_serial_t bsp_serial;
    HAL_StatusTypeDef ret;
    bsp_serial = (bsp_serial_t)serial->parent.handle;
    ret = HAL_UART_Transmit(&bsp_serial->handle, &data, 1, 10);
    if (ret != HAL_OK)
        return OM_ERROR;
    return OM_OK;
}

static size_t bsp_serial_transmit(HalSerial_t serial, const uint8_t* data, size_t length)
{
    bsp_serial_t bsp_serial;
    HAL_StatusTypeDef ret;
    uint32_t regparams;
    bsp_serial = (bsp_serial_t)serial->parent.handle;
    regparams = device_get_regparams(&serial->parent) & DEVICE_REG_TXTYPE_MASK;

    ret = (regparams == SERIAL_REG_DMA_TX)   ? HAL_UART_Transmit_DMA(&bsp_serial->handle, data, length)
          : (regparams == SERIAL_REG_INT_TX) ? HAL_UART_Transmit_IT(&bsp_serial->handle, data, length)
                                             : HAL_ERROR;

    if (ret != HAL_OK)
    {
        // TODO: log
        return 0;
    }
    return length;
}

/********************************** SERIAL INTERFACE DEFINITION ***************************************/
static SerialInterface_s bsp_serial_interface = 
{
    .configure = bsp_serial_configure,
    .control = bsp_serial_control,
    .getByte = bsp_serial_getByte,
    .putByte = bsp_serial_putByte,
    .transmit = bsp_serial_transmit,
};

/******************************************* RESOURCE *************************************************/
bsp_serial_s g_bsp_serial[] = {
#ifdef USE_SERIAL_1
    BSP_SERIAL_STATIC_INIT(USART1, "usart1", SERIAL_1_REG_PARAMS),
#endif
#ifdef USE_SERIAL_3
    BSP_SERIAL_STATIC_INIT(USART3, "usart3", SERIAL_3_REG_PARAMS),
#endif
#ifdef USE_SERIAL_6
    BSP_SERIAL_STATIC_INIT(USART6, "usart6", SERIAL_6_REG_PARAMS),
#endif
};

/******************************************* FUNCTION *************************************************/
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
