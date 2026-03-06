#ifndef __HAL_CAN_CORE_H
#define __HAL_CAN_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "core/data_struct/bitmap.h"
#include "core/data_struct/corelist.h"
#include "drivers/model/device.h"
#include "osal/osal_timer.h"

/* CAN 数据长度定义 */
typedef enum {
    CAN_DLC_0 = 0U, // 0 字节
    CAN_DLC_1 = 1U,
    CAN_DLC_2,
    CAN_DLC_3,
    CAN_DLC_4,
    CAN_DLC_5,
    CAN_DLC_6,
    CAN_DLC_7,
    CAN_DLC_8,
    /* 以下长度仅用于 CAN FD */
    CAN_DLC_12 = 9U,
    CAN_DLC_16,
    CAN_DLC_20,
    CAN_DLC_24,
    CAN_DLC_32,
    CAN_DLC_48,
    CAN_DLC_64,
} CanDlc;

/* CAN 同步跳转宽度 */
typedef enum {
    CAN_SYNCJW_1TQ = 0U,
    CAN_SYNCJW_2TQ,
    CAN_SYNCJW_3TQ,
    CAN_SYNCJW_4TQ
} CanSjw;

typedef enum {
    CAN_TSEG1_1TQ = 0U,
    CAN_TSEG1_2TQ,
    CAN_TSEG1_3TQ,
    CAN_TSEG1_4TQ,
    CAN_TSEG1_5TQ,
    CAN_TSEG1_6TQ,
    CAN_TSEG1_7TQ,
    CAN_TSEG1_8TQ,
    CAN_TSEG1_9TQ,
    CAN_TSEG1_10TQ,
    CAN_TSEG1_11TQ,
    CAN_TSEG1_12TQ,
    CAN_TSEG1_13TQ,
    CAN_TSEG1_14TQ,
    CAN_TSEG1_15TQ,
    CAN_TSEG1_16TQ,
    CAN_TSEG1_MAX,
} CanBS1;

typedef enum {
    CAN_TSEG2_1TQ = 0U,
    CAN_TSEG2_2TQ,
    CAN_TSEG2_3TQ,
    CAN_TSEG2_4TQ,
    CAN_TSEG2_5TQ,
    CAN_TSEG2_6TQ,
    CAN_TSEG2_7TQ,
    CAN_TSEG2_8TQ,
    CAN_TSEG2_MAX,
} CanBS2;

typedef enum {
    CAN_BAUD_10K  = 50000U,
    CAN_BAUD_20K  = 250000U,
    CAN_BAUD_50K  = 500000U,
    CAN_BAUD_100K = 1000000U,
    CAN_BAUD_125K = 1250000U,
    CAN_BAUD_250K = 2500000U,
    CAN_BAUD_500K = 5000000U,
    CAN_BAUD_800K = 8000000U,
    CAN_BAUD_1M   = 10000000U,
} CanBaudRate;

/* CAN 工作模式 */
typedef enum {
    CAN_WORK_NORMAL = 0U,     // 正常模式：收发均连接总线
    CAN_WORK_LOOPBACK,        // 环回模式：回环自收发，用于本机自检
    CAN_WORK_SILENT,          // 静默模式：仅监听总线，不主动发送
    CAN_WORK_SILENT_LOOPBACK, // 静默环回：本机回环且不驱动总线
} CanWorkMode;

/* CAN 节点错误状态 */
typedef enum CanNodeStatus {
    CAN_NODE_STATUS_ACTIVE,  // 错误主动
    CAN_NODE_STATUS_PASSIVE, // 错误被动
    CAN_NODE_STATUS_BUSOFF,  // 总线关闭（BUS-OFF）
} CanNodeErrStatus;

/* CAN 过滤器模式 */
typedef enum {
    CAN_FILTER_MODE_LIST, // 列表匹配模式
    CAN_FILTER_MODE_MASK, // 掩码匹配模式
} CanFilterMode;

/* CAN ID 类型 */
typedef enum {
    CAN_IDE_STD = 0U,
    CAN_IDE_EXT,
} CanIdType;

