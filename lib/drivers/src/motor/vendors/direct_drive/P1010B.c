/**
 * @file P1010B.c
 * @brief P1010B 直驱电机 CAN 首版驱动实现
 * @note 首版只实现 CAN 运行路径，不实现 RS485
 */

#include "drivers/motor/vendors/direct_drive/P1010B.h"
#include "osal/osal_core.h"
#include "osal/osal_time.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* 私有常量与位域映射*/
/* -------------------------------------------------------------------------- */

/** 每个 0x32/0x33 分组帧包含 4 个电机槽位*/
#define P1010B_GROUP_SLOT_COUNT (4U)
/** reply_base_id 掩码（高 4 位） */
#define P1010B_REPLY_ID_MASK (0xF0U)
/** motor_id 掩码（低 4 位） */
#define P1010B_REPLY_ID_LOW_MASK (0x0FU)
/** 协议应答基地址最小 nibble（0x50）*/
#define P1010B_REPLY_BASE_MIN_NIBBLE (0x5U)
/** 协议应答基地址最大 nibble（0xB0）*/
#define P1010B_REPLY_BASE_MAX_NIBBLE (0xBU)
/** reply_base_id 线性分发表项大小 */
#define P1010B_REPLY_DISPATCH_ENTRY_COUNT (P1010B_REPLY_BASE_MAX_NIBBLE - P1010B_REPLY_BASE_MIN_NIBBLE + 1U)
/** 默认同步事务超时（ms）*/
#define P1010B_DEFAULT_SYNC_TIMEOUT_MS (20U)
/** 非法模式目标 scale */
#define P1010B_TARGET_SCALE_INVALID (0.0f)
/** 开电流/位置模式目标 scale */
#define P1010B_TARGET_SCALE_X100 (100.0f)
/** 速度模式目标 scale */
#define P1010B_TARGET_SCALE_X10 (10.0f)
/** fault signature: calibrated 位偏移 */
#define P1010B_FAULT_SIGNATURE_CALIBRATED_SHIFT (0U)
/** fault signature: faultCode 起始位*/
#define P1010B_FAULT_SIGNATURE_FAULT_SHIFT (1U)
/** fault signature: faultCode 有效掩码 */
#define P1010B_FAULT_SIGNATURE_FAULT_MASK (0x0FU)
/** fault signature: alarmCode 起始位*/
#define P1010B_FAULT_SIGNATURE_ALARM_SHIFT (5U)
/** fault signature: alarmCode 有效掩码 */
#define P1010B_FAULT_SIGNATURE_ALARM_MASK (0x7FU)
/** reply_base_id -> 分发表索引*/
#define P1010B_REPLY_DISPATCH_INDEX(_replyBaseId) \
    ((uint8_t)((((uint8_t)(_replyBaseId)) >> 4U) - P1010B_REPLY_BASE_MIN_NIBBLE))

static const P1010BCommandDescriptor *p1010b_internal_find_command_descriptor(P1010BCommand command);
static const P1010BRxDispatchDescriptor *p1010b_internal_find_rx_dispatch_descriptor(uint8_t reply_base_id);

/* -------------------------------------------------------------------------- */
/* 基础工具函数                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief 校验电机 ID 是否在协议支持范围内
 */
static inline bool p1010b_internal_is_valid_motor_id(uint8_t motor_id)
{
    return (motor_id >= P1010B_MOTOR_ID_MIN && motor_id <= P1010B_MOTOR_ID_MAX);
}

/**
 * @brief 按大端读取 16 位有符号数
 */
static inline int16_t p1010b_internal_read_be_i16(const uint8_t *buffer)
{
    return (int16_t)(((uint16_t)buffer[0] << 8U) | (uint16_t)buffer[1]);
}

/**
 * @brief 按大端读取 32 位有符号数
 */
static inline int32_t p1010b_internal_read_be_i32(const uint8_t *buffer)
{
    return (int32_t)(((uint32_t)buffer[0] << 24U) | ((uint32_t)buffer[1] << 16U) | ((uint32_t)buffer[2] << 8U) |
                     (uint32_t)buffer[3]);
}

/**
 * @brief int32 -> int16 饱和截断
 */
static inline int16_t p1010b_internal_clamp_i16(int32_t value)
{
    if (value > INT16_MAX)
    {
        return INT16_MAX;
    }
    if (value < INT16_MIN)
    {
        return INT16_MIN;
    }
    return (int16_t)value;
}

/**
 * @brief 按当前模式把物理量目标值换算为协议原始值
 * @details
 * - 开环/电流/位置模式：x100
 * - 速度模式：x10
 * - 使用四舍五入后再做 int16 饱和
 */
static inline int16_t p1010b_internal_scale_target(float scale, float target_value)
{
    float scaled = target_value * scale;
    if (scaled >= 0.0f)
    {
        scaled += 0.5f;
    }
    else
    {
        scaled -= 0.5f;
    }
    return p1010b_internal_clamp_i16((int32_t)scaled);
}

/**
 * @brief 将电机 ID 映射到 0x32/0x33 分组
 * @return 0 表示 1~4 组；1 表示 5~8 组
 */
static inline uint8_t p1010b_internal_get_group_index(uint8_t motor_id)
{
    return (motor_id <= 4U) ? 0U : 1U;
}

/**
 * @brief 计算电机 ID 在分组帧中的槽位索引（0~3）
 */
static inline uint8_t p1010b_internal_get_slot_index(uint8_t motor_id)
{
    return (uint8_t)((motor_id - 1U) % P1010B_GROUP_SLOT_COUNT);
}

/**
 * @brief 参数白名单判定
 * @note 仅白名单参数允许写入，防止高风险参数被误写
 */
