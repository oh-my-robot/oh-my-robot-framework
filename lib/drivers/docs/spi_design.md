# Oh My Robot SPI 子系统架构设计

> 版本：v4.0
> 日期：2026-06-26
> 基于：Linux kernel spi_summary / Zephyr spi.h / ESP-IDF spi_master.h
> 项目约束：Device 模型、Completion/OsalMutex/Workqueue 基础设施

---

## 1. 架构分层

```
+====================================================================+
|  Application / Upper-layer Driver                                  |
|  e.g. imu_icm42688.c, flash_w25qxx.c, tft_st7789.c                |
|  Operates through: device_read / device_write / device_control      |
|                    spi_transfer / spi_transfer_async                |
|                    spi_write / spi_read / spi_write_then_read       |
+====================================================================+
           |                              |
           | Standard Device API           | SPI Extended API
           | (DevInterface ops)           | (spi_* functions)
           v                              v
+====================================================================+
|  HalSpiDevice (Peripheral Device — registered as Device)           |
|  - parent: Device         (global device list)                      |
|  - bus: SpiBus*           (owning bus, index-resolved at attach)    |
|  - cfg: SpiDeviceCfg      (per-device mode/maxHz/dataWidth/bitOrder)|
|  - cs: GpioPin            (resolved from cfg.csSpec at attach)     |
|  - suspended              (per-device power state)                  |
|  - asyncWork/asyncMsg/... (async slot — one in-flight per device)   |
|  Responsibilities: attach/detach, CS path routing, async slot       |
+====================================================================+
           |
           | 1:N — all devices on one bus share the same SpiBus
           v
+====================================================================+
|  SpiBus (Bus Controller — NOT registered as Device)                |
|  - hwPrivate / ops / lock / lastCfgDev / actualHz                  |
|  - transferDone / busy / asyncWq (framework-builtin wq)             |
|  - suspendedCount / deviceCount (suspend ref-counting)              |
|  - deviceList / busNode (linked list membership)                    |
|  Responsibilities: Mutex, reconfig cache, Completion, workqueue     |
|                    workqueue serialization, suspend gate             |
+====================================================================+
           |
           | SpiControllerOps function pointer table
           v
+====================================================================+
|  SpiControllerOps (BSP Hardware Abstraction — platform-independent) |
|  - configure(bus, cfg)     - transferOne(bus, tx, rx, len)          |
|  - control(bus, cmd, arg)  - setCs(bus, cs_id, assert) [hw CS]     |
+====================================================================+
           |
           | BSP implementation (DMA completely transparent to framework)
           v
+====================================================================+
|  BSP Private (Platform-specific, NOT visible to framework)          |
|  e.g. BspSpi { SpiBus parent; SPI_HandleTypeDef handle; ... }       |
|  CS GPIO via gpio_pin_write() — framework controls directly         |
|  ISR: DMA TC → HAL callback → hal_spi_isr(bus, status, n)           |
|  DMA channel / IRQ / NVIC → all in BSP private                      |
+====================================================================+
           |
           v
+====================================================================+
|  MCU Hardware (SPI Peripheral + GPIO + DMA + NVIC)                 |
+====================================================================+
```

---

## 2. 对象关系图解

### 2.1 结构关系：所有权与引用

