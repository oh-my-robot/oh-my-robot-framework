/**
 * @file P1010B.h
 * @brief P1010B 直驱电机 CAN 首版驱动接口
 * @details
 * - 首版只实现 CAN 运行路径；
 * - RS485 仅保留扩展位，不进入运行实现；
 * - 接口设计遵循“故障闭锁优先 + 参数白名单 + ISR 直解直路由”的边界约束。
 */

#ifndef __P1010B_DRIVER_H__
#define __P1010B_DRIVER_H__

#include "drivers/model/device.h"
#include "drivers/peripheral/can/pal_can_dev.h"
#include "sync/completion.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 协议常量（命令字 / 应答字）                                                   */
/* -------------------------------------------------------------------------- */

/** 组 1~4 电机给定命令（标准帧 ID） */
#define P1010B_CAN_CMD_DRIVE_GROUP_1_4 (0x32U)
/** 组 5~8 电机给定命令（标准帧 ID） */
#define P1010B_CAN_CMD_DRIVE_GROUP_5_8 (0x33U)
/** 反馈方式设置命令 */
#define P1010B_CAN_CMD_SET_REPORT_MODE (0x34U)
/** 主动查询命令 */
#define P1010B_CAN_CMD_ACTIVE_QUERY (0x35U)
/** 参数写入命令 */
#define P1010B_CAN_CMD_WRITE_PARAM (0x36U)
/** 参数读取命令 */
#define P1010B_CAN_CMD_READ_PARAM (0x37U)
/** 电机状态控制命令（使能/失能） */
#define P1010B_CAN_CMD_STATE_CONTROL (0x38U)
/** 参数保存命令 */
#define P1010B_CAN_CMD_SAVE_PARAM (0x39U)
/** 软件复位命令 */
#define P1010B_CAN_CMD_SOFTWARE_RESET (0x40U)

/** 给定反馈基地址（`0x50 + motor_id`） */
#define P1010B_CAN_ACK_DRIVE_BASE (0x50U)
/** 反馈方式设置应答基地址（`0x60 + motor_id`） */
#define P1010B_CAN_ACK_REPORT_MODE (0x60U)
/** 主动查询应答基地址（`0x70 + motor_id`） */
#define P1010B_CAN_ACK_ACTIVE_QUERY (0x70U)
/** 参数写入应答基地址（`0x80 + motor_id`） */
#define P1010B_CAN_ACK_WRITE_PARAM (0x80U)
/** 参数读取应答基地址（`0x90 + motor_id`） */
#define P1010B_CAN_ACK_READ_PARAM (0x90U)
/** 状态控制应答基地址（`0xA0 + motor_id`） */
#define P1010B_CAN_ACK_STATE_CONTROL (0xA0U)
/** 参数保存应答基地址（`0xB0 + motor_id`） */
#define P1010B_CAN_ACK_SAVE_PARAM (0xB0U)
/** 无应答基地址（异步命令占位） */
#define P1010B_CAN_ACK_NONE (0x00U)

/** 状态控制命令：失能 */
#define P1010B_STATE_CMD_DISABLE (0x01U)
/** 状态控制命令：使能 */
#define P1010B_STATE_CMD_ENABLE (0x02U)

/* -------------------------------------------------------------------------- */
/* 参数白名单（业务层开放写入）                                                  */
/* -------------------------------------------------------------------------- */

/** 故障屏蔽参数 ID */
#define P1010B_PARAM_FAULT_MASK (11U)
/** 心跳使能参数 ID */
#define P1010B_PARAM_HEARTBEAT_ENABLE (22U)
/** 工作模式参数 ID */
#define P1010B_PARAM_WORK_MODE (28U)
/** 电机 ID 参数 ID */
#define P1010B_PARAM_MOTOR_ID (42U)
/** CAN 波特率与通信类型参数 ID */
#define P1010B_PARAM_CAN_BAUD_MODE (43U)
/** 心跳时间参数 ID */
#define P1010B_PARAM_HEARTBEAT_TIME (47U)

/* -------------------------------------------------------------------------- */
/* 运行常量                                                                     */
/* -------------------------------------------------------------------------- */
#define P1010B_CAN_DLC (8U)
/** 支持的最小电机 ID */
#define P1010B_MOTOR_ID_MIN (1U)
/** 支持的最大电机 ID */
#define P1010B_MOTOR_ID_MAX (8U)
/** 主动上报类型槽位数量（0x34 payload[3..6]） */
#define P1010B_ACTIVE_REPORT_SLOT_COUNT (4U)
/** 参数保存命令保留载荷长度（0x39 payload[2..7]） */
#define P1010B_SAVE_PARAM_RESERVED_BYTES (6U)
/** 参数保存命令：设置绝对零位 */
#define P1010B_SAVE_PARAM_CMD_SET_ABSOLUTE_ZERO (0U)
/** 参数保存命令：保存参数 */
#define P1010B_SAVE_PARAM_CMD_SAVE (1U)

/**
 * @brief 主动上报数据类型 ID（附录1）
 * @note 枚举值与规格书附录1（反馈数据代号表）1~14 完整对齐。
 */