static inline bool p1010b_internal_is_parameter_whitelisted(uint8_t parameter_id)
{
    switch (parameter_id)
    {
    case P1010B_PARAM_FAULT_MASK:
    case P1010B_PARAM_HEARTBEAT_ENABLE:
    case P1010B_PARAM_WORK_MODE:
    case P1010B_PARAM_MOTOR_ID:
    case P1010B_PARAM_CAN_BAUD_MODE:
    case P1010B_PARAM_HEARTBEAT_TIME:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 打包分组控制帧载荷
 * @details
 * 每个电机 2 字节，顺序对应组内槽位 0~3
 */
static void p1010b_internal_pack_group_payload(const P1010BBus *bus, uint8_t group_index, uint8_t payload[P1010B_CAN_DLC])
{
    for (uint8_t slot = 0; slot < P1010B_GROUP_SLOT_COUNT; slot++)
    {
        int16_t raw_value = bus->groupTargetsRaw[group_index][slot];
        payload[(slot * 2U)] = (uint8_t)(((uint16_t)raw_value >> 8U) & 0xFFU);
        payload[(slot * 2U) + 1U] = (uint8_t)((uint16_t)raw_value & 0xFFU);
    }
}

/**
 * @brief 发送 1 帧标准 CAN 数据
 * @param bus 总线上下文
 * @param can_id 标准 ID
 * @param payload 8 字节载荷
 * @return `OM_OK` 发送成功功；失败返回错误
 */
static OmRet p1010b_internal_send_can_frame(P1010BBus *bus, uint16_t can_id, const uint8_t payload[P1010B_CAN_DLC])
{
    CanUserMsg message;

    if (!bus || !bus->canDevice || !payload)
        return OM_ERROR_PARAM;
    message.dsc = CAN_DATA_MSG_DSC_INIT(can_id, CAN_IDE_STD, P1010B_CAN_DLC);
    message.filterHandle = 0;
    message.userBuf = (uint8_t *)payload;
    if (device_write(bus->canDevice, 0, &message, 1) == 0U)
        return OM_ERROR;
    return OM_OK;
}

/**
 * @brief 计算目标缩放系数，调用方负责回写到缓存
 */
static float p1010b_internal_update_target_scale_cache(P1010BMode mode)
{
    switch (mode)
    {
    case P1010B_MODE_OPEN_LOOP:
    case P1010B_MODE_CURRENT:
    case P1010B_MODE_POSITION:
        return P1010B_TARGET_SCALE_X100;
    case P1010B_MODE_SPEED:
        return P1010B_TARGET_SCALE_X10;
    default:
        return P1010B_TARGET_SCALE_INVALID;
    }
}

/**
 * @brief 更新在线状态并触发边沿回调
 */
static inline void p1010b_internal_mark_online(P1010BDriver *driver, bool online)
{
    if (!driver)
    {
        return;
    }
    if (driver->telemetry.online == online)
    {
        return;
    }
    driver->telemetry.online = online;
    if (driver->callbacks.onOnlineChanged)
    {
        driver->callbacks.onOnlineChanged(driver, online);
    }
}

/**
 * @brief 解析同步事务等待超时
 */
static inline uint32_t p1010b_internal_resolve_sync_timeout_ms(const P1010BDriver *driver, uint32_t timeout_ms)
{
    if (timeout_ms > 0U)
    {
        return timeout_ms;
    }
    if (driver && driver->config.requestTimeoutMs > 0U)
    {
        return driver->config.requestTimeoutMs;
    }
    return P1010B_DEFAULT_SYNC_TIMEOUT_MS;
}

/**
 * @brief 在锁保护下清空同步事务状态
 */
static inline void p1010b_internal_clear_sync_transaction_locked(P1010BDriver *driver)
{
    if (!driver)
    {
        return;
    }
    driver->sync.pending = false;
    driver->sync.pendingCommand = P1010B_COMMAND_NONE;
    driver->sync.expectedReplyBaseId = 0U;
    driver->sync.expectedParameterId = 0U;
    driver->sync.expectedStateCommand = 0U;
}

/**
 * @brief 初始化响应对象
 */
static inline void p1010b_internal_init_response(P1010BResponse *response, P1010BCommand command, OmRet result)
{
    if (!response)
    {
        return;
    }

    memset(response, 0, sizeof(P1010BResponse));
    response->command = command;
    response->result = result;
}

/**
 * @brief 取消当前同步事务（线程上下文）
 */
static void p1010b_internal_cancel_sync_transaction(P1010BDriver *driver)
{
    if (!driver)
        return;
    osal_irq_lock_task();
    p1010b_internal_clear_sync_transaction_locked(driver);
    osal_irq_unlock_task();
}

/**
 * @brief 启动同步事务并登记匹配条件
 */
static OmRet p1010b_internal_begin_sync_transaction(P1010BDriver *driver, P1010BCommand command, const P1010BEncodedRequest *encoded)
{
    if (!driver || !encoded)
    {
        return OM_ERROR_PARAM;
    }

    /* 清空可能遗留的完成量信号，确保本次 wait 只对应当前事务*/
    while (completion_wait(&driver->sync.completion, 0U) == OM_OK)
    {
    }

    osal_irq_lock_task();
    if (driver->sync.pending)
    {
        osal_irq_unlock_task();
        return OM_ERROR_BUSY;
    }
    driver->sync.pending = true;
    driver->sync.pendingCommand = command;
    driver->sync.expectedReplyBaseId = encoded->ackBaseId;
    driver->sync.expectedParameterId = encoded->expectedParameterId;
    driver->sync.expectedStateCommand = encoded->expectedStateCommand;
    driver->sync.result = OM_ERROR_TIMEOUT;
    driver->sync.timestampMs = 0U;
    p1010b_internal_init_response(&driver->sync.response, command, OM_ERROR_TIMEOUT);
    osal_irq_unlock_task();
    return OM_OK;
}

/**
 * @brief 等待同步事务完成
 */
static OmRet p1010b_internal_wait_sync_transaction(P1010BDriver *driver, uint32_t timeout_ms, P1010BResponse *response)
{
    OmRet ret;

    ret = completion_wait(&driver->sync.completion, p1010b_internal_resolve_sync_timeout_ms(driver, timeout_ms));
    if (ret != OM_OK)
    {
        p1010b_internal_cancel_sync_transaction(driver);
        if (ret == OM_ERROR_TIMEOUT)
            /* 超时后消费可能迟到的 done 信号，避免污染下一次同步事务*/
            while (completion_wait(&driver->sync.completion, 0U) == OM_OK)
            {
            }
        return ret;
    }

    osal_irq_lock_task();
    ret = driver->sync.result;
    if (response)
        *response = driver->sync.response;
    osal_irq_unlock_task();
    return ret;
}

/**
 * @brief 发送同步事务请求（支持重试）
 */
static OmRet p1010b_internal_send_sync_transaction(P1010BDriver *driver, const P1010BRequest *request, const P1010BEncodedRequest *encoded, P1010BResponse *response)
{
    OmRet ret = OM_ERROR;
    uint32_t attempt_count;

    attempt_count = (uint32_t)driver->config.maxRetryCount + 1U;
    for (uint32_t attempt_index = 0U; attempt_index < attempt_count; attempt_index++)
    {
        ret = p1010b_internal_begin_sync_transaction(driver, request->command, encoded);
        if (ret != OM_OK)
            break;

        ret = p1010b_internal_send_can_frame(driver->bus, encoded->requestCanId, encoded->payload);
        if (ret != OM_OK)
        {
            p1010b_internal_cancel_sync_transaction(driver);
            break;
        }
        driver->telemetry.lastSuccessTxTimestampMs = osal_time_now_monotonic();

        ret = p1010b_internal_wait_sync_transaction(driver, request->timeoutMs, response);
        if (ret == OM_OK)
            break;
    }
    return ret;
}

/**
 * @brief 通过描述符判定同步应答是否匹配
 */
static bool p1010b_internal_is_sync_reply_matched_by_descriptor(const P1010BCommandDescriptor *descriptor,
                                                                const P1010BDriver *driver, const P1010BRawFrame *frame)
{
    uint8_t reply_base_id;

    if (!descriptor || !driver || !frame)
    {
        return false;
    }
    if (descriptor->ackBaseId == P1010B_CAN_ACK_NONE)
    {
        return false;
    }

    reply_base_id = (uint8_t)(frame->canId & P1010B_REPLY_ID_MASK);
    if (reply_base_id != descriptor->ackBaseId)
    {
        return false;
    }

    if (!descriptor->ackMatchFn)
    {
        return true;
    }
    return descriptor->ackMatchFn(driver, frame);
}

/**
 * @brief 由描述符在 ISR 内解码同步应答
 */
static void p1010b_internal_decode_sync_reply_by_descriptor_in_isr(const P1010BCommandDescriptor *descriptor, P1010BDriver *driver, const P1010BRawFrame *frame, P1010BIsrCallbackContext *callback_context)
{
    if (!descriptor || !driver || !frame)
    {
        return;
    }

    p1010b_internal_init_response(&driver->sync.response, descriptor->command, OM_OK);
    driver->sync.response.timestampMs = frame->timestampMs;
    memcpy(driver->sync.response.ackPayload, frame->payload, sizeof(frame->payload));
    if (descriptor->decodeAckFn)
    {
        descriptor->decodeAckFn(driver, frame, &driver->sync.response, callback_context);
    }
}

/**
 * @brief ISR 中尝试匹配并完成同步事务
 */
static void p1010b_internal_try_complete_sync_transaction(P1010BBus *bus, P1010BDriver *driver, const P1010BRawFrame *frame)
{
    OsalIrqIsrState irq_state;
    const P1010BCommandDescriptor *descriptor = NULL;
    bool matched = false;
    P1010BIsrCallbackContext callback_context = {0};

    if (!bus || !driver || !frame)
    {
        return;
    }

    irq_state = osal_irq_lock_from_isr();
    if (!driver->sync.pending)
    {
        osal_irq_unlock_from_isr(irq_state);
        return;
    }

    descriptor = p1010b_internal_find_command_descriptor(driver->sync.pendingCommand);
    matched = p1010b_internal_is_sync_reply_matched_by_descriptor(descriptor, driver, frame);
    if (matched)
    {
        p1010b_internal_decode_sync_reply_by_descriptor_in_isr(descriptor, driver, frame, &callback_context);
        driver->sync.result = OM_OK;
        driver->sync.timestampMs = frame->timestampMs;
        /* 先清 pending，再 done，避免线程侧唤醒后读到未收敛状态*/
        p1010b_internal_clear_sync_transaction_locked(driver);
        (void)completion_done(&driver->sync.completion);
    }
    else
    {
        bus->rxLateReadParamCount++;
    }
    osal_irq_unlock_from_isr(irq_state);

    if (callback_context.triggerParamReadCallback && driver->callbacks.onParamRead)
    {
        /* 回调放在解锁后执行，缩短 ISR 临界区*/
        driver->callbacks.onParamRead(driver, callback_context.callbackParameterId, callback_context.callbackParameterValue, OM_OK,
                                      frame->timestampMs);
    }
}

/**
 * @brief 解析给定反馈帧（0x50 + id）
 * @note 输出为语义化量，不向上层暴露原始字节
 */
static void p1010b_internal_update_feedback(P1010BDriver *driver, const P1010BRawFrame *frame)
{
    driver->telemetry.feedback.speedRpm = (float)p1010b_internal_read_be_i16(&frame->payload[0]) / 10.0f;
    driver->telemetry.feedback.iqAmpere = (float)p1010b_internal_read_be_i16(&frame->payload[2]) / 100.0f;
    driver->telemetry.feedback.absolutePosition = (uint16_t)p1010b_internal_read_be_i16(&frame->payload[4]);
    driver->telemetry.feedback.busVoltage = (float)p1010b_internal_read_be_i16(&frame->payload[6]) / 10.0f;
    driver->telemetry.feedback.timestampMs = frame->timestampMs;

    if (driver->callbacks.onFeedback)
    {
        driver->callbacks.onFeedback(driver, &driver->telemetry.feedback);
    }
}

/**
 * @brief 将有效故障字段打包为 32 位签名
 * @details
 * 仅打包规格书定义的有效字段：
 * - bit0: calibrated
 * - bit1..4: faultCode
 * - bit5..11: alarmCode
 * `statusBits` 为保留位，不参与回调触发判定
 */
static inline uint32_t p1010b_internal_pack_fault_signature(const P1010BFaultState *fault_state)
{
    uint32_t signature = 0U;

    if (!fault_state)
    {
        return 0U;
    }

    signature |= ((uint32_t)(fault_state->calibrated ? 1U : 0U) << P1010B_FAULT_SIGNATURE_CALIBRATED_SHIFT);
    signature |= (((uint32_t)fault_state->faultCode & P1010B_FAULT_SIGNATURE_FAULT_MASK) << P1010B_FAULT_SIGNATURE_FAULT_SHIFT);
    signature |= (((uint32_t)fault_state->alarmCode & P1010B_FAULT_SIGNATURE_ALARM_MASK) << P1010B_FAULT_SIGNATURE_ALARM_SHIFT);
    return signature;
}

/**
 * @brief 解析状态应答帧（0xA0 + id）
 * @details
 * 首版保留“故障/报警/状态位”语义；故障码非 0 时进入闭锁态
 */
static void p1010b_internal_update_fault_state(P1010BDriver *driver, const P1010BRawFrame *frame)
{
    P1010BFaultState old_fault_state = driver->telemetry.faultState;
    uint32_t old_fault_signature;
    uint32_t new_fault_signature;

    /*
     * 按《P1010B_111 电机规格V1.2》映射 0xA0+id 应答字段
     * DATA[2]=CMD，DATA[3]=校准状态，DATA[4]=故障码，DATA[5]=报警码，DATA[6..7]=保留
     */
    driver->telemetry.faultState.calibrated = ((frame->payload[3] & 0x01U) != 0U);
    driver->telemetry.faultState.faultCode = (uint16_t)frame->payload[4];
    driver->telemetry.faultState.alarmCode = (uint16_t)frame->payload[5];
    driver->telemetry.faultState.statusBits = (uint32_t)(((uint32_t)frame->payload[6] << 8U) | frame->payload[7]);

    if (driver->telemetry.faultState.faultCode != 0U)
    {
        driver->runtime.state = P1010B_STATE_FAULT_LOCKED;
        driver->runtime.lastRejectReason = P1010B_REJECT_FAULT_LOCKED;
    }

    old_fault_signature = p1010b_internal_pack_fault_signature(&old_fault_state);
    new_fault_signature = p1010b_internal_pack_fault_signature(&driver->telemetry.faultState);
    if (driver->callbacks.onFaultChanged && ((old_fault_signature ^ new_fault_signature) != 0U))
    {
        driver->callbacks.onFaultChanged(driver, &driver->telemetry.faultState);
    }
}

/**
 * @brief 记录最近接收时间并更新在线状态
 */
static void p1010b_internal_mark_rx_seen(P1010BDriver *driver, uint32_t timestamp_ms)
{
    if (!driver)
    {
        return;
    }
    driver->telemetry.lastSuccessRxTimestampMs = timestamp_ms;
    p1010b_internal_mark_online(driver, true);
}

/**
 * @brief reply_base_id 映射到分发表索引
 * @return 非负索引有效；-1 表示不在映射范围
 */
static int32_t p1010b_internal_reply_base_to_dispatch_index(uint8_t reply_base_id)
{
    uint8_t reply_nibble;

    if ((reply_base_id & P1010B_REPLY_ID_LOW_MASK) != 0U)
    {
        return -1;
    }

    reply_nibble = (uint8_t)(reply_base_id >> 4U);
    if (reply_nibble < P1010B_REPLY_BASE_MIN_NIBBLE || reply_nibble > P1010B_REPLY_BASE_MAX_NIBBLE)
    {
        return -1;
    }

    return (int32_t)(reply_nibble - P1010B_REPLY_BASE_MIN_NIBBLE);
}

/**
 * @brief 接收分发
 * @details
 * reply_base_id 线性映射到副作用处理与同步事务完成策略，避免 ISR 内分支扫描
 */
static const P1010BRxDispatchDescriptor g_p1010b_rx_dispatch_table[P1010B_REPLY_DISPATCH_ENTRY_COUNT] = {
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_DRIVE_BASE)] = {
        .replyBaseId = P1010B_CAN_ACK_DRIVE_BASE,
        .mayCompleteSync = false,
        .sideEffectFn = p1010b_internal_update_feedback,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_REPORT_MODE)] = {
        .replyBaseId = P1010B_CAN_ACK_REPORT_MODE,
        .mayCompleteSync = true,
        .sideEffectFn = NULL,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_ACTIVE_QUERY)] = {
        .replyBaseId = P1010B_CAN_ACK_ACTIVE_QUERY,
        .mayCompleteSync = true,
        .sideEffectFn = NULL,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_WRITE_PARAM)] = {
        .replyBaseId = P1010B_CAN_ACK_WRITE_PARAM,
        .mayCompleteSync = true,
        .sideEffectFn = NULL,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_READ_PARAM)] = {
        .replyBaseId = P1010B_CAN_ACK_READ_PARAM,
        .mayCompleteSync = true,
        .sideEffectFn = NULL,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_STATE_CONTROL)] = {
        .replyBaseId = P1010B_CAN_ACK_STATE_CONTROL,
        .mayCompleteSync = true,
        .sideEffectFn = p1010b_internal_update_fault_state,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_SAVE_PARAM)] = {
        .replyBaseId = P1010B_CAN_ACK_SAVE_PARAM,
        .mayCompleteSync = true,
        .sideEffectFn = NULL,
    },
};

