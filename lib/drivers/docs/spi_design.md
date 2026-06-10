# Oh My Robot SPI 子系统架构设计

> 版本：v3.0
> 日期：2026-06-10
> 基于：[成熟开源 SPI 子系统调研](./spi_subsystem_survey.md)
> 项目约束：Device 模型、PAL 规范、DoubleBuf/Completion/OsalMutex/Workqueue 基础设施

---

## 1. 架构分层

```
+====================================================================+
|  Application / Upper-layer Driver                                  |
|  e.g. imu_icm20602.c, flash_w25qxx.c, tft_st7789.c                |
|  Operates through: device_read / device_write / device_ctrl         |
|                   hal_spi_transfer / hal_spi_transfer_async         |
|                   hal_spi_write_then_read / hal_spi_transaction_*   |
+====================================================================+
           |                              |
           | Standard Device API           | SPI-specific Extended API
           | (DevInterface ops)           | (hal_spi_* functions)
           v                              v
+====================================================================+
|  HalSpiDevice (Peripheral Device — registered as Device)           |
|  - parent: Device         (participates in global device list)      |
|  - bus: SpiBus*           (owning bus)                             |
|  - cfg: SpiDeviceCfg      (per-device mode/freq)                 |
|  - cs: GpioPin            (resolved from cfg.csSpec at attach)     |
|  - inTransaction: uint8   (manual CS state)                        |
|  Responsibilities: device→bus routing, CS management              |
+====================================================================+
           |
           | Exactly one owning SpiBus per device
           v
+====================================================================+
|  SpiBus (Bus Controller — NOT registered as Device)                |
|  - hwPrivate / interface / lock / cachedDevice                    |
|  - txDbuf / transferDone / busy / asyncWq (自建 workqueue)       |
|  - suspendedCount / deviceCount                                    |
|  Responsibilities: Mutex, reconfig cache, DoubleBuf, Completion    |
|                    workqueue 序列化, suspend                       |
+====================================================================+
           |
           | SpiInterface function pointer table
           v
+====================================================================+
|  SpiInterface (BSP Hardware Abstraction — platform-independent)     |
|  - configure(bus, cfg)     - transfer(bus, tx, rx, len)             |
|  - control(bus, cmd, arg)  - cs_control(bus, csId, level) [hw CS] |
+====================================================================+
           |
           | BSP implementation (DMA 完全透明，框架无感知)
           v
+====================================================================+
|  BSP Private (Platform-specific, NOT visible to framework)          |
|  e.g. SpiBusPrivate { SPI_HW_Handle*, DMA_HW_Handle* }              |
|  Implementation: xxx_spi_configure / transfer / control / cs_control |
|  CS GPIO 通过 gpio_pin_write() 操作，BSP 无感知                    |
|  ISR: SPI_IRQHandler → HAL_SPI_IRQHandler → hal_spi_isr()            |
|  DMA 通道号 / IRQ / DMA 句柄 → 全部在 BSP Private 中               |
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
            │ inTransaction│ │ inTransaction│ │   │          │
            │ suspended    │ │ suspended    │ │   │          │
            └──────┬───────┘ └──────┬───────┘ │   │          │
                   │                │         │   │          │
                   │  所有设备共享同一条总线     │   │          │
                   │                │         │   │          │
                   ▼                ▼         ▼   ▼          │
            ┌──────────────────────────────────────────────┐ │
            │              SpiBus (总线控制器)              │ │
            │  ┌─────────────────────────────────────────┐ │ │
            │  │  interface: SpiInterface                 │ │◄┘
            │  │    → configure / transfer / control      │ │
            │  │    → cs_control (硬件 CS 降级路径)       │ │
            │  ├─────────────────────────────────────────┤ │
            │  │  lock: OsalMutex*   (总线互斥)           │ │
            │  │  cachedDevice       (配置缓存, 指针比较) │ │
            │  ├─────────────────────────────────────────┤ │
            │  │  txDbuf: DoubleBuf   (TX 乒乓缓冲)       │ │
            │  │  prefillTarget       (Peek 预填充目标)   │ │
            │  │  prefillEpoch        (防悬垂 epoch)      │ │
            │  ├─────────────────────────────────────────┤ │
            │  │  transferDone: Completion (传输完成信号) │ │
            │  │  lastStatus / lastTransferred (ISR→线程) │ │
            │  │  busy: volatile      (ISR 门禁)          │ │
            │  ├─────────────────────────────────────────┤ │
            │  │  asyncWq: Workqueue  (异步请求序列化)    │ │
            │  │  asyncEpoch          (请求递增序号)      │ │
            │  ├─────────────────────────────────────────┤ │
            │  │  suspendedCount / deviceCount            │ │
            │  └─────────────────────────────────────────┘ │
            │  hwPrivate → BSP 私有数据 (不透明指针)       │
            └──────────────────────────────────────────────┘
```

**关键关系**：
- HalSpiDevice **引用** SpiBus（多对一），不拥有
- SpiBus **拥有** OsalMutex、Completion、DoubleBuf、Workqueue
- SpiBus 通过 `interface` 函数表 **委托** BSP 执行硬件操作
- SpiBus 通过 `hwPrivate` 不透明指针持有 BSP 私有数据
- Device 父类通过全局链表 `parent.list` 注册到设备表

### 2.2 运行时交互：同步传输路径