```
                         ┌────────────────────────────┐
                         │      全局 Device 注册表      │
                         │   device_find("imu0")       │
                         └──────────┬─────────────────┘
                                    │ parent.list 链接
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
            ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
            │ HalSpiDevice │ │ HalSpiDevice │ │ HalSpiDevice │  ← 用户可见 Device
            │  (imu 从机)  │ │ (flash 从机) │ │  (lcd 从机)  │
            │              │ │              │ │              │
            │ parent:Device│ │ parent:Device│ │ parent:Device│
            │ bus ─────────┼─┼──────────────┼─┼───┐          │
            │ cfg:SpiDevCfg│ │ cfg:SpiDevCfg│ │   │          │
            │ cs:GpioPin   │ │ cs:GpioPin   │ │   │          │
            │ suspended    │ │ suspended    │ │   │          │
            │ asyncWork    │ │ asyncWork    │ │   │          │
            │ asyncMsg     │ │ asyncMsg     │ │   │          │
            │ asyncCb/Param│ │ asyncCb/Param│ │   │          │
            │ busNode──────┼─┼──────────────┼─┼───┤          │
            └──────┬───────┘ └──────┬───────┘ │   │          │
                   │                │         │   │          │
                   │  所有设备共享同一条总线     │   │          │
                   │                │         │   │          │
                   ▼                ▼         ▼   ▼          │
            ┌──────────────────────────────────────────────┐ │
            │              SpiBus (总线控制器)              │ │
            │  ┌─────────────────────────────────────────┐ │ │
            │  │  ops: SpiControllerOps                   │ │◄┘
            │  │    → configure / transferOne / control   │ │
            │  │    → setCs (hardware CS fallback)        │ │
            │  ├─────────────────────────────────────────┤ │
            │  │  lock: OsalMutex*  (bus mutual exclusion)│ │
            │  │  lastCfgDev        (config cache, ptr)   │ │
            │  │  actualHz           (feedback from BSP)   │ │
            │  ├─────────────────────────────────────────┤ │
            │  │  transferDone: Completion (ISR→thread)    │ │
            │  │  lastStatus / lastTransferred             │ │
            │  │  busy: volatile     (ISR gate)            │ │
            │  ├─────────────────────────────────────────┤ │
            │  │  asyncWq: Workqueue (async serialization) │ │
            │  │  asyncEpoch          (request counter)    │ │
            │  ├─────────────────────────────────────────┤ │
            │  │  suspendedCount / deviceCount             │ │
            │  │  deviceList / busNode (linked lists)      │ │
            │  └─────────────────────────────────────────┘ │
            │  hwPrivate → BSP private (opaque pointer)     │
            └──────────────────────────────────────────────┘
                               ▲
                               │ busNode
                        ┌──────┴──────┐
                        │ gSpiBusList │  全局 SPI 总线链表
                        │ (ListHead)  │  spi_bus_get(idx)
                        └─────────────┘
```

**关键关系**：
- HalSpiDevice **引用** SpiBus（多对一），不拥有
- SpiBus **拥有** OsalMutex、Completion、Workqueue
- SpiBus 通过 `ops` 函数表 **委托** BSP 执行硬件操作
- SpiBus 通过 `hwPrivate` 不透明指针持有 BSP 私有数据
- Device 父类通过全局链表 `parent.list` 注册到设备表
- SpiBus 通过 `busNode` 挂入全局 `gSpiBusList`，`spi_bus_get(idx)` 按注册序号查找

---

## 3. 数据结构

### 3.1 SpiTransfer — 单段传输描述符（caller 栈分配）

```c
#define SPI_XFER_FLAG_CS_HOLD    (1U << 0)  /* 本段后保持 CS asserted */
#define SPI_XFER_FLAG_DUAL       (1U << 1)  /* 预留：Dual-SPI (2-bit) */
#define SPI_XFER_FLAG_QUAD       (1U << 2)  /* 预留：Quad-SPI (4-bit) */

typedef struct {
    const uint8_t *txBuf;           /* 发送缓冲区（NULL = 发 dummy 0xFF） */
    uint8_t       *rxBuf;           /* 接收缓冲区（NULL = 丢弃 MISO）    */
    size_t         len;             /* 传输字节数                        */
    uint32_t       flags;           /* SPI_XFER_FLAG_*                   */
    uint32_t       speedHz;         /* per-transfer 频率覆盖（0 = 使用设备默认值） */
    uint8_t        bitsPerWord;     /* per-transfer 字宽覆盖（0 = 使用设备默认值） */
} SpiTransfer;
```

