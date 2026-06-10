/**
 * @file   pal_spi_dev.h
 * @brief  SPI 外设抽象层：SpiBus（总线管理）+ HalSpiDevice（从设备 / Device 模型）
 *
 * 架构：总线与设备分离
 *  - SpiBus: 基础设施，管理互斥/配置缓存/DoubleBuf/Completion/Workqueue，不注册为 Device
 *  - HalSpiDevice: 标准 Device，嵌入 Device 父类，注册到全局设备表
 *  - SpiInterface: BSP 硬件操作函数表（平台无关）
 *
 * 设计参考：Linux spi_summary / Zephyr spi.h / ESP-IDF spi_master.h
 */

#ifndef __HAL_SPI_DEV_H__
#define __HAL_SPI_DEV_H__

#include "async/workqueue.h"
#include "core/om_def.h"
#include "data_struct/double_buf.h"
#include "drivers/model/device.h"
#include "drivers/peripheral/gpio/pal_gpio_dev.h"
#include "osal/osal_mutex.h"
#include "sync/completion.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 前向声明
 *===========================================================================*/

typedef struct SpiBus           SpiBus;
typedef struct SpiInterface     SpiInterface;
typedef struct SpiDeviceCfg     SpiDeviceCfg;
typedef struct HalSpiDevice     HalSpiDevice;
typedef struct SpiAsyncRequest  SpiAsyncRequest;

/*===========================================================================
 * SPI 模式和常量
 *===========================================================================*/

#define SPI_MODE_0              (0U)    /* CPOL=0, CPHA=0 */
#define SPI_MODE_1              (1U)    /* CPOL=0, CPHA=1 */
#define SPI_MODE_2              (2U)    /* CPOL=1, CPHA=0 */
#define SPI_MODE_3              (3U)    /* CPOL=1, CPHA=1 */

#define SPI_MSB_FIRST           (0U)
#define SPI_LSB_FIRST           (1U)

#define SPI_DATA_WIDTH_8        (8U)
#define SPI_DATA_WIDTH_16       (16U)

/*===========================================================================
 * SPI 设备控制命令
 *===========================================================================*/

#define SPI_CMD_SET_CFG         (DEVICE_CMD_CFG)         /* 0x01  运行时更新从设备配置 */
#define SPI_CMD_GET_CFG         (0x10U)                  /* 读取当前从设备配置 */
#define SPI_CMD_SUSPEND         (DEVICE_CMD_SUSPEND)     /* 0x02  挂起从设备 */
#define SPI_CMD_RESUME          (DEVICE_CMD_RESUME)      /* 0x03  恢复从设备 */
#define SPI_CMD_ABORT           (0x11U)                  /* 中止当前异步传输 */

/*===========================================================================
 * SPI 错误码
 *===========================================================================*/

typedef enum {
    ERR_SPI_TRANSFER_TIMEOUT = 1U,  /* 传输超时 */
    ERR_SPI_BUS_HW_ERROR,           /* MODF / OVR / CRC */
    ERR_SPI_CS_CONFLICT,            /* 事务内调用自动 CS API */
    ERR_SPI_BUSY,                   /* 硬件传输进行中 */
    ERR_SPI_DEV_SUSPENDED,          /* 设备已挂起 */
} SpiErrCode;

/*===========================================================================
 * SpiInterface —— BSP 硬件操作函数表（平台无关）
 *===========================================================================*/