typedef enum {
    P1010B_REPORT_DATA_SPEED_RPM         = 1,  /**< 中心轴速度（raw/10，RPM） */
    P1010B_REPORT_DATA_BUS_CURRENT       = 2,  /**< 母线电流（raw/100，A） */
    P1010B_REPORT_DATA_IQ_AMPERE         = 3,  /**< IQ 电流（raw/100，A） */
    P1010B_REPORT_DATA_ROTOR_POSITION    = 4,  /**< 转子位置（0~32768） */
    P1010B_REPORT_DATA_FAULT_INFO        = 5,  /**< 故障信息 */
    P1010B_REPORT_DATA_WARNING_INFO      = 6,  /**< 警告信息 */
    P1010B_REPORT_DATA_MOS_TEMPERATURE   = 7,  /**< MOS 温度 */
    P1010B_REPORT_DATA_WINDING_TEMP      = 8,  /**< 电机绕组温度 */
    P1010B_REPORT_DATA_CURRENT_MODE      = 9,  /**< 当前模式 */
    P1010B_REPORT_DATA_BUS_VOLTAGE       = 10, /**< 当前系统电压（raw/10，V） */
    P1010B_REPORT_DATA_TURN_COUNT        = 11, /**< 当前累计圈数（raw/100） */
    P1010B_REPORT_DATA_SYSTEM_STATE      = 12, /**< 当前系统状态 */
    P1010B_REPORT_DATA_ABSOLUTE_POSITION = 13, /**< 绝对位置（0~32768） */
    P1010B_REPORT_DATA_PHASE_CURRENT_MAX = 14, /**< 相电流最大值（raw/100，A） */
} P1010BReportDataType_e;

/**
 * @brief 主动上报配置（0x34）
 */
typedef struct
{
    bool enable;                                            /**< 是否启用主动上报 */
    uint8_t periodMs;                                       /**< 上报周期（ms） */
    uint8_t dataTypeSlots[P1010B_ACTIVE_REPORT_SLOT_COUNT]; /**< 上报类型槽位（使用 P1010BReportDataType_e） */
} P1010BActiveReportConfig_s;

/**
 * @brief 驱动工作模式
 */
typedef enum {
    P1010B_MODE_OPEN_LOOP = 0u, /**< 开环/电压模式，给定值 = V * 100 */
    P1010B_MODE_MIT       = 1u, /**< MIT 模式，暂未实现（是官方协议没有给实现，后续考虑软件内置MIT） */
    P1010B_MODE_CURRENT   = 2u, /**< 电流模式，给定值 = A * 100，默认模式（官方手册规定的） */
    P1010B_MODE_SPEED     = 3u, /**< 速度模式，给定值 = RPM * 10 */
    P1010B_MODE_POSITION  = 4u  /**< 位置模式，给定值 = Cycle * 100 */
} P1010BMode_e;

/**
 * @brief 驱动状态机状态
 */
typedef enum {
    P1010B_STATE_DISABLED = 0, /**< 失能态，可进行受限配置 */
    P1010B_STATE_ENABLED,      /**< 使能态，可下发控制给定 */
    P1010B_STATE_FAULT_LOCKED, /**< 故障闭锁态，禁止控制给定 */
    P1010B_STATE_CONFIGURING   /**< 配置事务态（内部瞬时态） */
} P1010BState_e;

/**
 * @brief 最近一次控制拒绝原因
 * @note 用于诊断“为什么当前命令被拒绝”。
 */
typedef enum {
    P1010B_REJECT_NONE = 0,                  /**< 未拒绝 */
    P1010B_REJECT_NOT_ENABLED,               /**< 当前不在 Enabled */
    P1010B_REJECT_FAULT_LOCKED,              /**< 当前处于故障闭锁 */
    P1010B_REJECT_CONFIG_ONLY_WHEN_DISABLED, /**< 配置接口仅允许 Disabled */
    P1010B_REJECT_PARAM_NOT_WHITELISTED,     /**< 参数不在白名单 */
    P1010B_REJECT_INVALID_TARGET             /**< 目标值非法（保留扩展） */
} P1010BRejectReason_e;

/**
 * @brief 电机反馈快照（语义化量）
 */
typedef struct
{
    float speedRpm;            /**< 速度（RPM） */
    float iqAmpere;            /**< IQ 电流（A） */
    float busVoltage;          /**< 母线电压（V） */
    uint16_t absolutePosition; /**< 绝对位置编码值 */
    uint32_t timestampMs;      /**< 快照时间戳（ms） */
} P1010BFeedback_s;

/**
 * @brief 故障与报警快照
 */
typedef struct
{
    uint16_t faultCode;  /**< 故障码（故障 -> 闭锁） */
    uint16_t alarmCode;  /**< 报警码（当前仅上报，不自动降级） */
    uint32_t statusBits; /**< 协议保留位镜像（不参与 fault 回调触发判定） */
    bool calibrated;     /**< 校准是否完成 */
} P1010BFaultState_s;

/**
 * @brief 驱动配置
 * @note
 * - `motorId` 为实例绑定 ID；
 * - `requestTimeoutMs` 用于同步事务等待超时；
 * - `maxRetryCount` 为后续事务重试预留；
 * - limit 字段保留给后续限幅策略使用。
 */
typedef struct
{
    uint8_t motorId;                         /**< 电机 ID（1~8） */
    uint32_t requestTimeoutMs;               /**< 同步事务超时（ms） */
    uint8_t maxRetryCount;                   /**< 最大重试次数（预留） */
    P1010BMode_e defaultMode;                /**< 默认模式 */
    P1010BActiveReportConfig_s activeReport; /**< 默认主动上报配置（仅缓存，不在 register 自动下发） */
    int32_t currentLimitRaw;                 /**< 电流限幅（原始量，预留） */
    int32_t speedLimitRaw;                   /**< 速度限幅（原始量，预留） */
    int32_t positionLimitRaw;                /**< 位置限幅（原始量，预留） */
    int32_t voltageLimitRaw;                 /**< 电压限幅（原始量，预留） */
} P1010BConfig_s;

/**
 * @brief 默认主动上报配置结构体宏
 * @note 默认槽位：速度 / IQ 电流 / 母线电压 / 绝对位置。
 */
#define P1010B_ACTIVE_REPORT_DEFAULT_CONFIG                \
    ((P1010BActiveReportConfig_s){                         \
        .enable        = true,                             \
        .periodMs      = 10U,                              \
        .dataTypeSlots = {                                 \
            (uint8_t)P1010B_REPORT_DATA_SPEED_RPM,         \
            (uint8_t)P1010B_REPORT_DATA_IQ_AMPERE,         \
            (uint8_t)P1010B_REPORT_DATA_BUS_VOLTAGE,       \
            (uint8_t)P1010B_REPORT_DATA_ABSOLUTE_POSITION, \
        },                                                 \
    })