### 3.2 SpiMessage — 多段传输原子消息（caller 栈分配）

一条 message 包含一个或多个 transfer，框架保证整条 message 原子执行，不被其他设备中断。CS 行为通过 per-transfer `flags` 控制。

```c
typedef struct {
    SpiTransfer   *transfers;       /* IN:  传输描述符数组              */
    size_t         count;           /* IN:  数组元素个数                */
    size_t         transferred;     /* OUT: 全部传输总字节数             */
    OmRet          status;          /* OUT: 整体传输结果                 */
} SpiMessage;
```

### 3.3 SpiControllerOps — BSP 硬件操作函数表

```c
typedef struct SpiControllerOps {
    /* 配置 SPI 控制器寄存器（mode / 波特率 / 数据宽度 / 位序）。
     * actualHz 通过 bus->actualHz 回写，供框架超时计算。 */
    OmRet (*configure)(SpiBus *bus, const SpiDeviceCfg *cfg);

    /* 启动单段 SPI 全双工传输（非阻塞）。
     * tx==NULL → 发 dummy 0xFF，rx==NULL → 丢弃 MISO。
     * 必须启动 DMA/IRQ 后立即返回，完成后 BSP 调用 hal_spi_isr() 通知框架。 */
    OmRet (*transferOne)(SpiBus *bus, const uint8_t *tx, uint8_t *rx, size_t len);

    /* 通用控制接口（SPI_CMD_ABORT / SUSPEND / RESUME 等） */
    OmRet (*control)(SpiBus *bus, uint32_t cmd, void *arg);

    /* 硬件 CS 电平控制（仅硬件 CS 模式才实现，GPIO CS 模式置 NULL）。
     * GPIO CS 由框架直接调用 gpio_pin_write()，不经过此回调。 */
    void  (*setCs)(SpiBus *bus, uint8_t cs_id, bool assert);
} SpiControllerOps;
```

### 3.4 SpiDeviceCfg — 从设备配置（平台无关）

```c
typedef struct SpiDeviceCfg {
    GpioPinSpec     csSpec;             /* CS 引脚描述符（controller==NULL 表示硬件 CS） */
    uint8_t         mode;               /* SPI_MODE_0..3 */
    uint32_t        maxHz;              /* 最大 SCLK 频率 (Hz) */
    uint8_t         dataWidth;          /* SPI_DATA_WIDTH_8 / SPI_DATA_WIDTH_16 */
    uint8_t         bitOrder;           /* SPI_MSB_FIRST / SPI_LSB_FIRST */
    uint32_t        transferOverheadMs; /* 除 SCLK 纯传输时间外的额外预算（ms）。
                                           补偿 BSP DMA 启动/ISR 延迟/调度抖动。 */
} SpiDeviceCfg;
```

### 3.5 SpiBus — 总线控制器

```c
typedef struct SpiBus {
    /* ---- BSP 接口 ---- */
    void              *hwPrivate;     /* BSP 私有数据（不透明指针） */
    SpiControllerOps  *ops;           /* 硬件操作函数表 */

    /* ---- 并发控制 ---- */
    OsalMutex         *lock;          /* 总线互斥锁（非递归） */

    /* ---- 配置缓存 ---- */
    HalSpiDevice      *lastCfgDev;    /* 当前已配置的设备指针 */
    uint32_t           actualHz;      /* 当前 SCLK 实际频率（分频后真实值，由 configure 填充） */

    /* ---- 同步传输完成信号 ---- */
    Completion         transferDone;
    size_t             lastTransferred;  /* ISR 写入，同步路径读取 */
    OmRet              lastStatus;       /* ISR 写入的传输结果 */

    /* ---- 硬件传输状态 ---- */
    volatile uint8_t   busy;             /* 硬件传输进行中（ISR 门控） */

    /* ---- 异步调度（框架自建 per-bus workqueue） ---- */
    Workqueue          asyncWq;
    uint32_t           asyncEpoch;       /* 异步请求递增序号 */

    /* ---- 总线级 suspend ref-counting ---- */
    uint8_t            suspendedCount;   /* 已挂起的从设备数量 */
    uint8_t            deviceCount;      /* 总线上挂载的设备总数 */
    ListHead           deviceList;       /* 已挂载设备链表 */
    ListHead           busNode;          /* 全局总线链表节点 */
} SpiBus;
```