/**
 * @brief reply_base_id 查找接收分发表项
 * @return 有效分发表项指针；未命中返回 `NULL`
 */
static const P1010BRxDispatchDescriptor *p1010b_internal_find_rx_dispatch_descriptor(uint8_t reply_base_id)
{
    int32_t dispatch_index;
    const P1010BRxDispatchDescriptor *dispatch_descriptor;

    dispatch_index = p1010b_internal_reply_base_to_dispatch_index(reply_base_id);
    if (dispatch_index < 0)
    {
        return NULL;
    }

    dispatch_descriptor = &g_p1010b_rx_dispatch_table[(uint32_t)dispatch_index];
    if (dispatch_descriptor->replyBaseId != reply_base_id)
    {
        return NULL;
    }
    return dispatch_descriptor;
}

/**
 * @brief CAN 过滤器回调（ISR 上下文）
 * @details
 * - ISR 读出过滤器消息后直接完成协议解析与电机实例路由；
 * - 反馈/故障/读参应答均在 ISR 内更新状态并触发回调
 */
static void p1010b_internal_rx_callback(Device *device, void *param, CanFilterHandle filter_handle, size_t message_count)
{
    P1010BBus *bus = (P1010BBus *)param;
    CanUserMsg rx_message;
    uint8_t payload[P1010B_CAN_DLC];

    if (!bus || !device || !bus->canDevice)
    {
        return;
    }

    rx_message.filterHandle = filter_handle;
    rx_message.userBuf = payload;
    for (size_t message_index = 0U; message_index < message_count; message_index++)
    {
        P1010BRawFrame frame;
        uint8_t motor_id;
        uint8_t reply_base_id;
        P1010BDriver *target_driver;

        if (device_read(device, 0, &rx_message, 1) == 0U)
        {
            break;
        }
        if (rx_message.dsc.idType != CAN_IDE_STD || rx_message.dsc.dataLen != P1010B_CAN_DLC)
        {
            bus->rxDroppedCount++;
            continue;
        }

        frame.canId = rx_message.dsc.id;
        memcpy(frame.payload, payload, sizeof(frame.payload));
        frame.dataLength = (uint8_t)rx_message.dsc.dataLen;
        frame.timestampMs = rx_message.dsc.timeStamp;

        motor_id = (uint8_t)(frame.canId & P1010B_REPLY_ID_LOW_MASK);
        if (!p1010b_internal_is_valid_motor_id(motor_id))
        {
            continue;
        }
        target_driver = bus->driverTable[motor_id];
        if (!target_driver)
        {
            continue;
        }
        reply_base_id = (uint8_t)(frame.canId & P1010B_REPLY_ID_MASK);
        {
            const P1010BRxDispatchDescriptor *dispatch_descriptor = p1010b_internal_find_rx_dispatch_descriptor(reply_base_id);
            if (dispatch_descriptor)
            {
                if (dispatch_descriptor->sideEffectFn)
                {
                    dispatch_descriptor->sideEffectFn(target_driver, &frame);
                }
                if (dispatch_descriptor->mayCompleteSync)
                {
                    p1010b_internal_try_complete_sync_transaction(bus, target_driver, &frame);
                }
            }
        }
        p1010b_internal_mark_rx_seen(target_driver, frame.timestampMs);
    }
}