typedef enum {
    CAN_FILTER_ID_STD = 0U, // 仅过滤标准 ID
    CAN_FILTER_ID_EXT,      // 仅过滤扩展 ID
    CAN_FILTER_ID_STD_EXT,  // 同时过滤标准 ID 与扩展 ID
} CanFilterIdType;

/* CAN 报文类型 */
typedef enum {
    CAN_MSG_TYPE_DATA = 0U, // 数据帧
    CAN_MSG_TYPE_REMOTE     // 远程帧
} CanMsgType;

/* CAN FD 速率切换标志 */
typedef enum {
    CANFD_MSG_BRS_OFF = 0U,
    CANFD_MSG_BRS_ON
} CanFdMsgBrs;

typedef enum {
    CAN_MSG_CLASS = 0U,
    CAN_MSG_FD,
} CanFdMsgFormat;

typedef enum {
    CANFD_MSG_ESI_ACITVE,
    CANFD_MSG_ESI_PASSIVE,
} CanFdESI;

/* CAN 接收侧软件错误码 */
typedef enum {
    CAN_ERR_NONE = 0U,
    CAN_ERR_FILTER_EMPTY, // 过滤器资源为空
    CAN_ERR_FILTER_BANK,  // 过滤器 bank 参数错误
    CAN_ERR_UNKNOWN,      // 未知错误

    // FIFO 相关错误
    CAN_ERR_FIFO_EMPTY,
    CAN_ERR_SOFT_FIFO_OVERFLOW, // 软件 FIFO 溢出

} CanErrorCode;

/* CAN 中断事件 */
typedef enum {
    CAN_ISR_EVENT_INT_RX_DONE = 0U, // 接收中断完成
    CAN_ISR_EVENT_INT_TX_DONE,      // 发送中断完成
} CanIsrEvent;

typedef enum {
    CAN_ERR_EVENT_NONE,
    CAN_ERR_EVENT_RX_OVERFLOW      = 1U,      // 接收 FIFO 溢出
    CAN_ERR_EVENT_TX_FAIL          = 1U << 1, // 发送失败
    CAN_ERR_EVENT_ARBITRATION_FAIL = 1U << 2, // 仲裁失败
    CAN_ERR_EVENT_BUS_STATUS       = 1U << 3, // 总线状态变化（主动/被动/BUS-OFF）

    // 协议级详细错误计数可按需下沉到硬件层采集

    // CAN_ERR_EVENT_CRC_ERROR = 1U << 4, // CRC 错误
    // CAN_ERR_EVENT_FORMAT_ERROR = 1U << 5, // 格式错误
    // CAN_ERR_EVENT_STUFF_ERROR = 1U << 6, // 填充错误
    // CAN_ERR_EVENT_BITDOMINANT_ERROR = 1U << 7, // 位显性错误
    // CAN_ERR_EVENT_BITRECESSIVE_ERROR = 1U << 8, // 位隐性错误
    // CAN_ERR_EVENT_ACK_ERROR = 1U << 9, // ACK 错误
} CanErrEvent;

typedef uint16_t CanFilterHandle;

/**
 * @defgroup CAN_TX_MODE_DEF
 * @brief CAN 发送队列覆写策略
 *  @{
 */
#define CAN_TX_MODE_UNOVERWRTING (0U) // 不覆写：队列满时保留旧数据
#define CAN_TX_MODE_OVERWRITING  (1U) // 覆写：队列满时覆盖旧数据
/**
 * @defgroup CAN_TX_MODE_DEF
 * @}
 */

/**
 * @defgroup CAN_FILTER_CFG_DEF
 * @brief CAN 过滤器配置辅助宏
 * @{
 */
#define CAN_FILTER_REQUEST_INIT(_mode, _idType, _id, _mask, _rx_callback, _param)                                          \
    (CanFilterRequest)                                                                                                    \
    {                                                                                                                       \
        .workMode = _mode, .idType = _idType, .id = _id, .mask = _mask, .rxCallback = _rx_callback, .param = _param \
    }
/**
 * @defgroup CAN_FILTER_CFG_DEF
 * @}
 */

/**
 * @defgroup CAN_REG_DEF
 * @brief CAN 设备注册参数定义
 * @{
 */
