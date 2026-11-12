/**
 * @file   pal_spi_dev.h
 * @brief  SPI 外设抽象层（v2.0 — Linux message 模型）
 *
 * 核心理念：SpiMessage 是最小原子传输单位。
 * 应用层把"想要的全部行为"打包成 message（SpiTransfer 数组），
 * 框架保证整条 message 原子执行，CS 行为通过 per-transfer flag 控制。
 *
 * 设计参考：Linux Kernel spi_summary / Zephyr spi.h / ESP-IDF spi_master.h
 */

#ifndef __HAL_SPI_DEV_H__
#define __HAL_SPI_DEV_H__

#include "async/workqueue.h"
#include "core/om_def.h"
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

typedef struct SpiBus SpiBus;
typedef struct SpiControllerOps SpiControllerOps;
typedef struct SpiDeviceCfg SpiDeviceCfg;
typedef struct HalSpiDevice HalSpiDevice;

/*===========================================================================
 * SPI 模式和常量
 *===========================================================================*/

#define SPI_MODE_0        (0U) /* CPOL=0, CPHA=0 */
#define SPI_MODE_1        (1U) /* CPOL=0, CPHA=1 */
#define SPI_MODE_2        (2U) /* CPOL=1, CPHA=0 */
#define SPI_MODE_3        (3U) /* CPOL=1, CPHA=1 */

#define SPI_MSB_FIRST     (0U)
#define SPI_LSB_FIRST     (1U)

#define SPI_DATA_WIDTH_8  (8U)
#define SPI_DATA_WIDTH_16 (16U)

/*===========================================================================
 * SPI 设备控制命令
 *===========================================================================*/

#define SPI_CMD_SET_CFG (DEVICE_CMD_CFG)     /* 0x01  运行时更新从设备配置 */
#define SPI_CMD_GET_CFG (0x10U)              /* 读取当前从设备配置 */
#define SPI_CMD_SUSPEND (DEVICE_CMD_SUSPEND) /* 0x02  挂起从设备 */
#define SPI_CMD_RESUME  (DEVICE_CMD_RESUME)  /* 0x03  恢复从设备 */
#define SPI_CMD_ABORT   (0x11U)              /* 中止当前异步传输 */

/*===========================================================================
 * SPI 错误码（模块别名 → 通用错误码）
 *
 * 设计：error_code_system.md
 * 通用语义优先：所有别名均映射到 OM_ERR_* 通用码，共用数值不再冲突
 *===========================================================================*/

#define OM_ERR_SPI_TRANSFER_TIMEOUT OM_ERR_TIMEOUT       /* 传输超时 */
#define OM_ERR_SPI_DMA_ERROR        OM_ERR_IO            /* DMA / 硬件错误 */
#define OM_ERR_SPI_INVALID_CFG      OM_ERR_INVALID_ARG   /* 无效设备配置 */
#define OM_ERR_SPI_DEV_BUSY         OM_ERR_BUSY          /* 硬件传输进行中 */
#define OM_ERR_SPI_DEV_SUSPENDED    OM_ERR_NOT_SUPPORTED /* 设备已挂起 */

/* 模块特有码 BASE（0x1000+ 段，预留给未来无法映射到通用码的 SPI 专属错误） */
#define OM_ERR_SPI_BASE ((OmRet)0x1000)

/*===========================================================================
 * SpiTransfer —— 单段传输描述符（caller 栈分配）
 *===========================================================================*/

#define SPI_XFER_FLAG_CS_HOLD (1U << 0) /* 本段后保持 CS asserted */
#define SPI_XFER_FLAG_DUAL    (1U << 1) /* 预留：Dual-SPI (2-bit) */
#define SPI_XFER_FLAG_QUAD    (1U << 2) /* 预留：Quad-SPI (4-bit) */

typedef struct {
    const uint8_t *txBuf; /* 发送缓冲区（NULL = 发 dummy 0xFF） */
    uint8_t *rxBuf;       /* 接收缓冲区（NULL = 丢弃 MISO）    */
    size_t len;           /* 传输字节数                        */
    uint32_t flags;       /* SPI_XFER_FLAG_*                   */
    uint32_t speedHz;     /* per-transfer 频率覆盖（0 = 使用设备默认值） */
    uint8_t bitsPerWord;  /* per-transfer 字宽覆盖（0 = 使用设备默认值） */
} SpiTransfer;

/*===========================================================================
 * SpiMessage —— 多段传输原子消息（caller 栈分配）
 *===========================================================================*/

typedef struct {
    SpiTransfer *transfers; /* IN:  传输描述符数组              */
    size_t count;           /* IN:  数组元素个数                */
    size_t transferred;     /* OUT: 全部传输总字节数             */
    OmRet status;           /* OUT: 整体传输结果                 */
} SpiMessage;

