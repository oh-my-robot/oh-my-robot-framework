# FlashDev v1 实现设计（代码架构定稿）

> **状态**：实现设计定稿（2026-09-06，决策 D-01..D-08 已拍板）——动码依据。接口/语义总纲见同目录 `flash_dev_design.md`（v1）；本文件只承载**实现层架构**：执行模型、数据结构、接线、契约、测试。
>
> **决策沿革**：v0 同步忙等（真机实测饿死低优线程）→ v1 异步化。执行架构经成熟框架调研收敛：队列解耦提交者与设备串行约束；执行者按域划分；完成源可插拔；回调上下文固定。

## 1. 决策清单（拍板记录）

| # | 决策 | 结论 |
|---|---|---|
| D-01 | 请求队列深度 | **2**（可配宏 `OM_FLASH_QUEUE_DEPTH`）。语义 = 并发等待者数（在途 1 + 重叠等待 1），非吞吐缓冲 |
| D-02 | domain 语义与接线 | domain = **执行者归属**（worker 的作用域）。**默认每设备独立域**（NULL = 自动独立域）——并行 + 同总线多片等待期交错；共享域 = 省线程的**显式选项**（接受请求级串行）。适配器接线、框架内部持有、**调用者无感** |
| D-03 | read 语义 | 同步直跑（调用者上下文、无队列延迟）；与写擦互斥（**busy 协议，read 抢占式**）；忙时返回 `OM_ERR_BUSY`；XIP 直读不经 API |
| D-04 | done 契约 | 完成必调一次（成功/失败）；上下文 = 域 worker；**槽释放先于回调**（回调内 async 再提交必有空槽）；done 内同域同步等待 → **运行期拒绝 `OM_ERR_BUSY`**；done 可为 NULL（入队即忘） |
| D-05 | 域 worker 实现 | **复用 Workqueue 原语**（每域一个实例 = 自带 worker 线程/FIFO/门铃/生命周期）。**不借道 SPI per-bus wq**（flash 长操作会饿死同总线短传输） |
| D-06 | 后端契约 | 同步签名 + **让出硬契约**（write/erase 内部等待必须让出）。SPI 后端事务用**同步 spi_transfer**（其 completion 等 ISR = OS 级阻塞让出，白送） |
| D-07 | 完成源 | **事件主路径、轮询退化**（2026-09-06 定稿强化）：后端等待以硬件完成事件为主路径（ISR 给 completion/sem），软件合成轮询（睡眠轮询 BSY）为无中断芯片的退化路径；**事件化适用对象 = 完成延迟远大于事件开销的长操作（扇区擦/大块编程）**——亚毫秒粒度操作（单字 program ~30us）用微等待（低于调度粒度，天然不构成饿死），不做每字中断 |
| D-08 | 无 OSAL（bootloader） | 编译期裁剪：不建 worker；同步 API 直跑后端；async API 返回 `NOT_SUPPORTED`。**复用形态（K-16 证据）**：带 OS 的 bootloader 用同步等待原语（worker 让出）；裸机用 `OM_FLASH_SYNC_ONLY` + osal 空桩（U-Boot compat.h 同款：OS 原语裁剪为空转）；擦除的静默阻塞由上层策略消化（拆片 + 主机长超时——OpenBLT/ST 惯例），框架不提供事件化擦写 |

## 2. 模块与文件布局

```
lib/drivers/include/drivers/peripheral/flash/pal_flash_dev.h   公共接口（类型/API/done 契约注释）
lib/drivers/src/peripheral/flash/hal_flash.c                   框架：域/队列/执行路由/同步等待/read 协议
lib/drivers/src/peripheral/flash/flash_domain.c                FlashDomain 生命周期（Workqueue 包装）
samples/host/flash_dev_test/                                   host 仿真（桩扩展 thread/sem + 异步用例）
platform/bsp/vendor/STM32/STM32F4/adapters/flash/bsp_flash_f4.c  片内后端（寄存器级 + EOP 中断或让出轮询）
```