#define CAN_REG_INT_TX        DEVICE_REG_INT_TX     // 注册发送中断能力
#define CAN_REG_INT_RX        DEVICE_REG_INT_RX     // 注册接收中断能力
#define CAN_REG_IS_STANDALONG DEVICE_REG_STANDALONG // 注册为独占设备
/**
 * @defgroup CAN_REG_DEF
 * @}
 */

/**
 * @brief CAN 打开参数定义
 * @{
 */
#define CAN_O_INT_TX DEVICE_O_INT_TX // 打开发送中断
#define CAN_O_INT_RX DEVICE_O_INT_RX // 打开接收中断
                                     /**
                                      * @brief CAN 打开参数定义
                                      * @}
                                      */

/**
 * @defgroup CAN_CMD_DEF
 * @brief CAN 设备控制命令定义
 * @{
 */
#define CAN_CMD_CFG                  (0x00U) // 配置 CAN 控制器
#define CAN_CMD_SUSPEND              (0x01U) // 挂起 CAN 收发
#define CAN_CMD_RESUME               (0x02U) // 恢复 CAN 收发
#define CAN_CMD_SET_IOTYPE           (0x03U) // 设置 IO 类型（中断/轮询等）
#define CAN_CMD_CLR_IOTYPE           (0x04U) // 清除 IO 类型
#define CAN_CMD_CLOSE                (0x05U) // 关闭 CAN 设备
#define CAN_CMD_FLUSH                (0x06U) // 清空收发缓存
#define CAN_CMD_FILTER_ALLOC         (0x07U) // 分配并激活过滤器
#define CAN_CMD_FILTER_FREE          (0x08U) // 释放过滤器
#define CAN_CMD_START                (0x09U) // 启动 CAN 控制器
#define CAN_CMD_GET_STATUS           (0x0AU) // 获取 CAN 状态
#define CAN_CMD_GET_CAPABILITY       (0x0BU) // 获取硬件能力描述
/**
 * @defgroup CAN_CMD_DEF
 * @}
 */

/**
 * @defgroup CAN_MSG_DSC_STATIC_INIT_DEF
 * @brief CAN 报文描述符静态初始化宏
 * @param id CAN 报文 ID
 * @param idType CAN ID 类型
 * @param len CAN 载荷长度
 * @return CanMsgDsc
 * @{
 */
/**
 * @brief 初始化数据帧描述符
 */
#define CAN_DATA_MSG_DSC_INIT(_id, _idType, _len)                                   \
    (CanMsgDsc)                                                                   \
    {                                                                               \
        .id = _id, .idType = _idType, .msgType = CAN_MSG_TYPE_DATA, .dataLen = _len \
    }

/**
 * @brief 初始化远程帧描述符
 */
#define CAN_REMOTE_MSG_DSC_INIT(_id, _idType, _len)                                   \
    (CanMsgDsc)                                                                     \
    {                                                                                 \
        .id = _id, .idType = _idType, .msgType = CAN_MSG_TYPE_REMOTE, .dataLen = _len \
    }
/**
 * @defgroup CAN_MSG_DSC_STATIC_INIT_DEF
 * @}
 */