```
调用者线程                               hal_spi_isr (ISR 上下文)
══════════                               ══════════════════════════

hal_spi_transfer(dev, xfer)
  │
  ├─ spi_bus_lock(bus)         ─── 阻塞获取互斥锁
  ├─ suspend / busy / 事务 检查
  ├─ spi_bus_ensure_configured ─── cachedDevice 指针比较，不匹配则调 BSP configure()
  ├─ spi_cs_assert(dev)        ─── GPIO: gpio_pin_write  /  HW: cs_control
  │
  ├─ spi_do_transfer()
  │   ├─ interface->transfer() ─── 启动硬件 (非阻塞，立即返回)
  │   ├─ busy = 1
  │   ├─ completion_wait()     ─── 阻塞等待 ──┐
  │   │                                       │  DMA/INT 完成
  │   │                                       ├─► hal_spi_isr(bus, status, n)
  │   │                                       │     if (!busy) return  ← 门禁
  │   │                                       │     lastStatus = status
  │   │                                       │     lastTransferred = n
  │   │                                       │     completion_done()  ← 唤醒
  │   ├─ 醒来 (或超时)         ←── 恢复执行 ──┘
  │   ├─ [超时路径] irq_lock → busy=0 → drain completion
  │   └─ 返回 lastStatus, xfer→transferred
  │
  ├─ spi_cs_deassert(dev)
  ├─ spi_bus_unlock(bus)
  └─ return OM_OK / ERR_*
```

### 2.3 运行时交互：异步传输路径

```
调用者线程                  Workqueue Worker 线程                    hal_spi_isr (ISR 上下文)
══════════                  ════════════════════                    ══════════════════════════

hal_spi_transfer_async
  (dev, req)
  │
  ├─ 参数校验 + DoubleBuf
  │   容量检查
  ├─ req→dev     = dev
  ├─ req→epoch   = ++asyncEpoch
  ├─ work_init(req, func)
  └─ workqueue_enqueue ───────► 入队 pending 链表, sem_post
  return OM_OK
                               │
                               ├─ sem_wait 唤醒
                               ├─ spi_bus_lock(bus)
                               ├─ suspend 重检
                               ├─ ensure_configured
                               ├─ spi_cs_assert(dev)
                               │
                               ├─ DoubleBuf 数据就位:
                               │   [预填命中] dbuf_swap (零拷贝)
                               │   [未命中]   dbuf_flush → memcpy → commit
                               │
                               ├─ interface->transfer() ─── 启动 DMA ──┐
                               ├─ busy = 1                             │
                               ├─ spi_bus_unlock(bus)                  │
                               │                                       │
                               ├─ Peek 预填充:                         │
                               │   irq_lock                            │
                               │   if (pending 非空)                   │
                               │     memcpy → dbuf_mark_written        │
                               │     记录 prefillTarget / prefillEpoch │
                               │   irq_unlock                          │
                               │                                       │
                               ├─ completion_wait() ─── 阻塞等待 ──┐   │
                               │                                   │   │  DMA 完成
                               │                                   │   ├─► hal_spi_isr()
                               │                                   │   │     busy 门禁
                               │                                   │   │     completion_done
                               │   [超时] irq_lock→busy=0          │   │
                               │          drain completion          │   │
                               │          lock→cs_deassert→unlock   │   │
                               │          asyncCb(TIMEOUT)          │   │
                               │                                   │   │
                               │   [成功] ←── 醒来 ────────────────┘   │
                               │   req→status = lastStatus             │
                               │   req→transferred = lastTransferred   │
                               │   lock → cs_deassert                  │
                               │   busy=0, dbuf_consume                │
                               │   unlock                              │
                               │   asyncCb(param, req)                 │
                               │                                       │
                               └─ worker 循环取下一请求
```

**Peek 预填充协议**（核心优化）:

```
worker 处理 req[N]                         worker 处理 req[N+1]
══════════════════                         ══════════════════════

transfer(DMA 从 read-page 读)                dbuf_is_pending?
  │                                           req[N+1] == prefillTarget?
  │  unlock → Peek:                           req[N+1].epoch == prefillEpoch?
  │  查看 pending 链表                              │
  │  找到 req[N+1]                            [全部匹配] dbuf_swap (零拷贝!)
  │  memcpy(write-page,                                   bus→prefillTarget = NULL
  │         req[N+1].tx)                        [不匹配]   dbuf_flush
  │  dbuf_mark_written                                   memcpy + commit
  │  prefillTarget = req[N+1]                            bus→prefillTarget = NULL
  │                                           transfer(DMA 从 read-page 读)
  │  completion_wait(阻塞)                         │
  │       │                                        │ unlock → Peek ...
  │  ISR 唤醒                                       │ completion_wait(阻塞)
  │  dbuf_consume (释放旧 read-page)                │
  │  回调 req[N].asyncCb                             │
  │  worker 内循环取出 req[N+1] ─────────────────────┘
```

---

### 2.1 SpiBus — 总线控制器

