#ifndef __HAL_SERIAL_H__
#define __HAL_SERIAL_H__

#include "core/data_struct/ringbuffer.h"
#include "drivers/model/device.h"
#include "sync/completion.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SerialCfg  SerialCfg;
typedef struct SerialInterface  SerialInterface;
typedef struct HalSerial  HalSerial;
typedef struct SerialFifo  SerialFifo;
typedef struct SerialPriv  SerialPriv;

/* 数据位，由一些辅助运算的 magic number 构成*/
typedef enum DataBits {
    DATA_BITS_5 = 0x4000FU, // 5位数据位
    DATA_BITS_6 = 0x5001FU, // 6位数据位
    DATA_BITS_7 = 0x6003FU, // 7位数据位
    DATA_BITS_8 = 0x7007FU, // 8位数据位
    DATA_BITS_9 = 0x800FFU, // 9位数据位
} DataBits;

/* 停止位 */
typedef enum StopBits {
    STOP_BITS_0_5 = 0x00U,
    STOP_BITS_1,
    STOP_BITS_1_5,
    STOP_BITS_2,
} StopBits;

/* 校验位 */
typedef enum ParityE {
    PARITY_NONE = 0x00U,
    PARITY_ODD,
    PARITY_EVEN,
} Parity;

/* 硬件流控 */
typedef enum {
    FLOW_CTRL_NONE    = 0x00U,
    FLOW_CTRL_RTS     = 0x01U << 0,
    FLOW_CTRL_CTS     = 0x01U << 1,
    FLOW_CTRL_CTS_RTS = FLOW_CTRL_RTS | FLOW_CTRL_CTS,
} FlowCtrl;

typedef enum {
    OVERSAMPLING_16 = 0x00U,
    OVERSAMPLING_8  = 0x01U,
} Oversampling;

/* 串口中断事件 */
typedef enum {
    SERIAL_EVENT_INT_TXDONE = 0x01U, // 中断发送
    SERIAL_EVENT_INT_RXDONE,         // 中断接收
    SERIAL_EVENT_DMA_TXDONE,         // DMA发送完成
    SERIAL_EVENT_DMA_RXDONE,         // DMA接收完成
    SERIAL_EVENT_ERR_OCCUR,          // 错误中断
} SerialEvent;

typedef enum {
    ERR_SERIAL_RXFIFO_OVERFLOW = 1U,
    ERR_SERIAL_TXFIFO_OVERFLOW,
    ERR_SERIAL_INVALID_MEM, // 内存非法
    ERR_SERIAL_RX_TIMEOUT,  // 接收超时
    ERR_SERIAL_TX_TIMEOUT,  // 发送超时
    // TODO: ... 待完善，硬件错误类型比如ORE、TE之类的
} SerialErrCode;

/**
 * @defgroup SERIAL_DEVICE_PARAM_DEF
 * @brief 串口设备可选参数定义
 * @{
 */
/**
 * @defgroup SERIAL_OPEN_MODE
 * @brief 串口打开方式定义
 * @{
 */
#define SERIAL_O_BLCK_RX  (0x01U << 0)
#define SERIAL_O_BLCK_TX  (0x01U << 1)
#define SERIAL_O_NBLCK_RX (0x01U << 2)
#define SERIAL_O_NBLCK_TX (0x01U << 3)
/**
 * @defgroup SERIAL_OPEN_MODE
 * @}
 */

/**
 * @defgroup SERIAL_REG_PARAM
 * @brief 串口可选注册参数定义
 * @{
 */
#define SERIAL_REG_INT_RX  DEVICE_REG_INT_RX
#define SERIAL_REG_INT_TX  DEVICE_REG_INT_TX
#define SERIAL_REG_DMA_RX  DEVICE_REG_DMA_RX
#define SERIAL_REG_DMA_TX  DEVICE_REG_DMA_TX
#define SERIAL_REG_POLL_TX DEVICE_REG_POLL_TX
#define SERIAL_REG_POLL_RX DEVICE_REG_POLL_RX
/**
 * @defgroup SERIAL_REG_PARAM
 * @}
 */
/**
 * @defgroup SERIAL_CMD_PARAM
 * @brief 串口可选命令参数定义
 * @{
 */
#define SERIAL_CMD_SET_IOTPYE (DEVICE_CMD_SET_IOTYPE) // 指的是设置中断、DMA的收发，移植必须要实现的命令
#define SERIAL_CMD_CLR_IOTPYE (0x01U) // 清除中断、DMA的收发
#define SERIAL_CMD_SET_CFG    (DEVICE_CMD_CFG) // 配置串口
#define SERIAL_CMD_CLOSE      (0x03U) // 关闭串口
#define SERIAL_CMD_FLUSH      (0x04U) // 清空缓冲区
#define SERIAL_CMD_SUSPEND    (0x05U) // 挂起串口
#define SERIAL_CMD_RESUME     (0x06U) // 恢复串口
/**
 * @defgroup SERIAL_CMD_PARAM
 * @}
 */
/**
 * @defgroup SERIAL_DEVICE_PARAM_DEF
 * @}
 */

typedef enum {
    SERIAL_FIFO_IDLE = 0U, // 空闲
    SERIAL_FIFO_BUSY,      // 忙
} SerialFifoStatus;

/**
 * @brief 串口阻塞等待唤醒原因
 */