typedef struct CanErrCounter  CanErrCounter;
typedef struct CanErrCounter {
    // 软件层（逻辑/统计）计数
    size_t rxMsgCount;        // 接收报文总数
    size_t txMsgCount;        // 发送报文总数
    size_t rxSoftOverFlowCnt; // 软件 RX FIFO 溢出次数
    size_t rxFailCnt;         // 接收失败总次数
    size_t
        txSoftOverFlowCnt; // 软件 TX FIFO 溢出/覆写次数
    size_t txFailCnt;      // 发送失败总次数
    // 硬件层统计
    size_t txArbitrationFailCnt; // 发送仲裁失败次数
    size_t rxHwOverFlowCnt;      // 硬件 RX FIFO 溢出次数
    size_t txMailboxFullCnt;     // 硬件发送邮箱满次数
    // 协议层错误计数（CAN 标准）
    size_t rxErrCnt;           // REC：接收错误计数
    size_t txErrCnt;           // TEC：发送错误计数
    size_t bitDominantErrCnt;  /**
                                * @brief 位显性错误计数
                                * @note 节点发送隐性位(1)却在总线上读回显性位(0)时触发。
                                * @note 仲裁段和 ACK 段出现该现象可能是正常竞争，其他段通常代表异常。
                                * @note 常见原因：ID 冲突、物理层干扰、波特率不匹配、收发器或供电异常。
                                */
    size_t bitRecessiveErrCnt; /**
                                * @brief 位隐性错误计数
                                * @note 节点发送显性位(0)却在总线上读回隐性位(1)时触发。
                                * @note 常见于 Start/Arbitration/Control/Data 段，通常指向硬件连接问题。
                                * @note 常见原因：断路、收发器静默/待机、供电异常、短路、GPIO 复用错误。
                                */
    size_t formatErrCnt;       // 格式错误计数
    size_t crcErrCnt;          // CRC 错误计数
    size_t ackErrCnt;          // ACK 错误计数
    size_t stuffErrCnt;        // 位填充错误计数
} CanErrCounter;

/*
    一个 CAN 比特时间可划分为：SS 段、Prop 段、Phase1 段、Phase2 段。
    其中 SS 固定 1TQ，其余时段可配置。
    更多协议背景可参考：https://www.canfd.net/canbasic.html
*/
typedef struct CanBitTime {
    CanBS1 bs1;           // BS1（传播段 + 相位缓冲段 1）
    CanBS2 bs2;           // BS2（相位缓冲段 2）
    CanSjw syncJumpWidth; // 再同步跳转宽度 SJW
} CanBitTime;

/*
    工程经验：
    - 当 baudrate >= 500k 时，采样点建议约 0.8；
    - 当 0 < baudrate <= 500k 时，采样点建议约 0.75。
*/
typedef struct CanTimeCfg  CanTimeCfg;
typedef struct CanTimeCfg {
    CanBaudRate baudRate;
    uint16_t psc; // 预分频系数
    CanBitTime bitTimeCfg;
} CanTimeCfg;

/* CAN 过滤器请求参数 */
typedef struct CanFilterRequest  CanFilterRequest;
typedef struct CanFilterRequest {
    CanFilterMode workMode;
    CanFilterIdType idType;
    uint32_t id;
    uint32_t mask;
    void (*rxCallback)(Device* dev, void *param, CanFilterHandle handle, size_t msg_count);
    void *param;
} CanFilterRequest;

typedef struct CanFilterAllocArg  CanFilterAllocArg;
typedef struct CanFilterAllocArg {
    CanFilterRequest request;
    CanFilterHandle handle;
} CanFilterAllocArg;

typedef struct CanHwFilterCfg  CanHwFilterCfg;
typedef struct CanHwFilterCfg {
    size_t bank;
    CanFilterMode workMode;
    CanFilterIdType idType;
    uint32_t id;
    uint32_t mask;
} CanHwFilterCfg;

/* CAN 过滤器运行时对象 */
typedef struct CanFilter  CanFilter;
typedef struct CanFilter {
    CanFilterRequest request;              // 过滤器配置请求
    ListHead msgMatchedList; // 匹配该过滤器的报文链表
    uint32_t msgCount;               // 匹配报文累计计数
    uint8_t isActived;               // 激活标记
} CanFilter;

/**
 * @brief CAN 报文描述符
 * @note 用于描述 ID、ID 类型、报文类型、长度与时间戳等元数据。
 */
typedef struct CanMsgDsc  CanMsgDsc;
typedef struct CanMsgDsc {
    uint32_t id : 29;         // 报文 ID
    CanIdType idType : 1;   // ID 类型（标准/扩展）
    CanMsgType msgType : 1; // 报文类型（数据/远程）
    uint32_t reserved : 1;    // 保留位（补齐到 32bit）
    uint32_t dataLen;         // 载荷长度，见 @ref CanDlc
    uint32_t timeStamp;       // 接收或发送时间戳
} CanMsgDsc;