```c
/* 调用者分配（嵌入驱动上下文或静态），填充 tx/rx/len 后传入 transfer_async */
typedef struct SpiAsyncRequest {
    HalSpiDevice    *dev;              /* 所属设备（框架填充）          */
    const uint8_t   *tx;               /* 发送缓冲区（调用者填充）      */
    uint8_t         *rx;               /* 接收缓冲区（调用者填充）      */
    size_t           len;              /* 传输长度（调用者填充）        */
    size_t           transferred;      /* 实际传输字节数（ISR→bus→worker 填入） */
    OmRet            status;           /* 传输结果（ISR→bus→worker 填入）       */
    void           (*asyncCb)(void *param, struct SpiAsyncRequest *req); /* 完成回调  */
    void            *asyncParam;       /* 回调参数                     */
    Work             work;             /* workqueue 调度单元（含链表节点）*/
} SpiAsyncRequest;

/* 同步传输描述符（栈上临时构造，用完即弃）
 *
 * 字段语义：
 *  - hal_spi_transfer（全双工）: txLen == rxLen（强制），txLen 为双向传输字节数
 *  - hal_spi_write_then_read:     txLen = 命令字节数，rxLen = 读取字节数
 *  - hal_spi_transaction_transfer:用法同 hal_spi_transfer
 *  - transferred（输出）: 框架填充实际传输总字节数
 */
typedef struct SpiXfer {
    const uint8_t *txBuf;       /* 发送缓冲区（NULL = 发 dummy） */
    uint8_t       *rxBuf;       /* 接收缓冲区（NULL = 丢弃）     */
    size_t         txLen;       /* 发送字节数                    */
    size_t         rxLen;       /* 接收字节数                    */
    size_t         transferred; /* 硬件实际传输总字节数（框架填充） */
} SpiXfer;

typedef struct SpiBus {
    /* ---- BSP 接口 ---- */
    void            *hwPrivate;        /* BSP 私有数据（不透明指针）    */
    SpiInterface    *interface;        /* 硬件操作函数表                */

    /* ---- 并发控制 ---- */
    OsalMutex       *lock;             /* 总线互斥锁（非递归）         */

    /* ---- 配置缓存 ---- */
    HalSpiDevice    *cachedDevice;     /* 当前配置对应的设备指针        */

    /* ---- 双缓冲（TX-only，dbuf_page_size > 0 时启用）---- */
    DoubleBuf        txDbuf;           /* 乒乓双缓冲（init 时依 dbuf_page_size 决定是否启用）*/

    /* ---- 同步传输完成信号 ---- */
    Completion       transferDone;     /* 硬件传输完成时置位            */
    size_t           lastTransferred;  /* ISR 写入，同步路径读取        */
    OmRet            lastStatus;       /* ISR 写入的传输结果            */

    /* ---- 硬件传输状态 ---- */
    uint8_t          busy;             /* 硬件传输进行中（poll/IRQ/DMA）*/

    /* ---- Peek 预填充追踪 ---- */
    SpiAsyncRequest *prefillTarget;    /* 预填充目标请求（NULL=无预填） */

    /* ---- 异步 work 调度（框架自建 per-bus）---- */
    Workqueue        asyncWq;          /* workqueue 引擎（其内部 pending 队列 = 请求队列） */

    /* ---- 总线级 suspend ref-counting ---- */
    uint8_t          suspendedCount;   /* 已挂起的从设备数量            */
    uint8_t          deviceCount;      /* 总线上挂载的设备总数          */
} SpiBus;
```

### 2.2 HalSpiDevice — 从设备（注册为 Device）

```c
typedef struct HalSpiDevice {
    Device            parent;          /* 设备父类（标准 Device 模型）  */
    SpiBus           *bus;             /* 所属 SPI 总线               */
    SpiDeviceCfg      cfg;             /* 设备静态配置                 */
    GpioPin           cs;              /* CS 引脚句柄（attach 时解析） */

    uint8_t           inTransaction;   /* 是否处于手动 CS 事务中       */
    uint8_t           suspended;       /* 是否已挂起                  */
} HalSpiDevice;
```

### 2.3 SpiDeviceCfg — 从设备配置（平台无关）

```c
typedef struct SpiDeviceCfg {
    /* CS 引脚 — 复用 GPIO 子系统（controller==NULL 表示硬件 CS） */
    GpioPinSpec     csSpec;             /* 编译时引脚描述符（含 ACTIVE_LOW 标志） */

    /* SPI 模式 */
    uint8_t         mode;               /* SPI_MODE_0..3 (CPOL/CPHA 组合)   */
    uint32_t        maxHz;              /* 最大 SCLK 频率 (Hz)          */
    uint8_t         dataWidth;          /* SPI_DATA_WIDTH_8/16          */
    uint8_t         bitOrder;           /* SPI_MSB_FIRST / SPI_LSB_FIRST*/
} SpiDeviceCfg;
```

### 2.4 SpiInterface — BSP 硬件操作函数表