## 3. 核心数据结构

```c
/* ---- 请求（嵌入设备定长槽；Work 嵌入使请求可入域队列） ---- */

typedef struct FlashRequest {
    Work          work;        /* 域 workqueue 工作项（func 执行后端） */
    FlashDev     *dev;
    uint32_t      type;        /* FLASH_REQ_WRITE / FLASH_REQ_ERASE */
    uint32_t      addr;
    size_t        len;
    const void   *data;        /* write 数据（调用方缓冲存活至完成） */
    FlashDoneCb   done;        /* 用户回调（可 NULL） */
    void         *param;
    OmRet         result;
    volatile bool synced;      /* 同步等待原语在用（worker 完成后置位唤醒） */
    OsalSem      *syncSem;     /* 同步等待用（异步请求为 NULL） */
} FlashRequest;

/* ---- 域（执行者 = 一个 Workqueue 实例） ---- */

typedef struct FlashDomain {
    Workqueue     wq;          /* init/start = worker 线程（栈/优先级为域配置） */
    ListHead      devices;     /* 域内设备链表（worker 取请求遍历） */
    volatile bool inWorker;    /* 域 worker 执行中标记（同步等待的同域拒绝检测） */
} FlashDomain;

/* ---- 设备 ---- */

typedef struct FlashDev {
    Device parent;
    const FlashGeometry *geom;
    const FlashOps *ops;
    void *hw;
    /* 框架 v1 */
    FlashDomain    *domain;                     /* 所属域（register 时定，NULL=自动独立域） */
    FlashRequest    slots[OM_FLASH_QUEUE_DEPTH]; /* 定长请求槽（槽空闲才可提交） */
    ListHead        domainNode;                 /* 挂域设备链表 */
    volatile bool   busy;                       /* 设备执行协议：worker 执行中 or read 直跑中 */
} FlashDev;
```

## 4. 执行协议

**提交路径**（任意线程/ISR 安全——Workqueue enqueue 语义）：
```
flash_erase_async(dev, ...):
  校验/对齐 → 找空闲槽（无则 BUSY）→ 填充请求 + work_init
  → domain 若已停止（无 OSAL）→ NOT_SUPPORTED
  → workqueue_enqueue(&domain->wq, &req->work) → OM_OK（发起即返回）
```

**worker 执行**（work func，域 worker 线程上下文）：
```
req 取到 → dev->busy = true; domain->inWorker = true
  调后端（write/erase 同步实现，内部让出：sleep 轮询 或 等 EOP completion）
  req->result = 后端结果
dev->busy = false
req->synced 置位 + syncSem give（同步等待者唤醒）
槽释放先于用户回调：
  if (req->done) req->done(dev, req->result, req->param)   ← worker 上下文
（done 内可再 async 提交——槽已释放必有空位）
```

**read（同步直跑 + 抢占互斥）**：
```
flash_read(dev, ...):
  irq_lock: dev->busy ? → unlock 返回 BUSY
            : → dev->busy = true; unlock
  直跑后端 read（有界；SPI 事务 = 同步 spi_transfer）
  dev->busy = false
worker 取请求前若见 dev->busy（read 直跑中）→ 跳过该设备等下一轮
→ busy 标志 = 设备级互斥点；read 与写擦互斥，read 优先不排队
```

**同步等待原语**（flash_write/erase）：
```
if (domain 存在且 domain->inWorker && 目标设备属于本域):
    → 运行期拒绝 OM_ERR_BUSY（D-04：同域 worker 内同步等待 = 自锁，显式拒绝）
    （异域设备：真阻塞等其它域 worker，允许）
占槽 + syncSem → workqueue_enqueue → osal_sem_wait(syncSem)（让出）→ 返回 req->result
```

**队列满 / 忙的完整拒绝面**：槽满 → BUSY；read 遇 busy → BUSY；同域 worker 内同步等待 → BUSY。

## 5. 接线与生命周期