/* CAN 用户态报文对象 */
typedef struct CanUserMsg  CanUserMsg;
typedef struct CanUserMsg {
    CanMsgDsc dsc; /**
                      * @brief 报文描述符
                      * @ref CAN_MSG_DSC_STATIC_INIT_DEF
                      */
    CanFilterHandle filterHandle;    // 过滤器句柄/发送邮箱编号（未绑定时为无效值）
    /*
        userBuf 在不同层次的语义：
        1) 应用层 read：指向用户接收缓冲区，数据从 RX FIFO 拷贝到该地址；
        2) 应用层 write：指向用户发送缓冲区，数据从该地址拷贝到 TX FIFO；
        3) 框架层缓存：通常指向容器内部的 payload 区域；
        4) 硬件 ISR：可暂指向硬件寄存器/驱动提供的临时接收区。
    */
    uint8_t *userBuf;
} CanUserMsg;

/* CAN 硬件报文对象（驱动核心/BSP 内部使用） */
typedef struct CanHwMsg  CanHwMsg;
typedef struct CanHwMsg {
    CanMsgDsc dsc;
    int16_t hwFilterBank;
    int8_t hwTxMailbox;
    uint8_t *data;
} CanHwMsg;

/* CAN 报文链表节点 */
typedef struct CanMsgList  CanMsgList;
typedef struct CanMsgList {
    CanUserMsg userMsg;             // 用户态报文对象
    ListHead fifoListNode;    // FIFO 链表节点（used/free）
    ListHead matchedListNode; // 过滤匹配链表节点
    void *owner;                      // 所属对象（过滤器或邮箱）
    uint8_t container[0];             // 柔性负载区（具体类型映射到此处）
} CanMsgList;

/* CAN 接收/发送 FIFO 容器 */
typedef struct CanMsgFifo  CanMsgFifo;
typedef struct CanMsgFifo {
    void *msgBuffer;           // 报文缓存区首地址
    ListHead usedList; // 已使用链表
    ListHead freeList; // 空闲链表
    uint32_t freeCount;        // 空闲节点数量
    uint8_t isOverwrite;       // 覆写策略（0 不覆写，1 覆写）
} CanMsgFifo;

typedef struct CanMailbox  CanMailbox;
typedef struct CanMailbox {
    uint8_t isBusy;        // 邮箱忙标记
    CanMsgList* pMsgList; // 当前邮箱绑定的报文
    uint32_t bank;         // 邮箱编号
    ListHead list; // 邮箱链表节点
} CanMailbox;

/* CAN 功能配置选项 */
// TODO: 后续按平台能力细化功能位语义
typedef struct CanFunctionalCfg  CanFunctionalCfg;
typedef struct CanFunctionalCfg {
    uint16_t autoRetransmit : 1;    // 自动重传使能
    uint16_t txPriority : 1;        // 发送优先级策略（ID/请求顺序）
    uint16_t rxFifoLockMode : 1;    // RX FIFO 锁定模式
    uint16_t timeTriggeredMode : 1; // 时间触发通信模式
    uint16_t autoWakeUp : 1;        // 自动唤醒使能
    uint16_t autoBusOff : 1;        // 自动 BUS-OFF 管理使能
    uint16_t isRxOverwrite : 1;     // RX FIFO 覆写策略
    uint16_t isTxOverwrite : 1;     // TX FIFO 覆写策略
    uint16_t rsv : 9;               // 保留位
} CanFunctionalCfg;

typedef struct CanCfg  CanCfg;
typedef struct CanCfg {
    CanWorkMode workMode;           // 工作模式
    CanTimeCfg normalTimeCfg;       // 位时序配置
    size_t filterNum;                 // 过滤器数量
    size_t mailboxNum;                // 发送邮箱数量
    size_t rxMsgListBufSize;          // RX FIFO 报文缓存数量
    size_t txMsgListBufSize;          // TX FIFO 报文缓存数量
    uint32_t statusCheckTimeout;      // 状态轮询周期（毫秒）
    CanFunctionalCfg functionalCfg; // 功能开关配置
} CanCfg;