```c
typedef struct SpiInterface {
    /**
     * @brief 配置 SPI 控制器寄存器（mode/波特率/数据宽度/位序）
     */
    OmRet   (*configure)(SpiBus *bus, const SpiDeviceCfg *cfg);

    /**
     * @brief 发起 SPI 全双工传输（非阻塞）
     * @param tx   发送缓冲区（NULL = 发 dummy 0xFF）
     * @param rx   接收缓冲区（NULL = 丢弃 MISO）
     * @param len  传输字节数
     * @return     OM_OK 启动成功，OM_ERR_* 启动失败
     * @note  BSP 内部根据硬件能力选择 poll/IRQ/DMA 路径
     * @note  轮询模式内部死等完成后调 hal_spi_isr()
     * @note  DMA/INT 模式启动后立即返回，ISR 回调 hal_spi_isr()
     * @note  DMA 对框架完全透明，通道/IRQ/句柄全在 BSP private
     * @note  实际传输字节数统一由 hal_spi_isr 的 transferred 参数上报
     */
    OmRet   (*transfer)(SpiBus *bus, const uint8_t *tx, uint8_t *rx, size_t len);

    /**
     * @brief 通用控制接口（SPI_CMD_ABORT 等）
     */
    OmRet   (*control)(SpiBus *bus, uint32_t cmd, void *arg);

    /**
     * @brief 硬件 CS 电平控制（仅硬件 CS 模式，GPIO CS 由框架直接调用 gpio_pin_write）
     * @param csId  CS 线编号（0/1/2...，对应 SPI 外设硬件 CS 选择寄存器）
     * @param level 0=选中(assert), 1=释放(deassert)
     * @note  当 dev->cfg.csSpec.controller == NULL 时才走此路径
     */
    OmRet   (*cs_control)(SpiBus *bus, uint8_t csId, uint8_t level);

} SpiInterface;
```

### 2.5 枚举与宏定义

```c
/* SPI 错误码 */
typedef enum SpiErrCode {
    ERR_SPI_TRANSFER_TIMEOUT = 1U,  /* 传输超时                       */
    ERR_SPI_BUS_HW_ERROR,           /* MODF / OVR / CRC                 */
    ERR_SPI_CS_CONFLICT,            /* 事务内调用自动 CS API            */
    ERR_SPI_BUSY,                   /* 硬件传输进行中                  */
    ERR_SPI_DEV_SUSPENDED,          /* 设备已挂起                       */
} SpiErrCode;

/* SPI 控制命令 */
#define SPI_CMD_SET_CFG         (DEVICE_CMD_CFG)         /* 0x01 */
#define SPI_CMD_GET_CFG         (0x10U)
#define SPI_CMD_SUSPEND         (DEVICE_CMD_SUSPEND)     /* 0x02 */
#define SPI_CMD_RESUME          (DEVICE_CMD_RESUME)      /* 0x03 */
#define SPI_CMD_ABORT           (0x11U)

/* 常量 */
#define SPI_MODE_0              (0U)    /* CPOL=0, CPHA=0 */
#define SPI_MODE_1              (1U)    /* CPOL=0, CPHA=1 */
#define SPI_MODE_2              (2U)    /* CPOL=1, CPHA=0 */
#define SPI_MODE_3              (3U)    /* CPOL=1, CPHA=1 */
#define SPI_MSB_FIRST           (0U)
#define SPI_LSB_FIRST           (1U)
#define SPI_DATA_WIDTH_8        (8U)
#define SPI_DATA_WIDTH_16       (16U)
```

---

## 3. 关键设计决策

| # | 决策 | 选择 | 参考 | 理由 |
|---|------|------|------|------|
| 1 | **传输粒度** | 单次调用 + write_then_read + 手动CS事务 | Linux/ESP-IDF | 90%场景是寄存器读写；多段用手动事务；未来 QSPI 扩展时再设计多段消息链 |
| 2 | **CS 管理** | 三层：自动 / 手动事务；GPIO CS 为主 + 硬件 CS 降级 | Linux/Zephyr | GPIO 子系统复用，ACTIVE_LOW 自动反转，硬件 CS 走 cs_control 回调 |
| — | **CS 实现** | GpioPin 复用 GPIO 子系统 | Linux gpiod / Zephyr gpio_dt_spec | 删除 csControl 裸指针，BSP 少一个函数；硬件 CS 时 csSpec.controller==NULL 走降级路径 |
| 3 | **异步调度** | Per-bus 单 worker workqueue（框架自建）| Linux per-controller kthread | ISR 极薄仅清标志+入队；框架内部化，调用者无感知 |
| 4 | **双缓冲** | 框架内部 TX DoubleBuf，dbuf_page_size 控制启停 | Linux/ESP-IDF | dbuf_page_size>0 启用并行优化，=0 跳过且不分配内存；transfer_async 始终可用，DoubleBuf 非公开 API |
| 5 | **DMA 集成** | BSP Private 完全透明 | ESP-IDF/NuttX | DMA 通道/IRQ/句柄全在 BSP；框架只管 busy 标志 |
| 6 | **错误模型** | 四级分级：参数→竞争→超时→硬件 | OM OmRet 体系 | 逐步升级，异步错误通过 per-request 回调上报 |
| 7 | **配置缓存** | cachedDevice 指针比较 | RT-Thread | 单指令，无哈希，IMU 1kHz 无效重配全部消除 |
| 8 | **超时** | 动态计算 = (len × 8000 / maxHz) + overhead | Linux/ESP-IDF | 取消静态 transferTimeoutMs；每次传输根据实际长度和时钟自动计算，短传输短超时、长传输长超时 |
| 9 | **低功耗** | SpiBus 引用计数 + 自动外设时钟控制 | — | 仅全部设备 suspend 时才关 SPI 外设时钟 |
| 10 | **SpiBus 注册** | 不注册为 Device | Linux/ESP-IDF | 用户永远操作从设备，总线是内部基础设施 |
| 11 | **异步回调** | per-request 回调 + workqueue 序列化 | Linux per-message 回调 | workqueue_enqueue 自动串行；回调随请求携带，无需二次分发 |

---

## 4. API 函数签名

### 4.1 总线生命周期