/**
 * @brief 初始化 P1010B 总线上下文
 * @details
 * - 分配一个过滤器用于收包
 * - 首版采用总线级全匹配，由 ISR `motor_id` 做二次路由
 */
OmRet p1010b_bus_init(P1010BBus *bus, Device *can_device)
{
    OmRet ret;
    CanFilterAllocArg filter_alloc_arg;

    if (!bus || !can_device)
    {
        return OM_ERROR_PARAM;
    }

    memset(bus, 0, sizeof(P1010BBus));
    bus->canDevice = can_device;

    memset(&filter_alloc_arg, 0, sizeof(filter_alloc_arg));
    /*
     * 首版采用总线级统一收包，驱动侧low nibble 路由到具体电机实例
     * 因此使用全匹配过滤器，避免单电机过滤导致同总线多电机实例不可见
     */
    filter_alloc_arg.request =
        CAN_FILTER_REQUEST_INIT(CAN_FILTER_MODE_MASK, CAN_FILTER_ID_STD, 0U, 0U, p1010b_internal_rx_callback, bus);
    ret = device_ctrl(can_device, CAN_CMD_FILTER_ALLOC, &filter_alloc_arg);
    if (ret != OM_OK)
    {
        return ret;
    }
    bus->filterHandle = filter_alloc_arg.handle;
    bus->filterAllocated = true;

    return OM_OK;
}

/**
 * @brief 释放总线上下文资源
 */
OmRet p1010b_bus_deinit(P1010BBus *bus)
{
    OmRet ret = OM_OK;

    if (!bus)
    {
        return OM_ERROR_PARAM;
    }
    for (uint8_t motor_id = P1010B_MOTOR_ID_MIN; motor_id <= P1010B_MOTOR_ID_MAX; motor_id++)
    {
        P1010BDriver *driver = bus->driverTable[motor_id];
        if (!driver)
        {
            continue;
        }
        completion_deinit(&driver->sync.completion);
    }
    if (bus->filterAllocated && bus->canDevice)
    {
        ret = device_ctrl(bus->canDevice, CAN_CMD_FILTER_FREE, &bus->filterHandle);
    }
    memset(bus, 0, sizeof(P1010BBus));
    return ret;
}

/**
 * @brief 注册电机驱动实例到总线
 * @note 同一 `motor_id` 只允许绑定一个实例
 */
OmRet p1010b_register(P1010BBus *bus, P1010BDriver *driver, const P1010BConfig *config)
{
    OmRet ret;
    uint8_t motor_id;

    if (!bus || !driver || !bus->canDevice || !bus->filterAllocated)
    {
        return OM_ERROR_PARAM;
    }

    memset(driver, 0, sizeof(P1010BDriver));
    driver->bus = bus;
    driver->config = P1010B_DEFAULT_CONFIG(P1010B_MOTOR_ID_MIN);
    if (config)
    {
        driver->config = *config;
    }
    if (!p1010b_internal_is_valid_motor_id(driver->config.motorId))
    {
        return OM_ERROR_PARAM;
    }
    motor_id = driver->config.motorId;
    if (bus->driverTable[motor_id] && bus->driverTable[motor_id] != driver)
    {
        return OM_ERR_CONFLICT;
    }

    driver->runtime.state = P1010B_STATE_DISABLED;
    driver->runtime.currentMode = driver->config.defaultMode;
    driver->runtime.targetScale = p1010b_internal_update_target_scale_cache(driver->runtime.currentMode);
    driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
    driver->runtime.activeReport = driver->config.activeReport;
    ret = completion_init(&driver->sync.completion);
    if (ret != OM_OK)
    {
        return ret;
    }
    bus->driverTable[motor_id] = driver;
    return OM_OK;
}

/**
 * @brief 设置事件回调函数
 */
void p1010b_set_callbacks(P1010BDriver *driver, P1010BFeedbackCallback feedback_cb, P1010BFaultCallback fault_cb,
                          P1010BOnlineCallback online_cb, P1010BParamReadCallback param_read_cb)
{
    if (!driver)
    {
        return;
    }
    driver->callbacks.onFeedback = feedback_cb;
    driver->callbacks.onFaultChanged = fault_cb;
    driver->callbacks.onOnlineChanged = online_cb;
    driver->callbacks.onParamRead = param_read_cb;
}

/**
 * @brief 判断运行模式是否合法
 */