typedef struct SpiInterface {

    /**
     * @brief 配置 SPI 控制器寄存器（mode / 波特率 / 数据宽度 / 位序）
     * @retval OM_OK           配置成功
     * @retval OM_ERROR_PARAM  参数非法
     */
    OmRet (*configure)(SpiBus *bus, const SpiDeviceCfg *cfg);

    /**
     * @brief 发起 SPI 全双工传输（非阻塞）
     * @param tx   发送缓冲区（NULL = 发 dummy 0xFF）
     * @param rx   接收缓冲区（NULL = 丢弃 MISO）
     * @param len  传输字节数
     * @retval OM_OK           启动成功
     * @retval OM_ERROR_PARAM  参数非法
     * @note  BSP 内部根据硬件能力选择 poll / IRQ / DMA 路径
     * @note  轮询模式内部死等完成后调 hal_spi_isr()
     * @note  DMA / INT 模式启动后立即返回，ISR 回调 hal_spi_isr()
     * @note  DMA 对框架完全透明，通道/IRQ/句柄全在 BSP private
     */
    OmRet (*transfer)(SpiBus *bus, const uint8_t *tx, uint8_t *rx, size_t len);

    /**
     * @brief 通用控制接口（SPI_CMD_ABORT 等）
     */
    OmRet (*control)(SpiBus *bus, uint32_t cmd, void *arg);

    /**
     * @brief 硬件 CS 电平控制（仅硬件 CS 模式）
     * @param csId   CS 线编号，来自 cfg.csSpec.offset
     * @param level  0 = 选中(assert), 1 = 释放(deassert)
     * @note  仅当 dev->cfg.csSpec.controller == NULL 时走此路径
     * @note  GPIO CS 由框架直接调用 gpio_pin_write()，不经过此接口
     */
    OmRet (*cs_control)(SpiBus *bus, uint8_t csId, uint8_t level);

} SpiInterface;

/*===========================================================================
 * SpiDeviceCfg —— 从设备静态配置（平台无关）
 *===========================================================================*/

typedef struct SpiDeviceCfg {
    GpioPinSpec     csSpec;             /* CS 引脚描述符（controller==NULL 表示硬件 CS） */
    uint8_t         mode;               /* SPI_MODE_0..3 */
    uint32_t        maxHz;              /* 最大 SCLK 频率 (Hz) */
    uint8_t         dataWidth;          /* SPI_DATA_WIDTH_8 / SPI_DATA_WIDTH_16 */
    uint8_t         bitOrder;           /* SPI_MSB_FIRST / SPI_LSB_FIRST */
    uint32_t        transferOverheadMs; /* 传输超时附加余量（ms），补偿 BSP 启动/ISR/调度延迟 */
} SpiDeviceCfg;

/*===========================================================================
 * SpiAsyncRequest —— 异步传输请求（调用者分配，携带 per-request 回调）
 *===========================================================================*/

typedef struct SpiAsyncRequest {
    HalSpiDevice    *dev;           /* 所属设备（框架填充） */
    const uint8_t   *tx;            /* 发送缓冲区（调用者填充） */
    uint8_t         *rx;            /* 接收缓冲区（调用者填充） */
    size_t           len;           /* 传输长度（调用者填充） */
    size_t           transferred;   /* 实际传输字节数（ISR→bus→worker 填入） */
    OmRet            status;        /* 传输结果（ISR→bus→worker 填入） */
    void           (*asyncCb)(void *param, struct SpiAsyncRequest *req);
    void            *asyncParam;    /* 回调参数 */
    uint32_t         epoch;         /* 入队时的 bus->asyncEpoch 快照（防悬垂复用） */
    Work             work;          /* workqueue 调度单元 */
} SpiAsyncRequest;

/*===========================================================================
 * SpiXfer —— 同步传输描述符（栈上临时构造）
 *===========================================================================*/

typedef struct SpiXfer {
    const uint8_t *txBuf;       /* 发送缓冲区（NULL = 发 dummy） */
    uint8_t       *rxBuf;       /* 接收缓冲区（NULL = 丢弃）     */
    size_t         txLen;       /* 发送字节数                    */
    size_t         rxLen;       /* 接收字节数                    */
    size_t         transferred; /* 实际传输总字节数（框架填充）   */
} SpiXfer;

/*===========================================================================
 * SpiBus —— 总线控制器（不注册为 Device）
 *===========================================================================*/