### 3.6 HalSpiDevice — 从设备（注册为 Device）

```c
typedef struct HalSpiDevice {
    Device         parent;          /* 设备父类（标准 Device 模型） */
    SpiBus        *bus;             /* 所属 SPI 总线（detach 时置 NULL） */
    SpiDeviceCfg   cfg;             /* 设备静态配置 */
    GpioPin        cs;              /* CS 引脚句柄（attach 时从 cfg.csSpec 解析） */
    uint8_t        suspended;       /* 是否已挂起 */
    ListHead       busNode;         /* 挂载到 SpiBus.deviceList 的链表节点 */

    /* ---- 异步传输 slot（每设备同一时刻仅 1 个在途请求） ---- */
    Work           asyncWork;       /* 嵌入 workqueue 的 Work 节点 */
    SpiMessage    *asyncMsg;        /* 当前异步 message */
    void         (*asyncCb)(void *param, SpiMessage *msg);
    void          *asyncCbParam;
    uint32_t       asyncEpoch;      /* 排入时快照 bus->asyncEpoch */
    bool           asyncCsHeld;     /* worker 内跨 transfer 的 CS 保持状态 */
    uint8_t        asyncBusy;       /* 异步 slot 是否被占用（irq_lock 保护） */
} HalSpiDevice;
```

### 3.7 常量与错误码

```c
/* SPI 模式 */
#define SPI_MODE_0              (0U)    /* CPOL=0, CPHA=0 */
#define SPI_MODE_1              (1U)    /* CPOL=0, CPHA=1 */
#define SPI_MODE_2              (2U)    /* CPOL=1, CPHA=0 */
#define SPI_MODE_3              (3U)    /* CPOL=1, CPHA=1 */
#define SPI_MSB_FIRST           (0U)
#define SPI_LSB_FIRST           (1U)
#define SPI_DATA_WIDTH_8        (8U)
#define SPI_DATA_WIDTH_16       (16U)

/* 控制命令 */
#define SPI_CMD_SET_CFG         (DEVICE_CMD_CFG)         /* 0x01  运行时更新从设备配置 */
#define SPI_CMD_GET_CFG         (0x10U)                  /* 读取当前从设备配置 */
#define SPI_CMD_SUSPEND         (DEVICE_CMD_SUSPEND)     /* 0x02  挂起从设备 */
#define SPI_CMD_RESUME          (DEVICE_CMD_RESUME)      /* 0x03  恢复从设备 */
#define SPI_CMD_ABORT           (0x11U)                  /* 中止当前异步传输 */

/* 错误码 — 映射到 OM 通用码 */
#define OM_ERR_SPI_TRANSFER_TIMEOUT   OM_ERR_TIMEOUT
#define OM_ERR_SPI_DMA_ERROR          OM_ERR_IO
#define OM_ERR_SPI_INVALID_CFG        OM_ERR_INVALID_ARG
#define OM_ERR_SPI_DEV_BUSY           OM_ERR_BUSY
#define OM_ERR_SPI_DEV_SUSPENDED      OM_ERR_NOT_SUPPORTED
```

---

## 4. API 函数签名

### 4.1 总线生命周期

