# Flash 设备抽象（FlashDev）设计 v1 定稿

> **状态**：v1 定稿（2026-09-06，异步模型重构——F-04 经真机验证修订）。开工序列步骤 ① 的接口文档。升格 ADR 时机：异步接口经片内适配器（STM32F4）落地并真机验证后。
>
> **版本沿革**：v0（同步阻塞语义，F-01..F-05）→ 真机验证暴露"同步忙等饿死低优先级线程、系统卡顿"→ 多片/多介质调研（双层锁、read 语义）→ **v1：写/擦异步化 + read 同步保留 + 多片独立并发** → v1 二次修订（异步执行架构调研）：**per-device 定长请求队列 + 分域 worker（执行者按物理串行域划分）+ 完成源可插拔（片内 EOP 中断 / 软件轮询让出）**；纠偏：F4 片内 flash 有 EOP 完成中断（此前"无中断"表述错误）。
>
> **约束锚点**：同目录 `reference_design_notes.md` 的 `P-xx` / `K-xx`；与 `multi_strategy_boot_design.md` 同族。
>
> **调研基础**：CMSIS-Driver Flash（事件模型实核）、Zephyr flash API（stm32 async 提交实核）、Linux spi-nor（双层锁实核）、RT-Thread SFUD（整操作持锁反例实核）、ESP-IDF（按片锁/逐命令拆实核）、Rust embedded-storage(-async)。

---

## 1. 目标与范围

- 片内 flash（STM32F4 / MSPM0）与外部 SPI NOR（W25Q，步骤 ④）共用**一个**设备接口族（P-01）；厂商驱动只在平台适配器内（P-02）。
- **多物理片共存是一等场景**：片内 + 片外、同总线多片——不同物理设备的逻辑与时序互相独立（一片擦除中另一片可操作）。
- v1 落地范围：接口族（异步写/擦 + 同步读 + 同步薄封装）+ 框架侧校验/几何 + STM32F4 片内适配器。外部 NOR 后端留步骤 ④（接口须可容纳，§10）。

## 2. 调研收敛结论（→ OMR 落点）

| 业界共识 | OMR 落点 |
|---|---|
| 最小 API = read / write(program) / erase / 几何 | 同步 read + 异步 write/erase（同步薄封装保留） |
| write = program、不自动擦、已擦是调用方义务 | 头文件强注释 + §6 |
| 擦除整块、不静默扩擦 | 框架强制对齐校验 |
| 擦后值可查 | `FlashGeometry.erasedValue` |
| 非均一扇区可表达 | 几何双模（F-01） |
| 分区 = 设备层之上独立语义 | 分区表后置（P-03） |
| **异步只给写/擦，read 保持同步**（Zephyr 无 read_async；CMSIS read 双模式但异步是介质属性；Linux NOR read=memcpy） | F-06（v1） |
| **双层锁**：每片 device 锁管本片时序；共享总线锁只覆盖传输瞬间（Linux spi-nor 最清晰：总线锁 per 传输、WIP 轮询在锁外）；等待须可让出 | F-04（v1 修订）+ §7 |
| 每物理片 = 一个 device 实例（片内片外同族不同实例，仅 ESP 同抽象统一） | F-05 + §8 |
| 掉电无物理恢复，上层事务化 | 文档级担保 + 上层事务化 |

## 3. 第一性原理接口铁律

1. **三档操作单位递增**：读（任意字节）⊂ 写（最小单元，1→0 单向）⊂ 擦（整扇区）——粒度分别显式化。
2. **介质差异收敛在后端**：片内（寄存器序列）与 SPI NOR（命令+CS）对上层同一语义空间。
3. **XIP 读取不走 API**：取指/映射读是总线行为、链接期事实；geometry 不承载映射基址。
4. **掉电语义只做文档级担保**：半擦/半写由上层事务化恢复。
5. **硬件差异全部沉入适配器**：编程粒度、擦除耗时、**操作完成的事件源形态（真中断 vs 软件合成）**、RWW/stall 行为——由适配器上报/实现，框架不做芯片假设。

## 4. F 系列定论