static bool p1010b_internal_is_mode_supported(P1010BMode mode)
{
    switch (mode)
    {
    case P1010B_MODE_OPEN_LOOP:
    case P1010B_MODE_CURRENT:
    case P1010B_MODE_SPEED:
    case P1010B_MODE_POSITION:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 校验请求 flags 是否为合法二选一模式
 * @param flags 请求标志
 * @return `true` 合法；`false` 非法
 */
static bool p1010b_internal_is_request_flags_valid(uint8_t flags)
{
    return (flags == (uint8_t)P1010B_REQUEST_FLAG_SYNC || flags == (uint8_t)P1010B_REQUEST_FLAG_ASYNC);
}

/**
 * @brief 解析请求实际执行模式（SYNC/ASYNC）
 * @details
 * `request->flags == NONE` 时，回退使用命令描述符默认 flags
 */
static OmRet p1010b_internal_resolve_request_flags(const P1010BCommandDescriptor *descriptor, const P1010BRequest *request, uint8_t *resolved_flags)
{
    uint8_t request_flags;

    request_flags = request->flags;
    if (request_flags == (uint8_t)P1010B_REQUEST_FLAG_NONE)
    {
        request_flags = descriptor->defaultRequestFlags;
    }
    if (!p1010b_internal_is_request_flags_valid(request_flags))
    {
        return OM_ERROR_PARAM;
    }

    *resolved_flags = request_flags;
    return OM_OK;
}

/**
 * @brief 控制给定命令守卫
 * @details
 * 仅在 `ENABLED` 且无故障闭锁时允许给定
 */
static OmRet p1010b_internal_guard_target(P1010BDriver *driver, const P1010BRequest *request)
{
    if (driver->runtime.state == P1010B_STATE_FAULT_LOCKED || driver->telemetry.faultState.faultCode != 0U)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_FAULT_LOCKED;
        return OM_ERROR;
    }
    if (driver->runtime.state != P1010B_STATE_ENABLED)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_NOT_ENABLED;
        return OM_ERROR_BUSY;
    }
    if (request->command == P1010B_COMMAND_SET_TARGET && driver->runtime.targetScale <= 0.0f)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_INVALID_TARGET;
        return OM_ERROR_PARAM;
    }
    return OM_OK;
}

/**
 * @brief 主动上报配置命令守卫
 * @details
 * 仅允许在 `DISABLED` 配置态下下发，并要求周期非零
 */
static OmRet p1010b_internal_guard_set_active_report(P1010BDriver *driver, const P1010BRequest *request)
{
    if (request->args.setActiveReport.config.periodMs == 0U)
    {
        return OM_ERROR_PARAM;
    }
    if (driver->runtime.state != P1010B_STATE_DISABLED)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_CONFIG_ONLY_WHEN_DISABLED;
        return OM_ERROR_BUSY;
    }
    return OM_OK;
}

/**
 * @brief 参数写命令守卫
 * @details
 * 首版限制 Disabled 态且参数在白名单中
 */
static OmRet p1010b_internal_guard_write_parameter(P1010BDriver *driver, const P1010BRequest *request)
{
    if (driver->runtime.state != P1010B_STATE_DISABLED)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_CONFIG_ONLY_WHEN_DISABLED;
        return OM_ERROR_BUSY;
    }
    if (!p1010b_internal_is_parameter_whitelisted(request->args.writeParameter.parameterId))
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_PARAM_NOT_WHITELISTED;
        return OM_ERROR_NOT_SUPPORT;
    }
    return OM_OK;
}

/**
 * @brief 参数读命令守卫
 * @note 参数 ID 0 视为无效请求
 */
static OmRet p1010b_internal_guard_read_parameter(P1010BDriver *driver, const P1010BRequest *request)
{
    (void)driver;
    if (request->args.readParameter.parameterId == 0U)
    {
        return OM_ERROR_PARAM;
    }
    return OM_OK;
}

/**
 * @brief 状态控制命令守卫
 * @details
 * - 仅接受 ENABLE/DISABLE
 * - 故障闭锁下禁止 ENABLE
 */
static OmRet p1010b_internal_guard_state_control(P1010BDriver *driver, const P1010BRequest *request)
{
    uint8_t state_command;

    state_command = request->args.stateControl.command;
    if (state_command != P1010B_STATE_CMD_ENABLE && state_command != P1010B_STATE_CMD_DISABLE)
    {
        return OM_ERROR_PARAM;
    }
    if (state_command == P1010B_STATE_CMD_ENABLE &&
        (driver->runtime.state == P1010B_STATE_FAULT_LOCKED || driver->telemetry.faultState.faultCode != 0U))
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_FAULT_LOCKED;
        return OM_ERROR;
    }
    return OM_OK;
}

/**
 * @brief 参数保存命令守卫
 * @note 仅允许 Disabled 态执行
 */
static OmRet p1010b_internal_guard_save_parameters(P1010BDriver *driver, const P1010BRequest *request)
{
    (void)request;
    if (driver->runtime.state != P1010B_STATE_DISABLED)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_CONFIG_ONLY_WHEN_DISABLED;
        return OM_ERROR_BUSY;
    }
    return OM_OK;
}

/**
 * @brief 模式设置命令守卫
 * @note 仅支持受控模式集合，且仅允许 Disabled 态配置
 */
static OmRet p1010b_internal_guard_set_mode(P1010BDriver *driver, const P1010BRequest *request)
{
    if (!p1010b_internal_is_mode_supported(request->args.setMode.mode))
    {
        return OM_ERROR_PARAM;
    }
    if (driver->runtime.state != P1010B_STATE_DISABLED)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_CONFIG_ONLY_WHEN_DISABLED;
        return OM_ERROR_BUSY;
    }
    return OM_OK;
}

/**
 * @brief 打包参数写载荷（0x36）
 * @details 32 位参数值按协议定义以低字节在前方式编码payload[2..5]
 */
static void p1010b_internal_encode_parameter_write_payload(uint8_t motor_id, uint8_t parameter_id, int32_t parameter_value,
                                                           uint8_t payload[P1010B_CAN_DLC])
{
    payload[0] = motor_id;
    payload[1] = parameter_id;
    payload[2] = (uint8_t)((uint32_t)parameter_value & 0xFFU);
    payload[3] = (uint8_t)(((uint32_t)parameter_value >> 8U) & 0xFFU);
    payload[4] = (uint8_t)(((uint32_t)parameter_value >> 16U) & 0xFFU);
    payload[5] = (uint8_t)(((uint32_t)parameter_value >> 24U) & 0xFFU);
}

/**
 * @brief 编码原始给定帧（0x32/0x33）
 * @details
 * 先写 group 缓存，再重新打包整帧，保证同组多电机给定一致
 */
static OmRet p1010b_internal_encode_target_raw_value(P1010BDriver *driver, int16_t raw_target, P1010BEncodedRequest *encoded)
{
    uint8_t group_index;
    uint8_t slot_index;

    group_index = p1010b_internal_get_group_index(driver->config.motorId);
    slot_index = p1010b_internal_get_slot_index(driver->config.motorId);
    encoded->requestCanId = (group_index == 0U) ? P1010B_CAN_CMD_DRIVE_GROUP_1_4 : P1010B_CAN_CMD_DRIVE_GROUP_5_8;
    driver->bus->groupTargetsRaw[group_index][slot_index] = raw_target;
    p1010b_internal_pack_group_payload(driver->bus, group_index, encoded->payload);
    return OM_OK;
}

/**
 * @brief 编码语义给定命令（自动 scale）
 */
static OmRet p1010b_internal_encode_set_target(P1010BDriver *driver, const P1010BRequest *request,
                                               P1010BEncodedRequest *encoded)
{
    int16_t raw_target;

    raw_target = p1010b_internal_scale_target(driver->runtime.targetScale, request->args.setTarget.targetValue);
    return p1010b_internal_encode_target_raw_value(driver, raw_target, encoded);
}

/**
 * @brief 编码软件复位命令（0x40）
 */
static OmRet p1010b_internal_encode_software_reset(P1010BDriver *driver, const P1010BRequest *request,
                                                   P1010BEncodedRequest *encoded)
{
    (void)driver;
    (void)request;
    encoded->payload[0] = 0x01U;
    return OM_OK;
}

/**
 * @brief 编码主动上报配置命令（0x34）
 */
static OmRet p1010b_internal_encode_set_active_report(P1010BDriver *driver, const P1010BRequest *request,
                                                      P1010BEncodedRequest *encoded)
{
    const P1010BActiveReportConfig *report_config;

    report_config = &request->args.setActiveReport.config;
    encoded->payload[0] = driver->config.motorId;
    encoded->payload[1] = report_config->enable ? 1U : 0U;
    encoded->payload[2] = report_config->periodMs;
    encoded->payload[3] = report_config->dataTypeSlots[0];
    encoded->payload[4] = report_config->dataTypeSlots[1];
    encoded->payload[5] = report_config->dataTypeSlots[2];
    encoded->payload[6] = report_config->dataTypeSlots[3];
    return OM_OK;
}