typedef struct SpiBus {
    /* ---- BSP 接口 ---- */
    void            *hwPrivate;        /* BSP 私有数据（不透明指针） */
    SpiInterface    *interface;        /* 硬件操作函数表 */

    /* ---- 并发控制 ---- */
    OsalMutex       *lock;             /* 总线互斥锁（非递归） */

    /* ---- 配置缓存 ---- */
    HalSpiDevice    *cachedDevice;     /* 当前配置对应的设备指针 */

    /* ---- 双缓冲（TX-only，dbuf_page_size > 0 时启用） ---- */
    DoubleBuf        txDbuf;

    /* ---- 同步传输完成信号 ---- */
    Completion       transferDone;
    size_t           lastTransferred;  /* ISR 写入，同步路径读取 */
    OmRet            lastStatus;       /* ISR 写入的传输结果 */

    /* ---- 硬件传输状态 ---- */
    volatile uint8_t busy;             /* 硬件传输进行中（ISR 读 / 线程写，需 volatile） */

    /* ---- Peek 预填充追踪 ---- */
    SpiAsyncRequest *prefillTarget;    /* 预填充目标请求（NULL=无预填） */
    uint32_t         prefillEpoch;     /* 预填充时的请求 epoch（防悬垂复用） */

    /* ---- 异步调度（框架自建 per-bus workqueue） ---- */
    Workqueue        asyncWq;
    uint32_t         asyncEpoch;       /* 异步请求递增序号（每个 transfer_async +1） */

    /* ---- 总线级 suspend ref-counting ---- */
    uint8_t          suspendedCount;   /* 已挂起的从设备数量 */
    uint8_t          deviceCount;      /* 总线上挂载的设备总数 */
} SpiBus;

/*===========================================================================
 * HalSpiDevice —— 从设备（注册为 Device）
 *===========================================================================*/

typedef struct HalSpiDevice {
    Device            parent;          /* 设备父类（标准 Device 模型） */
    SpiBus           *bus;             /* 所属 SPI 总线 */
    SpiDeviceCfg      cfg;             /* 设备静态配置 */
    GpioPin           cs;              /* CS 引脚句柄（attach 时从 cfg.csSpec 解析） */
    uint8_t           inTransaction;   /* 是否处于手动 CS 事务中 */
    uint8_t           suspended;       /* 是否已挂起 */
} HalSpiDevice;

/*===========================================================================
 * 内部辅助 — CS 双路径封装
 *===========================================================================*/

static inline void spi_cs_assert(HalSpiDevice *dev)
{
    if (gpio_pin_valid(dev->cs))
        gpio_pin_write(dev->cs, 0);
    else if (dev->bus->interface->cs_control)
        dev->bus->interface->cs_control(dev->bus, dev->cfg.csSpec.offset, 0);
}

static inline void spi_cs_deassert(HalSpiDevice *dev)
{
    if (gpio_pin_valid(dev->cs))
        gpio_pin_write(dev->cs, 1);
    else if (dev->bus->interface->cs_control)
        dev->bus->interface->cs_control(dev->bus, dev->cfg.csSpec.offset, 1);
}

/*===========================================================================
 * 总线生命周期
 *===========================================================================*/

/**
 * @brief 注册 SPI 总线（内部分配 lock + completion + workqueue + 可选 DoubleBuf）
 * @param bus           调用者分配的 SpiBus 实例
 * @param hwPrivate     BSP 私有数据指针（生命周期由调用者管理）
 * @param interface     硬件操作函数表
 * @param dbuf_page_size 双缓冲 page 大小（> 0 启用 DoubleBuf，0 禁用）
 * @retval OM_OK             成功
 * @retval OM_ERROR_MEMORY   动态分配失败
 * @retval OM_ERROR_PARAM    参数非法
 */
OmRet hal_spi_bus_register(SpiBus *bus, void *hwPrivate,
                           SpiInterface *interface,
                           size_t dbuf_page_size);

/**
 * @brief 反初始化 SPI 总线（确保无活跃传输时调用）
 */
void hal_spi_bus_deinit(SpiBus *bus);

/*===========================================================================
 * 设备挂载 / 移除
 *===========================================================================*/

/**
 * @brief 将从设备挂载到 SPI 总线并注册为 Device
 * @param bus   SPI 总线
 * @param dev   从设备实例
 * @param name  设备名称（注册到全局设备表）
 * @param cfg   从设备静态配置
 * @retval OM_OK             成功
 * @retval OM_ERROR_PARAM    参数非法（name/dev/cfg 为 NULL，或 maxHz == 0）
 */
OmRet hal_spi_device_attach(SpiBus *bus, HalSpiDevice *dev,
                            const char *name, const SpiDeviceCfg *cfg);