typedef enum {
    SERIAL_WAIT_WAKE_NONE = 0U, // 正常等待中
    SERIAL_WAIT_WAKE_DONE,      // 正常完成
    SERIAL_WAIT_WAKE_ABORT,     // 被控制命令（如 suspend）中止
    SERIAL_WAIT_WAKE_ERROR,     // 被错误路径中止
} SerialWaitWakeReason;

typedef struct SerialCfg {
    uint32_t baudrate;               // 波特率
    DataBits dataBits;             // 数据位
    StopBits stopBits : 2;         // 停止位
    Parity parity : 2;             // 校验位
    FlowCtrl flowCtrl : 2;         // 硬件流控
    Oversampling overSampling : 1; // 采样率
    uint32_t reserved : 25;          // 保留位，若是更改了本结构体，请务必记得更新该值
    uint32_t txBufSize;              // 发送缓冲区大小
    uint32_t rxBufSize;              // 接收缓冲区大小
} SerialCfg;

#define SERIAL_MIN_TX_BUFSZ (64U)
#define SERIAL_MIN_RX_BUFSZ (64U)

/**
 * @brief 阻塞收发默认等待超时（ms）
 * @note 该超时用于保证阻塞路径可中止，避免永久等待。
 */
#define SERIAL_BLOCK_TX_WAIT_TIMEOUT_MS (120000U)
#define SERIAL_BLOCK_RX_WAIT_TIMEOUT_MS (120000U)

#define SERIAL_RXFLUSH      (0U)
#define SERIAL_TXFLUSH      (1U)
#define SERIAL_TXRXFLUSH    (2U)

/* 阻塞读写等待策略（可按项目覆写） */
#ifndef SERIAL_BLOCK_WAIT_SLICE_MS
#define SERIAL_BLOCK_WAIT_SLICE_MS (20U)
#endif

#ifndef SERIAL_BLOCK_TX_TIMEOUT_MS
#define SERIAL_BLOCK_TX_TIMEOUT_MS (1000U)
#endif

#ifndef SERIAL_BLOCK_RX_TIMEOUT_MS
#define SERIAL_BLOCK_RX_TIMEOUT_MS (1000U)
#endif

/* 默认配置 */
#define SERIAL_DEFAULT_CFG                                                                                                        \
    (SerialCfg)                                                                                                                 \
    {                                                                                                                             \
        .baudrate = 115200U, .dataBits = DATA_BITS_8, .stopBits = STOP_BITS_1, .parity = PARITY_NONE, .flowCtrl = FLOW_CTRL_NONE, \
        .overSampling = OVERSAMPLING_16, .txBufSize = SERIAL_MIN_TX_BUFSZ, .rxBufSize = SERIAL_MIN_RX_BUFSZ                       \
    }

/**
 * @brief 串口接口函数表
 * @note 该结构体定义了串口的基本操作函数，包括配置、发送、接收、控制和传输。 —— 语义
 */
typedef struct SerialInterface {
    OmRet (*configure)(HalSerial* serial, SerialCfg* cfg);
    OmRet (*putByte)(HalSerial* serial, uint8_t data);     // 非阻塞发送一个字节，返回是否成功，成功返回OM_OK，失败返回OM_ERR
    OmRet (*getByte)(HalSerial* serial, uint8_t *buf);     // 非阻塞接收一个字节，返回是否成功，成功返回OM_OK，失败返回OM_ERR
    OmRet (*control)(HalSerial* serial, uint32_t cmd, void *arg);
    size_t (*transmit)(HalSerial* serial, const uint8_t *data, size_t length);
} SerialInterface;

typedef struct SerialFifo {
    Ringbuf rb;                       // 环形缓冲区
    Completion cpt;                   // 完成信号
    volatile int32_t loadSize;          // 串口加载的总数据长度
    volatile SerialFifoStatus status; // fifo状态
    volatile SerialWaitWakeReason waitReason; // 阻塞等待唤醒原因
} SerialFifo;

// TODO: 引入超时机制
typedef struct SerialPriv {
    SerialFifo* txFifo;
    SerialFifo* rxFifo;
} SerialPriv;

// 串口实例 结构体
typedef struct HalSerial {
    Device parent;                // 设备父类
    SerialInterface* interface;    // 串口接口
    SerialCfg cfg;        // 配置信息
    SerialPriv priv;    // 私有属性
} HalSerial;

OmRet serial_register(HalSerial* serial, char *name, void *handle, uint32_t regparams);
OmRet serial_hw_isr(HalSerial* serial, SerialEvent event, void *arg, size_t arg_size);

static inline SerialFifo* serial_get_rxfifo(HalSerial* serial)
{
    return serial->priv.rxFifo;
}

static inline SerialFifo* serial_get_txfifo(HalSerial* serial)
{
    return serial->priv.txFifo;
}

/**
 * @brief 获取接收mask - 用于特殊数据位
 *
 * @param serial 串口设备
 * @return uint32_t 接收mask
 * @note 这个函数将被底层驱动调用获取mask，该值用于统一不同数据位之间接收数据的差异。一般用不着
 */
static inline uint32_t serial_get_rxmask(HalSerial* serial)
{
    return (((uint32_t)(serial->cfg.parity == PARITY_NONE) << ((serial->cfg.dataBits >> 16))) | serial->cfg.dataBits) & 0x1FF;
}

#ifdef __cplusplus
}
#endif

#endif
