#ifndef __OM_DEF_H__
#define __OM_DEF_H__

#include "port/om_port_compiler.h"
#include <stddef.h>
#include <stdint.h>

#ifndef FRAMEWORK_VERSION
#define FRAMEWORK_VERSION "v0.0.0+gunknown"
#endif

/* compiler specific */
#define OM_ATTRIBUTE(x) __port_attribute(x)
#define OM_USED __port_used
#define OM_WEAK __port_weak
#define OM_SECTION(x) __port_section(x)
#define OM_ALIGN(n) __port_align(n)
#define OM_PACKED __port_packed

#define DBG_PARAM_DEF(type, name) static volatile type name

#define OM_NULL ((void*)0)

typedef enum
{
    OM_FALSE = 0U,
    OM_TRUE = 1U
} OmBool;

typedef enum
{
    OM_DISABLE = 0U,
    OM_ENABLE = !OM_DISABLE
} OmAction;

/* 返回值定义(标准错误码) */
typedef enum
{
    OM_OK = 0U,           // 成功
    OM_ERROR,             // 通用错误
    OM_ERR_CONFLICT,      // 参数冲突
    OM_ERR_OVERFLOW,      // 溢出错误
    OM_ERROR_TIMEOUT,     // 超时错误
    OM_ERROR_DMA,         // DMA错误
    OM_ERROR_MEMORY,      // 内存错误
    OM_ERROR_PARAM,       // 参数错误
    OM_ERROR_NULL,        // 参数为NULL
    OM_ERROR_BUSY,        // 忙错误
    OM_ERROR_EMPTY,       // 空转错误
    OM_ERROR_NOT_SUPPORT, // 不支持的操作
} OmRet;

typedef enum
{
    OM_LOG_LEVEL_DEBUG = 0U, // 调试信息
    OM_LOG_LEVEL_INFO,       // 信息
    OM_LOG_LEVEL_WARN,       // 警告
    OM_LOG_LEVEL_ERROR,      // 错误
    OM_LOG_LEVEL_FATAL,      // 致命错误
    OM_LOG_LEVEL_MAX,        // 日志级别最大值
} OmLogLevel;

#define OM_WAIT_FOREVER 0xffffffffU

#define DEV_STATUS_CLOSED (0x00U)       // 设备关闭
#define DEV_STATUS_OK (0x01U << 0)      // 设备正常
#define DEV_STATUS_BUSY_RX (0x01U << 1) // 设备接收中
#define DEV_STATUS_BUSY_TX (0x01U << 2) // 设备发送中
#define DEV_STATUS_BUSY (DEV_STATUS_BUSY_RX | DEV_STATUS_BUSY_TX)
#define DEV_STATUS_TIMEOUT (0x01U << 3) // 设备超时
#define DEV_STATUS_ERR (0x01U << 4)     // 设备错误
#define DEV_STATUS_INITED (0x01U << 5)  // 设备初始化完成
#define DEV_STATUS_OPENED (0x01U << 6)  // 设备打开
#define DEV_STATUS_SUSPEND (0x01U << 7) // 设备挂起

#define DEVICE_CMD_CFG (0x01U)        // 配置设备
#define DEVICE_CMD_SUSPEND (0x02U)    // 挂起设备
#define DEVICE_CMD_RESUME (0x03U)     // 恢复设备
#define DEVICE_CMD_SET_IOTYPE (0x04U) // 设置IO方式，DMA、中断、轮询等

/*
    注册参数
    位域 - 含义
    [0:0] : 1bit，指示该设备是否为独占设备，即是否能够被重复open
    [2:1] : 2bits，指示该设备的读写权限，  00：保留    01：只读；   10：只写；  11：读写
    [4:3] : 2bits，指示该设备的底层写方式，00：轮询；   01：中断；  10：DMA；   11：保留
    [6:5] : 2bits，指示该设备的底层读方式，00：轮询；   01：中断；  10：DMA；   11：保留
    [31:7] : 24bits，保留位，可由用户自定义
*/
// [0:0]
#define DEVICE_REG_STANDALONG (0x01U) // 表示同一时间只能被一个任务使用
// [2:1]
#define DEVICE_REG_RDONLY (0x01U << 1)                          // 只读权限
#define DEVICE_REG_WRONLY (0x02U << 1)                          // 只写权限
#define DEVICE_REG_RDWR (DEVICE_REG_RDONLY | DEVICE_REG_WRONLY) // 读写权限
#define DEVICE_REG_RDWR_MASK (0x03U << 1)                       // 读写权限掩码
// [4:3]
#define DEVICE_REG_POLL_TX (0x00U)          // 轮询写方式
#define DEVICE_REG_INT_TX (0x01U << 3)      // 中断写方式
#define DEVICE_REG_DMA_TX (0x02U << 3)      // DMA写方式
#define DEVICE_REG_TXTYPE_MASK (0x03U << 3) // 写方式掩码
// [6:5]
#define DEVICE_REG_POLL_RX (0x00U)          // 轮询读方式
#define DEVICE_REG_INT_RX (0x01U << 5)      // 中断读方式
#define DEVICE_REG_DMA_RX (0x02U << 5)      // DMA读方式
#define DEVICE_REG_RXTYPE_MASK (0x03U << 5) // 读方式掩码
// 注册参数掩码
#define DEVICE_REG_IOTYPE_MASK (DEVICE_REG_TXTYPE_MASK | DEVICE_REG_RXTYPE_MASK)
#define DEVICE_REG_MASK (DEVICE_REG_STANDALONG | DEVICE_REG_RDWR_MASK | DEVICE_REG_TXTYPE_MASK | DEVICE_REG_RXTYPE_MASK)

/*
    设备打开参数
    位域 - 含义
    [6:0]: 7bits，指示该设备的打开参数，与注册参数的含义相同
    [7:7]: 1bit，指示该设备的写模式，
*/
//
#define DEVICE_O_RDONLY (DEVICE_REG_RDONLY)
#define DEVICE_O_WRONLY (DEVICE_REG_WRONLY)
#define DEVICE_O_RDWR (DEVICE_REG_RDWR)
#define DEVICE_O_RDWR_MASK (DEVICE_REG_RDWR_MASK)

#define DEVICE_O_POLL_TX (DEVICE_REG_POLL_TX)
#define DEVICE_O_INT_TX (DEVICE_REG_INT_TX)
#define DEVICE_O_DMA_TX (DEVICE_REG_DMA_TX)
#define DEVICE_O_TXTYPE_MASK (DEVICE_REG_TXTYPE_MASK)

#define DEVICE_O_POLL_RX (DEVICE_REG_POLL_RX)
#define DEVICE_O_INT_RX (DEVICE_REG_INT_RX)
#define DEVICE_O_DMA_RX (DEVICE_REG_DMA_RX)
#define DEVICE_O_RXTYPE_MASK (DEVICE_REG_RXTYPE_MASK)

#define DEVICE_O_IOTYPE_MASK (DEVICE_REG_IOTYPE_MASK)
#define DEVICE_O_MASK (DEVICE_REG_MASK)

#endif // __OM_DEF_H__