```c
OmRet   spi_bus_register(SpiBus *bus, void *hw_private,
                        SpiControllerOps *ops);
/* 分配 lock + completion + workqueue，将 bus 注册到全局总线链表 */

void    spi_bus_unregister(SpiBus *bus);
/* 从全局总线表摘除，不释放内部资源（反操作 register），可 re-register */

void    spi_bus_deinit(SpiBus *bus);
/* 销毁总线全部内部资源。前提：已 unregister 且 deviceCount==0 */

SpiBus *spi_bus_get(uint8_t idx);
/* 按注册序号（0,1,...）获取 SpiBus 指针。BSP 注册顺序决定 mapping */
```

### 4.2 设备挂载 / 移除

```c
OmRet spi_device_attach(uint8_t busIdx, HalSpiDevice *dev,
                         const char *name, const SpiDeviceCfg *cfg);
void  spi_device_detach(HalSpiDevice *dev);
```

### 4.3 标准 Device 接口（DevInterface）

```c
OmRet  spi_dev_init(Device *dev);          /* NOP（资源在 attach 时分配） */
OmRet  spi_dev_open(Device *dev, uint32_t oparam);   /* NOP */
OmRet  spi_dev_close(Device *dev);                  /* NOP */
size_t spi_dev_read(Device *dev, void *ctrl_info, void *data, size_t len);
size_t spi_dev_write(Device *dev, void *ctrl_info, void *data, size_t len);
OmRet  spi_dev_control(Device *dev, size_t cmd, void *arg);
```

### 4.4 核心数据传输 API

```c
/* 同步传输整条 SpiMessage（多段 transfer 原子执行） */
OmRet spi_transfer(HalSpiDevice *dev, SpiMessage *msg);
/* msg->transfers/msg->count 由 caller 填充，msg->transferred/msg->status 由框架回填 */

/* 异步传输整条 SpiMessage（per-device single slot，callback 通知完成） */
OmRet spi_transfer_async(HalSpiDevice *dev, SpiMessage *msg,
                          void (*callback)(void *param, SpiMessage *msg),
                          void *param);
/* msg 需保持存活直到 callback 被调用。每设备同一时刻最多 1 个异步请求。 */
```

### 4.5 便利层 API

```c
/* 纯写：发送 buf 的 len 字节，丢弃 MISO */
OmRet spi_write(HalSpiDevice *dev, const uint8_t *buf, size_t len);

/* 纯读：发 dummy 0xFF 接收 len 字节到 buf */
OmRet spi_read(HalSpiDevice *dev, uint8_t *buf, size_t len);

/* 先写后读（单 CS 周期内）：先发 tx 再收 rx，覆盖 ~90% 寄存器读写场景 */
OmRet spi_write_then_read(HalSpiDevice *dev,
                           const uint8_t *tx, size_t tx_len,
                           uint8_t *rx, size_t rx_len);
```

### 4.6 CS / 低功耗 / ISR

```c
/* 手动释放 CS（CS_HOLD 消息结束后调用） */
void spi_cs_deassert(HalSpiDevice *dev);

/* 低功耗挂起/恢复（suspend count 达 deviceCount 时关 SPI 外设时钟） */
OmRet spi_device_suspend(HalSpiDevice *dev);
OmRet spi_device_resume(HalSpiDevice *dev);

/* 框架 ISR 入口（由 BSP DMA/中断处理函数调用） */
void hal_spi_isr(SpiBus *bus, OmRet status, size_t transferred);
/* busy=1 检查门控，防止超时路径已清零 busy 后的迟到 ISR */
```

---

## 5. 关键设计决策

