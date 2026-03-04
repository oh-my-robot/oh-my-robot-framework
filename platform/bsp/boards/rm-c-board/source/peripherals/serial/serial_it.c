/*
 * @file bsp_serial_it.c
 * @brief 串口中断处理模块
 * @details 该模块负责处理串口的中断事件，包括接收数据、发送完成等。
 */
#include "bsp_serial.h"
#include "core/om_cpu.h"
#include "core/om_interrupt.h"
// 参数Memory为需要处理的DMA缓冲区
static void _bsp_serial_dmarx_deal(bsp_serial_t bsp_serial, HAL_DMA_MemoryTypeDef Memory)
{
    HAL_UART_RxEventTypeTypeDef rx_event_type;
    ptrdiff_t recv_len; // 实际本轮中断需要处理的字节数
    size_t dma_cnt;     // 截止本轮中断，DMA接收到的总字节数
    void* rx_buf;       // 需要处理的数据缓冲区首地址

    rx_event_type = HAL_UARTEx_GetRxEventType(&bsp_serial->handle);

    if (rx_event_type != HAL_UART_RXEVENT_TC)
    {
        dma_cnt = bsp_serial->handle.RxXferSize - __HAL_DMA_GET_COUNTER(bsp_serial->handle.hdmarx);
        recv_len = dma_cnt - bsp_serial->rx_multibuf->last_rx_cnt;
    }
    else
    {
        recv_len = bsp_serial->handle.RxXferSize - bsp_serial->rx_multibuf->last_rx_cnt;
        dma_cnt = 0;
    }
    // while(recv_len < 0)
    rx_buf = (Memory != MEMORY0) ? bsp_serial->rx_multibuf->container1 : bsp_serial->rx_multibuf->container0;
    rx_buf += bsp_serial->rx_multibuf->last_rx_cnt;

    bsp_serial->rx_multibuf->last_rx_cnt = dma_cnt;

    if (rx_buf && recv_len)
        serial_hw_isr(&bsp_serial->parent, SERIAL_EVENT_DMA_RXDONE, rx_buf, recv_len);
}

static HAL_DMA_MemoryTypeDef _bsp_get_dma_work_mem(DMA_HandleTypeDef* hdma)
{
    if (hdma->Instance->CR & DMA_SxCR_CT)
    {
        return MEMORY1;
    }
    else
    {
        return MEMORY0;
    }
}

static void _bsp_serial_rxdma_m1cplt_cb(DMA_HandleTypeDef* _hdma)
{
    bsp_serial_t bsp_serial;
    bsp_serial = (bsp_serial_t)_hdma->Parent; // _hdma->Parent --> huart --> bsp_serial，结构体指针的小妙招
    // HAL库触发M1half时，huart的RxEventType要手动更改
    bsp_serial->handle.RxEventType = HAL_UART_RXEVENT_TC;
    _bsp_serial_dmarx_deal(bsp_serial, MEMORY1);
}

static void _bsp_serial_rxdma_m1half_cb(DMA_HandleTypeDef* _hdma)
{
    bsp_serial_t bsp_serial;
    bsp_serial = (bsp_serial_t)_hdma->Parent; // _hdma->Parent --> huart --> bsp_serial
    // HAL库触发M1half时，huart的RxEventType要手动更改
    bsp_serial->handle.RxEventType = HAL_UART_RXEVENT_HT;
    _bsp_serial_dmarx_deal(bsp_serial, MEMORY1);
}

static void _bsp_serial_rxdma_m0cplt_cb(DMA_HandleTypeDef* _hdma)
{
    bsp_serial_t bsp_serial;
    bsp_serial = (bsp_serial_t)_hdma->Parent; // _hdma->Parent --> huart --> bsp_serial，结构体指针的小妙招
    // HAL库触发M1half时，huart的RxEventType要手动更改
    bsp_serial->handle.RxEventType = HAL_UART_RXEVENT_TC;
    _bsp_serial_dmarx_deal(bsp_serial, MEMORY0);
}