```c
OmRet hal_spi_bus_register(SpiBus *bus, void *hwPrivate,
                          SpiInterface *interface,
                          size_t dbuf_page_size);
/* dbuf_page_size > 0: 框架内部分配 2×dbuf_page_size 并初始化 txDbuf，启用 CPU/DMA 并行
 * dbuf_page_size == 0: 跳过 DoubleBuf 分配，transfer_async 仍可用（无并行优化）
 * 内部自动创建 workqueue（单 worker，per-bus），无需调用者提供 */
void   hal_spi_bus_deinit(SpiBus *bus);
OmRet  hal_spi_device_attach(SpiBus *bus, HalSpiDevice *dev,
                             const char *name, const SpiDeviceCfg *cfg);
void   hal_spi_device_detach(HalSpiDevice *dev);
```

### 4.2 标准 Device 接口（DevInterface）

```c
OmRet  hal_spi_dev_init(Device *dev);          /* NOP（资源已在 attach 时分配）    */
OmRet  hal_spi_dev_open(Device *dev, uint32_t oparam);   /* NOP（SPI 无 open/close 语义） */
OmRet  hal_spi_dev_close(Device *dev);                  /* NOP                        */

/* read: Zephyr 风格"主收"——发 dummy 接收 len 字节到 data。
 * ctrl_info != NULL 时视为 const uint8_t*，先发其所指 1 字节命令前缀；
 * ctrl_info == NULL 时纯发 dummy 接收。多用单寄存器读取场景。      */
size_t hal_spi_dev_read(Device *dev, void *ctrl_info, void *data, size_t len);

/* write: Zephyr 风格"主发"——发送 data 的 len 字节，丢弃接收。
 * ctrl_info != NULL 时视为 const uint8_t*，先发其所指 1 字节命令前缀；
 * ctrl_info == NULL 时纯发数据。多用单寄存器写入场景。            */
size_t hal_spi_dev_write(Device *dev, void *ctrl_info, void *data, size_t len);

/* control: SET_CFG / GET_CFG / SUSPEND / RESUME / ABORT */
OmRet  hal_spi_dev_control(Device *dev, size_t cmd, void *arg);
```

### 4.3 SPI 扩展 API

```c
/* 同步全双工传输（自动 CS），结果写入 xfer->transferred */
OmRet  hal_spi_transfer(HalSpiDevice *dev, SpiXfer *xfer);

/* 一问一答（单 CS 周期内先写命令再读数据），结果写入 xfer->transferred */
OmRet  hal_spi_write_then_read(HalSpiDevice *dev, SpiXfer *xfer);

/* 异步传输（调用者提供 SpiAsyncRequest，携带 per-request 回调） */
OmRet  hal_spi_transfer_async(HalSpiDevice *dev, SpiAsyncRequest *req);
/* req 由调用者分配，tx/rx/len/asyncCb/asyncParam 由调用者填充，req 需保持存活直到 asyncCb 被调用 */

/* 手动 CS 事务（Flash 多段读取） */
OmRet  hal_spi_transaction_begin(HalSpiDevice *dev);
OmRet  hal_spi_transaction_transfer(HalSpiDevice *dev, SpiXfer *xfer);
OmRet  hal_spi_transaction_end(HalSpiDevice *dev);

/* 低功耗 */
OmRet  hal_spi_device_suspend(HalSpiDevice *dev);
OmRet  hal_spi_device_resume(HalSpiDevice *dev);
```

### 4.4 框架 ISR 入口

ISR 内由 BSP 调用，传入传输结果：

```c
void hal_spi_isr(SpiBus *bus, OmRet status, size_t transferred);
/* status:    OM_OK 成功 / OM_ERR_TIMEOUT / OM_ERR_HARDWARE */
/* 先检查 busy（传输是否已被放弃），再写状态 + completion_done */
```

---

## 5. 关键流程

### CS 双路径内部辅助

框架内部通过 `spi_cs_assert` / `spi_cs_deassert` 内联函数封装 CS 操作，双路径对调用者透明：

```c
/* 内部辅助 — GPIO CS (csSpec.controller != NULL) 与硬件 CS (controller == NULL) 双路径 */
static inline void spi_cs_assert(HalSpiDevice *dev) {
    if (gpio_pin_valid(dev->cs))
        gpio_pin_write(dev->cs, 0);                      // GPIO: 逻辑 0 = assert
    else if (dev->bus->interface->cs_control)
        dev->bus->interface->cs_control(dev->bus, dev->cfg.csSpec.offset, 0); // HW CS
}

static inline void spi_cs_deassert(HalSpiDevice *dev) {
    if (gpio_pin_valid(dev->cs))
        gpio_pin_write(dev->cs, 1);                      // GPIO: 逻辑 1 = deassert
    else if (dev->bus->interface->cs_control)
        dev->bus->interface->cs_control(dev->bus, dev->cfg.csSpec.offset, 1);
}
```

GPIO 路径下 `GPIO_FLAG_ACTIVE_LOW` 由 GPIO 子系统自动反转物理电平，框架只关心逻辑电平（0=assert, 1=deassert）。

### 5.1 同步传输