/**
 * @brief 默认驱动配置结构体宏
 * @param _motorId 目标电机 ID（1~8）
 */
#define P1010B_DEFAULT_CONFIG(_motorId)                          \
    ((P1010BConfig_s){                                           \
        .motorId          = (_motorId),                          \
        .requestTimeoutMs = 20U,                                 \
        .maxRetryCount    = 1U,                                  \
        .defaultMode      = P1010B_MODE_CURRENT,                 \
        .activeReport     = P1010B_ACTIVE_REPORT_DEFAULT_CONFIG, \
        .currentLimitRaw  = 0,                                   \
        .speedLimitRaw    = 0,                                   \
        .positionLimitRaw = 0,                                   \
        .voltageLimitRaw  = 0,                                   \
    })

/**
 * @brief 统一请求命令字
 * @details
 * - 同步命令：需要等待应答，使用 `p1010b_request_sync()`。
 * - 异步命令：发送即返回，使用 `p1010b_submit()`。
 */
typedef enum {
    P1010B_COMMAND_NONE = 0,          /**< 空命令/未初始化占位，不可执行 */
    P1010B_COMMAND_SET_MODE,          /**< 模式设置（逻辑命令，映射写参数28，默认同步） */
    P1010B_COMMAND_SET_TARGET,        /**< 语义给定（自动scale，映射0x32/0x33，默认异步） */
    P1010B_COMMAND_SET_ACTIVE_REPORT, /**< 配置主动上报（0x34 -> 0x60+id，默认同步） */
    P1010B_COMMAND_ACTIVE_QUERY,      /**< 主动查询4槽数据（0x35 -> 0x70+id，默认同步） */
    P1010B_COMMAND_WRITE_PARAMETER,   /**< 写参数（0x36 -> 0x80+id，默认同步） */
    P1010B_COMMAND_READ_PARAMETER,    /**< 读参数（0x37 -> 0x90+id，默认同步） */
    P1010B_COMMAND_STATE_CONTROL,     /**< 使能/失能控制（0x38 -> 0xA0+id，默认同步） */
    P1010B_COMMAND_SAVE_PARAMETERS,   /**< 参数保存（0x39 -> 0xB0+id，默认同步） */
    P1010B_COMMAND_SOFTWARE_RESET,    /**< 软件复位（0x40，默认异步） */
} P1010BCommand_e;

/**
 * @brief 请求标志位
 * @details
 * - 仅支持二选一：SYNC 或 ASYNC；
 * - 传 0 表示使用命令描述符默认模式。
 */
typedef enum {
    P1010B_REQUEST_FLAG_NONE  = 0U,
    P1010B_REQUEST_FLAG_SYNC  = (1U << 0U),
    P1010B_REQUEST_FLAG_ASYNC = (1U << 1U)
} P1010BRequestFlag_e;

/**
 * @brief 统一请求对象
 * @note
 * - `command` 决定本次请求语义；
 * - `flags` 决定同步/异步语义，0 表示采用默认模式；
 * - `timeoutMs` 仅对同步命令生效，0 表示使用驱动默认超时；
 * - `args` 中仅与 `command` 对应的字段有效。
 */
typedef struct
{
    P1010BCommand_e command;
    uint8_t flags;
    uint32_t timeoutMs;
    union {
        struct
        {
            P1010BActiveReportConfig_s config;
        } setActiveReport;
        struct
        {
            uint8_t dataTypeSlots[4U];
        } activeQuery;
        struct
        {
            uint8_t parameterId;
            int32_t parameterValue;
        } writeParameter;
        struct
        {
            uint8_t parameterId;
        } readParameter;
        struct
        {
            uint8_t command;
        } stateControl;
        struct
        {
            uint8_t command; /**< 保存命令（payload[0]，建议使用 P1010B_SAVE_PARAM_CMD_*） */
            struct
            {
                /**
                 * @brief 绝对零位数据（payload[1]）
                 * @note 根据规格书，当前版本仅开放该数据位可选。
                 */
                bool absoluteZero;
                /**
                 * @brief 参数保存命令保留位（payload[2..7]）
                 * @note 预留未来扩展，不作运行时约束。
                 */
                uint8_t reservedPayload[P1010B_SAVE_PARAM_RESERVED_BYTES];
            } data;
        } saveParameters;
        struct
        {
            P1010BMode_e mode;
        } setMode;
        struct
        {
            float targetValue;
        } setTarget;
    } args;
} P1010BRequest_s;

/**
 * @brief 统一响应对象
 * @note
 * - `ackPayload` 保留原始应答载荷，便于调试定位；
 * - `data` 为结构化结果，仅部分命令使用（例如 ACTIVE_QUERY/READ_PARAMETER）。
 */
typedef struct
{
    P1010BCommand_e command;
    OmRet_e result;
    uint32_t timestampMs;
    uint8_t ackPayload[8U];
    union {
        int16_t activeQueryValues[4U];
        struct
        {
            uint8_t parameterId;
            int32_t parameterValue;
        } readParameter;
        struct
        {
            uint8_t stateCommand;
            uint8_t reserved[3];
            P1010BFaultState_s faultState;
        } stateControl;
    } data;
} P1010BResponse_s;

/**
 * @brief 线程侧解析使用的原始帧缓存结构
 */
typedef struct
{
    uint32_t canId;       /**< 标准帧 ID */
    uint8_t payload[8];   /**< 帧载荷（8 字节） */
    uint8_t dataLength;   /**< 帧长（应为 8） */
    uint32_t timestampMs; /**< 帧时间戳（ms） */
} P1010BRawFrame_s;

/** @brief P1010B 驱动实例前置声明 */
typedef struct P1010BDriver P1010BDriver_s;

/**
 * @brief 反馈更新回调
 */