static void _bsp_serial_rxdma_m0half_cb(DMA_HandleTypeDef* _hdma)
{
    bsp_serial_t bsp_serial;
    bsp_serial = (bsp_serial_t)_hdma->Parent; // _hdma->Parent --> huart --> bsp_serial，结构体指针的小妙招
    // HAL库触发M1half时，huart的RxEventType要手动更改
    bsp_serial->handle.RxEventType = HAL_UART_RXEVENT_HT;
    _bsp_serial_dmarx_deal(bsp_serial, MEMORY0);
}

void bsp_serial_dma_cfg(bsp_serial_t bsp_serial, uint32_t dma_regparams)
{
    DMA_HandleTypeDef* hdma = NULL;
    UART_HandleTypeDef* huart = &bsp_serial->handle;
    if (dma_regparams == SERIAL_REG_DMA_TX)
    {
        hdma = huart->hdmatx;
        hdma->Init.Direction = DMA_MEMORY_TO_PERIPH;
        hdma->Init.Mode = DMA_NORMAL;
    }
    else if (dma_regparams == SERIAL_REG_DMA_RX)
    {
        hdma = huart->hdmarx;
        hdma->Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma->Init.Mode = DMA_CIRCULAR; // 循环模式
        while (!bsp_serial->rx_multibuf)
        {
        }; // TODO: assert
        while (!bsp_serial->rx_multibuf->container0)
            ; // TODO: assert
        while (!bsp_serial->rx_multibuf->container_len)
            ; // TODO: assert
    }
    if (hdma == NULL)
        while (1)
        {
        }; // TODO: assert
    hdma->Init.PeriphInc = DMA_PINC_DISABLE;
    hdma->Init.MemInc = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma->Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma->Init.Priority = DMA_PRIORITY_LOW;
    hdma->Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(hdma) != HAL_OK)
    {
        OM_CPU_ERRHANDLER("HAL_DMA_Init failed", OM_LOG_LEVEL_FATAL);
    }
    if (dma_regparams == SERIAL_REG_DMA_RX)
    {
        /* 没配置container1，则进行常规的DMARX配置，否则进行多buf的DMARX配置 */
        if (!bsp_serial->rx_multibuf->container1)
        {
            HAL_UARTEx_ReceiveToIdle_DMA(&bsp_serial->handle, bsp_serial->rx_multibuf->container0, bsp_serial->rx_multibuf->container_len);
        }
        else
        {
            // TODO：
            // 这一块导致整个函数过长，未来将其打包放进一个新的函数里面去，专门负责配置DMA多buf，更符合单一责任原则
            HAL_StatusTypeDef ret;
            uint32_t primask;
            HAL_DMA_RegisterCallback(hdma, HAL_DMA_XFER_M1CPLT_CB_ID, _bsp_serial_rxdma_m1cplt_cb);
            HAL_DMA_RegisterCallback(hdma, HAL_DMA_XFER_M1HALFCPLT_CB_ID, _bsp_serial_rxdma_m1half_cb);
            HAL_UARTEx_ReceiveToIdle_DMA(huart, bsp_serial->rx_multibuf->container0, bsp_serial->rx_multibuf->container_len);
            /* 失能DMA会立马触发TC，所以需要先关闭TC中断，但是该标志位要在失能DMA的情况才能清除，造成逻辑上的死循环
               解决办法是先关闭全局中断，然后失能DMA，这样就不会立即触发TC中断，这时再清除TC标志即可
            */
            primask = om_hw_disable_interrupt(); // 注意，在框架内所有的关闭硬件中断操作都必须使用om_hw_disable/enable_irq()，以避免一些错误
            __HAL_DMA_DISABLE(hdma);
            __HAL_DMA_DISABLE_IT(hdma, DMA_IT_TC);
            om_hw_restore_interrupt(primask);
            // 擦HAL_UARTEx_ReceiveToIdle_DMA的屁股，该函数内会LOCK(hdma)并设置hdma为busy
            __HAL_UNLOCK(hdma);
            hdma->State = HAL_DMA_STATE_READY;
            HAL_DMA_RegisterCallback(hdma, HAL_DMA_XFER_CPLT_CB_ID, _bsp_serial_rxdma_m0cplt_cb);
            HAL_DMA_RegisterCallback(hdma, HAL_DMA_XFER_HALFCPLT_CB_ID, _bsp_serial_rxdma_m0half_cb);
            ret = HAL_DMAEx_MultiBufferStart_IT(hdma,
                                                (uint32_t)&huart->Instance->DR, // 不要忘记取地址
                                                (uint32_t)bsp_serial->rx_multibuf->container0,
                                                (uint32_t)bsp_serial->rx_multibuf->container1, bsp_serial->rx_multibuf->container_len);
            while (ret != HAL_OK)
            {
            }; // TODO: assert
        }
    }
}