/**
 * @brief 从总线移除从设备（清理 cachedDevice 引用）
 */
void hal_spi_device_detach(HalSpiDevice *dev);

/*===========================================================================
 * 标准 Device 接口（DevInterface 6 个函数）
 *===========================================================================*/

OmRet  hal_spi_dev_init(Device *dev);
OmRet  hal_spi_dev_open(Device *dev, uint32_t oparam);
OmRet  hal_spi_dev_close(Device *dev);

/**
 * @brief Zephyr 风格"主收" — 发 dummy 接收 len 字节
 * @param ctrl_info  NULL 纯发 dummy；非 NULL 视为 const uint8_t* 先发 1 字节命令前缀
 */
size_t hal_spi_dev_read(Device *dev, void *ctrl_info, void *data, size_t len);

/**
 * @brief Zephyr 风格"主发" — 发送 data 的 len 字节，丢弃接收
 * @param ctrl_info  NULL 纯发数据；非 NULL 视为 const uint8_t* 先发 1 字节命令前缀
 */
size_t hal_spi_dev_write(Device *dev, void *ctrl_info, void *data, size_t len);

OmRet  hal_spi_dev_control(Device *dev, size_t cmd, void *arg);

/*===========================================================================
 * SPI 扩展 API
 *===========================================================================*/

/**
 * @brief 同步全双工传输（自动 CS）
 * @param xfer  txLen == rxLen 全双工传输描述符，结果写入 xfer->transferred
 * @retval OM_OK             成功
 * @retval OM_ERROR_PARAM    参数非法
 * @retval ERR_SPI_BUSY      异步传输进行中
 * @retval ERR_SPI_CS_CONFLICT  处于手动事务中
 * @retval ERR_SPI_DEV_SUSPENDED 设备已挂起
 * @retval ERR_SPI_TRANSFER_TIMEOUT 传输超时
 */
OmRet hal_spi_transfer(HalSpiDevice *dev, SpiXfer *xfer);

/**
 * @brief 一问一答（单 CS 周期内先写命令再读数据）
 * @param xfer  txLen = 命令字节数，rxLen = 读取字节数，结果写入 xfer->transferred
 */
OmRet hal_spi_write_then_read(HalSpiDevice *dev, SpiXfer *xfer);

/**
 * @brief 异步传输（调用者提供 SpiAsyncRequest，携带 per-request 回调）
 * @param req  调用者分配，填充 tx/rx/len/asyncCb/asyncParam，框架填充 dev/status/transferred
 * @retval OM_OK             入队成功
 * @retval OM_ERROR_PARAM    参数非法
 * @retval ERR_SPI_DEV_SUSPENDED 设备已挂起
 * @note  req 需保持存活直到 asyncCb 被调用
 * @note  若 DoubleBuf 启用且 req->len > dbuf_page_size 则返回 OM_ERROR_PARAM
 */
OmRet hal_spi_transfer_async(HalSpiDevice *dev, SpiAsyncRequest *req);

/*===========================================================================
 * 手动 CS 事务 API
 *===========================================================================*/

OmRet  hal_spi_transaction_begin(HalSpiDevice *dev);
OmRet  hal_spi_transaction_transfer(HalSpiDevice *dev, SpiXfer *xfer);
OmRet  hal_spi_transaction_end(HalSpiDevice *dev);

/*===========================================================================
 * 低功耗
 *===========================================================================*/

OmRet  hal_spi_device_suspend(HalSpiDevice *dev);
OmRet  hal_spi_device_resume(HalSpiDevice *dev);

/*===========================================================================
 * 框架 ISR 入口（由 BSP 的 DMA / 中断处理函数调用）
 *===========================================================================*/

/**
 * @brief SPI 硬件传输完成 ISR 入口
 * @param bus         SPI 总线
 * @param status      传输结果（OM_OK / OM_ERROR_TIMEOUT / OM_ERROR_DMA）
 * @param transferred 实际传输字节数
 * @note  ISR 上下文调用，先检查 busy（传输是否已被放弃），再写状态 + completion_done
 */
void hal_spi_isr(SpiBus *bus, OmRet status, size_t transferred);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_SPI_DEV_H__ */