/*===========================================================================
 * SpiControllerOps —— BSP 硬件操作函数表（平台无关）
 *===========================================================================*/

typedef struct SpiControllerOps {

    /**
     * @brief 配置 SPI 控制器寄存器（mode / 波特率 / 数据宽度 / 位序）
     * @retval OM_OK                   配置成功
     * @retval OM_ERR_INVALID_ARG      参数非法
     */
    OmRet (*configure)(SpiBus *bus, const SpiDeviceCfg *cfg);

    /**
     * @brief 启动单段 SPI 全双工传输（非阻塞）
     * @param tx   发送缓冲区（NULL = 发 dummy 0xFF）
     * @param rx   接收缓冲区（NULL = 丢弃 MISO）
     * @param len  传输字节数
     * @retval OM_OK                   启动成功
     * @retval OM_ERR_INVALID_ARG      参数非法
     * @note  ASYNC contract: 必须启动 DMA/IRQ 后立即返回，不得轮询等待。
     *        传输完成后 BSP 必须调用 hal_spi_isr() 通知框架。
     *        BSP 内部根据硬件能力选择 poll / IRQ / DMA 路径。
     */
    OmRet (*transferOne)(SpiBus *bus, const uint8_t *tx, uint8_t *rx, size_t len);

    /**
     * @brief 通用控制接口（SPI_CMD_ABORT / SUSPEND / RESUME 等）
     */
    OmRet (*control)(SpiBus *bus, uint32_t cmd, void *arg);

    /**
     * @brief 硬件 CS 电平控制（仅硬件 CS 模式才实现，GPIO CS 模式置 NULL）
     * @param csId   CS 线编号，来自 cfg.csSpec.offset
     * @param assert true = 选中(active), false = 释放(inactive)
     * @note  仅当 dev->cfg.csSpec.controller == NULL 时走此路径
     * @note  GPIO CS 由框架直接调用 gpio_pin_write()，不经过此回调
     */
    void (*setCs)(SpiBus *bus, uint8_t cs_id, bool assert);

} SpiControllerOps;

/*===========================================================================
 * SpiDeviceCfg —— 从设备静态配置（平台无关）
 *===========================================================================*/

typedef struct SpiDeviceCfg {
    GpioPinSpec csSpec;          /* CS 引脚描述符（controller==NULL 表示硬件 CS） */
    uint8_t mode;                /* SPI_MODE_0..3 */
    uint32_t maxHz;              /* 最大 SCLK 频率 (Hz) */
    uint8_t dataWidth;           /* SPI_DATA_WIDTH_8 / SPI_DATA_WIDTH_16 */
    uint8_t bitOrder;            /* SPI_MSB_FIRST / SPI_LSB_FIRST */
    uint32_t transferOverheadMs; /* 除 SCLK 纯传输时间外的额外预算（ms）。
                                    补偿 BSP DMA 启动/ISR 延迟/调度抖动。
                                    建议值 ≥ 5 ms，高频小数据适当增大。 */
} SpiDeviceCfg;

/*===========================================================================
 * SpiBus —— 总线控制器（不注册为 Device）
 *===========================================================================*/

typedef struct SpiBus {
    /* ---- BSP 接口 ---- */
    void *hwPrivate;       /* BSP 私有数据（不透明指针） */
    SpiControllerOps *ops; /* 硬件操作函数表 */

    /* ---- 并发控制 ---- */
    OsalMutex *lock; /* 总线互斥锁（非递归） */

    /* ---- 配置缓存 ---- */
    HalSpiDevice *lastCfgDev; /* 当前已配置的设备指针 */
    uint32_t actualHz;        /* 当前 SCLK 实际频率（分频后的真实值，由 configure 填充） */

    /* ---- 同步传输完成信号 ---- */
    Completion transferDone;
    size_t lastTransferred; /* ISR 写入，同步路径读取 */
    OmRet lastStatus;       /* ISR 写入的传输结果 */

    /* ---- 硬件传输状态 ---- */
    volatile uint8_t busy; /* 硬件传输进行中（ISR 门控，需 volatile） */

    /* ---- 异步调度（框架自建 per-bus workqueue） ---- */
    Workqueue asyncWq;
    uint32_t asyncEpoch; /* 异步请求递增序号 */

    /* ---- 总线级 suspend ref-counting ---- */
    uint8_t suspendedCount; /* 已挂起的从设备数量 */
    uint8_t deviceCount;    /* 总线上挂载的设备总数 */
    ListHead deviceList;    /* 已挂载设备链表 */
    ListHead busNode;       /* 全局总线链表节点 */
} SpiBus;

/*===========================================================================
 * HalSpiDevice —— 从设备（注册为 Device）
 *===========================================================================*/