| # | 定论 | 理由（一句话） |
|---|---|---|
| F-01 | 几何双模：均匀单值 or 区域表 | 三案同构；F4 非均一、MSPM0 均一 |
| F-02 | `drivers/peripheral/flash/`、`flash_*`、`DEVICE_TYPE_FLASH`、`OM_USE_HAL_FLASH` | 与 SPI/CAN/SERIAL 族同构 |
| F-03 | 标准 DevInterface read/write 薄转发（ctrl_info 携带偏移） | Device 外壳完整性 |
| F-04 | **异步写/擦 + 同步读（v1 修订，原 v0：全同步 + 可睡眠互斥全排他）**：per-device 定长请求队列 + **分域 worker 执行者**（线程数 = 物理串行域数，非设备数）+ 完成回调；同步等待原语保留（bootloader/无 RTOS 用）；**完成源可插拔**（片内 EOP 中断 / 软件轮询让出） | 真机实测：同步忙等饿死低优线程、系统级卡顿；业界：队列解耦提交者与设备串行约束（blk-mq 无 per-queue 线程）、per-域 worker 有产业先例（esp_flash_dispatcher）、MTD 同步 API 被正式正名（调用者可让出） |
| F-05 | 整片一个 `flash0`；bank = 几何区域条目；多片 = 多个设备实例（天然独立） | 每物理片 = 设备 = 锁与状态单元（K-15） |
| F-06 | **read 保持同步**（不建 read_async） | read 有界且快：XIP/映射 = 直访瞬时；SPI 读 = 有界总线事务；业界无 read 单独异步先例；NAND 若来（read 与 write 同构）再按慢介质单独 async（Zephyr 路径） |

## 5. 接口定稿（骨架）

```c
/* lib/drivers/include/drivers/peripheral/flash/pal_flash_dev.h */

/* ---- 几何（适配器静态 const，同 v0：capacity/erasedValue/writeUnit/pageSize/双模） ---- */
typedef struct {
    uint32_t offset;   /* 区域首扇区偏移（设备内偏移） */
    uint32_t size;     /* 扇区大小（=擦除单位） */
    uint32_t count;    /* 连续扇区数 */
} FlashSectorRegion;

typedef struct {
    uint32_t capacity;
    uint8_t  erasedValue;
    uint16_t writeUnit;
    uint16_t pageSize;
    uint32_t sectorSize;   /* >0 均匀；==0 走 sectorRegions */
    uint16_t sectorCount;
    const FlashSectorRegion *sectorRegions;
} FlashGeometry;

/* ---- 完成回调与后端 ops（异步契约） ---- */

typedef struct FlashDev FlashDev;

typedef void (*FlashDoneCb)(FlashDev *dev, OmRet status, void *param);

typedef struct FlashOps {
    /** 同步读：框架校验后调用；后端为 XIP 直访或总线事务，须有界快速 */
    OmRet (*read)(FlashDev *dev, uint32_t addr, void *buf, size_t len);

    /** 异步写（program 语义，假定已擦）：启动编程，完成后调 done(dev, status, param)。
     *  后端可逐字/分片推进；全程不得忙等占 CPU（等待期间让出或事件驱动）；
     *  共享总线仅传输瞬间占用。 */
    void (*write)(FlashDev *dev, uint32_t addr, const void *data, size_t len,
                  FlashDoneCb done, void *param);

    /** 异步擦（整扇区制）：启动擦除，完成后调 done(dev, status, param)。同上 */
    void (*erase)(FlashDev *dev, uint32_t addr, size_t len,
                  FlashDoneCb done, void *param);
} FlashOps;

/* ---- 设备对象（每物理片一个实例；队列/域字段见 §7 实现设计） ---- */

typedef struct FlashDev {
    Device parent;
    const FlashGeometry *geom;
    const FlashOps *ops;
    void *hw;                 /* 适配器私有（片内：寄存器句柄；SPI：总线+CS 引用） */
    /* 框架持有（v1）：per-device 定长请求队列 + 分域 worker 归属，见 §7 */
} FlashDev;

/* ---- 核心 API ---- */

OmRet flash_register(FlashDev *dev, const char *name, const FlashGeometry *geom,
                     const FlashOps *ops, void *hw);
FlashDev *flash_find(const char *name);
const FlashGeometry *flash_geometry(FlashDev *dev);

/* read：同步（有界快速，XIP 直访/总线事务）；禁止在 ISR 调用（总线路径可阻塞） */
OmRet flash_read(FlashDev *dev, uint32_t addr, void *buf, size_t len);

/* 异步写/擦：提交 per-device 请求队列即返回；队列满返回 OM_ERR_BUSY；
 * 完成回调在所属域 worker 线程上下文执行（固定上下文；回调内可再提交 async，
 * 禁止在回调内调本设备同步等待原语——自锁） */
OmRet flash_write_async(FlashDev *dev, uint32_t addr, const void *data, size_t len,
                        FlashDoneCb done, void *param);
OmRet flash_erase_async(FlashDev *dev, uint32_t addr, size_t len,
                        FlashDoneCb done, void *param);

/* 同步等待原语（= 队列提交 + 内部等待；bootloader/无 RTOS/简单调用方）：
 * 阻塞调用线程至本请求完成；执行在域 worker（内部让出），调用线程睡眠 */
OmRet flash_write(FlashDev *dev, uint32_t addr, const void *data, size_t len);
OmRet flash_erase(FlashDev *dev, uint32_t addr, size_t len);
```