// CAN 默认配置
#define CAN_DEFUALT_CFG                                                                                                \
    (CanCfg)                                                                                                         \
    {                                                                                                                  \
        .workMode = CAN_WORK_NORMAL, .rxMsgListBufSize = 32, .txMsgListBufSize = 32, .mailboxNum = 3, .filterNum = 28, \
        .functionalCfg =                                                                                               \
            {                                                                                                          \
                .autoRetransmit    = 0,                                                                                \
                .autoBusOff        = 0,                                                                                \
                .autoWakeUp        = 0,                                                                                \
                .rxFifoLockMode    = 0,                                                                                \
                .timeTriggeredMode = 0,                                                                                \
                .txPriority        = 0,                                                                                \
                .isRxOverwrite     = 0,                                                                                \
                .isTxOverwrite     = 0,                                                                                \
                .rsv               = 0,                                                                                \
            },                                                                                                         \
        .normalTimeCfg =                                                                                               \
            {                                                                                                          \
                .baudRate = CAN_BAUD_1M,                                                                               \
            },                                                                                                         \
        .statusCheckTimeout = 10,                                                                                      \
    }


typedef struct CanHwCapability  CanHwCapability;
typedef struct CanHwCapability {
    uint8_t hwBankCount;
    const uint8_t *hwBankList;
} CanHwCapability;

typedef struct CanFilterResMgr  CanFilterResMgr;
typedef struct CanFilterResMgr {
    uint16_t slotCount;             // 过滤器槽位总数
    uint16_t maxHwBank;             // 最大硬件 bank 数
    AwBitmapAtomic slotUsedMap;   // 槽位使用位图
    AwBitmapAtomic hwBankUsedMap; // bank 使用位图
    uint8_t *hwBankList;            // 可用 bank 列表
    int16_t *slotToHwBank;          // 槽位到 bank 的映射表
} CanFilterResMgr;                // 过滤器资源管理器
typedef struct HalCanHandler  HalCanHandler;

typedef struct CanAdapterInterface  CanAdapterInterface;
typedef struct CanAdapterInterface {
    /**
     * @brief 分配 FIFO 报文缓存并挂接到空闲链表
     * @param free_list_head 目标空闲链表头
     * @param msg_num 报文节点数量
     * @return 分配得到的缓存首地址，失败返回 NULL
     */
    void *(*msgbufferAlloc)(ListHead *free_list_head, uint32_t msg_num);
} CanAdapterInterface;

typedef struct CanHwInterface  CanHwInterface;
typedef struct CanHwInterface {
    /**
     * @brief 配置 CAN 硬件控制器
     * @param can CAN 设备句柄
     * @param cfg CAN 配置参数
     * @return 执行结果：`OM_OK` 成功，`AWLF_ERROR_PARAM` 参数非法
     */
    OmRet (*configure)(HalCanHandler* can, CanCfg* cfg);
    /**
     * @brief 执行 CAN 设备控制命令
     * @param can CAN 设备句柄
     * @param cmd 控制命令，见 @ref CAN_CMD_DEF
     * @param arg 命令参数
     * @retval OM_OK 执行成功
     * @retval AWLF_ERROR_PARAM 参数非法或命令不支持
     */
    OmRet (*control)(HalCanHandler* can, uint32_t cmd, void *arg);
    /**
     * @brief 发送一帧报文到硬件邮箱
     * @param can CAN 设备句柄
     * @param msg 硬件报文对象
     * @return 执行结果
     * @retval OM_OK 发送成功
     * @retval AWLF_ERROR_PARAM 参数非法
     * @retval AWLF_ERR_OVERFLOW 硬件邮箱不可用
     * @note 成功后应回填 `msg->hwTxMailbox` 为实际使用的邮箱编号
     */
    OmRet (*sendMsgMailbox)(HalCanHandler* can, CanHwMsg* msg);
    /**
     * @brief 从指定 RX FIFO 读取一帧报文到 `msg->userBuf`
     * @param can CAN 设备句柄
     * @param msg 用户报文对象
     * @param rxfifo_bank RX FIFO 编号
     * @return 执行结果
     * @retval OM_OK 读取成功
     * @retval AWLF_ERROR_PARAM 参数非法
     * @retval AWLF_ERROR_EMPTY FIFO 为空
     * @retval AWLF_ERR_OVERFLOW FIFO 溢出
     *
     * @note `rxfifo_bank` 由 BSP 中断上下文传入。
     * @note 当框架层 RX FIFO 为空且未启用覆写时，`msg` 可能被置为 NULL，
     *       BSP 应读取并丢弃当前硬件帧以清除中断源。
     *
     */
    OmRet (*recvMsg)(HalCanHandler* can, CanHwMsg* msg, int32_t rxfifo_bank);
} CanHwInterface;