/******************************************* IRQHANDLER *************************************************/

/**
 * @brief  触发hal层isr，更新bsp串口状态
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size)
{
    bsp_serial_t bsp_serial = (bsp_serial_t)huart;
    HAL_DMA_MemoryTypeDef WorkMemory = MEMORY0;
    if (huart->RxEventType == HAL_UART_RXEVENT_IDLE && bsp_serial->rx_multibuf->container1)
    {
        WorkMemory = _bsp_get_dma_work_mem(huart->hdmarx);
        /*
            由于_bsp_serial_dma_cfg中配置多buf时使用的是HAL_UARTEx_ReceiveToIdle_DMA，该函数中会将空闲回调函数设置为
            HAL_UARTEx_RxEventCallback，因此无论空闲发生后当前WorkMemory是MEMORY0还是MEMORY1，都会跳转到HAL_UARTEx_RxEventCallback中。
            于是存在一种情况，当前状态的WorkMemory是MEMORY0，接收一定字节数据后，MEMORY0被填满，WorkMemory被切换为MEMORY1，
            若恰好此时发生空闲中断，进入HAL_UARTEx_RxEventCallback，我们初次获取到的WorkMemory就是MEMORY1。
            这意味着如果不进行特殊判断，_bsp_serial_dmarx_deal中将会操作MEMORY1中的数据。
            但是实际上我们需要处理的数据还在MEMORY0中！！只不过WorkMEMORY刚切换，恰好就发生了空闲中断。
            因此额外判断当状态为MEMORY1，且DMA计数为最大值（也就是一个字节都没有接收到，代表刚好切换WorkMemory）时，
            更改WorkMemory。
            对于初次获得WorkMemory为MEMORY0的情况也同理，归根结底是HAL_UARTEx_RxEventCallback的触发机制不区分MEMORY0和MEMORY1的问题
        */
        if (__HAL_DMA_GET_COUNTER(huart->hdmarx) == huart->RxXferSize)
            WorkMemory = (WorkMemory != MEMORY0) ? MEMORY0 : MEMORY1;
    }
    _bsp_serial_dmarx_deal(bsp_serial, WorkMemory);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
    SerialEvent event;
    uint32_t regparams;
    bsp_serial_t bsp_serial;
    bsp_serial = (bsp_serial_t)huart; // 这就是为什么要把UART_HandleTypeDef成员放在bsp_serial_t中的第一位
                                      // 可以直接强转，避免了冗余的判断
    regparams = device_get_regparams(&bsp_serial->parent.parent) & DEVICE_REG_TXTYPE_MASK;
    event = (regparams == SERIAL_REG_DMA_TX) ? SERIAL_EVENT_DMA_TXDONE : SERIAL_EVENT_INT_TXDONE;
    serial_hw_isr(&bsp_serial->parent, event, NULL, bsp_serial->handle.TxXferSize); // TxXferSize为发送的字节数
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
    bsp_serial_t bsp_serial;
    bsp_serial = (bsp_serial_t)huart;
    serial_hw_isr(&bsp_serial->parent, SERIAL_EVENT_ERR_OCCUR, NULL, 0);
}