typedef void (*P1010BFeedbackCallback_t)(P1010BDriver_s *driver, const P1010BFeedback_s *feedback);
/**
 * @brief 故障/报警变化回调
 */
typedef void (*P1010BFaultCallback_t)(P1010BDriver_s *driver, const P1010BFaultState_s *fault_state);
/**
 * @brief 在线状态变化回调
 */
typedef void (*P1010BOnlineCallback_t)(P1010BDriver_s *driver, bool is_online);
/**
 * @brief 读参完成回调
 * @note 该回调在 ISR 上下文触发，回调实现必须保持非阻塞。
 */
typedef void (*P1010BParamReadCallback_t)(P1010BDriver_s *driver, uint8_t parameter_id, int32_t parameter_value, OmRet_e result,
                                          uint32_t timestamp_ms);

/**
 * @brief 请求执行等待模式
 * @details
 * - `P1010B_WAIT_MODE_ASYNC`：发送后立即返回；
 * - `P1010B_WAIT_MODE_SYNC`：等待 ISR 匹配应答并回填响应对象。
 */
typedef enum {
    P1010B_WAIT_MODE_ASYNC = 0,
    P1010B_WAIT_MODE_SYNC  = 1
} P1010BWaitMode_e;

/**
 * @brief 编码后请求对象（执行器内部使用）
 * @note
 * - `requestCanId` 为最终发送 CAN ID；
 * - `ackBaseId` 为同步事务预期应答基地址；
 * - `expected*` 字段用于同步匹配（参数/状态命令）；
 * - `payload` 为固定 8 字节协议载荷。
 */
typedef struct
{
    uint16_t requestCanId;
    uint8_t ackBaseId;
    uint8_t expectedParameterId;
    uint8_t expectedStateCommand;
    uint8_t payload[P1010B_CAN_DLC];
} P1010BEncodedRequest_s;

/**
 * @brief ISR 回调上下文（解码器 -> 回调触发桥接）
 */
typedef struct
{
    bool triggerParamReadCallback;
    uint8_t callbackParameterId;
    int32_t callbackParameterValue;
} P1010BIsrCallbackContext_s;

typedef struct P1010BCommandDescriptor P1010BCommandDescriptor_s;

/** @brief 命令状态守卫函数类型 */
typedef OmRet_e (*P1010BStateGuardFn_t)(P1010BDriver_s *driver, const P1010BRequest_s *request);
/** @brief 命令编码函数类型 */
typedef OmRet_e (*P1010BEncodeFn_t)(P1010BDriver_s *driver, const P1010BRequest_s *request, P1010BEncodedRequest_s *encoded);
/** @brief 同步应答匹配函数类型 */
typedef bool (*P1010BAckMatchFn_t)(const P1010BDriver_s *driver, const P1010BRawFrame_s *frame);
/** @brief 同步应答解码函数类型 */
typedef void (*P1010BDecodeAckFn_t)(P1010BDriver_s *driver, const P1010BRawFrame_s *frame, P1010BResponse_s *response,
                                    P1010BIsrCallbackContext_s *callback_context);
/** @brief 命令执行后处理函数类型 */
typedef void (*P1010BPostCommitFn_t)(P1010BDriver_s *driver, const P1010BRequest_s *request, OmRet_e result,
                                     const P1010BResponse_s *response);

/**
 * @brief 命令描述符
 * @details
 * 每条命令通过一行描述符串联：状态守卫 -> 编码 -> 同步匹配 -> 解码 -> 后处理。
 */
struct P1010BCommandDescriptor {
    P1010BCommand_e command;
    uint16_t requestCanId;
    uint8_t ackBaseId;
    uint8_t defaultRequestFlags;
    bool useConfiguringState;
    P1010BStateGuardFn_t stateGuardFn;
    P1010BEncodeFn_t encodeFn;
    P1010BAckMatchFn_t ackMatchFn;
    P1010BDecodeAckFn_t decodeAckFn;
    P1010BPostCommitFn_t postCommitFn;
};

/** @brief RX 副作用处理函数类型（反馈/故障等） */
typedef void (*P1010BRxSideEffectFn_t)(P1010BDriver_s *driver, const P1010BRawFrame_s *frame);

/**
 * @brief 接收分发表项
 * @details
 * 用于把 replyBaseId O(1) 路由到副作用处理与同步完成路径。
 */
typedef struct
{
    uint8_t replyBaseId;
    bool mayCompleteSync;
    P1010BRxSideEffectFn_t sideEffectFn;
} P1010BRxDispatchDescriptor_s;

/**
 * @brief P1010B 总线上下文（一个 CAN 设备对应一个实例）
 * @note
 * - `groupTargetsRaw` 用于维护 0x32/0x33 的组帧缓存；
 * - `driverTable[motorId]` 用于把总线帧路由到具体驱动实例；
 * - ISR 直接完成协议解析与状态路由，不依赖额外软件队列中转。
 */
typedef struct
{
    Device_t canDevice;             /**< 绑定的 CAN 设备 */
    CanFilterHandle_t filterHandle; /**< 过滤器句柄 */
    bool filterAllocated;           /**< 过滤器是否已分配 */
    int16_t groupTargetsRaw[2][4];  /**< 组帧缓存（2 组 x 每组 4 槽） */
    uint32_t rxDroppedCount;        /**< 线程解析格式丢弃计数（非标准帧或 DLC!=8） */
    uint32_t rxLateReadParamCount;  /**< 同步应答迟到/未匹配计数 */

    P1010BDriver_s *driverTable[P1010B_MOTOR_ID_MAX + 1U]; /**< 按 motorId 索引驱动实例 */
} P1010BBus_s;

/**
 * @brief 驱动运行态数据（实例状态）
 */
typedef struct
{
    P1010BMode_e currentMode;                /**< 当前模式 */
    P1010BState_e state;                     /**< 当前状态机状态 */
    P1010BRejectReason_e lastRejectReason;   /**< 最近拒绝原因 */
    float targetScale;                       /**< 当前模式目标缩放系数（注册/设模时确定） */
    P1010BActiveReportConfig_s activeReport; /**< 生效中的主动上报配置 */
} P1010BDriverRuntime_s;