| # | 决策 | 选择 | 参考 | 理由 |
|---|------|------|------|------|
| 1 | **传输粒度** | SpiMessage = SpiTransfer[]，多段原子执行 + CS_HOLD per-transfer flag | Linux spi_message | 覆盖单寄存器读写（1 段）至 Flash 多段命令，QSPI 扩展预留 DUAL/QUAD flag |
| 2 | **CS 管理** | 双路径：GPIO（框架直控 gpio_pin_write）+ 硬件 CS（setCs 回调降级） | Linux gpiod / Zephyr gpio_dt_spec | GPIO 子系统复用，ACTIVE_LOW 自动反转；硬件 CS 走 controller==NULL 路径 |
| 3 | **异步调度** | Per-bus 单 worker workqueue + per-device async slot | Linux per-controller kthread | ISR 极薄（写 lastStatus/lastTransferred + completion_done）；框架内部化，调用者无感知 |
| 4 | **异步 slot** | HalSpiDevice 内嵌 async Work/SpiMessage*/callback，irq_lock 保护 asyncBusy | — | 消除 SpiAsyncRequest 动态分配，零 malloc；同一设备仅 1 个在途请求 |
| 5 | **DMA 集成** | BSP Private 完全透明 | ESP-IDF/NuttX | DMA 通道/IRQ/句柄全在 BSP；框架只通过 transferOne 契约间接驱动 |
| 6 | **错误模型** | SPI 别名 → OM 通用码（OM_ERR_TIMEOUT/IO/INVALID_ARG/BUSY/NOT_SUPPORTED） | OM OmRet 体系 | 无模块专属错误枚举，统一命名空间，通用语义优先 |
| 7 | **配置缓存** | lastCfgDev 指针比较 | RT-Thread | 单指令，无哈希，IMU 1kHz 无效重配全部消除 |
| 8 | **actualHz 反馈** | configure 回写 bus->actualHz，超时公式用实际频率 | Linux spi_controller.min_speed_hz | 分频器离散量化可能导致实际频率偏离 maxHz，超时必须基于真实值 |
| 9 | **总线发现** | spi_bus_get(idx) — 纯逻辑序号，全局链表 + irq_lock 保护 | Linux spi_bus_num | 上层零 BSP 依赖；无硬编码容量上限；链表操作微秒级 |
| 10 | **生命周期** | register / unregister / deinit 三态分离 | — | unregister 可逆（摘表不销毁），deinit 不可逆（前提 deviceCount==0，持锁销毁） |
| 11 | **SpiBus 不可见** | 不注册为 Device | Linux/ESP-IDF | 用户永远操作从设备，总线是内部基础设施 |
| 12 | **超时** | 动态计算 = (len × 8 × 1000 / actualHz) + transferOverheadMs | Linux/ESP-IDF | 每次传输根据实际长度和 actualHz 计算，短传输短超时 |

---

## 6. 关键流程

### 6.1 CS 双路径（内部辅助）

```c
/* GPIO CS (csSpec.controller != NULL) 与硬件 CS (controller == NULL) 双路径 */
static inline void spi_cs_assert_dev(HalSpiDevice *dev) {
    if (gpio_pin_valid(dev->cs))
        gpio_pin_write(dev->cs, 0);                // GPIO: 逻辑 0 = assert
    else if (dev->bus->ops->setCs)
        dev->bus->ops->setCs(dev->bus, dev->cfg.csSpec.offset, true);  // HW CS
}

static inline void spi_cs_deassert_dev(HalSpiDevice *dev) {
    if (gpio_pin_valid(dev->cs))
        gpio_pin_write(dev->cs, 1);                // GPIO: 逻辑 1 = deassert
    else if (dev->bus->ops->setCs)
        dev->bus->ops->setCs(dev->bus, dev->cfg.csSpec.offset, false);
}
```

### 6.2 同步传输（spi_transfer）