```
hal_spi_transfer(dev, xfer)
  ├─ 参数校验（dev/xfer/txLen==rxLen 拒绝不等，txBuf或rxBuf非NULL）
  ├─ if (dev->suspended) → return ERR_SPI_DEV_SUSPENDED
  ├─ 拒绝嵌套（inTransaction → return ERR_SPI_CS_CONFLICT）
  ├─ osal_mutex_lock(bus->lock) — 阻塞获取
  ├─ busy 检查（异步传输进行中 → unlock, return ERR_SPI_BUSY）
  ├─ spi_bus_ensure_configured(bus, dev)
  │    └─ cachedDevice == dev? skip : configure() + 更新 cache
  ├─ spi_cs_assert(dev)  — GPIO: gpio_pin_write(cs,0); HW: cs_control(bus,csId,0)
  ├─ transfer(bus, xfer->txBuf, xfer->rxBuf, xfer->txLen) — 启动硬件（非阻塞）
  │    └─ ret != OM_OK → spi_cs_deassert, unlock, return ret
  ├─ busy = 1
  ├─ 动态计算超时 = (xfer->txLen * 8000 / dev->cfg.maxHz) + OVERHEAD_MS
  ├─ completion_wait(&bus->transferDone, computed_timeout)
  │    ├─ timeout → busy=0; completion_wait(&bus->transferDone, 0);  // drain 残余
  │    │             spi_cs_deassert(dev); unlock; return ERR_SPI_TRANSFER_TIMEOUT
  │    └─ ISR: hal_spi_isr(bus, OK, transferred)
  │         if (!bus->busy) return;    // 被超时放弃
  │         bus->lastTransferred = transferred; bus->lastStatus = OK
  │         completion_done(&bus->transferDone)
  │         → 唤醒后 busy=0, 读取 bus->lastTransferred
  ├─ spi_cs_deassert(dev)
  ├─ xfer->transferred = bus->lastTransferred
  └─ osal_mutex_unlock(bus->lock)
       return bus->lastStatus
```

### 5.2 异步传输（调用者提供 SpiAsyncRequest）

```
=== 发起异步请求 ===
hal_spi_transfer_async(dev, req)  // req 由调用者分配，tx/rx/len 已填充
  ├─ 参数校验（dev/req/asyncCb非NULL, req->len>0, tx或rx非NULL）
  ├─ if (dev->suspended) → return ERR_SPI_DEV_SUSPENDED
  ├─ if (DoubleBuf 已启用且 req->len > dbuf_page_size) → return OM_ERR_PARAM
  ├─ req->dev = dev; req->status = OM_OK
  ├─ work_init(&req->work, spi_async_worker_func, req)
  └─ workqueue_enqueue(&bus->asyncWq, &req->work) → return OM_OK

=== Workqueue Worker（任务上下文） ===
spi_async_worker_func(req):
  ├─ osal_mutex_lock(bus->lock)
  ├─ if (req->dev->suspended) → unlock; req->status=ERR_SPI_DEV_SUSPENDED; goto 回调
  ├─ spi_bus_ensure_configured(bus, req->dev)
  ├─ spi_cs_assert(req->dev)

  ├─== DoubleBuf 数据就位 ==
  │   if (DoubleBuf 启用) {
  │       if (dbuf_is_pending(&bus->txDbuf) && req == bus->prefillTarget) {
  │           // 上轮预填充目标匹配 → 仅翻转（零拷贝）
  │           dbuf_swap(&bus->txDbuf);
  │           bus->prefillTarget = NULL;
  │       } else {
  │           // 无预填 或 目标被 cancel → CPU 拷贝 + 翻转
  │           if (dbuf_is_pending(&bus->txDbuf)) dbuf_flush(&bus->txDbuf);  // 丢弃失效预填
  │           memcpy(dbuf_get_write_ptr(&bus->txDbuf), req->tx, req->len);
  │           dbuf_commit(&bus->txDbuf, req->len);
  │           bus->prefillTarget = NULL;
  │       }
  │       uint8_t *rp = dbuf_get_read_ptr(&bus->txDbuf, NULL);
  │       ret = transfer(bus, rp, req->rx, req->len);   // DMA 从读 page 启动
  │   } else {
  │       ret = transfer(bus, req->tx, req->rx, req->len); // 直接 DMA
  │   }

  ├─ ret != OM_OK → spi_cs_deassert, unlock, req->status=ret, goto 回调
  ├─ busy = 1
  ├─ osal_mutex_unlock(bus->lock)

  ├─== Peek 预填充（CPU/DMA 时间并行） ==
  │   if (DoubleBuf 启用) {
  │       OsalIrqIsrState k; osal_irq_lock(&k);
  │       if (!list_empty(&bus->asyncWq.pending)) {
  │           SpiAsyncRequest *next =
  │               list_first_entry(&bus->asyncWq.pending, SpiAsyncRequest, work.node);
  │           // CPU 填充下一个 dbuf 写 page，与当前 DMA 并行
  │           memcpy(dbuf_get_write_ptr(&bus->txDbuf), next->tx, next->len);
  │           dbuf_mark_written(&bus->txDbuf, next->len);
  │           bus->prefillTarget = next;    // 记录预填充目标
  │       }
  │       osal_irq_unlock(k);
  │   }

  ├─ 动态计算超时 = (req->len * 8000 / req->dev->cfg.maxHz) + OVERHEAD_MS
  ├─ completion_wait(&bus->transferDone, computed_timeout)
  │    ├─ timeout → osal_irq_lock; busy=0;               // ISR gate: 阻止迟到 ISR 投递
  │    │             completion_wait(&bus->transferDone, 0); // drain 残留在途信号
  │    │             osal_irq_unlock;
  │    │             osal_mutex_lock; spi_cs_deassert; unlock;
  │    │             req->status=ERR_SPI_TRANSFER_TIMEOUT; goto 回调
  │    └─ ISR: hal_spi_isr(bus, OK/ERR_HARDWARE, transferred)
  │         → if (!bus->busy) return;    // 被超时放弃
  │         → completion_done(&bus->transferDone)
  │         → func 醒来: req->status = bus->lastStatus
  │                       req->transferred = bus->lastTransferred
  ├─ osal_mutex_lock(bus->lock)
  ├─ spi_cs_deassert(req->dev)
  ├─ busy = 0
  ├─ if (DoubleBuf 启用) dbuf_consume(&bus->txDbuf);  // 释放刚传输完的读 page
  ├─ osal_mutex_unlock(bus->lock)
  ├─ 回调: req->asyncCb(req->asyncParam, req)  // req 内含 status/transferred
  └─ return (Worker 回到 sem_wait，下一个请求由 workqueue 序列出队)

=== DMA 完成 ISR（TC 或 Error） ===
BSP ISR:
  ├─ hal_spi_isr(bus, status, transferred)
  │    ├─ if (!bus->busy) return;  // 传输已被放弃（超时），不投递残余信号
  │    ├─ bus->lastTransferred = transferred; bus->lastStatus = status
  │    └─ completion_done(&bus->transferDone)               // 唤醒阻塞的 func
  │    (无 workqueue_enqueue — func 在 completion_wait 中阻塞等待)
```