typedef struct HalSpiDevice {
    Device parent;     /* 设备父类（标准 Device 模型） */
    SpiBus *bus;       /* 所属 SPI 总线（detach 时置 NULL） */
    SpiDeviceCfg cfg;  /* 设备静态配置 */
    GpioPin cs;        /* CS 引脚句柄（attach 时从 cfg.csSpec 解析） */
    uint8_t suspended; /* 是否已挂起 */
    ListHead busNode;  /* 挂载到 SpiBus.deviceList 的链表节点 */

    /* ---- 异步传输 slot（每设备同一时刻仅 1 个在途请求） ---- */
    Work asyncWork;       /* 嵌入 workqueue 的 Work 节点 */
    SpiMessage *asyncMsg; /* 当前异步 message */
    void (*asyncCb)(void *param, SpiMessage *msg);
    void *asyncCbParam;
    uint32_t asyncEpoch; /* 排入时快照 bus->asyncEpoch */
    bool asyncCsHeld;    /* worker 内跨 transfer 的 CS 保持状态 */
    uint8_t asyncBusy;   /* 异步 slot 是否被占用（irq_lock 保护） */
} HalSpiDevice;

/*===========================================================================
 * 内部辅助 — CS 双路径封装（仅供 hal_spi.c 使用）
 *===========================================================================*/

static inline void spi_cs_assert_dev(HalSpiDevice *dev)
{
    if (gpio_pin_valid(dev->cs))
        gpio_pin_write(dev->cs, 0);
    else if (dev->bus->ops->setCs)
        dev->bus->ops->setCs(dev->bus, dev->cfg.csSpec.offset, true);
}

static inline void spi_cs_deassert_dev(HalSpiDevice *dev)
{
    if (gpio_pin_valid(dev->cs))
        gpio_pin_write(dev->cs, 1);
    else if (dev->bus->ops->setCs)
        dev->bus->ops->setCs(dev->bus, dev->cfg.csSpec.offset, false);
}

/*===========================================================================
 * 公共 CS 控制（供 CS_HOLD 结束后的手动释放）
 *===========================================================================*/

void spi_cs_deassert(HalSpiDevice *dev);

/*===========================================================================
 * 总线生命周期
 *===========================================================================*/

OmRet spi_bus_register(SpiBus *bus, void *hw_private,
                       SpiControllerOps *ops);

/** @brief 从全局总线表中摘除（反操作：register），不释放内部资源 */
void spi_bus_unregister(SpiBus *bus);

/** @brief 销毁总线全部内部资源
 *  @pre   必须已调 spi_bus_unregister，且所有从设备已 detach（deviceCount==0） */
void spi_bus_deinit(SpiBus *bus);

/** @brief 按注册序号获取 SpiBus 指针（0,1,...） */
SpiBus *spi_bus_get(uint8_t idx);

/*===========================================================================
 * 设备挂载 / 移除
 *===========================================================================*/

OmRet spi_device_attach(uint8_t busIdx, HalSpiDevice *dev,
                        const char *name, const SpiDeviceCfg *cfg);
void spi_device_detach(HalSpiDevice *dev);

/*===========================================================================
 * 标准 Device 接口（DevInterface 6 个函数）
 *===========================================================================*/

OmRet spi_dev_init(Device *dev);
OmRet spi_dev_open(Device *dev, uint32_t oparam);
OmRet spi_dev_close(Device *dev);
size_t spi_dev_read(Device *dev, void *ctrl_info, void *data, size_t len);
size_t spi_dev_write(Device *dev, void *ctrl_info, void *data, size_t len);
OmRet spi_dev_control(Device *dev, size_t cmd, void *arg);

/*===========================================================================
 * 核心数据传输 API（仅 2 个）
 *===========================================================================*/

OmRet spi_transfer(HalSpiDevice *dev, SpiMessage *msg);

OmRet spi_transfer_async(HalSpiDevice *dev, SpiMessage *msg,
                         void (*callback)(void *param, SpiMessage *msg),
                         void *param);

/*===========================================================================
 * 便利层（内部组装 message，覆盖 90% 场景）
 *===========================================================================*/

OmRet spi_write(HalSpiDevice *dev, const uint8_t *buf, size_t len);
OmRet spi_read(HalSpiDevice *dev, uint8_t *buf, size_t len);
OmRet spi_write_then_read(HalSpiDevice *dev,
                          const uint8_t *tx, size_t tx_len,
                          uint8_t *rx, size_t rx_len);

/*===========================================================================
 * 低功耗
 *===========================================================================*/

OmRet spi_device_suspend(HalSpiDevice *dev);
OmRet spi_device_resume(HalSpiDevice *dev);

/*===========================================================================
 * 框架 ISR 入口（由 BSP 的 DMA / 中断处理函数调用）
 *===========================================================================*/

void hal_spi_isr(SpiBus *bus, OmRet status, size_t transferred);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_SPI_DEV_H__ */