/**
 * @brief 驱动遥测数据（反馈/故障/在线）
 */
typedef struct
{
    P1010BFeedback_s feedback;         /**< 最近反馈快照 */
    P1010BFaultState_s faultState;     /**< 最近故障快照 */
    bool online;                       /**< 在线状态（仅由接收边沿更新，不含超时检测） */
    uint32_t lastSuccessRxTimestampMs; /**< 最近成功接收时间 */
    uint32_t lastSuccessTxTimestampMs; /**< 最近成功发送时间 */
} P1010BDriverTelemetry_s;

/**
 * @brief 驱动同步事务数据（请求-应答匹配上下文）
 */
typedef struct
{
    bool pending;                   /**< 是否存在未决同步事务 */
    P1010BCommand_e pendingCommand; /**< 当前同步事务命令 */
    uint8_t expectedReplyBaseId;    /**< 期望应答基地址 */
    uint8_t expectedParameterId;    /**< 期望参数 ID（参数读写事务使用） */
    uint8_t expectedStateCommand;   /**< 期望状态命令（0x38 事务使用） */
    OmRet_e result;               /**< 当前同步事务结果 */
    uint32_t timestampMs;           /**< 当前同步事务完成时间戳 */
    P1010BResponse_s response;      /**< 同步事务应答缓存 */
    Completion_s completion;        /**< 同步事务 completion */
} P1010BDriverSync_s;

/**
 * @brief 驱动回调注册表（用户回调）
 */
typedef struct
{
    P1010BFeedbackCallback_t onFeedback;    /**< 反馈回调 */
    P1010BFaultCallback_t onFaultChanged;   /**< 故障回调 */
    P1010BOnlineCallback_t onOnlineChanged; /**< 在线状态回调 */
    P1010BParamReadCallback_t onParamRead;  /**< 读参完成回调（ISR 上下文） */
} P1010BDriverCallbacks_s;

/**
 * @brief P1010B 驱动实例
 * @note
 * 一个实例对应一个电机 ID；多个实例可共享同一个 `P1010BBus_s`。
 * @details
 * 结构体仅保存实例状态数据，不承载通用策略函数。
 */
struct P1010BDriver {
    P1010BBus_s *bus;                  /**< 所属总线（顶层保留） */
    P1010BConfig_s config;             /**< 驱动配置（顶层保留） */
    P1010BDriverRuntime_s runtime;     /**< 运行态数据 */
    P1010BDriverTelemetry_s telemetry; /**< 遥测数据 */
    P1010BDriverSync_s sync;           /**< 同步事务数据 */
    P1010BDriverCallbacks_s callbacks; /**< 用户回调注册表 */
};

/* -------------------------------------------------------------------------- */
/* Bus API                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief 初始化总线上下文并分配过滤器
 * @param bus 总线上下文
 * @param can_device 已打开的 CAN 设备句柄
 * @return `OM_OK` 成功，其他返回值表示失败原因
 */
OmRet_e p1010b_bus_init(P1010BBus_s *bus, Device_t can_device);

/**
 * @brief 释放总线上下文资源
 * @param bus 总线上下文
 * @return `OM_OK` 成功，其他返回值表示失败原因
 */
OmRet_e p1010b_bus_deinit(P1010BBus_s *bus);

/* -------------------------------------------------------------------------- */
/* Driver lifecycle API                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief 注册驱动实例到总线
 * @param bus 总线上下文
 * @param driver 驱动实例
 * @param config 驱动配置（可为 NULL，表示使用默认配置）
 * @return `OM_OK` 成功；`OM_ERR_CONFLICT` 表示 motorId 冲突
 */
OmRet_e p1010b_register(P1010BBus_s *bus, P1010BDriver_s *driver, const P1010BConfig_s *config);

/**
 * @brief 设置事件回调
 * @param driver 驱动实例
 * @param feedback_cb 反馈回调
 * @param fault_cb 故障回调
 * @param online_cb 在线状态回调
 * @param param_read_cb 读参完成回调（ISR 上下文）
 */
void p1010b_set_callbacks(P1010BDriver_s *driver, P1010BFeedbackCallback_t feedback_cb, P1010BFaultCallback_t fault_cb,
                          P1010BOnlineCallback_t online_cb, P1010BParamReadCallback_t param_read_cb);

/* -------------------------------------------------------------------------- */
/* Unified request API                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief 异步提交请求（仅接受异步命令）
 * @note 仅支持 SET_TARGET / SOFTWARE_RESET。
 */
OmRet_e p1010b_submit(P1010BDriver_s *driver, const P1010BRequest_s *request);

/**
 * @brief 同步请求（仅接受同步命令）
 * @note 支持 SET_ACTIVE_REPORT / ACTIVE_QUERY / WRITE_PARAMETER /
 *       READ_PARAMETER / STATE_CONTROL / SAVE_PARAMETERS / SET_MODE。
 */
OmRet_e p1010b_request_sync(P1010BDriver_s *driver, const P1010BRequest_s *request, P1010BResponse_s *response);

/* -------------------------------------------------------------------------- */
/* Behavior API                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief 失能电机（构造+执行一体，同步）
 * @param driver 驱动实例
 * @param timeout_ms 超时时间（ms），0 表示使用驱动默认超时
 * @param response 可选输出响应（可传 `NULL`）
 * @return `OM_OK` 成功，其他值表示失败原因
 */
OmRet_e p1010b_disable(P1010BDriver_s *driver, uint32_t timeout_ms, P1010BResponse_s *response);

/**
 * @brief 使能电机（构造+执行一体，同步）
 * @param driver 驱动实例
 * @param timeout_ms 超时时间（ms），0 表示使用驱动默认超时
 * @param response 可选输出响应（可传 `NULL`）
 * @return `OM_OK` 成功，其他值表示失败原因
 */