/**
 * @brief 编码主动查询命令（0x35）
 */
static OmRet p1010b_internal_encode_active_query(P1010BDriver *driver, const P1010BRequest *request,
                                                 P1010BEncodedRequest *encoded)
{
    (void)driver;
    encoded->payload[0] = request->args.activeQuery.dataTypeSlots[0];
    encoded->payload[1] = request->args.activeQuery.dataTypeSlots[1];
    encoded->payload[2] = request->args.activeQuery.dataTypeSlots[2];
    encoded->payload[3] = request->args.activeQuery.dataTypeSlots[3];
    return OM_OK;
}

/**
 * @brief 编码参数写命令（0x36）
 */
static OmRet p1010b_internal_encode_write_parameter(P1010BDriver *driver, const P1010BRequest *request,
                                                    P1010BEncodedRequest *encoded)
{
    p1010b_internal_encode_parameter_write_payload(driver->config.motorId, request->args.writeParameter.parameterId, request->args.writeParameter.parameterValue, encoded->payload);
    encoded->expectedParameterId = request->args.writeParameter.parameterId;
    return OM_OK;
}

/**
 * @brief 编码参数读命令（0x37）
 */
static OmRet p1010b_internal_encode_read_parameter(P1010BDriver *driver, const P1010BRequest *request,
                                                   P1010BEncodedRequest *encoded)
{
    encoded->payload[0] = driver->config.motorId;
    encoded->payload[1] = request->args.readParameter.parameterId;
    encoded->expectedParameterId = request->args.readParameter.parameterId;
    return OM_OK;
}

/**
 * @brief 编码状态控制命令（0x38）
 * @details 状态命令写入与 motor_id 对应的组槽位
 */
static OmRet p1010b_internal_encode_state_control(P1010BDriver *driver, const P1010BRequest *request,
                                                  P1010BEncodedRequest *encoded)
{
    uint8_t command_slot_index;

    command_slot_index = (uint8_t)(driver->config.motorId - 1U);
    encoded->payload[command_slot_index] = request->args.stateControl.command;
    encoded->expectedStateCommand = request->args.stateControl.command;
    return OM_OK;
}

/**
 * @brief 编码参数保存命令（0x39）
 * @details
 * `saveParameters` 采用“命令 + 数据”双层模型：
 * - payload[0]：保存命令；
 * - payload[1]：当前数据位（绝对零位）
 * - payload[2..7]：保留位（预留未来扩展）
 */
static OmRet p1010b_internal_encode_save_parameters(P1010BDriver *driver, const P1010BRequest *request,
                                                    P1010BEncodedRequest *encoded)
{
    uint8_t index;

    (void)driver;
    encoded->payload[0] = request->args.saveParameters.command;
    encoded->payload[1] = request->args.saveParameters.data.absoluteZero ? 1U : 0U;

    for (index = 0U; index < P1010B_SAVE_PARAM_RESERVED_BYTES; index++)
    {
        encoded->payload[index + 2U] = request->args.saveParameters.data.reservedPayload[index];
    }
    return OM_OK;
}

/**
 * @brief 编码模式设置命令
 * @details 逻辑命令 `SET_MODE` 映射为写参数 `P1010B_PARAM_WORK_MODE`
 */
static OmRet p1010b_internal_encode_set_mode(P1010BDriver *driver, const P1010BRequest *request, P1010BEncodedRequest *encoded)
{
    p1010b_internal_encode_parameter_write_payload(driver->config.motorId, P1010B_PARAM_WORK_MODE, (int32_t)request->args.setMode.mode, encoded->payload);
    encoded->expectedParameterId = P1010B_PARAM_WORK_MODE;
    return OM_OK;
}

/**
 * @brief 写参应答匹配条件
 * @details 仅使用 `0x80` 应答路径，按 parameter_id 精确匹配
 */
static bool p1010b_internal_ack_match_parameter(const P1010BDriver *driver, const P1010BRawFrame *frame)
{
    /* 仅使用 0x80 写参应答路径；0x90 读参应答不携带 parameter_id*/
    return (frame->payload[1] == driver->sync.expectedParameterId);
}

/**
 * @brief 状态控制应答匹配条件
 * @details 按应答内回显的状态命令字匹配
 */
static bool p1010b_internal_ack_match_state_control(const P1010BDriver *driver, const P1010BRawFrame *frame)
{
    return (frame->payload[2] == driver->sync.expectedStateCommand);
}

/**
 * @brief 解码主动查询应答（0x70）
 */
static void p1010b_internal_decode_ack_active_query(P1010BDriver *driver, const P1010BRawFrame *frame, P1010BResponse *response,
                                                    P1010BIsrCallbackContext *callback_context)
{
    (void)driver;
    (void)callback_context;
    for (uint8_t index = 0U; index < 4U; index++)
        response->data.activeQueryValues[index] = p1010b_internal_read_be_i16(&frame->payload[index * 2U]);
}

/**
 * @brief 解码参数读取应答（0x90）
 * @details
 * 读参应答不携带 parameter_id，使用同步事务上下文回填 parameter_id
 */
static void p1010b_internal_decode_ack_read_parameter(P1010BDriver *driver, const P1010BRawFrame *frame, P1010BResponse *response,
                                                      P1010BIsrCallbackContext *callback_context)
{
    int32_t parameter_value;

    parameter_value = p1010b_internal_read_be_i32(&frame->payload[1]);
    response->data.readParameter.parameterId = driver->sync.expectedParameterId;
    response->data.readParameter.parameterValue = parameter_value;
    callback_context->triggerParamReadCallback = true;
    callback_context->callbackParameterId = driver->sync.expectedParameterId;
    callback_context->callbackParameterValue = parameter_value;
}

/**
 * @brief 解码状态控制应答（0xA0）
 */
static void p1010b_internal_decode_ack_state_control(P1010BDriver *driver, const P1010BRawFrame *frame, P1010BResponse *response,
                                                     P1010BIsrCallbackContext *callback_context)
{
    (void)callback_context;
    response->data.stateControl.stateCommand = frame->payload[2];
    response->data.stateControl.faultState = driver->telemetry.faultState;
}

/**
 * @brief 通用后处理：成功后清除拒绝原因
 */
static void p1010b_internal_post_commit_clear_reject_on_success(P1010BDriver *driver, const P1010BRequest *request, OmRet result,
                                                                const P1010BResponse *response)
{
    (void)request;
    (void)response;
    if (result == OM_OK)
        driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
}

/**
 * @brief 主动上报配置后处理
 * @details 同步事务成功后刷新 driver 侧生效配置缓存
 */
static void p1010b_internal_post_commit_set_active_report(P1010BDriver *driver, const P1010BRequest *request, OmRet result,
                                                          const P1010BResponse *response)
{
    (void)response;
    if (result != OM_OK)
        return;
    driver->runtime.activeReport = request->args.setActiveReport.config;
    driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
}

/**
 * @brief 模式设置后处理
 * @details 成功后更新 `currentMode` 与 `targetScale` 缓存
 */
static void p1010b_internal_post_commit_set_mode(P1010BDriver *driver, const P1010BRequest *request, OmRet result,
                                                 const P1010BResponse *response)
{
    (void)response;
    if (result != OM_OK)
        return;
    driver->runtime.currentMode = request->args.setMode.mode;
    driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
    driver->runtime.targetScale = p1010b_internal_update_target_scale_cache(driver->runtime.currentMode);
}

/**
 * @brief 状态控制后处理
 * @details
 * - ENABLE 成功后若仍有 faultCode，则维持故障闭锁
 * - DISABLE 成功后依 faultCode 决定 Disabled 或 FaultLocked
 */