```
spi_transfer(dev, msg)
  ├─ 参数校验（dev/bus/msg/msg->transfers 非 NULL，msg->count > 0）
  ├─ if (dev->suspended) → return OM_ERR_SPI_DEV_SUSPENDED
  ├─ spi_bus_lock(bus)
  ├─ 持锁重检 suspended / busy
  ├─ spi_ensure_configured(bus, dev)
  │    └─ lastCfgDev == dev? skip : ops->configure() + 更新 lastCfgDev + actualHz
  │
  ├─ for each SpiTransfer in msg:
  │    ├─ len==0 或 txBuf/rxBuf 双 NULL → skip
  │    ├─ if (!cs_held): spi_cs_assert_dev(dev); cs_held = true
  │    ├─ 动态超时 = spi_calc_timeout_ms(xfer->len, bus->actualHz, dev->cfg.transferOverheadMs)
  │    ├─ spi_do_transfer_one(bus, tx, rx, len, &transferred, timeout)
  │    │    ├─ bus->busy = 1        (必须在 transferOne 前：DMA 可能提前完成触发 ISR)
  │    │    ├─ ops->transferOne()   (启动 DMA，非阻塞返回)
  │    │    ├─ [失败] bus->busy = 0, return error
  │    │    ├─ completion_wait()    (阻塞等待 ISR)
  │    │    │    ├─ ISR: hal_spi_isr → busy 门控 → 写 lastStatus/lastTransferred → completion_done
  │    │    │    └─ [超时] irq_lock→busy=0, drain completion, return TIMEOUT
  │    │    └─ msg->transferred += transferred
  │    ├─ [错误] spi_cs_deassert_dev(dev), unlock, return
  │    ├─ if (xfer->flags & CS_HOLD): cs_held = true
  │    └─ else: spi_cs_deassert_dev(dev); cs_held = false
  │
  ├─ msg->status = OM_OK
  └─ spi_bus_unlock(bus)
```

### 6.3 异步传输（spi_transfer_async）

```
=== 发起 ===
spi_transfer_async(dev, msg, callback, param)
  ├─ 参数校验（dev/bus/msg/callback 非 NULL，msg->transfers/msg->count 有效）
  ├─ if (dev->suspended) → return OM_ERR_SPI_DEV_SUSPENDED
  ├─ irq_lock: if (dev->asyncBusy) → irq_unlock, return OM_ERR_BUSY
  ├─ dev->asyncBusy = 1; dev->asyncEpoch = ++bus->asyncEpoch
  ├─ irq_unlock
  ├─ 填充 slot: dev->asyncMsg = msg; dev->asyncCb = callback; dev->asyncCbParam = param
  ├─ dev->asyncCsHeld = false; msg->status = OM_OK; msg->transferred = 0
  ├─ work_init(&dev->asyncWork, spi_async_worker_func, NULL)
  └─ workqueue_enqueue(&bus->asyncWq, &dev->asyncWork) → return OM_OK

=== Worker (spi_async_worker_func) ===
  ├─ container_of(work) → dev; bus = dev->bus; msg = dev->asyncMsg
  ├─ [bus==NULL] msg->status = NOT_SUPPORTED, goto done
  ├─ spi_bus_lock(bus)
  ├─ 权威检查: dev->bus != bus? → NOT_SUPPORTED
  ├─ dev->suspended? → SUSPENDED
  ├─ spi_ensure_configured(bus, dev)
  │
  ├─ for each SpiTransfer in msg:
  │    ├─ tx_src = xfer->txBuf（零拷贝，DMA 直接从 caller buffer 读取）
  │    ├─ busy=1 → ops->transferOne() → [失败] busy=0, goto cleanup
  │    ├─ unlock → completion_wait(timeout)
  │    ├─ [超时] irq_lock→busy=0, drain completion, lock, msg->status=TIMEOUT, goto cleanup
  │    ├─ msg->transferred += bus->lastTransferred
  │    ├─ [硬件错误] bus->lastStatus != OK → goto cleanup
  │    ├─ CS_HOLD? asyncCsHeld=true : deassert, asyncCsHeld=false
  │    └─ lock 重检 suspend/detach
  │
  ├─ msg->status = OM_OK
  │
  ├─ cleanup: if (asyncCsHeld) deassert; if (locked) unlock
  └─ done: irq_lock→asyncBusy=0 → irq_unlock → dev->asyncCb(param, msg)
```

### 6.4 便利层（spi_write_then_read）

