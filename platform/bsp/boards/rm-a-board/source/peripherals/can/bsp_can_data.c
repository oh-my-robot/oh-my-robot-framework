/**
 * @file  bsp_can_data.c
 * @brief rm-a-board CAN 板数据（板瘦身：板=数据，供共享适配层 bsp_can_f4.c 消费）
 * @details 契约见 can/bsp_can_f4.h；本文件定义全部板特有数据表。宏 BSP_CAN_COUNT
 *          在板 shim bsp_can.h 定义，与 gBspCan 条目数由下方编译期校验钳制。
 */
#include "bsp_can.h"

/* 常用波特率预设参数（rm-a-board 当前 APB1 = 45MHz；末尾 psc=0 哨兵项） */
const CanTimeCfg BspCanBitTimeTable[] = {
    /* 10K ~ 500K 与 1M 统一采用 15TQ / 86.7% 采样点风格：
     * bitrate = 45MHz / PSC / (1 + 12 + 2)
     */
    {CAN_BAUD_10K, 300, {CAN_TSEG1_12TQ, CAN_TSEG2_2TQ, CAN_SYNCJW_1TQ}},
    {CAN_BAUD_20K, 150, {CAN_TSEG1_12TQ, CAN_TSEG2_2TQ, CAN_SYNCJW_1TQ}},
    {CAN_BAUD_50K, 60, {CAN_TSEG1_12TQ, CAN_TSEG2_2TQ, CAN_SYNCJW_1TQ}},
    {CAN_BAUD_100K, 30, {CAN_TSEG1_12TQ, CAN_TSEG2_2TQ, CAN_SYNCJW_1TQ}},
    {CAN_BAUD_125K, 24, {CAN_TSEG1_12TQ, CAN_TSEG2_2TQ, CAN_SYNCJW_1TQ}},
    {CAN_BAUD_250K, 12, {CAN_TSEG1_12TQ, CAN_TSEG2_2TQ, CAN_SYNCJW_1TQ}},
    {CAN_BAUD_500K, 6, {CAN_TSEG1_12TQ, CAN_TSEG2_2TQ, CAN_SYNCJW_1TQ}},
    /* 45MHz 下无法精确整分出 800kbps，这里选择最接近且采样点仍合理的一组：
     * 45MHz / 4 / (1 + 11 + 2) = 803.6kbps, sample point = 85.7%
     */
    {CAN_BAUD_800K, 4, {CAN_TSEG1_11TQ, CAN_TSEG2_2TQ, CAN_SYNCJW_1TQ}},
    /* ST 官方推荐的 bxCAN 1Mbps 配置之一：
     * APB1 = 45MHz, PSC = 3, BS1 = 12TQ, BS2 = 2TQ, SJW = 1TQ
     * 实际位率 = 45MHz / 3 / (1 + 12 + 2) = 1Mbps
     * 采样点 = (1 + 12) / (1 + 12 + 2) = 86.7%
     */
    {CAN_BAUD_1M, 3, {CAN_TSEG1_12TQ, CAN_TSEG2_2TQ, CAN_SYNCJW_1TQ}},
    {0, 0, {0, 0, 0}}, /* 哨兵：psc=0 标记表尾，供 extern 表遍历终止 */
};

/* filter bank 分配（F4 家族规则：CAN1=0..13，CAN2=14..27） */
static const uint8_t gBspCan1HwBanks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static const uint8_t gBspCan2HwBanks[] = {14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};

/* 实例表（顺序 = CanIdx_e 枚举：CAN1 在前） */
BspCan_s gBspCan[] = {
    BSP_CAN_STATIC_INIT(CAN1, "can1", CAN1_REG_PARAMS, gBspCan1HwBanks, 14),
    BSP_CAN_STATIC_INIT(CAN2, "can2", CAN2_REG_PARAMS, gBspCan2HwBanks, 14),
};

/* 引脚表 [实例][RX, TX] */
const BspCanPinCfg BspCanPinTable[BSP_CAN_COUNT][2] = {
    {{GPIOD, GPIO_PIN_0, GPIO_AF9_CAN1}, {GPIOD, GPIO_PIN_1, GPIO_AF9_CAN1}},
    {{GPIOB, GPIO_PIN_12, GPIO_AF9_CAN2}, {GPIOB, GPIO_PIN_13, GPIO_AF9_CAN2}},
};

/* 中断表 [实例][RX0, RX1, TX, SCE]（优先级低于 RTOS 可屏蔽阈值） */
const BspCanIrqCfg BspCanIrqTable[BSP_CAN_COUNT][4] = {
    {{CAN1_RX0_IRQn, 5}, {CAN1_RX1_IRQn, 5}, {CAN1_TX_IRQn, 5}, {CAN1_SCE_IRQn, 5}},
    {{CAN2_RX0_IRQn, 5}, {CAN2_RX1_IRQn, 5}, {CAN2_TX_IRQn, 5}, {CAN2_SCE_IRQn, 5}},
};

/* 一致性兜底：BSP_CAN_COUNT 必须等于 gBspCan 条目数（C99 技巧，编译期校验） */
typedef char bsp_can_count_ok[(BSP_CAN_COUNT == sizeof(gBspCan) / sizeof(gBspCan[0])) ? 1 : -1];