static void p1010b_internal_post_commit_state_control(P1010BDriver *driver, const P1010BRequest *request, OmRet result,
                                                      const P1010BResponse *response)
{
    uint8_t state_command;

    (void)response;
    if (result != OM_OK)
        return;

    state_command = request->args.stateControl.command;
    if (state_command == P1010B_STATE_CMD_ENABLE)
    {
        if (driver->telemetry.faultState.faultCode != 0U)
        {
            driver->runtime.state = P1010B_STATE_FAULT_LOCKED;
            driver->runtime.lastRejectReason = P1010B_REJECT_FAULT_LOCKED;
            return;
        }
        driver->runtime.state = P1010B_STATE_ENABLED;
        driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
        return;
    }

    driver->runtime.state = (driver->telemetry.faultState.faultCode == 0U) ? P1010B_STATE_DISABLED : P1010B_STATE_FAULT_LOCKED;
    driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
}

/**
 * @brief 软件复位后处理
 * @details 成功后回到 Disabled，并清除在线标记，等待后续报文重建在线态
 */
static void p1010b_internal_post_commit_software_reset(P1010BDriver *driver, const P1010BRequest *request, OmRet result,
                                                       const P1010BResponse *response)
{
    (void)request;
    (void)response;
    if (result != OM_OK)
        return;
    driver->runtime.state = P1010B_STATE_DISABLED;
    driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
    p1010b_internal_mark_online(driver, false);
}

/**
 * @brief 命令描述符表
 * @details
 * `P1010BCommand` 作为线性索引，统一维护守卫/编码/匹配/解码/后处理策略
 */
static const P1010BCommandDescriptor g_p1010b_command_descriptors[P1010B_COMMAND_SOFTWARE_RESET + 1U] = {
    [P1010B_COMMAND_SET_ACTIVE_REPORT] = {
        .command = P1010B_COMMAND_SET_ACTIVE_REPORT,
        .requestCanId = P1010B_CAN_CMD_SET_REPORT_MODE,
        .ackBaseId = P1010B_CAN_ACK_REPORT_MODE,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = false,
        .stateGuardFn = p1010b_internal_guard_set_active_report,
        .encodeFn = p1010b_internal_encode_set_active_report,
        .ackMatchFn = NULL,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_set_active_report,
    },
    [P1010B_COMMAND_ACTIVE_QUERY] = {
        .command = P1010B_COMMAND_ACTIVE_QUERY,
        .requestCanId = P1010B_CAN_CMD_ACTIVE_QUERY,
        .ackBaseId = P1010B_CAN_ACK_ACTIVE_QUERY,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = false,
        .stateGuardFn = NULL,
        .encodeFn = p1010b_internal_encode_active_query,
        .ackMatchFn = NULL,
        .decodeAckFn = p1010b_internal_decode_ack_active_query,
        .postCommitFn = NULL,
    },
    [P1010B_COMMAND_WRITE_PARAMETER] = {
        .command = P1010B_COMMAND_WRITE_PARAMETER,
        .requestCanId = P1010B_CAN_CMD_WRITE_PARAM,
        .ackBaseId = P1010B_CAN_ACK_WRITE_PARAM,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = true,
        .stateGuardFn = p1010b_internal_guard_write_parameter,
        .encodeFn = p1010b_internal_encode_write_parameter,
        .ackMatchFn = p1010b_internal_ack_match_parameter,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_clear_reject_on_success,
    },
    [P1010B_COMMAND_READ_PARAMETER] = {
        .command = P1010B_COMMAND_READ_PARAMETER,
        .requestCanId = P1010B_CAN_CMD_READ_PARAM,
        .ackBaseId = P1010B_CAN_ACK_READ_PARAM,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = false,
        .stateGuardFn = p1010b_internal_guard_read_parameter,
        .encodeFn = p1010b_internal_encode_read_parameter,
        /*
         * 规格 0x90 应答 payload[1..4] 为参数值高到低字节
         * 不携parameter_id，因此读参匹配仅依赖 pendingCommand + ackBaseId
         */
        .ackMatchFn = NULL,
        .decodeAckFn = p1010b_internal_decode_ack_read_parameter,
        .postCommitFn = NULL,
    },
    [P1010B_COMMAND_STATE_CONTROL] = {
        .command = P1010B_COMMAND_STATE_CONTROL,
        .requestCanId = P1010B_CAN_CMD_STATE_CONTROL,
        .ackBaseId = P1010B_CAN_ACK_STATE_CONTROL,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = false,
        .stateGuardFn = p1010b_internal_guard_state_control,
        .encodeFn = p1010b_internal_encode_state_control,
        .ackMatchFn = p1010b_internal_ack_match_state_control,
        .decodeAckFn = p1010b_internal_decode_ack_state_control,
        .postCommitFn = p1010b_internal_post_commit_state_control,
    },
    [P1010B_COMMAND_SAVE_PARAMETERS] = {
        .command = P1010B_COMMAND_SAVE_PARAMETERS,
        .requestCanId = P1010B_CAN_CMD_SAVE_PARAM,
        .ackBaseId = P1010B_CAN_ACK_SAVE_PARAM,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = false,
        .stateGuardFn = p1010b_internal_guard_save_parameters,
        .encodeFn = p1010b_internal_encode_save_parameters,
        .ackMatchFn = NULL,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_clear_reject_on_success,
    },
    [P1010B_COMMAND_SET_MODE] = {
        .command = P1010B_COMMAND_SET_MODE,
        .requestCanId = P1010B_CAN_CMD_WRITE_PARAM,
        .ackBaseId = P1010B_CAN_ACK_WRITE_PARAM,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = true,
        .stateGuardFn = p1010b_internal_guard_set_mode,
        .encodeFn = p1010b_internal_encode_set_mode,
        .ackMatchFn = p1010b_internal_ack_match_parameter,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_set_mode,
    },
    [P1010B_COMMAND_SET_TARGET] = {
        .command = P1010B_COMMAND_SET_TARGET,
        .requestCanId = 0U,
        .ackBaseId = P1010B_CAN_ACK_NONE,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_ASYNC,
        .useConfiguringState = false,
        .stateGuardFn = p1010b_internal_guard_target,
        .encodeFn = p1010b_internal_encode_set_target,
        .ackMatchFn = NULL,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_clear_reject_on_success,
    },
    [P1010B_COMMAND_SOFTWARE_RESET] = {
        .command = P1010B_COMMAND_SOFTWARE_RESET,
        .requestCanId = P1010B_CAN_CMD_SOFTWARE_RESET,
        .ackBaseId = P1010B_CAN_ACK_NONE,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_ASYNC,
        .useConfiguringState = false,
        .stateGuardFn = NULL,
        .encodeFn = p1010b_internal_encode_software_reset,
        .ackMatchFn = NULL,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_software_reset,
    },
};

/**
 * @brief 按命令字查找描述符
 * @return 有效描述符指针；命令非法或描述符未配置时返回 `NULL`
 */
static const P1010BCommandDescriptor *p1010b_internal_find_command_descriptor(P1010BCommand command)
{
    const P1010BCommandDescriptor *descriptor;
    if (command <= P1010B_COMMAND_NONE || command > P1010B_COMMAND_SOFTWARE_RESET)
        return NULL;
    descriptor = &g_p1010b_command_descriptors[(uint32_t)command];
    if (descriptor->command != command)
        return NULL;
    if (!descriptor->encodeFn)
        return NULL;
    return descriptor;
}

/**
 * @brief 统一请求执行入口
 * @details
 * 执行链路：参数校验 -> flags 解析 -> 状态守卫 -> 编码 -> 发送 ->
 * （同步路径）等待应答 -> 后处理
 */