### 5.3 异步连续投递示例

```
// 用户分配两个 SpiAsyncRequest（嵌入在驱动上下文中），各带自己的回调
imu1.tx = cmd1; imu1.rx = buf1; imu1.len = 32;
imu1.asyncCb = imu_data_callback; imu1.asyncParam = &imu_ctx;
imu2.tx = cmd2; imu2.rx = buf2; imu2.len = 32;
imu2.asyncCb = imu_data_callback; imu2.asyncParam = &imu_ctx;

时刻T1: hal_spi_transfer_async(imuDev, &imu1)
  → workqueue_enqueue(&bus->asyncWq, &imu1.work)

时刻T2: hal_spi_transfer_async(imuDev, &imu2)
  → workqueue_enqueue(&bus->asyncWq, &imu2.work)   // imu2 排队在工作队列中

时刻T2a: Worker 取出 imu1 → transfer() → completion_wait(阻塞)

时刻T3: DMA 完成 ISR
  → hal_spi_isr(bus, OK, 32): completion_done → func 醒来

时刻T4: func 继续: cs_deassert → 回调 imu1.asyncCb(imu1.asyncParam, &imu1) → return
  → Worker 内循环自动取出 imu2 → transfer() → completion_wait(阻塞)

时刻T5: imu2 DMA 完成
  → ISR → completion_done → func 醒来 → 回调 imu2.asyncCb(imu2.asyncParam, &imu2) → return
  → 队列空: worker 回到 sem_wait
```

### 5.4 手动 CS 事务（Flash 多段读取）

```
hal_spi_transaction_begin(dev)
  ├─ lock → busy 检查 → ensure_configured
  ├─ spi_cs_assert(dev) → inTransaction=1
  └─ return OM_OK (锁持有，CS保持低)

// 用户侧多次调用（CS 保持低，锁保持持有，内 ASSERT(dev->inTransaction)）:
SpiXfer cmdXfer = {.txBuf=cmd, .txLen=4, .rxBuf=NULL, .rxLen=0};
hal_spi_transaction_transfer(dev, &cmdXfer);            // 写命令，cmdXfer.transferred=4
SpiXfer dataXfer = {.txBuf=NULL, .txLen=0, .rxBuf=data, .rxLen=256};
hal_spi_transaction_transfer(dev, &dataXfer);           // 读数据，dataXfer.transferred=256

hal_spi_transaction_end(dev)
  ├─ spi_cs_deassert(dev) → inTransaction=0
  └─ unlock
```

### 5.5 设备切换 + 配置缓存

```
IMU 完成 → Flash 接管：
1. IMU worker 回调完成 → unlock
2. Flash 线程 osal_mutex_lock → 获取锁
3. ensure_configured: cachedDevice==imuDev != flashDev
   → configure(bus, &flashDev->cfg) — 重配 mode/freq
   → cachedDevice = flashDev
4. (IMU CS 已在 IMU worker 中 deassert，无需操作)
5. spi_cs_assert(flashDev)
6. 传输 → spi_cs_deassert(flashDev) → unlock
```

### 5.6 DoubleBuf LCD 帧推送（Peek 预填充实现 CPU/DMA 并行）