#ifdef USE_SERIAL_1
void USART1_IRQHandler(void)
{
    bsp_serial_t bsp_serial = &g_bsp_serial[SERIAL1_IDX];
    if (__HAL_UART_GET_IT_SOURCE(&bsp_serial->handle, UART_IT_RXNE) && __HAL_UART_GET_FLAG(&bsp_serial->handle, UART_FLAG_RXNE))
    {
        uint8_t data = bsp_serial->handle.Instance->DR;
        __HAL_UART_CLEAR_FLAG(&bsp_serial->handle, UART_FLAG_RXNE);
        serial_hw_isr(&bsp_serial->parent, SERIAL_EVENT_INT_RXDONE, &data, 1);
    }

    HAL_UART_IRQHandler(&bsp_serial->handle);
}
#ifdef USE_SERIAL1_DMA_TX
void SERIAL_1_DMA_TX_IRQ_Handler(void)
{
    bsp_serial_t bsp_serial = &g_bsp_serial[SERIAL1_IDX];
    HAL_DMA_IRQHandler(bsp_serial->handle.hdmatx);
}
#endif /* USE_SERIAL1_DMA_TX */
#ifdef USE_SERIAL1_DMA_RX
void SERIAL_1_DMA_RX_IRQ_Handler(void)
{
    bsp_serial_t bsp_serial = &g_bsp_serial[SERIAL1_IDX];
    HAL_DMA_IRQHandler(bsp_serial->handle.hdmarx);
}

#endif /* USE_SERIAL1_DMA_RX */
#endif /* USE_SERIAL_1 */

#ifdef USE_SERIAL_3
void USART3_IRQHandler(void)
{
    bsp_serial_t bsp_serial = &g_bsp_serial[SERIAL3_IDX];
    if (__HAL_UART_GET_IT_SOURCE(&bsp_serial->handle, UART_IT_RXNE) && __HAL_UART_GET_FLAG(&bsp_serial->handle, UART_FLAG_RXNE))
    {
        uint8_t data = bsp_serial->handle.Instance->DR;
        __HAL_UART_CLEAR_FLAG(&bsp_serial->handle, UART_FLAG_RXNE);
        serial_hw_isr(&bsp_serial->parent, SERIAL_EVENT_INT_RXDONE, &data, 1);
    }

    HAL_UART_IRQHandler(&bsp_serial->handle);
}
#ifdef USE_SERIAL3_DMA_TX
void SERIAL_3_DMA_TX_IRQ_Handler(void)
{
    bsp_serial_t bsp_serial = &g_bsp_serial[SERIAL3_IDX];
    HAL_DMA_IRQHandler(bsp_serial->handle.hdmatx);
}
#endif /* USE_SERIAL3_DMA_TX */
#ifdef USE_SERIAL3_DMA_RX
void SERIAL_3_DMA_RX_IRQ_Handler(void)
{
    bsp_serial_t bsp_serial = &g_bsp_serial[SERIAL3_IDX];
    HAL_DMA_IRQHandler(bsp_serial->handle.hdmarx);
}

#endif /* USE_SERIAL3_DMA_RX */
#endif /* USE_SERIAL_3 */

#ifdef USE_SERIAL_6
void USART6_IRQHandler(void)
{
    bsp_serial_t bsp_serial = &g_bsp_serial[SERIAL6_IDX];
    if (__HAL_UART_GET_IT_SOURCE(&bsp_serial->handle, UART_IT_RXNE) && __HAL_UART_GET_FLAG(&bsp_serial->handle, UART_FLAG_RXNE))
    {
        uint8_t data = bsp_serial->handle.Instance->DR;
        __HAL_UART_CLEAR_FLAG(&bsp_serial->handle, UART_FLAG_RXNE);
        serial_hw_isr(&bsp_serial->parent, SERIAL_EVENT_INT_RXDONE, &data, 1);
    }

    HAL_UART_IRQHandler(&bsp_serial->handle);
}
#ifdef USE_SERIAL6_DMA_TX
void SERIAL_6_DMA_TX_IRQ_Handler(void)
{
    bsp_serial_t bsp_serial = &g_bsp_serial[SERIAL6_IDX];
    HAL_DMA_IRQHandler(bsp_serial->handle.hdmatx);
}
#endif /* USE_SERIAL6_DMA_TX */
#ifdef USE_SERIAL6_DMA_RX
void SERIAL_6_DMA_RX_IRQ_Handler(void)
{
    bsp_serial_t bsp_serial = &g_bsp_serial[SERIAL6_IDX];
    HAL_DMA_IRQHandler(bsp_serial->handle.hdmarx);
}

#endif /* USE_SERIAL6_DMA_RX */
#endif /* USE_SERIAL_6 */