static OmRet p1010b_internal_execute_request(P1010BDriver *driver, const P1010BRequest *request, P1010BResponse *response, P1010BWaitMode wait_mode)
{
    const P1010BCommandDescriptor *descriptor = NULL;
    P1010BEncodedRequest encoded = {0};
    OmRet ret = OM_ERROR_PARAM;
    uint8_t request_flags = (uint8_t)P1010B_REQUEST_FLAG_NONE;
    bool config_state_entered = false;
    P1010BState previous_state = P1010B_STATE_DISABLED;

    if (!driver || !request || !driver->bus || !driver->bus->canDevice)
        return OM_ERROR_PARAM;

    if (response)
        p1010b_internal_init_response(response, request->command, OM_ERROR_PARAM);

    descriptor = p1010b_internal_find_command_descriptor(request->command);
    if (!descriptor)
    {
        ret = OM_ERROR_NOT_SUPPORT;
        goto execute_finish;
    }

    ret = p1010b_internal_resolve_request_flags(descriptor, request, &request_flags);
    if (ret != OM_OK)
        goto execute_finish;
    if (wait_mode == P1010B_WAIT_MODE_SYNC && request_flags != (uint8_t)P1010B_REQUEST_FLAG_SYNC)
    {
        ret = OM_ERROR_NOT_SUPPORT;
        goto execute_finish;
    }
    if (wait_mode == P1010B_WAIT_MODE_ASYNC && request_flags != (uint8_t)P1010B_REQUEST_FLAG_ASYNC)
    {
        ret = OM_ERROR_NOT_SUPPORT;
        goto execute_finish;
    }

    if (descriptor->stateGuardFn)
    {
        ret = descriptor->stateGuardFn(driver, request);
        if (ret != OM_OK)
            goto execute_finish;
    }

    encoded.requestCanId = descriptor->requestCanId;
    encoded.ackBaseId = descriptor->ackBaseId;
    ret = descriptor->encodeFn(driver, request, &encoded);
    if (ret != OM_OK)
        goto execute_finish;

    /* 异步路径：仅发送，不等待*/
    if (request_flags == (uint8_t)P1010B_REQUEST_FLAG_ASYNC)
    {
        ret = p1010b_internal_send_can_frame(driver->bus, encoded.requestCanId, encoded.payload);
        if (ret == OM_OK)
            driver->telemetry.lastSuccessTxTimestampMs = osal_time_now_monotonic();
        goto execute_finish;
    }

    /* 同步路径：可选进入 CONFIGURING 瞬态，等待 ISR 完成事务*/
    if (descriptor->useConfiguringState)
    {
        previous_state = driver->runtime.state;
        driver->runtime.state = P1010B_STATE_CONFIGURING;
        config_state_entered = true;
    }

    ret = p1010b_internal_send_sync_transaction(driver, request, &encoded, response);
    if (config_state_entered)
        driver->runtime.state = previous_state;

execute_finish:
    /* 统一写回结果，避免不同返回分支漏写 response*/
    if (response)
        response->result = ret;
    if (descriptor && descriptor->postCommitFn)
        descriptor->postCommitFn(driver, request, ret, response);
    return ret;
}

/**
 * @brief 异步提交请求（发送即返回
 */
OmRet p1010b_submit(P1010BDriver *driver, const P1010BRequest *request)
{
    return p1010b_internal_execute_request(driver, request, NULL, P1010B_WAIT_MODE_ASYNC);
}

/**
 * @brief 同步提交请求（等待应答）
 */
OmRet p1010b_request_sync(P1010BDriver *driver, const P1010BRequest *request, P1010BResponse *response)
{
    if (!response)
        return OM_ERROR_PARAM;
    return p1010b_internal_execute_request(driver, request, response, P1010B_WAIT_MODE_SYNC);
}

/**
 * @brief 行为 API：执行同步请求（允许 response 为空
 */
static OmRet p1010b_internal_execute_sync_behavior(P1010BDriver *driver, P1010BRequest request, uint32_t timeout_ms,
                                                   P1010BResponse *response)
{
    P1010BResponse local_response = {0};

    request.timeoutMs = timeout_ms;
    if (response != NULL)
    {
        return p1010b_request_sync(driver, &request, response);
    }
    return p1010b_request_sync(driver, &request, &local_response);
}

/**
 * @brief 行为 API：执行异步请求
 */
static OmRet p1010b_internal_execute_async_behavior(P1010BDriver *driver, P1010BRequest request)
{
    return p1010b_submit(driver, &request);
}

/**
 * @brief 失能电机（构造+执行一体，同步）
 */
OmRet p1010b_disable(P1010BDriver *driver, uint32_t timeout_ms, P1010BResponse *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_disable(), timeout_ms, response);
}

/**
 * @brief 使能电机（构造+执行一体，同步）
 */
OmRet p1010b_enable(P1010BDriver *driver, uint32_t timeout_ms, P1010BResponse *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_enable(), timeout_ms, response);
}

/**
 * @brief 设置工作模式（构造+执行一体，同步）
 */
OmRet p1010b_set_mode(P1010BDriver *driver, P1010BMode mode, uint32_t timeout_ms, P1010BResponse *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_set_mode(mode), timeout_ms, response);
}

/**
 * @brief 设置主动上报配置（构造+执行一体，同步）
 */
OmRet p1010b_set_active_report(P1010BDriver *driver, const P1010BActiveReportConfig *config, uint32_t timeout_ms, P1010BResponse *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_set_active_report(config), timeout_ms, response);
}

/**
 * @brief 主动查询指定 4 槽数据（构造+执行一体，同步）
 */
OmRet p1010b_active_query_slots(P1010BDriver *driver, P1010BReportDataType slot0, P1010BReportDataType slot1,
                                P1010BReportDataType slot2, P1010BReportDataType slot3, uint32_t timeout_ms,
                                P1010BResponse *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_active_query_slots(slot0, slot1, slot2, slot3), timeout_ms,
                                                 response);
}

/**
 * @brief 写参数（构造+执行一体，同步）
 */
OmRet p1010b_write_parameter(P1010BDriver *driver, uint8_t parameter_id, int32_t parameter_value, uint32_t timeout_ms,
                             P1010BResponse *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_write_parameter(parameter_id, parameter_value), timeout_ms,
                                                 response);
}

/**
 * @brief 读参数（构造+执行一体，同步）
 */
OmRet p1010b_read_parameter(P1010BDriver *driver, uint8_t parameter_id, uint32_t timeout_ms, P1010BResponse *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_read_parameter(parameter_id), timeout_ms, response);
}

/**
 * @brief 保存参数（构造+执行一体，同步）
 * @note 根据规格书，当前保存数据中仅绝对零位可选
 */
OmRet p1010b_save_parameters(P1010BDriver *driver, bool set_absolute_zero, uint32_t timeout_ms, P1010BResponse *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_save_parameters(set_absolute_zero), timeout_ms, response);
}

/**
 * @brief 下发目标值（构造+执行一体，异步）
 */
OmRet p1010b_set_target(P1010BDriver *driver, float target_value)
{
    return p1010b_internal_execute_async_behavior(driver, p1010b_req_set_target(target_value));
}

/**
 * @brief 软件复位（构造+执行一体，异步）
 */
OmRet p1010b_software_reset(P1010BDriver *driver)
{
    return p1010b_internal_execute_async_behavior(driver, p1010b_req_software_reset());
}

/**
 * @brief 获取反馈快照
 */
const P1010BFeedback *p1010b_get_feedback(const P1010BDriver *driver)
{
    return driver ? &driver->telemetry.feedback : NULL;
}

/**
 * @brief 获取故障快照
 */
const P1010BFaultState *p1010b_get_fault_state(const P1010BDriver *driver)
{
    return driver ? &driver->telemetry.faultState : NULL;
}

/**
 * @brief 获取当前状态机状态
 */
P1010BState p1010b_get_state(const P1010BDriver *driver)
{
    if (!driver)
        return P1010B_STATE_DISABLED;
    return driver->runtime.state;
}

/**
 * @brief 获取最近一次拒绝原因
 */
P1010BRejectReason p1010b_get_last_reject_reason(const P1010BDriver *driver)
{
    if (!driver)
        return P1010B_REJECT_NONE;
    return driver->runtime.lastRejectReason;
}

/**
 * @brief 查询在线状态
 */
bool p1010b_is_online(const P1010BDriver *driver)
{
    if (!driver)
        return false;
    return driver->telemetry.online;
}

/**
 * @brief 获取线程解析格式丢弃计数（非标准帧或 DLC!=8）
 */
uint32_t p1010b_get_rx_drop_count(const P1010BBus *bus)
{
    if (!bus)
        return 0U;
    return bus->rxDroppedCount;
}