OmRet_e p1010b_enable(P1010BDriver_s *driver, uint32_t timeout_ms, P1010BResponse_s *response);

/**
 * @brief 设置工作模式（构造+执行一体，同步）
 * @param driver 驱动实例
 * @param mode 目标模式
 * @param timeout_ms 超时时间（ms），0 表示使用驱动默认超时
 * @param response 可选输出响应（可传 `NULL`）
 * @return `OM_OK` 成功，其他值表示失败原因
 */
OmRet_e p1010b_set_mode(P1010BDriver_s *driver, P1010BMode_e mode, uint32_t timeout_ms, P1010BResponse_s *response);

/**
 * @brief 设置主动上报配置（构造+执行一体，同步）
 * @param driver 驱动实例
 * @param config 主动上报配置
 * @param timeout_ms 超时时间（ms），0 表示使用驱动默认超时
 * @param response 可选输出响应（可传 `NULL`）
 * @return `OM_OK` 成功，其他值表示失败原因
 */
OmRet_e p1010b_set_active_report(P1010BDriver_s *driver, const P1010BActiveReportConfig_s *config, uint32_t timeout_ms, P1010BResponse_s *response);

/**
 * @brief 主动查询指定 4 槽数据（构造+执行一体，同步）
 * @param driver 驱动实例
 * @param slot0 槽位 0 类型
 * @param slot1 槽位 1 类型
 * @param slot2 槽位 2 类型
 * @param slot3 槽位 3 类型
 * @param timeout_ms 超时时间（ms），0 表示使用驱动默认超时
 * @param response 可选输出响应（可传 `NULL`）
 * @return `OM_OK` 成功，其他值表示失败原因
 */
OmRet_e p1010b_active_query_slots(P1010BDriver_s *driver, P1010BReportDataType_e slot0, P1010BReportDataType_e slot1,
                                    P1010BReportDataType_e slot2, P1010BReportDataType_e slot3, uint32_t timeout_ms,
                                    P1010BResponse_s *response);

/**
 * @brief 写参数（构造+执行一体，同步）
 * @param driver 驱动实例
 * @param parameter_id 参数 ID
 * @param parameter_value 参数值
 * @param timeout_ms 超时时间（ms），0 表示使用驱动默认超时
 * @param response 可选输出响应（可传 `NULL`）
 * @return `OM_OK` 成功，其他值表示失败原因
 */
OmRet_e p1010b_write_parameter(P1010BDriver_s *driver, uint8_t parameter_id, int32_t parameter_value, uint32_t timeout_ms,
                                 P1010BResponse_s *response);

/**
 * @brief 读参数（构造+执行一体，同步）
 * @param driver 驱动实例
 * @param parameter_id 参数 ID
 * @param timeout_ms 超时时间（ms），0 表示使用驱动默认超时
 * @param response 可选输出响应（可传 `NULL`）
 * @return `OM_OK` 成功，其他值表示失败原因
 */
OmRet_e p1010b_read_parameter(P1010BDriver_s *driver, uint8_t parameter_id, uint32_t timeout_ms, P1010BResponse_s *response);

/**
 * @brief 保存参数（构造+执行一体，同步）
 * @param driver 驱动实例
 * @param set_absolute_zero 是否设置绝对零点
 * @param timeout_ms 超时时间（ms），0 表示使用驱动默认超时
 * @param response 可选输出响应（可传 `NULL`）
 * @return `OM_OK` 成功，其他值表示失败原因
 * @note 根据规格书，当前保存数据中仅绝对零位可选。
 */
OmRet_e p1010b_save_parameters(P1010BDriver_s *driver, bool set_absolute_zero, uint32_t timeout_ms, P1010BResponse_s *response);

/**
 * @brief 下发目标值（构造+执行一体，异步）
 * @param driver 驱动实例
 * @param target_value 语义化目标值
 * @return `OM_OK` 成功，其他值表示失败原因
 */
OmRet_e p1010b_set_target(P1010BDriver_s *driver, float target_value);

/**
 * @brief 软件复位（构造+执行一体，异步）
 * @param driver 驱动实例
 * @return `OM_OK` 成功，其他值表示失败原因
 */
OmRet_e p1010b_software_reset(P1010BDriver_s *driver);

/* -------------------------------------------------------------------------- */
/* Request helper API                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief 创建基础请求对象
 * @param command 命令
 * @param flags 请求标志（SYNC/ASYNC，传 0 使用默认模式）
 * @return 初始化后的请求对象
 */
static inline P1010BRequest_s p1010b_request_make(P1010BCommand_e command, uint8_t flags)
{
    return (P1010BRequest_s){
        .command   = command,
        .flags     = flags,
        .timeoutMs = 0U,
    };
}

/**
 * @brief 构造状态控制同步请求
 * @param state_command 状态命令（`P1010B_STATE_CMD_ENABLE` / `P1010B_STATE_CMD_DISABLE`）
 * @return 已填充的 `P1010B_COMMAND_STATE_CONTROL` 同步请求对象
 * @note 该构造器固定 `flags=SYNC`，建议配合 `p1010b_request_sync()` 使用。
 */
static inline P1010BRequest_s p1010b_req_state_control(uint8_t state_command)
{
    P1010BRequest_s request           = p1010b_request_make(P1010B_COMMAND_STATE_CONTROL, (uint8_t)P1010B_REQUEST_FLAG_SYNC);
    request.args.stateControl.command = state_command;
    return request;
}

/**
 * @brief 构造使能同步请求
 * @return 已填充的 `P1010B_COMMAND_STATE_CONTROL` 同步请求对象（ENABLE）
 */
static inline P1010BRequest_s p1010b_req_enable(void)
{
    return p1010b_req_state_control(P1010B_STATE_CMD_ENABLE);
}

/**
 * @brief 构造失能同步请求
 * @return 已填充的 `P1010B_COMMAND_STATE_CONTROL` 同步请求对象（DISABLE）
 */