/**
 * @brief CAN 状态管理对象
 */
typedef struct CanStatusManager  CanStatusManager;
typedef struct CanStatusManager {
    OsalTimer* statusTimer;         // 状态轮询定时器
    CanNodeErrStatus nodeErrStatus; // 节点错误状态
    CanErrCounter errCounter;
} CanStatusManager;

/* CAN 接收链路处理对象 */
typedef struct CanRxHandler  CanRxHandler;
typedef struct CanRxHandler {
    CanFilter* filterTable; // 过滤器表
    CanMsgFifo rxFifo;     // 接收 FIFO
} CanRxHandler;

/* CAN 发送链路处理对象 */
typedef struct CanTxHandler  CanTxHandler;
typedef struct CanTxHandler {
    CanMailbox* pMailboxes;      // 邮箱数组
    CanMsgFifo txFifo;          // 发送 FIFO
    ListHead mailboxList; // 可用邮箱链表
} CanTxHandler;

/* CAN 设备句柄 */
// TODO: 当前实现以稳定可用优先，后续可按单生产者多消费者模型进一步优化。
typedef struct HalCanHandler {
    Device parent;                        // 父设备对象
    CanRxHandler rxHandler;               // 接收链路
    CanTxHandler txHandler;               // 发送链路
    CanStatusManager statusManager;       // 状态与错误统计
    CanFilterResMgr filterResMgr;         // 过滤器资源管理
    CanHwInterface* hwInterface;           // 硬件抽象接口
    CanAdapterInterface* adapterInterface; // 适配层接口（经典 CAN / CAN FD）
    CanCfg cfg;                           // 生效配置
} HalCanHandler;

#define IS_CAN_FILTER_INVALID(pCan, handle)  ((handle) >= (pCan)->filterResMgr.slotCount)
#define IS_CAN_MAILBOX_INVALID(pCan, bank) ((bank) < 0 || (bank) >= (pCan)->cfg.mailboxNum)

/**
 * @brief 获取经典 CAN 适配器接口
 * @return 经典 CAN 适配器接口指针
 */
CanAdapterInterface* hal_can_get_classic_adapter_interface(void);

/**
 * @brief 获取 CAN FD 适配器接口
 * @return CAN FD 适配器接口指针
 */
CanAdapterInterface* hal_can_get_canfd_adapter_interface(void);

/**
 * @brief 注册 CAN 设备（兼容经典 CAN 与 CAN FD）
 * @param can CAN 设备句柄
 * @param name 设备名称
 * @param handle 底层硬件句柄
 * @param regparams 注册参数，见 @ref CAN_REG_DEF
 * @return 注册结果
 * @retval OM_OK 成功
 * @retval AWLF_ERROR_PARAM 参数错误
 * @retval AWLF_ERR_CONFLICT 设备名冲突
 */
OmRet hal_can_register(HalCanHandler* can, char *name, void *handle, uint32_t regparams);

/**
 * @brief CAN 中断统一入口
 * @param can CAN 设备句柄
 * @param event 中断事件类型
 * @param param 中断附加参数
 * @note `param` 语义随事件类型变化：
 * @note 1) `CAN_ISR_EVENT_INT_RX_DONE`：`param` 为 RX FIFO 编号；
 * @note 2) `CAN_ISR_EVENT_INT_TX_DONE`：`param` 为发送邮箱编号；
 * @note 3) 错误事件：`param` 为错误码或状态码。
 */
void hal_can_isr(HalCanHandler* can, CanIsrEvent event, size_t param);

/**
 * @brief CAN 错误中断处理
 * @param can CAN 设备句柄
 * @param err_event 错误事件位图
 */
void can_error_isr(HalCanHandler* can, uint32_t err_event, size_t param);

#ifdef __cplusplus
}
#endif

#endif
