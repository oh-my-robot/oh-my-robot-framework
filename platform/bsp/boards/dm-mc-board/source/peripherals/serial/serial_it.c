#include "bsp_serial.h"

void bsp_serial_dma_cfg(bsp_serial_t bsp_serial, uint32_t dma_regparams)
{
    (void)bsp_serial;
    (void)dma_regparams;
}

void bsp_serial_get_irq_diag(bsp_serial_t bsp_serial, bsp_serial_irq_diag_t* diag)
{
    if (bsp_serial == NULL || diag == NULL)
    {
        return;
    }

    *diag = bsp_serial->irq_diag;
}

void bsp_serial_reset_irq_diag(bsp_serial_t bsp_serial)
{
    bsp_serial_irq_diag_t zero = {0};

    if (bsp_serial == NULL)
    {
        return;
    }

    bsp_serial->irq_diag = zero;
}

static void bsp_serial_irq_rx_byte(bsp_serial_t bsp_serial)
{
    uint8_t data;

    bsp_serial->irq_diag.irq_entry_count++;

    if (__HAL_UART_GET_IT_SOURCE(&bsp_serial->handle, UART_IT_RXNE) &&
        __HAL_UART_GET_FLAG(&bsp_serial->handle, UART_FLAG_RXNE))
    {
        bsp_serial->irq_diag.irq_rxne_branch_count++;
        data = (uint8_t)(bsp_serial->handle.Instance->RDR & 0xFFU);
        if (serial_hw_isr(&bsp_serial->parent, SERIAL_EVENT_INT_RXDONE, &data, 1U) == OM_OK)
        {
            bsp_serial->irq_diag.irq_hw_isr_ok_count++;
        }
        else
        {
            bsp_serial->irq_diag.irq_hw_isr_err_count++;
        }
    }

    HAL_UART_IRQHandler(&bsp_serial->handle);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
    bsp_serial_t bsp_serial = (bsp_serial_t)huart;
    (void)serial_hw_isr(&bsp_serial->parent, SERIAL_EVENT_INT_TXDONE, NULL, bsp_serial->handle.TxXferSize);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
    bsp_serial_t bsp_serial = (bsp_serial_t)huart;
    bsp_serial->irq_diag.irq_error_cb_count++;
    (void)serial_hw_isr(&bsp_serial->parent, SERIAL_EVENT_ERR_OCCUR, NULL, 0U);
}

#ifdef USE_SERIAL_1
void USART1_IRQHandler(void)
{
    bsp_serial_irq_rx_byte(&g_bsp_serial[SERIAL1_IDX]);
}
#endif

#ifdef USE_SERIAL_5
void UART5_IRQHandler(void)
{
    bsp_serial_irq_rx_byte(&g_bsp_serial[SERIAL5_IDX]);
}
#endif

#ifdef USE_SERIAL_7
void UART7_IRQHandler(void)
{
    bsp_serial_irq_rx_byte(&g_bsp_serial[SERIAL7_IDX]);
}
#endif

#ifdef USE_SERIAL_8
void UART8_IRQHandler(void)
{
    bsp_serial_irq_rx_byte(&g_bsp_serial[SERIAL8_IDX]);
}
#endif

#ifdef USE_SERIAL_9
void UART9_IRQHandler(void)
{
    bsp_serial_irq_rx_byte(&g_bsp_serial[SERIAL9_IDX]);
}
#endif

#ifdef USE_SERIAL_10
void USART10_IRQHandler(void)
{
    bsp_serial_irq_rx_byte(&g_bsp_serial[SERIAL10_IDX]);
}
#endif
