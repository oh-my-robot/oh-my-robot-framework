#include "bsp_can.h"

void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef* hcan)
{
    BspCan_t BspCan = (BspCan_t)hcan;
    hal_can_isr(&BspCan->parent, CAN_ISR_EVENT_INT_TX_DONE, 0); // 编号0
}

void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef* hcan)
{
    BspCan_t BspCan = (BspCan_t)hcan;
    hal_can_isr(&BspCan->parent, CAN_ISR_EVENT_INT_TX_DONE, 1); // 编号1
}

void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef* hcan)
{
    BspCan_t BspCan = (BspCan_t)hcan;
    hal_can_isr(&BspCan->parent, CAN_ISR_EVENT_INT_TX_DONE, 2); // 编号2
}

void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef* hcan)
{
    (void)hcan;
}

void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef* hcan)
{
    (void)hcan;
}

void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef* hcan)
{
    (void)hcan;
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan)
{
    BspCan_t BspCan = (BspCan_t)hcan;
    while (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0))
    {
        hal_can_isr(&BspCan->parent, CAN_ISR_EVENT_INT_RX_DONE, 0); // 编号0
    }
}

void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef* hcan)
{
    BspCan_t BspCan = (BspCan_t)hcan;
    while (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO1))
    {
        hal_can_isr(&BspCan->parent, CAN_ISR_EVENT_INT_RX_DONE, 1); // 编号1
    }
}

void HAL_CAN_RxFifo0FullCallback(CAN_HandleTypeDef* hcan)
{
    (void)hcan;
    // can_error_isr(&BspCan->parent, CAN_ERR_EVENT_RX_OVERFLOW, 0);
}

void HAL_CAN_RxFifo1FullCallback(CAN_HandleTypeDef* hcan)
{
    (void)hcan;
    // can_error_isr(&BspCan->parent, CAN_ERR_EVENT_RX_OVERFLOW, 1);
}

void HAL_CAN_SleepCallback(CAN_HandleTypeDef* hcan)
{
    (void)hcan;
}

void HAL_CAN_WakeUpFromRxMsgCallback(CAN_HandleTypeDef* hcan)
{
    (void)hcan;
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef* hcan)
{
    BspCan_t BspCan = (BspCan_t)hcan;
    CanStatusManager* statusManager = &BspCan->parent.statusManager;
    // 超级无敌大屎山……应该有可以优化的地方，但是暂时没时间去管了
    // 先处理仲裁失败和发送失败，因为这两个是最常用的
    // 邮箱0
    if (hcan->ErrorCode & HAL_CAN_ERROR_TX_ALST0)
    {
        can_error_isr(&BspCan->parent, CAN_ERR_EVENT_ARBITRATION_FAIL, 0);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_ALST0;
    }
    else if (hcan->ErrorCode & HAL_CAN_ERROR_TX_TERR0)
    {
        can_error_isr(&BspCan->parent, CAN_ERR_EVENT_TX_FAIL, 0);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_TERR0;
    }
    // 邮箱1
    if (hcan->ErrorCode & HAL_CAN_ERROR_TX_ALST1)
    {
        can_error_isr(&BspCan->parent, CAN_ERR_EVENT_ARBITRATION_FAIL, 1);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_ALST1;
    }
    else if (hcan->ErrorCode & HAL_CAN_ERROR_TX_TERR1)
    {
        can_error_isr(&BspCan->parent, CAN_ERR_EVENT_TX_FAIL, 1);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_TERR1;
    }
    // 邮箱2
    if (hcan->ErrorCode & HAL_CAN_ERROR_TX_ALST2)
    {
        can_error_isr(&BspCan->parent, CAN_ERR_EVENT_ARBITRATION_FAIL, 2);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_ALST2;
    }
    else if (hcan->ErrorCode & HAL_CAN_ERROR_TX_TERR2)
    {
        can_error_isr(&BspCan->parent, CAN_ERR_EVENT_TX_FAIL, 2);
        hcan->ErrorCode &= ~HAL_CAN_ERROR_TX_TERR2;
    }

    // 接收FIFO0
    if (hcan->ErrorCode & HAL_CAN_ERROR_RX_FOV0)
    {
        statusManager->errCounter.rxHwOverFlowCnt++;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_RX_FOV0;
    }
    if (hcan->ErrorCode & HAL_CAN_ERROR_RX_FOV1)
    {
        statusManager->errCounter.rxHwOverFlowCnt++;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_RX_FOV1;
    }
    // CAN协议错误处理

    if (hcan->ErrorCode & HAL_CAN_ERROR_CRC)
    {
        statusManager->errCounter.crcErrCnt++;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_CRC;
    }
    if (hcan->ErrorCode & HAL_CAN_ERROR_FOR)
    {
        statusManager->errCounter.formatErrCnt++;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_FOR;
    }
    if (hcan->ErrorCode & HAL_CAN_ERROR_STF)
    {
        statusManager->errCounter.stuffErrCnt++;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_STF;
    }
    if (hcan->ErrorCode & HAL_CAN_ERROR_ACK)
    {
        statusManager->errCounter.ackErrCnt++;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_ACK;
    }
    if (hcan->ErrorCode & HAL_CAN_ERROR_BR)
    {
        statusManager->errCounter.bitRecessiveErrCnt++;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_BR;
    }
    if (hcan->ErrorCode & HAL_CAN_ERROR_BD)
    {
        statusManager->errCounter.bitDominantErrCnt++;
        hcan->ErrorCode &= ~HAL_CAN_ERROR_BD;
    }
    // 获取错误计数
    uint32_t err = hcan->Instance->ESR;
    statusManager->errCounter.txErrCnt = (err >> 16 & 0xFF);
    statusManager->errCounter.rxErrCnt = (err >> 24);
    // 总线状态检查
    can_error_isr(&BspCan->parent, CAN_ERR_EVENT_BUS_STATUS, 0);
}

#ifdef USE_CAN1
void CAN1_RX0_IRQHandler(void)
{
    BspCan_t BspCan = &gBspCan[BSP_CAN1_IDX];
    HAL_CAN_IRQHandler(&BspCan->handle);
}

void CAN1_RX1_IRQHandler(void)
{
    BspCan_t BspCan = &gBspCan[BSP_CAN1_IDX];
    HAL_CAN_IRQHandler(&BspCan->handle);
}

void CAN1_TX_IRQHandler(void)
{
    BspCan_t BspCan = &gBspCan[BSP_CAN1_IDX];
    HAL_CAN_IRQHandler(&BspCan->handle);
}

void CAN1_SCE_IRQHandler(void)
{
    BspCan_t BspCan = &gBspCan[BSP_CAN1_IDX];
    HAL_CAN_IRQHandler(&BspCan->handle);
}

#endif

#ifdef USE_CAN2
void CAN2_RX0_IRQHandler(void)
{
    BspCan_t BspCan = &gBspCan[BSP_CAN2_IDX];
    HAL_CAN_IRQHandler(&BspCan->handle);
}

void CAN2_RX1_IRQHandler(void)
{
    BspCan_t BspCan = &gBspCan[BSP_CAN2_IDX];
    HAL_CAN_IRQHandler(&BspCan->handle);
}

void CAN2_TX_IRQHandler(void)
{
    BspCan_t BspCan = &gBspCan[BSP_CAN2_IDX];
    HAL_CAN_IRQHandler(&BspCan->handle);
}

void CAN2_SCE_IRQHandler(void)
{
    BspCan_t BspCan = &gBspCan[BSP_CAN2_IDX];
    HAL_CAN_IRQHandler(&BspCan->handle);
}
#endif
