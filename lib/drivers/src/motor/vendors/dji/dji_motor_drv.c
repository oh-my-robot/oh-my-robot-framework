/**
 * @file dji_motor_drv.c
 * @brief DJI 电机驱动实现
 */

#include "drivers/motor/vendors/dji/dji_motor_drv.h"
#include "drivers/peripheral/can/pal_can_dev.h"
#include <math.h>
#include <string.h>

/* --- 静态内存池管理 --- */
/** @brief TxUnit 静态内存池（避免运行时动态分配） */
static DJIMotorTxUnit s_tx_pool[DJI_MOTOR_MAX_TX_UNITS];
/** @brief TxUnit 使用标记，1 表示该槽位已分配 */
static uint8_t s_pool_usage_map[DJI_MOTOR_MAX_TX_UNITS] = {0};

/**
 * @brief 从静态池分配一个发送单元
 * @return 分配成功返回发送单元指针，池耗尽返回 `NULL`
 */
static DJIMotorTxUnit *dji_alloc_tx_unit_static(void)
{
    for (int i = 0; i < DJI_MOTOR_MAX_TX_UNITS; i++)
    {
        if (s_pool_usage_map[i] == 0)
        {
            s_pool_usage_map[i] = 1;
            memset(&s_tx_pool[i], 0, sizeof(DJIMotorTxUnit));
            INIT_LIST_HEAD(&s_tx_pool[i].list);
            return &s_tx_pool[i];
        }
    }
    return NULL;
}

/**
 * @brief 按 CAN ID 查找或创建发送单元
 * @param bus 总线管理器
 * @param canId 目标控制帧 ID
 * @return 对应发送单元指针，失败返回 `NULL`
 * @note 同一 CAN ID 的多个电机会共享同一个 TxUnit（组包发送）
 */
static DJIMotorTxUnit *dji_get_or_create_tx_unit(DJIMotorBus *bus, uint32_t can_id)
{
    DJIMotorTxUnit *unit;
    ListHead *pos;
    list_for_each(pos, &bus->txList)
    {
        unit = list_entry(pos, DJIMotorTxUnit, list);
        if (unit->canId == can_id)
            return unit;
    }

    unit = dji_alloc_tx_unit_static();
    if (unit == NULL)
    {
        /* TxUnit 池耗尽，交由上层返回 OM_ERROR_MEMORY。 */
        return NULL;
    }

    unit->canId = can_id;
    list_add_tail(&unit->list, &bus->txList);
    return unit;
}

/* --- 内部核心回调 --- */

/**
 * @brief CAN 接收回调，负责将反馈帧分发到目标电机对象
 * @param dev CAN 设备句柄
 * @param param 用户参数（DJIMotorBus*）
 * @param filterHandle 当前命中的过滤器句柄
 * @param count 本次可读取的报文数量
 */
static void dji_rx_callback(Device *dev, void *param, CanFilterHandle filter_handle, size_t count)
{
    DJIMotorBus *bus = (DJIMotorBus *)param;
    CanUserMsg msg;
    uint8_t buf[8];
    msg.userBuf = buf;
    msg.filterHandle = filter_handle;

    for (size_t i = 0; i < count; i++)
    {
        if (device_read(dev, 0, &msg, 1) > 0)
        {
            const uint32_t id = msg.dsc.id;

            /* 输入范围保护，避免访问 rxMap 越界。 */
            if (id < DJI_RX_ID_START || id > DJI_RX_ID_END)
            {
                /* 非法 ID 直接丢弃，继续处理后续报文。 */
                continue;
            }

            DJIMotorDrv *motor = bus->rxMap[id - DJI_RX_ID_START];
            if (motor)
            {
                uint8_t *d = msg.userBuf;

                /* 步骤 1：保存上一帧关键状态，供差分与边沿检测使用。 */
                motor->measure.lastAngle = motor->measure.angle;
                motor->measure.lastErrorCode = motor->measure.errorCode;

                /* 步骤 2：解析 DJI 反馈协议。 */
                motor->measure.angle = (int16_t)(d[0] << 8 | d[1]);
                motor->measure.velocity = (int16_t)(d[2] << 8 | d[3]);
                motor->measure.current = (int16_t)(d[4] << 8 | d[5]);
                motor->measure.temp = d[6];
                motor->measure.errorCode = d[7];

                /* 步骤 3：估算多圈角度。
                 * 8192 编码为一圈，4096 作为半圈阈值判断过零方向。
                 */
                int16_t diff = motor->measure.angle - motor->measure.lastAngle;
                if (diff < -4096)
                    motor->measure.roundCount++;
                else if (diff > 4096)
                    motor->measure.roundCount--;
                motor->measure.totalAngle = motor->measure.roundCount * 8192 + motor->measure.angle;

                /* 步骤 4：错误码边沿触发回调，避免持续错误导致回调风暴。 */
                if (motor->errorCallback && (motor->measure.errorCode != motor->measure.lastErrorCode))
                    motor->errorCallback(motor, motor->measure.errorCode);
            }
        }
    }
}

/* --- Core API --- */

/** @copydoc dji_motor_bus_init */
OmRet dji_motor_bus_init(DJIMotorBus *bus, Device *can_dev)
{
    if (!bus || !can_dev)
        return OM_ERROR_PARAM;

    memset(bus, 0, sizeof(DJIMotorBus));
    bus->canDev = can_dev;
    bus->filterHandle = 0;
    INIT_LIST_HEAD(&bus->txList);

    /* 过滤条件覆盖 0x200~0x20F（掩码 0x7F0），匹配 DJI 电机反馈 ID 段。 */
    CanFilterAllocArg alloc_arg = {0};
    alloc_arg.request.workMode = CAN_FILTER_MODE_MASK;
    alloc_arg.request.idType = CAN_FILTER_ID_STD;
    alloc_arg.request.id = 0x200;
    alloc_arg.request.mask = 0x7F0;
    alloc_arg.request.rxCallback = dji_rx_callback;
    alloc_arg.request.param = bus;

    OmRet ret = device_ctrl(can_dev, CAN_CMD_FILTER_ALLOC, &alloc_arg);
    if (ret != OM_OK)
        return ret;
    bus->filterHandle = alloc_arg.handle;
    return OM_OK;
}