用户只需连续调用 `transfer_async`；worker func 在阻塞前自动预取下一个请求填充 dbuf：
```
Frame 1 (初次，写 page 无预填数据):
  lcdReq1.tx = fb1; lcdReq1.len = FB_SIZE;
  hal_spi_transfer_async(lcd, &lcdReq1)
    → workqueue_enqueue → worker 取出:
      dbuf_is_pending(&txDbuf) && req == bus->prefillTarget? → false  // 无预填
      memcpy(dbuf_get_write_ptr, fb1, FB_SIZE)                        // CPU 填充 P0
      dbuf_commit(&txDbuf, FB_SIZE)                                   // flip → P0 可读
      transfer(bus, dbuf_get_read_ptr, NULL, FB_SIZE)                 // kick DMA from P0
      (之间: Frame 2 被应用入队 → workqueue_enqueue)
      Peek: list_first_entry(&asyncWq.pending) → lcdReq2
      memcpy(dbuf_get_write_ptr, fb2, FB_SIZE)                        // CPU 填充 P1（与 DMA P0 并行！）
      dbuf_mark_written(&txDbuf, FB_SIZE)                             // 标记 P1 就绪但不翻转
      bus->prefillTarget = &lcdReq2                                   // 记录预填目标
      completion_wait(&bus->transferDone)                             // ← worker 阻塞

DMA ISR (Frame 1 完成):
  → hal_spi_isr(bus, OK, FB_SIZE): completion_done → worker 醒来
  → dbuf_consume(&txDbuf)                                            // 释放 P0
  → 回调 lcdReq1.asyncCb(lcdReq1.asyncParam, &lcdReq1)               // Frame 1 完成
  → return → worker 内循环取出 lcdReq2:

Frame 2 (prefillTarget 匹配):
      dbuf_is_pending(&txDbuf) && req == bus->prefillTarget → true   // P1 就绪 + 目标匹配
      dbuf_swap(&txDbuf)                                              // 翻转 → P1 可读（零拷贝！）
      bus->prefillTarget = NULL
      transfer(bus, dbuf_get_read_ptr, NULL, FB_SIZE)                 // 立即 kick DMA from P1
      (之间: Frame 3 入队)
      Peek: memcpy(dbuf_get_write_ptr, fb3, FB_SIZE)                  // CPU 填充 P0（与 DMA P1 并行！）
      dbuf_mark_written(&txDbuf, FB_SIZE)
      bus->prefillTarget = &lcdReq3
      completion_wait(...)

DMA ISR (Frame 2 完成):
  → 醒来 → dbuf_consume → 回调 → return
  → 队列空（Frame 3 未到）: worker 回到 sem_wait

后续帧以此类推。若 lcdReq2 被 cancel，worker 取出 lcdReq3 时:
  prefillTarget (&lcdReq2) != &lcdReq3 → dbuf_flush (丢弃失效预填) → memcpy + commit 正常路径
```

### 5.7 低功耗挂起/恢复

```
hal_spi_device_suspend(dev):
  ├─ osal_mutex_lock(bus->lock)
  ├─ if (dev->suspended) → unlock, return  // 幂等
  ├─ dev->suspended = true; bus->suspendedCount++
  ├─ if (bus->suspendedCount == bus->deviceCount): bus->hw_clock_disable()
  └─ osal_mutex_unlock(bus->lock)

hal_spi_device_resume(dev):
  ├─ osal_mutex_lock(bus->lock)
  ├─ if (!dev->suspended) → unlock, return  // 幂等
  ├─ if (bus->suspendedCount == bus->deviceCount): bus->hw_clock_enable()
  ├─ bus->suspendedCount--; dev->suspended = false
  └─ osal_mutex_unlock(bus->lock)
```

---

## 6. 设计决策对照开源方案

| 决策 | Linux | Zephyr | ESP-IDF | NuttX | RT-Thread | **OM 选择** |
|------|:---:|:---:|:---:|:---:|:---:|:---:|
| 传输粒度 | message 队列 | spi_buf_set | transaction 队列 | 单次调用 | message 链 | **两阶 + 手动事务** |
| CS 管理 | Core 统一 | spi_context | 硬件自动 | 驱动自行 | Core 统一 | **GPIO 子系统 + hw CS 降级** |
| CS 实现 | gpiod | gpio_dt_spec | spics_io_num | LH 内部 | rt_pin | **GpioPin** |
| 异步机制 | kworker | k_poll/RTIO | 事务队列+ISR | 无 | 无 | **Workqueue** |
| DMA | Core 映射 | 框架级 | 驱动透明 | LH 内部 | xfer()内 | **BSP 透明** |
| 配置缓存 | 每消息重配 | 位域比较 | 预计算 | 无 | owner 比较 | **cachedDevice 指针** |
| 设备可见性 | Bus 不可见 | Bus=Device | Bus 不可见 | Bus=Device | Bus=Device | **Bus 不可见** |
| 异步回调 | per-message | signal | per-transaction | 无 | 无 | **per-request + 队列** |

---

## 7. 需修改的文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `lib/drivers/include/drivers/peripheral/spi/pal_spi_dev.h` | 完全重写 | 所有结构体/枚举/宏/函数声明 |
| `lib/drivers/src/peripheral/spi/hal_spi.c` | 完全重写 | 框架实现（含 workqueue worker 回调） |
| `lib/drivers/include/drivers/model/device.h` | +1 | `DEVICE_TYPE_SPI` 枚举值 |
| `lib/drivers/include/drivers/peripheral/pal_dev.h` | +3 | `#ifdef OM_USE_HAL_SPI` 条件包含 |
| `lib/include/core/om_config.h` | +1 | `#define OM_USE_HAL_SPI` 开关 |

## 8. 验证方法

1. `xmake f -c --toolchain=armclang -m debug && xmake` 编译通过
2. 头文件可独立编译（所有类型自包含，无前向声明缺失）
3. SpiInterface 模式对齐 SerialInterface（configure/transfer/control + ISR 入口）
4. HalSpiDevice 的 DevInterface 模式对齐 HalSerial
5. Workqueue 集成路径可编译（`spi_async_worker_func` 引用 workqueue.h）