```
适配器/板级自注册（例：bsp_flash_self_init）：
  FlashDomain *dom = flash_domain_create(&cfg);   /* cfg: name/prio/stack；NULL 也允许=设备用自动域 */
  flash_register(&dev, "flash0", geom, ops, hw, NULL);   /* NULL → 框架自动独立域（默认） */
  同总线多片共享线程预算时：传同一 dom（显式共享，D-02）

flash_domain_create：workqueue_init + workqueue_start（WORKQUEUE_STATE_RUNNING）
无 OSAL 编译期（OM_FLASH_NO_OSAL）：domain 创建返回 NOT_SUPPORTED；同步 API 直跑；async 拒绝
```

## 6. 后端契约（适配器职责）

- `read`：有界同步（片内 memcpy / SPI 同步事务）。
- `write/erase`：同步执行、**等待必须让出**（实现自由）：
  - 片内 F4：发命令后阻塞等 **EOP 中断 completion**（首选，零轮询）；或 `while(BSY) osal_sleep_ms(1)`
  - SPI NOR：命令/状态事务 = 同步 `spi_transfer`（内部等 ISR 已让出）；WIP 轮询间隙 `osal_sleep_ms(1)`；事务不持总线锁跨等待
- 适配器不感知槽/域/队列——只实现"同步函数 + 让出"。

### 6.1 完成源策略：事件主路径 / 轮询退化（D-07 强化，2026-09-06）

统一范式 = **等待完成事件**（worker 阻塞于 sem/completion，均为让出）；事件来源按硬件能力选择：

| 完成源 | 路径属性 | 适用 |
|---|---|---|
| 硬件完成中断（片内 EOP/ERR → ISR 给 sem） | **主路径**（零轮询零唤醒） | 扇区擦（~2s）、批量编程等长操作 |
| 睡眠轮询合成（`while(BSY) sleep(1ms)`） | **退化路径**（无中断芯片） | 无完成中断的片内 flash / 外部介质 |
| 字级微等待（紧凑轮询 ~30us） | 微等待（非轮询主路径） | program 单字粒度——低于调度粒度不构成饿死，**不做每字中断**（中断开销 > 节省） |

**事件化适用边界**：完成延迟远大于事件开销的操作（擦除秒级、批量编程 ms 级）；亚毫秒粒度操作用微等待。框架/worker 不感知事件来源——后端契约"同步 + 让出"不变（sem_wait 与 sleep 轮询同为让出），D-07 的"完成源后端自由"在语义上收束为"**事件优先、轮询退化**"。

## 7. host 仿真改造与测试矩阵

- host_osal 桩扩展：`osal_thread`（平台线程包装）+ `osal_sem`——Workqueue 依赖齐备
- 测试组（沿用既有 73 项同步语义 + 新增）：
  1. async 发起即返回；完成回调顺序 = 提交顺序；回调在 worker 上下文
  2. 槽满 → BUSY；完成回调内再提交（链式）可行
  3. 同步等待原语结果 = 异步同结果；并发两同步调用者占两槽
  4. read-BUSY：写擦在途时 read 拒绝、完成恢复；read 直跑抢占互斥无混合读
  5. 同域 worker 内同步等待 → BUSY（不死锁）；异域同步等待正常
  6. 多域独立：两设备各独立域同时长擦写 → 完成互不拖累（时间断言）
  7. done 为 NULL（入队即忘）语义
- 真机（rm-a）：EOP 中断路径 + log 线程全程活性（v1 验收核心）

## 8. 成本

- 每设备：槽 2 × (Work + 请求字段 ≈ 56B) ≈ 112B + busy/域指针
- 每域：一个 Workqueue（worker 线程栈 1~1.5KB + sem/completion）
- 提交/read 检查：irq_lock 短临界；无动态分配

## 9. 关联

- 接口与语义总纲：`flash_dev_design.md`（同目录，v1）
- 调研档案与锚点：`reference_design_notes.md`（同目录）
- 存储系统全景与留口：`storage_landscape.md`（同目录）