```
spi_write_then_read(dev, tx, tx_len, rx, rx_len)
  ├─ 栈上构造 SpiTransfer[1]:
  │    .txBuf = tx, .rxBuf = rx, .len = tx_len + rx_len
  │    .flags = SPI_XFER_FLAG_CS_HOLD  (CS 保持全程 — write + read 在单 CS 周期内)
  ├─ 栈上构造 SpiMessage: .transfers = &xfer, .count = 1
  ├─ spi_transfer(dev, &msg)
  └─ spi_cs_deassert(dev)  (手动释放 CS)
```

### 6.5 设备切换 + 配置缓存

```
IMU 完成 → Flash 接管：
1. IMU transfer CS deassert → spi_bus_unlock
2. Flash 线程 spi_bus_lock → 获取锁
3. spi_ensure_configured: lastCfgDev==imuDev != flashDev
   → ops->configure(bus, &flashDev->cfg) — 重配 mode/CPOL/CPHA/BR/DFF/LSBFIRST
   → lastCfgDev = flashDev, bus->actualHz = 实际频率
4. spi_cs_assert_dev(flashDev) → ops->transferOne → completion_wait → ...
5. spi_cs_deassert_dev(flashDev) → spi_bus_unlock

同设备连续传输（IMU 轮询）：
1. IMU transfer 完成 → unlock
2. IMU 下一帧 → lock
3. spi_ensure_configured: lastCfgDev == imuDev → skip configure！（零重配开销）
```

### 6.6 低功耗挂起/恢复

```
spi_device_suspend(dev):
  ├─ lock → 幂等检查 → dev->suspended = 1 → bus->suspendedCount++
  ├─ if (suspendedCount == deviceCount):
  │      ops->control(bus, SPI_CMD_SUSPEND, NULL)  (关闭 SPI 外设时钟)
  └─ unlock

spi_device_resume(dev):
  ├─ lock → 幂等检查
  ├─ if (suspendedCount == deviceCount):
  │      ops->control(bus, SPI_CMD_RESUME, NULL)  (恢复 SPI 外设时钟)
  ├─ suspendedCount-- → dev->suspended = 0
  ├─ bus->lastCfgDev = NULL  (suspend 期间配置可能丢失，失效缓存)
  └─ unlock
```

---

## 7. 设计决策对照开源方案

| 决策 | Linux | Zephyr | ESP-IDF | **OM 选择** |
|------|:---:|:---:|:---:|:---:|
| 传输粒度 | spi_message | spi_buf_set | spi_transaction_t | **SpiMessage (SpiTransfer[])** |
| CS 管理 | Core 统一 | spi_context | 硬件自动 | **GPIO 直控 + hw setCs 降级** |
| CS 实现 | gpiod | gpio_dt_spec | spics_io_num | **GpioPin** |
| 异步机制 | kworker | RTIO | 事务队列+ISR | **per-bus Workqueue** |
| DMA | Core 映射 | 框架级 | 驱动透明 | **BSP 透明（transferOne 契约）** |
| 配置缓存 | 每消息重配 | 位域比较 | 预计算 | **lastCfgDev 指针** |
| 总线发现 | spi_bus_num | DT label | spi_host_device_t | **spi_bus_get(idx)** |
| 总线可见性 | Bus 不可见 | Bus=Device | Bus 不可见 | **Bus 不可见** |
| 异步回调 | per-message | signal | per-transaction | **per-device callback + slot** |
| 错误码 | errno | 负数 | ESP_ERR_* | **OM_ERR_* 通用码 + SPI 别名** |

---

## 8. 相关文件

| 文件 | 说明 |
|------|------|
| `lib/drivers/include/drivers/peripheral/spi/pal_spi_dev.h` | 公共头文件：数据结构、API 声明、常量/错误码 |
| `lib/drivers/src/peripheral/spi/hal_spi.c` | 框架实现：同步/异步路径、总线生命周期、CS 双路径 |
| `lib/drivers/docs/spi_design.md` | 本文档 — 架构设计说明 |