## 6. 语义强约定（进入头文件注释）

| API | 地址语义 | 对齐约束 | 前置/并发 | 错误 |
|---|---|---|---|---|
| `flash_read`（同步） | 设备内偏移 | 无 | 禁止 ISR；与在途异步写擦的并发语义见 §7 | 越界 → `INVALID_ARG` |
| `flash_write_async` | 同 | addr/len 为 writeUnit 倍数（框架校验） | 目标已擦（调用方义务）；队列提交 | 越界/未对齐 → `INVALID_ARG`；队列满 → `OM_ERR_BUSY` |
| `flash_erase_async` | 同 | addr/len 为扇区边界（不静默扩擦） | 队列提交 | 非对齐/越界 → `INVALID_ARG`；队列满 → `BUSY` |
| `flash_write/erase`（同步等待原语） | 同 | 同 async | 阻塞调用线程至完成（执行在域 worker） | 同 async + 完成状态透传 |

回调约定：`done(dev, status, param)` 在**所属域 worker 线程上下文**执行（固定上下文，可调 osal API）；status = 操作结果。回调触发时本请求已出队、队列已释放——**回调内可安全再提交 async**；禁止在回调内调**本设备**同步等待原语（域 worker 自锁；框架对同域 worker 线程的同步调用做直跑检测，见 §7）。
错误码：`OM_ERR_FLASH_*` 别名映射通用码（INVALID_ARG/IO/BUSY/TIMEOUT/NOT_SUPPORTED）+ `0x1000+` 段预留。

## 7. 并发与异步模型（F-04 v1 展开，二次修订）

**动因（真机实测）**：同步忙等（HAL 原子轮询）持 CPU 1s 级 → FreeRTOS 下所有低优先级线程（含 log 后端 LOW）被持续就绪的调用线程饿死 → 系统级卡顿。RWW 只保证硬件可取指，不改变调度；**等待必须让出 CPU**。

**模型（队列 + 分域 worker + 可插拔完成源）**：
1. **per-device 定长请求队列**：并发提交者与设备串行约束解耦（blk-mq 同款思路）；队列满 → `OM_ERR_BUSY`。**队列在设备侧** → 多片独立时序：片 A 排队满不影响片 B 提交。
2. **执行者 = 分域 worker**：线程数 = **物理串行域数**而非设备数——片内域一条（所有无共享总线的片内设备）；每条共享 SPI 总线一条域（总线物理串行，共享 worker 不损失并行度；`esp_flash_dispatcher` per-域后台任务为产业先例）。域线程优先级/栈为域配置。worker 主循环：等门铃 → 扫域内设备队列取请求 → 调后端同步实现（内部让出）→ 回调/完成信号 → 循环。
3. **后端契约 = 同步签名 + 让出硬契约**（read/write/erase 三同步函数；write/erase 的 BSY 等待**必须让出**，实现自由）：
   - 有完成中断（片内 F4 **EOP 中断**——纠偏：F4 确有 EOP；或外设 RY/BY）：后端内部"发命令 → 阻塞等 completion（ISR give）"——让出 + 零轮询；
   - 无中断芯片：后端内部"BSY 轮询 + `osal_sleep_ms(1)` 让出"。
   适配器按硬件选择，框架不感知。