/** @copydoc dji_motor_register */
OmRet dji_motor_register(DJIMotorBus *bus, DJIMotorDrv *motor, DJIMotorType type, uint8_t id, DJIMotorCtrlMode mode)
{
    if (!bus || !motor || id == 0 || type > DJI_MOTOR_TYPE_UNKNOWN)
        return OM_ERROR_PARAM;

    memset(motor, 0, sizeof(DJIMotorDrv)); /* 清空句柄，消除历史状态影响。 */
    /* 接收 ID 与电机类型相关：
     * C6x0: 0x201~0x208
     * GM6020: 0x205~0x20B
     */
    uint16_t rx_id;
    if (type == DJI_MOTOR_TYPE_GM6020)
        rx_id = 0x204 + id;
    else
        rx_id = 0x200 + id;

    if (rx_id < DJI_RX_ID_START || rx_id > DJI_RX_ID_END)
        return OM_ERROR_PARAM;
    uint8_t map_idx = rx_id - DJI_RX_ID_START;
    if (bus->rxMap[map_idx] != NULL)
        return OM_ERR_CONFLICT;

    uint32_t target_can_id = 0;
    uint8_t buf_idx = 0;

    /* 根据“类型 + 模式 + 物理 ID”推导目标控制帧与槽位偏移。 */
    if (type == DJI_MOTOR_TYPE_GM6020)
    {
        if (mode == DJI_CTRL_MODE_VOLTAGE)
        {
            target_can_id = (id <= 4) ? DJI_TX_ID_MIX_1_FF : DJI_TX_ID_GM6020_V_5_7;
            buf_idx = (id - (id <= 4 ? 1 : 5)) * 2;
        }
        else
        {
            target_can_id = (id <= 4) ? DJI_TX_ID_GM6020_C_1_4 : DJI_TX_ID_GM6020_C_5_7;
            buf_idx = (id - (id <= 4 ? 1 : 5)) * 2;
        }
    }
    else
    {
        target_can_id = (id <= 4) ? DJI_TX_ID_C6X0_1_4 : DJI_TX_ID_MIX_1_FF;
        buf_idx = (id - (id <= 4 ? 1 : 5)) * 2;
    }

    DJIMotorTxUnit *unit = dji_get_or_create_tx_unit(bus, target_can_id);
    if (unit == NULL)
        return OM_ERROR_MEMORY;

    /* usageMask 每一位代表一个 2-byte 控制槽，置位冲突即表示映射冲突。 */
    uint8_t slot_bit = (1U << (buf_idx / 2U));
    if (unit->usageMask & slot_bit)
    {
        /* 槽位冲突，说明电机 ID/模式映射发生重叠。 */
        return OM_ERR_CONFLICT;
    }

    /* 电流/电压编码值到物理量的换算比例。 */
    switch (type)
    {
    case DJI_MOTOR_TYPE_C610:
        motor->scale = 10.0f / 16384.0f;
        break;
    case DJI_MOTOR_TYPE_C620:
        motor->scale = 20.0f / 16384.0f;
        break;
    case DJI_MOTOR_TYPE_GM6020:
        motor->scale = (mode == DJI_CTRL_MODE_CURRENT) ? 3.0f / 16384.0f : 24.0f / 25000.0f;
        break;
    default:
        /* 非法类型，拒绝注册。 */
        return OM_ERROR_PARAM;
    }

    unit->usageMask |= slot_bit;
    motor->link.rxId = rx_id;
    motor->link.txUnit = unit;
    motor->link.txBufIdx = buf_idx;
    motor->link.type = type;
    motor->mode = mode;

    /* 首帧通常可能存在跳变，上层可按需丢弃首帧或延迟闭环。 */
    motor->measure.lastAngle = 0;
    bus->rxMap[map_idx] = motor;

    return OM_OK;
}

/** @copydoc dji_motor_set_output */
void dji_motor_set_output(DJIMotorDrv *motor, int16_t output)
{
    if (!motor || !motor->link.txUnit)
        return;

    /* 仅更新缓存与脏标记，不直接触发 IO，便于控制循环统一调度发送。 */
    motor->targetOutput = output;
    uint8_t *b = motor->link.txUnit->txBuffer;
    uint8_t i = motor->link.txBufIdx;
    b[i] = (uint8_t)(output >> 8);
    b[i + 1] = (uint8_t)(output & 0xFF);
    motor->link.txUnit->isDirty = 1;
}

/** @copydoc dji_motor_bus_sync */
void dji_motor_bus_sync(DJIMotorBus *bus)
{
    if (!bus)
        return;

    DJIMotorTxUnit *unit;
    ListHead *pos;
    list_for_each(pos, &bus->txList)
    {
        unit = list_entry(pos, DJIMotorTxUnit, list);
        if (unit->usageMask && unit->isDirty)
        {
            CanUserMsg msg;
            msg.dsc = CAN_DATA_MSG_DSC_INIT(unit->canId, CAN_IDE_STD, 8);
            msg.userBuf = unit->txBuffer;
            int write_ret = (int)device_write(bus->canDev, 0, &msg, 1);

            /* 仅在发送成功时清脏；失败时保留脏位，下一周期自动重试。 */
            if (write_ret > 0)
                unit->isDirty = 0;
        }
    }
}
