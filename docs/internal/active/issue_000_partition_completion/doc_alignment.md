# 文档对齐清单（ADR-0027 落码后）

- 日期：2026-09-20
- 权威来源：`docs/adr/0027-flash_pal_drop_async_domain.md`（决策）+ `docs/adr/0026-partition_semantics_completion.md`（分区语义）
- 性质：**清单，不是修订本身**。下列设计文档是 boot/OTA 工作流的决策档案，其修订归该工作流；本清单只标出"何处已与当前代码不符"与"应改成什么"。

## 一、必须修订（内容与代码相反）

### 1. `docs/boot_ota/flash_dev_impl_design.md`（受影响最大——整份实现设计围绕已删除机制展开）

| 位置 | 现状 | 应改为 |
|---|---|---|
| 文件说明（第 5 行） | "v1 异步化；执行者按域划分；完成源可插拔；回调上下文固定" | 执行模型改为"调用者上下文同步 + 每设备睡眠互斥量"；指向 ADR-0027 |
| D-01 请求队列深度 = 2 / `OM_FLASH_QUEUE_DEPTH` | 队列与宏均已删除 | 条目作废（队列满 `BUSY` 失败模式随之消失） |
| D-02 domain 语义与接线 | `FlashDomain`、共享域、`flash_domain_init` 全删 | 条目作废；多片并发的表达改为"每设备一把锁" |
| D-03 read 语义（busy 协议 / read 抢占 / 忙时 `BUSY`） | 已改为读/写/擦共用一把设备锁：忙时**等锁**，不返回 `BUSY` | 重写为锁语义；`OM_ERR_FLASH_BUSY` 不再由数据通路产生 |
| D-04 done 契约 | `FlashDoneCb`、回调上下文、槽释放次序全删 | 条目作废（无回调面） |
| D-05 域 worker 复用 Workqueue | `flash_domain.c` 已删除 | 条目作废 |
| D-08 无 OSAL 裁剪（async API 返回 `NOT_SUPPORTED`） | 无异步面可裁剪；osal-none 下"无线程 + 一把永不争用的锁" | 重写：裁剪点从"async 面"变为"无变化"（同步面本就一致） |
| 第 24–25 行文件清单 | 列了 `flash_domain.c`；`hal_flash.c` 职责写作"域/队列/执行路由" | 删 `flash_domain.c`；`hal_flash.c` 职责改为"几何校验 / 擦后校验 / 每设备锁" |
| 第 33–68 行结构体（`FlashRequest` / `FlashDomain` / 设备内 `slots[]`/`domain`/`busy`） | 全部不存在 | 按当前 `pal_flash_dev.h` 的 `FlashDev` 重写（`geom` / `ops` / `hw` / `lock`） |
| 第 76 行起 `flash_erase_async` 时序、"worker 执行"章节 | 全部不存在 | 换成同步路径时序（校验 → 取锁 → 后端 → 擦后校验 → 解锁） |

### 2. `docs/boot_ota/flash_dev_design.md`

| 位置 | 现状 | 应改为 |
|---|---|---|
| 第 5 行版本沿革 | "v1：写/擦异步化…… 分域 worker……" | 追加一段：**v2 = 移除异步执行体**（ADR-0027），并说明 K-14 被重新解读为"真凶是忙等" |
| 第 29 / 49 行 F-04 决策行 | "异步写/擦 + 同步读；per-device 定长请求队列 + 分域 worker" | F-04 改为"写/擦在调用者上下文同步执行 + 每设备睡眠互斥；后端让出 CPU 为承重契约" |
| 第 78–93 行 `FlashDoneCb` 与两个 async 原型 | 均已删除 | 删除该段 |
| 第 96–103 行 `FlashDev` 定义 | 含"队列/域字段" | 按当前头文件重写（含 `OsalMutex *lock`） |
| 第 116–139 行 async 说明 + 回调约定 + 能力表三行 | 全部不存在 | 删除；能力表只留 `flash_read` / `flash_write` / `flash_erase` 三行，错误码列按新契约（越界 `RANGE` 先于未对齐 `INVALID_ARG`） |
| 第 124–125 行"同步等待原语 = 队列提交 + 内部等待" | 同步现在是唯一形态，不再"等待 worker" | 改为"同步 API = 取设备锁 + 后端直跑" |

### 3. `docs/boot_ota/reference_design_notes.md`（K-16 的"含义"列）

| 位置 | 现状 | 应改为 |
|---|---|---|
| K-16 含义列 | "FlashDev 保持'同步 API + 域 worker + 后端完成源可插拔'"；"async 随 `OM_FLASH_SYNC_ONLY` 编除" | 改为"同步 API + 每设备锁 + 后端完成源可插拔（EOP 中断主路径 / 轮询退化，D-07）"；`OM_FLASH_SYNC_ONLY` 已无消费面（该宏只存在于设计态注释），删除该提法 |

> K-16 的**调研结论本身**（"flash 框架 API 对调用者保持同步，异步完成源压驱动/后端吸收"）与 ADR-0027 同向——它与代码的分歧只在"执行体放 worker 还是调用者"。故 K-16 只需改"含义"列，证据列不动。

## 二、需要追加说明（不是错，但会误导）

- `docs/boot_ota/flash_dev_impl_design.md` 的 D-06（后端让出契约）与 D-07（完成源：EOP 中断主路径 / 轮询退化）**仍然有效**，但语义升级：D-06 从"契约"升为**承重条款**——它单独承担了原本由 worker 承担的"不饿死其它任务"职责；D-07 的等待点从 worker 上下文平移到调用者上下文。建议在两处各加一行注记指向 ADR-0027。

## 三、明确不改

- `docs/adr/0022`（osal_none_bare_metal）、`docs/adr/0020`、`docs/adr/0026`：ADR 是历史记录，不回改；ADR-0027 已在"与既有决策的关系"里声明对 K-14 / D-06 / D-07 的修订关系。
- `docs/boot_ota/reference_design_notes.md` 的 K-14 条目正文：保留原记录，由 ADR-0027 作**重新解读**（真凶是忙等而非同步），不改原文。

## 四、代码侧改动清单（对应本清单的代码事实）

已由 ADR-0027 覆盖，此处仅列面：

- 删除：`lib/drivers/src/peripheral/flash/flash_domain.c`；`pal_flash_dev.h` 的 `FlashDoneCb` / `FlashDomain` / `flash_domain_init` / `FlashRequest` / `OM_FLASH_QUEUE_DEPTH` / `flash_write_async` / `flash_erase_async` / `flash_set_done_cb`。
- 新增：`FlashDev.lock`（每设备睡眠互斥量，`flash_register` 期建立）；`flash_lock` / `flash_unlock`；擦后校验分块粒度 `FLASH_VERIFY_CHUNK`（64 → 256 B，按后端读调用开销折中）。
- 语义：越界 = `OM_ERR_RANGE`（改偏移即可），未对齐/空参 = `OM_ERR_INVALID_ARG`（参数不成立）；越界先于未对齐报出。擦后校验未达 `erasedValue` = `OM_ERR_FLASH_UNUSABLE`（跳过该区），后端擦除失败 = `OM_ERR_FLASH_IO`（可重试同区）。
- 头文件依赖面收窄：`pal_flash_dev.h` 不再引入 `osal_sem.h` / `osal_thread.h` / `osal_time.h`（适配器自行 include 所需）。
- 验证目标（真机）与 host 测试的期望值按新契约更新；`samples/pal/*/main.c` 的 `CHECK` 增加 20 ms 输出节流（串口日志后端非阻塞提交，爆发输出会被截断——详见 ADR-0027"验证环境的两处坑"）。