4. **回调上下文固定 = 域 worker 线程**。同步等待原语（flash_write/erase）= 队列提交 + completion 等待；框架对"调用线程 == 该设备域 worker"的同步调用做**直跑检测**（不经队列直接执行，防自锁）。
5. **read 语义（F-06）**：同步有界；与在途写擦的并发：同设备在途时 flash_read 返回 `BUSY`（杜绝混合读）；XIP 直读不经 API 不受限。ISR 读走 XIP 直访通道。
6. **锁的分层**（外部 SPI 多片共享总线）：总线锁只覆盖单条 SPI 传输瞬间（pal_spi 传输粒度）；后端让出等待在总线锁外 → 同总线多片交错（Linux spi-nor 双层锁同款；避开 SFUD 整操作持锁反例）。域 worker 串行化本域提交 → 域内设备天然无并发提交冲突。
7. 与 XIP：执行取指不经 API。

## 8. 设备视图（F-05 展开 + 多片）

- 每物理片 = 一个设备实例（`flash0` 片内、`flash_ext0` 片外……）——设备 = 锁/状态/slot 单元（K-15）；bank 只是几何区域分组，不构成设备判据。
- 多片共存：片内与片外同族不同实例（ESP-IDF 同抽象多实例为业界先例）；同 SPI 总线多片靠 §7 锁分层获得独立时序。
- 槽/分区布局经上层分区表表达，与物理片/ bank 解耦。
- 实测（§10）：F427 RWW 有效 → 跨 bank 布局使"写 staging 期间业务照跑"成立；同 bank 擦写仍 stall → staging 放执行 bank 外。

## 9. 边界（不在本层）

分区表/槽位/meta（P-03）· 磨损环形（P-05）· 坏块/ECC（NAND 属驱动级，ER-2）· JEDEC/SFDP 探测（适配器内）· 加密/签名（上层）。

## 10. 落地验证状态

| 验证项 | 结果 |
|---|---|
| 1. 适配器 geometry | 真机修正：F427 = dual-bank 24 扇区（每 bank {16K×4,64K,128K×7}），6 区域表已固化（初版单区假设被擦除定位实验证伪） |
| 2. 框架校验路径（同步薄封装语义） | ✅ 越界/非对齐/半扇区拒绝 6 项 + 几何 + 布局探测 PASS（21/21） |
| 3. 擦写实况/RWW | ✅ 128K 擦 1059ms、16K 擦 ~260ms、4KB 写 16ms、64KB 写 255ms；**RWW 有效**（擦/写 bank2 时 bank1 心跳 delta=5/1 全程存活；早期"全停摆"系观测伪影 + 线程 return bug，经 RAM 计数观测修正） |
| 4. 调度卡顿根因（v1 动因） | ✅ 实测证实：同步忙等饿死 LOW log 线程（FreeRTOS 持续就绪压制）——v0 同步语义在 RTOS 下不可接受 → v1 异步化 |
| 5. 双工具链 × 双板 | ✅ rm-a/rm-c × gnu-rm/armclang 全绿 |
| 6. 异步接口（队列 + 分域 worker + 完成源）实现与 host 仿真 | ✅ v1 全链落地 + host 75/75 + **真机 40-41/0**（rm-a/F427：链回调/read-BUSY/队列满/回调内同步拒绝/无回调 + 心跳全程活性） |
| 7. W25Q 后端可容纳性 | 待步骤 ④ |

## 11. 关联

- 调研档案与 P/K 锚点：`reference_design_notes.md`（同目录）
- 多策略 boot 草案：`multi_strategy_boot_design.md`（同目录）
- 存储系统全景与演进留口：`storage_landscape.md`（同目录）
- 实现层架构（代码定稿，D-01..D-08）：`flash_dev_impl_design.md`（同目录）