static inline P1010BRequest_s p1010b_req_disable(void)
{
    return p1010b_req_state_control(P1010B_STATE_CMD_DISABLE);
}

/**
 * @brief 构造模式设置同步请求
 * @param mode 目标工作模式（`P1010BMode_e`）
 * @return 已填充的 `P1010B_COMMAND_SET_MODE` 同步请求对象
 * @note 该请求最终映射为写参数 `P1010B_PARAM_WORK_MODE`。
 */
static inline P1010BRequest_s p1010b_req_set_mode(P1010BMode_e mode)
{
    P1010BRequest_s request   = p1010b_request_make(P1010B_COMMAND_SET_MODE, (uint8_t)P1010B_REQUEST_FLAG_SYNC);
    request.args.setMode.mode = mode;
    return request;
}

/**
 * @brief 构造主动上报配置同步请求
 * @param config 主动上报配置指针（建议非 `NULL`）
 * @return 已填充的 `P1010B_COMMAND_SET_ACTIVE_REPORT` 同步请求对象
 * @note
 * - 该构造器固定 `flags=SYNC`；
 * - 若 `config==NULL`，`args` 保持零值，执行时通常会因 `periodMs==0` 被守卫拒绝。
 */
static inline P1010BRequest_s p1010b_req_set_active_report(const P1010BActiveReportConfig_s *config)
{
    P1010BRequest_s request = p1010b_request_make(P1010B_COMMAND_SET_ACTIVE_REPORT, (uint8_t)P1010B_REQUEST_FLAG_SYNC);
    if (config != NULL) {
        request.args.setActiveReport.config = *config;
    }
    return request;
}

/**
 * @brief 构造目标值异步请求（自动 scale）
 * @param target_value 语义化目标值（单位取决于当前模式：A/V/RPM/Cycle）
 * @return 已填充的 `P1010B_COMMAND_SET_TARGET` 异步请求对象
 * @note
 * - 该构造器固定 `flags=ASYNC`，建议配合 `p1010b_submit()` 使用；
 * - 驱动会按当前模式对应的目标缩放缓存自动换算为协议原始量。
 */
static inline P1010BRequest_s p1010b_req_set_target(float target_value)
{
    P1010BRequest_s request            = p1010b_request_make(P1010B_COMMAND_SET_TARGET, (uint8_t)P1010B_REQUEST_FLAG_ASYNC);
    request.args.setTarget.targetValue = target_value;
    return request;
}

/**
 * @brief 构造主动查询同步请求（4 槽位显式指定）
 * @param slot0 槽位 0 类型
 * @param slot1 槽位 1 类型
 * @param slot2 槽位 2 类型
 * @param slot3 槽位 3 类型
 * @return 已填充的 `P1010B_COMMAND_ACTIVE_QUERY` 同步请求对象
 */
static inline P1010BRequest_s p1010b_req_active_query_slots(P1010BReportDataType_e slot0, P1010BReportDataType_e slot1,
                                                            P1010BReportDataType_e slot2, P1010BReportDataType_e slot3)
{
    P1010BRequest_s request                   = p1010b_request_make(P1010B_COMMAND_ACTIVE_QUERY, (uint8_t)P1010B_REQUEST_FLAG_SYNC);
    request.args.activeQuery.dataTypeSlots[0] = (uint8_t)slot0;
    request.args.activeQuery.dataTypeSlots[1] = (uint8_t)slot1;
    request.args.activeQuery.dataTypeSlots[2] = (uint8_t)slot2;
    request.args.activeQuery.dataTypeSlots[3] = (uint8_t)slot3;
    return request;
}

/**
 * @brief 构造参数写同步请求
 * @param parameter_id 参数 ID
 * @param parameter_value 参数值
 * @return 已填充的 `P1010B_COMMAND_WRITE_PARAMETER` 同步请求对象
 */
static inline P1010BRequest_s p1010b_req_write_parameter(uint8_t parameter_id, int32_t parameter_value)
{
    P1010BRequest_s request                    = p1010b_request_make(P1010B_COMMAND_WRITE_PARAMETER, (uint8_t)P1010B_REQUEST_FLAG_SYNC);
    request.args.writeParameter.parameterId    = parameter_id;
    request.args.writeParameter.parameterValue = parameter_value;
    return request;
}

/**
 * @brief 构造参数读同步请求
 * @param parameter_id 参数 ID
 * @return 已填充的 `P1010B_COMMAND_READ_PARAMETER` 同步请求对象
 */
static inline P1010BRequest_s p1010b_req_read_parameter(uint8_t parameter_id)
{
    P1010BRequest_s request                = p1010b_request_make(P1010B_COMMAND_READ_PARAMETER, (uint8_t)P1010B_REQUEST_FLAG_SYNC);
    request.args.readParameter.parameterId = parameter_id;
    return request;
}

/**
 * @brief 构造参数保存同步请求
 * @param set_absolute_zero 是否设置绝对零点
 * @return 已填充的 `P1010B_COMMAND_SAVE_PARAMETERS` 同步请求对象
 * @note
 * - `saveParameters` 采用“命令 + 数据”双层模型；
 * - 根据规格书，当前保存数据中仅绝对零位可选。
 */
static inline P1010BRequest_s p1010b_req_save_parameters(bool set_absolute_zero)
{
    P1010BRequest_s request               = p1010b_request_make(P1010B_COMMAND_SAVE_PARAMETERS, (uint8_t)P1010B_REQUEST_FLAG_SYNC);
    request.args.saveParameters.command   = set_absolute_zero ? P1010B_SAVE_PARAM_CMD_SET_ABSOLUTE_ZERO : P1010B_SAVE_PARAM_CMD_SAVE;
    request.args.saveParameters.data.absoluteZero = set_absolute_zero;
    for (uint8_t index = 0U; index < P1010B_SAVE_PARAM_RESERVED_BYTES; index++) {
        request.args.saveParameters.data.reservedPayload[index] = 0U;
    }
    return request;
}

/**
 * @brief 构造软件复位异步请求
 * @return 已填充的 `P1010B_COMMAND_SOFTWARE_RESET` 异步请求对象
 */
static inline P1010BRequest_s p1010b_req_software_reset(void)
{
    return p1010b_request_make(P1010B_COMMAND_SOFTWARE_RESET, (uint8_t)P1010B_REQUEST_FLAG_ASYNC);
}

/**
 * @brief 构造主动上报基础同步请求（速度/IQ/电压/绝对位置）
 * @param enable 是否启用主动上报
 * @param period_ms 上报周期（ms）
 * @return 已填充的 `P1010B_COMMAND_SET_ACTIVE_REPORT` 同步请求对象
 */
static inline P1010BRequest_s p1010b_req_set_active_report_basic(bool enable, uint8_t period_ms)
{
    P1010BActiveReportConfig_s config = {
        .enable        = enable,
        .periodMs      = period_ms,
        .dataTypeSlots = {
            (uint8_t)P1010B_REPORT_DATA_SPEED_RPM,
            (uint8_t)P1010B_REPORT_DATA_IQ_AMPERE,
            (uint8_t)P1010B_REPORT_DATA_BUS_VOLTAGE,
            (uint8_t)P1010B_REPORT_DATA_ABSOLUTE_POSITION,
        },
    };
    return p1010b_req_set_active_report(&config);
}

/**
 * @brief 构造主动查询基础同步请求（速度/IQ/电压/绝对位置）
 * @return 已填充的 `P1010B_COMMAND_ACTIVE_QUERY` 同步请求对象
 */
static inline P1010BRequest_s p1010b_req_active_query_basic(void)
{
    return p1010b_req_active_query_slots(P1010B_REPORT_DATA_SPEED_RPM, P1010B_REPORT_DATA_IQ_AMPERE,
                                         P1010B_REPORT_DATA_BUS_VOLTAGE, P1010B_REPORT_DATA_ABSOLUTE_POSITION);
}

/**
 * @brief 构造心跳使能参数写同步请求
 * @param enable 是否启用心跳
 * @return 已填充的 `P1010B_COMMAND_WRITE_PARAMETER` 同步请求对象
 */
static inline P1010BRequest_s p1010b_req_set_heartbeat_enable(bool enable)
{
    return p1010b_req_write_parameter(P1010B_PARAM_HEARTBEAT_ENABLE, enable ? 1 : 0);
}

/**
 * @brief 构造心跳周期参数写同步请求
 * @param period_ms 心跳周期（ms）
 * @return 已填充的 `P1010B_COMMAND_WRITE_PARAMETER` 同步请求对象
 */
static inline P1010BRequest_s p1010b_req_set_heartbeat_time_ms(uint32_t period_ms)
{
    return p1010b_req_write_parameter(P1010B_PARAM_HEARTBEAT_TIME, (int32_t)period_ms);
}

/**
 * @brief 构造故障屏蔽参数写同步请求
 * @param mask 故障屏蔽位
 * @return 已填充的 `P1010B_COMMAND_WRITE_PARAMETER` 同步请求对象
 */
static inline P1010BRequest_s p1010b_req_set_fault_mask(uint32_t mask)
{
    return p1010b_req_write_parameter(P1010B_PARAM_FAULT_MASK, (int32_t)mask);
}

/**
 * @brief 构造 CAN 波特率与通信类型参数写同步请求
 * @param mode 参数值
 * @return 已填充的 `P1010B_COMMAND_WRITE_PARAMETER` 同步请求对象
 */
static inline P1010BRequest_s p1010b_req_set_can_baud_mode(uint32_t mode)
{
    return p1010b_req_write_parameter(P1010B_PARAM_CAN_BAUD_MODE, (int32_t)mode);
}

/**
 * @brief 为请求设置同步超时并返回副本
 * @param request 请求对象副本
 * @param timeout_ms 超时时间（ms）
 * @return 回填了 `timeoutMs` 的请求对象
 */
static inline P1010BRequest_s p1010b_req_with_timeout(P1010BRequest_s request, uint32_t timeout_ms)
{
    request.timeoutMs = timeout_ms;
    return request;
}

/* -------------------------------------------------------------------------- */
/* Read-only API                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief 获取最近反馈快照
 * @param driver 驱动实例
 * @return 非空返回反馈快照地址；参数非法返回 `NULL`
 */
const P1010BFeedback_s *p1010b_get_feedback(const P1010BDriver_s *driver);
/**
 * @brief 获取最近故障快照
 * @param driver 驱动实例
 * @return 非空返回故障快照地址；参数非法返回 `NULL`
 */
const P1010BFaultState_s *p1010b_get_fault_state(const P1010BDriver_s *driver);
/**
 * @brief 获取当前状态
 * @param driver 驱动实例
 * @return 当前状态；参数非法返回 `P1010B_STATE_DISABLED`
 */
P1010BState_e p1010b_get_state(const P1010BDriver_s *driver);
/**
 * @brief 获取最近拒绝原因
 * @param driver 驱动实例
 * @return 最近拒绝原因；参数非法返回 `P1010B_REJECT_NONE`
 */
P1010BRejectReason_e p1010b_get_last_reject_reason(const P1010BDriver_s *driver);
/**
 * @brief 获取在线状态
 * @param driver 驱动实例
 * @return `true` 在线；`false` 离线或参数非法
 */
bool p1010b_is_online(const P1010BDriver_s *driver);
/**
 * @brief 获取线程解析格式丢弃计数（非标准帧或 DLC!=8）
 * @param bus 总线上下文
 * @return 丢弃计数；参数非法返回 `0`
 */
uint32_t p1010b_get_rx_drop_count(const P1010BBus_s *bus);

#ifdef __cplusplus
}
#endif

#endif /* __P1010B_DRIVER_H__ */
