# 成熟存储栈中的 workqueue / worker 线程：一手源码证据

> 调研时间：2026-09-19。所有行号锚定到具体版本（见各节标注的仓库 + tag/commit）。
> 引用格式：`路径:行号` + 版本。

## 0. 一句话结论

**成熟存储栈里 workqueue / worker 线程大量存在，但它们都出现在「上下文不合法（不能睡眠/在 IRQ）」或「后台家务」两个位置上；器件驱动层的擦写默认在调用者上下文同步跑，把 flash 驱动 worker 化的个别案例（Zephyr ESP32 flash 驱动）动机是硬件拓扑而不是"擦写太久"**。Linux MTD 甚至把 erase 的异步回调机制从 API 里**删除**了：`struct erase_info` 在 v6.12 里只剩 `{addr, len, fail_addr}`，删它的官方理由是 "None of the mtd->_erase() implementations work in an asynchronous manner"（commit `884cfd9023ce`，v4.17）。

形态上真正接近「每设备 worker + 请求 FIFO + 提交-完成」的现成样本只有四处：

1. Linux SPI 控制器层：每控制器一个 `kthread_worker` + message FIFO；队列空时 `spi_sync` 在调用者上下文 inline 跑，队列非空则入队 + `wait_for_completion`（`drivers/spi/spi.c:4523`、`:2056`）。
2. f2fs 的 flush 线程：每 sb 一个 `f2fs_flush-%u:%u` kthread + llist 队列；队列空（`queued_flush==1`）时 inline 提交，否则入队 + `wait_for_completion`（`fs/f2fs/segment.c:611-618`、`:623`、`:637`）。
3. Zephyr 的 ESP32 flash 驱动（`CONFIG_ESP_FLASH_ASYNC` 下）：per-device `k_mutex` + `k_work` + `k_sem`，同步 API = 加锁 → `k_work_submit` → `k_sem_take`（`drivers/flash/flash_esp32.c:560-562`）。
4. Linux mtdblock（`mtd_blkdevs.c`）：请求队列由 blk-mq 托管，真正的擦写在 blk-mq 的派发上下文里同步执行（`BLK_MQ_F_BLOCKING` 标签集，`mtd_blkdevs.c:331-332`），实测栈回溯落在 **kblockd worker** 里（commit `ca6263a0c950` 的 splat：`Workqueue: kblockd blk_mq_run_work_fn ... mtd_queue_rq ... mtd_blktrans_work ... spi_mem_exec_op`）。

而 UBI 的 `bgt_thread`（每 UBI 设备一个后台线程）属于典型的「后台家务」：它做的是**擦除队列 + 磨损均衡 + 擦洗**，动机写在其子系统头部注释里，不是「把长操作异步化给调用者」。

## 1. 分层总表

版本锚定：Linux `v6.12`（git.kernel.org plain）；Zephyr `v4.1.0`（部分 main 分支，逐条标注）；ESP-IDF `v5.4.1`；NuttX `master @66703d09573e`；RT-Thread `master`；littlefs `v2.11.0`；MCUboot `main`；U-Boot `master`；OpenBLT `master`。

| 层级 | 是否用 wq/worker | 粒度 | 用途 | 源码锚点 |
|---|---|---|---|---|
| 块层（Linux blk-mq） | 是（`kblockd`） | **全局 1 个**（`WQ_MEM_RECLAIM\|WQ_HIGHPRI`） | (a)-变体：**延迟派发**，避免在中断/软中断上下文跑队列；并把"提交者不在该 hctx CPU 上"的队列重跑搬走 | `block/blk-core.c:1253`；`block/blk-mq.c:2408`（`blk_mq_run_work_fn`）、`:2194`（`blk_mq_delay_run_hw_queue`）、`:2212-2244`（`blk_mq_run_hw_queue`，"We can't run the queue inline with interrupts disabled."）、`:1441/1480/1486`（requeue） |
| 块层→MTD 翻译（mtdblock） | 无自有 worker；**借用 blk-mq 派发上下文**（提交者直接派发 或 kblockd） | 无（每 dev 一个请求队列，但执行者不是专属线程） | (a)-间接：把可能睡眠的闪存操作交给可睡眠上下文 | `drivers/mtd/mtd_blkdevs.c:120`（`mtd_blktrans_work`）、`:166`（`mtd_queue_rq` 直接调用它）、`:332`（`BLK_MQ_F_BLOCKING`）、`:161`（`cond_resched()`）；实证栈回溯见 commit `ca6263a0c950` |
| 闪存器件抽象（Linux MTD core / mtdchar） | **否** | — | erase 同步在调用者上下文 | `drivers/mtd/mtdcore.c:1408`（`master->_erase()`）；`drivers/mtd/mtdchar.c:950`（`mtd_erase()`）；`include/linux/mtd/mtd.h:30-34`（`struct erase_info` 已无 callback） |
| flash 器件驱动（Linux spi-nor / cfi） | **否** | — | WIP 轮询 + `cond_resched()` | `drivers/mtd/spi-nor/core.c:715-739`（`:733` `cond_resched()`）、调用点 `:1712`、`:1842`、`:2122` |
| flash 管理层（Linux UBI） | 是（`ubi->bgt_thread`） | **每 UBI 设备** 1 个 kthread | (b) 后台家务：擦除队列 + 磨损均衡 + 擦洗 | `drivers/mtd/ubi/wl.c:22-24`（注释）、`:1656`（`ubi_thread()`）、`:554-563`（入队 + `wake_up_process`）、`drivers/mtd/ubi/build.c:1025`（`kthread_create`）；另有同步路径 `wl.c:632`（`do_sync_erase`） |
| 文件系统（Linux JFFS2） | 是（GC 线程） | **每 MTD** 1 个 kthread（`jffs2_gcd_mtdN`） | (b) 擦除队列 + GC + 磨损均衡 | `fs/jffs2/background.c:27-45`（SIGHUP 触发 `:27`、`kthread_run` `:45`）；唤醒条件 `fs/jffs2/nodemgmt.c:843-888` |
| 文件系统（Linux f2fs） | 是（flush / discard / GC / checkpoint 四个 kthread + post-read wq） | **每 sb**；post-read wq 为每 sb 一个 `WQ_UNBOUND\|WQ_HIGHPRI` wq | (a) flush 提交-等待；(b) discard/GC/checkpoint；(a) 读后解密/解压/校验卸载 | flush：`fs/f2fs/segment.c:561`（线程）、`:595`（`f2fs_issue_flush`）、`:611-618`（队列空时 inline）、`:623/633`（入队+唤醒）、`:637`（`wait_for_completion`）、`:690`（`kthread_run`）；discard：`:1901`、`:2287`；GC：`fs/f2fs/gc.c:31`、`:189`、`:220`；ckpt：`fs/f2fs/checkpoint.c:1906`、`:1914`；post-read wq：`fs/f2fs/data.c:4178-4187`，使用点 `data.c:316`、`compress.c:1785` |
| 文件系统（littlefs / FatFs / SPIFFS） | **否** | — | 同步 API + 用户提供的锁回调 | littlefs `lfs.h:173-191`、`lfs.c:5955-5961`；FatFs appnote "Re-entrancy"；SPIFFS `spiffs.h:189-195`、`:366` |
| 日志层（jbd2 / ext4） | 是（`jbd2/<dev>` kthread） | **每 journal** 1 个 kthread | (b) commit + checkpoint | `fs/jbd2/journal.c:143-165`（注释）、`:261-263`（`kthread_run`） |
| 设备映射（dm-crypt） | 是（`kcryptd_io-*` + `kcryptd-*`） | **每 dm-crypt 设备** 2 个 wq | (a) 加解密/IO 提交必须在可睡眠上下文 | `drivers/md/dm-crypt.c:1849-1861`（注释）、`:3441-3474`（分配）、`:1960`、`:2162`、`:2240`（提交点） |
| 设备映射（dm-thin） | 是（ordered wq） | **每 pool** 1 个 | (a) 延迟提交 bio（与元数据提交串行） | `drivers/md/dm-thin.c:2988`（`alloc_ordered_workqueue`）、`:431-439`（`wake_worker` 注释） |
| 设备映射（dm-cache） | 是 | **每 cache 设备** 1 个 wq | (a) deferred bio + (b) 数据迁移 | `drivers/md/dm-cache-target.c:2520`、`:370-372`、`:452`、`:460` |
| 设备映射（dm-kcopyd） | 是 | **每 client（每个使用它的 target 实例）** 1 个 wq | (b) 异步拷贝引擎（snapshot/thin/cache 共用） | `drivers/md/dm-kcopyd.c:937`、`:212`、`:650` |
| NVMe 驱动 | 是（`nvme-wq` / `nvme-reset-wq` / `nvme-delete-wq`） | **全局** 3 个（所有 controller 共享） | (b-管理面)：scan/AEN/keep-alive/fw 激活/reset/delete + 相互 flush 串行化（数据面提交仍同步） | 注释 `drivers/nvme/host/core.c:105-115`；分配 `:5051-5063` |
| MMC/SD 卡检测 | 是（`system_freezable_wq`） | **全局** wq，每 host 一个 delayed_work | (b) 卡检测/重扫 | `drivers/mmc/core/core.c:64-74`（注释给出"允许不同 work 并发 + PM 冻结"），`mmc_rescan` `:2207` |
| SDHCI 控制器 | 是（`sdhci`） | **每 host** 1 个（`WQ_UNBOUND\|WQ_MEM_RECLAIM\|WQ_HIGHPRI`） | (a) 请求收尾搬出 IRQ 上下文 | `drivers/mmc/host/sdhci.c:4809`、`:1550`、`:3228` |
| SPI 控制器（总线层） | 是（kthread worker，**不是** workqueue） | **每 controller** 1 个 kthread | (a) message pump：FIFO + 提交-完成；队列空时调用者 inline | `drivers/spi/spi.c:2056`（`kthread_create_worker`）、`:1936-1941`（`spi_pump_messages`）、`:1847`（`__spi_pump_messages`）、`:4498/4523-4535`（`__spi_sync` inline 快速路径）、`:2047`（`sched_set_fifo`） |
| pstore | 是（全局 `system_wq`） | 全局 | (a) 把"读记录"从 timer 上下文推迟出去 | `fs/pstore/platform.c:730-740` |
| Zephyr 系统工作队列 | 是（`k_sys_work_q`，`"sysworkq"`） | **全局** 1 个 | 通用延后处理设施；**flash / NVS / littlefs 都不用它** | `kernel/system_work_q.c:20-34` |
| Zephyr ESP32 flash 驱动 | 是（`k_work` + 专属 `k_work_q`） | **每 flash 设备** 1 个 work + 1 个 wq（`CONFIG_ESP_FLASH_ASYNC_WORK`） | (a) 提交-等待；动机是双核 marshalling（host CPU 才拥有 flash 控制器） | `drivers/flash/flash_esp32.c:63-66`、`:96-109`、`:552-562`、`:591-608`、`:634-652`、`:736-741` |
| NuttX `hpwork`/`lpwork` | 是 | **全局** 2 个队列（可配线程数 `CONFIG_SCHED_HPNTHREADS`/`LPNTHREADS`） | (a) 官方定位："delayed processing from interrupt handlers"；lpwork 面向 "file system clean-up operations" | `include/nuttx/wqueue.h:45-70`（hpwork 定位见 `:47-61`，原文见 §2.6）；实现 `sched/wqueue/kwork_thread.c:187`、`:413` |
| NuttX MTD / littlefs / NXFFS | **否** | — | GC 在调用者上下文 inline | `drivers/mtd/smart.c:4026`（GC 实现）、`:4923/5149/5287`（调用点）；`fs/littlefs/lfs_vfs.c:567`（仅互斥量） |
| RT-Thread `rt_workqueue` | 是（每队列一个 `rt_thread`，另有 `sys_workq`） | **每队列** 1 个线程 | (a)/(b) 通用延后处理；**FAL / DFS / MTD / block 层零引用** | `components/drivers/ipc/workqueue.c:232`、`:297`、`:461` |
| ESP-IDF `esp_flash` | **否**（无专用 task） | — | 调用者上下文同步 + 命令级 `vTaskDelay` 让出 + 分段 | `components/spi_flash/flash_ops.c:69-70`(注释)/`:82-90`(guard)；`components/spi_flash/esp_flash_api.c:640-656`；`components/spi_flash/include/esp_flash.h:62-72`；`components/spi_flash/spi_flash_os_func_app.c:188-199`；`components/spi_flash/Kconfig`（`SPI_FLASH_YIELD_DURING_ERASE` 等） |
| U-Boot | **否**（宏空操作） | — | 单线程主循环；UBI 后台线程被改成同步 drain | `include/linux/compat.h:247-293`；`drivers/mtd/ubi/wl.c:528-557`、`:628`、`:1037`；`doc/develop/cyclic.rst` |
| MCUboot / OpenBLT | **否** | — | 擦写同步调用 | MCUboot `boot/bootutil/src/bootutil_area.c:276`；OpenBLT `Target/Source/ARMCM4_STM32F4/nvm.c:79/109/120/150` |

## 2. 重点章节一：器件驱动层（MTD / flash 器件驱动 / SPI 控制器）用不用 wq

### 2.1 Linux MTD core（flash 器件抽象层本体）：**不用**，同步在调用者上下文

- `mtd_erase()`（`drivers/mtd/mtdcore.c:1375-1420`）把参数转成 `adjinstr` 后直接调用 `master->_erase(master, &adjinstr)`（`mtdcore.c:1408`）并 `return ret`。整个文件里 `workqueue` / `work_struct` / `kthread` / `queue_work` **零命中**。
- 字符设备（ioctl）路径也一样：`MEMERASE` / `MEMERASE64` 分支在 `drivers/mtd/mtdchar.c:950` 直接 `ret = mtd_erase(mtd, erase);`；`mtdchar.c` 同样零命中 wq 相关符号。
- API 类型里已经没有异步完成的位置：`struct erase_info` 在 v6.12 里是
  ```c
  struct erase_info {
          uint64_t addr;
          uint64_t len;
          uint64_t fail_addr;
  };
  ```
  （`include/linux/mtd/mtd.h:30-34`）；`grep -n callback include/linux/mtd/mtd.h` **零命中**。
- 官方理由（两条 commit 原文，2018-02-12，v4.17 合并窗口）：
  - `884cfd9023ce` "mtd: Stop assuming mtd_erase() is asynchronous"：
    > None of the mtd->_erase() implementations work in an asynchronous manner, so let's simplify MTD users that call mtd_erase(). All they need to do is check the value returned by mtd_erase() and assume that != 0 means failure.
  - `e7bfb3fdbde3` "mtd: Stop updating erase_info->state and calling mtd_erase_callback()"：
    > MTD users are no longer checking erase_info->state to determine if the erase operation failed or succeeded. Moreover, mtd_erase_callback() is now a NOP. We can safely get rid of all mtd_erase_callback() calls and all erase_info->state assignments. While at it, get rid of the erase_info->state field, all MTD_ERASE_XXX definitions and the mtd_erase_callback() function.

  即：**连"异步擦除"这个 API 概念都被删掉了**，因为所有实现本来就是同步的。

### 2.2 Linux spi-nor（SPI flash 器件驱动）：**不用**，同步轮询 + `cond_resched()`

- `spi_nor_wait_till_ready_with_timeout()`（`drivers/mtd/spi-nor/core.c:715-739`）：
  ```c
  while (!timeout) {
          if (time_after_eq(jiffies, deadline)) timeout = 1;
          ret = spi_nor_ready(nor);
          if (ret < 0) return ret;
          if (ret) return 0;
          cond_resched();
  }
  ```
  ——在**调用者上下文**轮询状态寄存器直到 WIP 清零，唯一的"让出"手段是 `cond_resched()`（`:733`）。
- erase 路径的调用点：`spi_nor_erase_multi_sector()` 内 `core.c:1712`（以及 `:1842`、`:2122` 的其他擦/写路径）。
- 同层对照：`drivers/mtd/chips/cfi_cmdset_0002.c` 同样是 jiffies 超时轮询（如 `:882`、`:1456`），无 wq。

### 2.3 Linux mtdblock（`mtd_blkdevs.c`，块翻译层）：请求在 **blk-mq 派发上下文**里 inline 执行，没有自己的 worker

- v6.12 的 `drivers/mtd/mtd_blkdevs.c` **没有任何 kthread**（`grep kthread/thread` 只命中函数名 `mtd_blktrans_work`）。**旧内核里是否曾有 per-device `mtd_blktrans_thread` 未取得一手确认，见 §5.5。**
- `mtd_queue_rq()`（`mtd_blkdevs.c:166-181`）：把请求 `list_add_tail` 进 `dev->rq_list`，然后**立即**调用 `mtd_blktrans_work(dev)`，返回 `BLK_STS_OK`。
- `mtd_blktrans_work()`（`mtd_blkdevs.c:120-163`）是一个 `while(1)` 循环：取请求 → `do_blktrans_request()` → `blk_mq_end_request()` → `cond_resched()`（`:161`）。空闲时还会调用翻译层的后台钩子 `tr->background(dev)`（`:133-146`，注释 "Do background processing just once per idle period."）。
- 它靠 tag set 标志告诉块层"这里会睡眠"：`blk_mq_alloc_sq_tag_set(new->tag_set, &mtd_mq_ops, 2, BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_BLOCKING)`（`mtd_blkdevs.c:331-332`）。
- 实证它实际跑在 kblockd 的 worker 里：commit `ca6263a0c950` "mtd_blkdevs: avoid soft lockups with some mtd/spi devices" 的 splat 与原文：
  > With some spi devices, the heavy cpu usage due to polling the spi registers may lead to netdev timeouts, RCU complaints, etc. This can be acute in the absence of CONFIG_PREEMPT. This patch allows to give enough breathing room to avoid those incorrectly detected netdev timeouts for example.

  其 splat：
  ```
  Workqueue: kblockd blk_mq_run_work_fn
  ...
   spi_mem_exec_op
   spi_mem_dirmap_read
   spi_nor_read_data
   spi_nor_read
   mtd_read_oob
   mtd_read
   mtdblock_readsect
   mtd_blktrans_work
   mtd_queue_rq
   blk_mq_dispatch_rq_list
   ...
   __blk_mq_run_hw_queue
  ```
- 结论：mtdblock 的擦/写在"派发上下文"跑（可能是提交者自己的直接派发路径，也可能是 kblockd worker），**不是**"每设备一个 worker + FIFO"结构。

### 2.4 Linux UBI：**有**每设备后台线程 `bgt_thread`，但用途是"后台家务"（擦除队列 + 磨损均衡 + 擦洗）

- 子系统头部注释（`drivers/mtd/ubi/wl.c:22-24`）原文：
  > When physical eraseblocks are returned to the WL sub-system by means of the 'ubi_wl_put_peb()' function, they are scheduled for erasure. The erasure is done asynchronously in context of the per-UBI device background thread, which is also managed by the WL sub-system.
- 线程主体 `ubi_thread()`（`wl.c:1656-1712`，注释 `:1656-1659` "@ubi_thread - UBI background thread."）；创建于 `drivers/mtd/ubi/build.c:1025`（`kthread_create(ubi_thread, ubi, "%s", ubi->bgt_name)`），使能于 `build.c:1056-1059`（`ubi->thread_enabled = 1; wake_up_process(ubi->bgt_thread);`）。
- 工作队列与唤醒：`schedule_ubi_work()`（`wl.c:573-578`，持 `ubi->work_sem` 读锁）→ `__schedule_ubi_work()`（`wl.c:554-563`，尾部 `wake_up_process(ubi->bgt_thread)` 于 `:561`）。触发点：PEB 被 put 回（LEB 释放）、需要 wear-leveling/scrubbing（`ensure_wear_leveling()` `wl.c:1014+`）、`ubi_wl_scrub_peb()`。
- 同时存在同步擦除路径 `do_sync_erase()`（`wl.c:632-644`，注释 "@do_sync_erase - run the erase worker synchronously."），供 WL worker 内部使用（`wl.c:903`、`:920`、`:966`、`:972`）。
- 等待所有 pending work 的 API：`ubi_wl_flush()`（`wl.c:1429`）。
- 归类：**（b）后台家务**。写路径（`ubi_wl_put_peb`）不等待擦除完成；动机是把擦除从写路径上摘掉 + 集中做磨损均衡/擦洗，而不是给上层提供"提交-完成"接口。

### 2.5 Linux 其他器件的 wq（均不在 flash 器件层本体）

| 位置 | 形态 | 用途 | 锚点 |
|---|---|---|---|
| MMC 卡检测/重扫 | 全局 `system_freezable_wq` + 每 host `delayed_work` | (b) 后台探测 | `drivers/mmc/core/core.c:64-74`（注释给出两条理由：多个不同 work 可并发、PM 时队列被冻结）、`mmc_rescan` `:2207` |
| SDHCI 请求收尾 | 每 host `alloc_workqueue("sdhci", WQ_UNBOUND\|WQ_MEM_RECLAIM\|WQ_HIGHPRI, 0)` | (a) 把请求收尾搬出 IRQ 上下文 | `drivers/mmc/host/sdhci.c:4809`；`queue_work(host->complete_wq, &host->complete_work)` 于 `:1550`、`:3228` |
| NVMe 管理面 | 3 个**全局** wq（`nvme-wq`/`nvme-reset-wq`/`nvme-delete-wq`，`WQ_UNBOUND\|WQ_MEM_RECLAIM\|WQ_SYSFS`） | (b-变体) 控制面异步 + 相互 flush 串行化 | 注释 `drivers/nvme/host/core.c:105-115`；分配 `:5051-5063` |
| dm-crypt | 每 dm-crypt 设备 2 个 wq（`kcryptd_io-*` max_active=1；`kcryptd-*` 或 `WQ_CPU_INTENSIVE` max_active=1、或 `WQ_UNBOUND` max_active=ncpus） | (a) 必须在可睡眠上下文做加解密/IO 提交 | 注释 `drivers/md/dm-crypt.c:1849-1861`（"Needed because it would be very unwise to do decryption in an interrupt context."）；分配 `:3441-3474`；提交 `:1960`、`:2162`、`:2240` |
| dm-thin | 每 pool 一个 **ordered** wq（`alloc_ordered_workqueue("dm-thin", WQ_MEM_RECLAIM)`） | (a) 延迟提交 bio（必须与元数据提交串行） | `drivers/md/dm-thin.c:2988`；`wake_worker()` 注释 `:431-439` |
| dm-cache | 每 cache 设备一个 wq（`dm-cache`, `WQ_MEM_RECLAIM`） | (a)+(b)：延迟 bio + 迁移 worker | `drivers/md/dm-cache-target.c:2520`；deferred/migration worker `:370-372`、`:452`、`:460` |
| dm-kcopyd（snapshot/thin/cache 共用） | 每 client（即每个用它的 target 实例）一个 wq（`kcopyd`, `WQ_MEM_RECLAIM`） | (b) 异步拷贝引擎 | `drivers/md/dm-kcopyd.c:937`；kick `:212`；`do_work()` `:650` |
| pstore | 全局 `system_wq` + timer | (a) 从 timer 上下文把"读记录"推迟出去 | `fs/pstore/platform.c:730-740`（`pstore_timefunc` 里 `schedule_work(&pstore_work)`） |
| jbd2（ext4 等日志） | 每 journal 一个 kthread `jbd2/<dev>` | (b) 日志提交 + checkpoint | 注释 `fs/jbd2/journal.c:143-159`；创建 `:261-263` |
| f2fs | 每 sb 多个 kthread（flush/discard/GC/checkpoint）+ 一个 post-read wq | 混合，见 §3 | `fs/f2fs/segment.c:690`（flush）、`:2287`（discard）；post-read wq 定义 `fs/f2fs/data.c:4178-4187`，调用点 `fs/f2fs/super.c:4565` |

### 2.6 NuttX：框架有 `hpwork`/`lpwork`，但 **MTD 层与文件系统层一个都不用**

- 框架的官方定位（`include/nuttx/wqueue.h:47-61` 原文）：
  > CONFIG_SCHED_HPWORK. Create a dedicated "worker" thread to handle delayed processing from interrupt handlers. This feature is required for some drivers but, if there are not complaints, can be safely disabled. ...
  > CONFIG_SCHED_LPWORK. If CONFIG_SCHED_LPWORK is selected then a lower-priority work queue will be created. This lower priority work queue is better suited for more extended processing (such as file system clean-up operations)
- 实现：worker 线程循环 `work_thread()`（`sched/wqueue/kwork_thread.c:187-...`，`for (;;)` 于 `:203`），延时工作由 wdog 驱动（`work_timer_expired()` `:413`），队列就是全局 hpwork/lpwork（`work_start_highpri()` `:643`、`work_start_lowpri()` `:683`）。
- **MTD 驱动层零引用**（本报告逐文件 grep `work_queue` / `work_signal` / `work_cancel` / `HPWORK` / `LPWORK` / `kthread` / `pthread_create`）：`drivers/mtd/smart.c`(188KB)、`ftl.c`、`sector512.c`、`dhara.c`、`m25px.c`、`mtd_config.c`、`nvblk.c`、`mtd_rwbuffer.c` 全部 0 命中。
- SMART FS 的垃圾回收是在调用者上下文 inline 跑的：`smart_garbagecollect()`（`drivers/mtd/smart.c:4018-4026`，注释 "Performs garbage collection if needed. This is determined by the count of released sectors relative to free and total sectors."），调用点 `smart.c:4923`、`:5149`、`:5287`，都在写/释放路径里。
- littlefs 的 NuttX 移植层只有互斥量：`fs/littlefs/lfs_vfs.c:567`（`nxmutex_lock`）等，无 wq。
- NXFFS：`fs/nxffs/nxffs_initialize.c` 无 work_queue/kthread/GC 线程。
- 结论：NuttX 的 workqueue 是"给驱动做中断延后处理/系统级清理"的通用设施，**flash 器件驱动与文件系统不用它**。

### 2.7 Zephyr：系统工作队列存在，但 flash / NVS / littlefs **全同步**（唯一例外是 ESP32 flash 驱动）

- 系统工作队列：`struct k_work_q k_sys_work_q;`（`kernel/system_work_q.c:20`，v4.1.0），配置 `.name = "sysworkq"`、`CONFIG_SYSTEM_WORKQUEUE_PRIORITY`（v4.1.0 `:22-34`）；声明见 `include/zephyr/kernel.h`（main 分支 `:3488`）的 `extern struct k_work_q k_sys_work_q;`。
- 代码搜索（GitHub code search，`repo:zephyrproject-rtos/zephyr`，main 分支）：
  - `path:subsys/fs + k_work` → **0**
  - `path:drivers/mtd + k_work` → **0**
  - `path:subsys/storage + k_work` → **0**
  - `path:drivers/flash + k_work` → **2**（`drivers/flash/flash_esp32.c`、`drivers/flash/flash_stm32wba_fm.c`）
- 具体驱动（v4.1.0 源码实测，锁都是同步原语）：
  - `drivers/flash/spi_nor.c`：`k_sem`（`:160`）当设备互斥，`acquire_device()`/`release_device()`（`:576-602`）——读写擦在调用者上下文跑。
  - `subsys/fs/nvs/nvs.c`：只有 `k_mutex`（`:761`、`:978`、`:1012`、`:1157`、`:1192`、`:1390`、`:1400`）。
  - `subsys/fs/littlefs_fs.c`：只有 `k_mutex`（`:93`、`:98`、`:927`、`:992`）。
- 例外（值得单独看）：`drivers/flash/flash_esp32.c`（main 分支，`CONFIG_ESP_FLASH_ASYNC` 打开时）确实实现了"每设备 worker + 提交/等待"：
  - `K_THREAD_STACK_DEFINE(esp_flash_workqueue_stack, ...)` + `static struct k_work_q esp_flash_workqueue;`（`:63-66`），`k_work_queue_start(...)`（`:737-741`）；
  - per-device `struct flash_req` + `k_mutex lock` + `k_work work` + `k_sem sync`（`:96-109`）；
  - 同步 API 的形态就是"提交 + 等"：
    ```c
    if (k_mutex_lock(&data->lock, K_SECONDS(CONFIG_ESP_FLASH_ASYNC_TIMEOUT))) return -ETIMEDOUT;
    req->op = FLASH_OP_ERASE; ...
    k_work_submit(&data->work);
    k_sem_take(&data->sync, FLASH_SEM_TIMEOUT);
    k_mutex_unlock(&data->lock);
    ```
    （`flash_esp32_erase_async()` `:591-608`；read/write 同形 `:541-565`、`:567-589`）
  - work handler `flash_worker()`（`:634-652`）：在"host"CPU 上直接执行 `flash_process_request()` 并 `k_sem_give()`；在 remote CPU 上则通过 IPM（核间消息）把请求转给 host CPU（`ipm_send(data->ipm, -1, CMD_REQUEST, &data->req, sizeof(struct flash_req))`）。
  - 该例外不是"为了不阻塞毫秒级擦写"，而是**双核拓扑**（flash 控制器归 host CPU 所有，remote CPU 必须把请求 marshalling 过去）+ 异步 API 语义需要。

### 2.8 RT-Thread：有 `rt_workqueue`，但 FAL / DFS / MTD / block 层**零引用**

- 框架实现（`components/drivers/ipc/workqueue.c`，master）：`rt_workqueue_create()` 为每个队列 `rt_thread_create(name, _workqueue_thread_entry, queue, stack_size, priority, 10)`（`:232`）；提交接口 `rt_workqueue_submit_work()`（`:297`）、`rt_workqueue_urgent_work()`（`:315`）；系统队列 `sys_workq`（`:412`、`:461`，`RT_SYSTEM_WORKQUEUE_STACKSIZE`）。形态是"work 项 + 延时 ticks"，**没有"完成通知/等待"语义**。
- 代码搜索（`repo:RT-Thread/rt-thread`，master）：
  - `workqueue path:components/dfs` → **0**
  - `workqueue path:components/drivers/mtd` → **0**
  - `workqueue path:components/drivers/block` → **0**
  - 对照查询 `workqueue path:components/drivers/wlan` → 6（确认语法有效）
- FAL（Flash Abstraction Layer，`RT-Thread-packages/fal@master`）：`src/fal.c`、`src/fal_flash.c`、`src/fal_rtt.c` 中 `workqueue`/`rt_thread`/`rt_sem`/`rt_mutex` **全部 0 命中**——纯粹的同步薄封装。
- SFUD（`armink/SFUD@master`，`sfud/src/sfud.c`）：无 workqueue/线程，只有可选的 SPI 总线锁回调 `spi->lock(spi)` / `spi->unlock(spi)`（`:419-421`、`:450-452`、`:473-475`、`:503-505`、`:543-545`、`:604-606` …），擦/写/读同步执行。
- MTD 层存在（`components/drivers/mtd/`：`mtd_nor.c`、`mtd_nor_spi`、`mtd-spi-nor.c`、`mtd_nand.c` 等），但不引用 workqueue。

### 2.9 ESP-IDF：擦/写在**调用者上下文**同步执行；靠"分段 + 让出"而不是 worker task

- 锁模型 = 关中断/关 cache/停另一个核，而不是互斥量或线程（`components/spi_flash/flash_ops.c:82-90`）：
  ```c
  const DRAM_ATTR spi_flash_guard_funcs_t g_flash_guard_default_ops = {
      .start = spi_flash_disable_interrupts_caches_and_other_cpu,
      .end   = spi_flash_enable_interrupts_caches_and_other_cpu,
  };
  ```
- 抗饿死手段一：**分段**。`flash_ops.c:69-70` 原文注释：
  > /* Limit number of bytes written/read in a single SPI operation,
  >    as these operations disable all higher priority tasks from running. */
  （`MAX_WRITE_CHUNK` 默认 8192、`MAX_READ_CHUNK` 16384）
- 抗饿死手段二：**在 erase 过程中让出 CPU**（不是把工作交给别的线程）。`components/spi_flash/esp_flash_api.c:640` 注释 "// Don't lock the SPI flash for the entire erase, as this may be very long"，`:650-656` 在每条擦除命令前调用 `chip->chip_drv->yield(chip, 0)`。
- OS 回调契约写在公开头文件里（`components/spi_flash/include/esp_flash.h:62-72`）：
  > /** Yield to other tasks. Called during erase operations. */
  > esp_err_t (*check_yield)(void *arg, uint32_t chip_status, uint32_t* out_request);
  > /** Yield to other tasks. Called during erase operations. */
  > esp_err_t (*yield)(void *arg, uint32_t* out_status);
- 默认实现就是 `vTaskDelay`（`components/spi_flash/spi_flash_os_func_app.c:188-199`）：
  ```c
  static IRAM_ATTR esp_err_t spi_flash_os_yield(void *arg, uint32_t* out_status)
  {
      if (likely(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)) {
          vTaskDelay(CONFIG_SPI_FLASH_ERASE_YIELD_TICKS);
      }
      ...
  }
  ```
  时间算法注释见同文件 `:39-49`。
- Kconfig 官方措辞（`components/spi_flash/Kconfig`）：
  - `SPI_FLASH_YIELD_DURING_ERASE`（默认 y）："This allows to yield the CPUs between erase commands. **Prevents starvation of other tasks.** Please use this configuration together with SPI_FLASH_ERASE_YIELD_DURATION_MS and SPI_FLASH_ERASE_YIELD_TICKS after carefully checking flash datasheet to avoid a watchdog timeout."
  - `SPI_FLASH_ERASE_YIELD_DURATION_MS`（默认 20）："If a duration of one erase command is large then it will yield CPUs after finishing a current command."
  - `SPI_FLASH_ERASE_YIELD_TICKS`（默认 1）："Defines how many ticks will be before returning to continue a erasing."
  - `SPI_FLASH_WRITE_CHUNK_SIZE`："smaller value here ensures that cache (and non-IRAM resident interrupts) remains disabled for shorter duration."
  - `SPI_FLASH_BYPASS_BLOCK_ERASE`："This will be much slower overall in most cases, but improves latency for other code to run."
- 结论：ESP-IDF 面对的问题与"忙等饿死低优线程"完全一致，但它的解法是**调用者上下文 + 命令级让出 + 分段**，没有专用擦写 task。

### 2.10 U-Boot：单线程模型；workqueue/kthread 是宏空操作，UBI 的后台线程被改成同步 drain

- `include/linux/compat.h`（master）原文：
  ```c
  #define kthread_should_stop(...)   0                      // :247
  #define schedule_work(work)        do {} while (0)        // :251
  #define INIT_WORK(work, fun)       do {} while (0)        // :252
  struct work_struct {};                                    // :254
  #define kthread_create(...)        __builtin_return_address(0)  // :291
  #define kthread_stop(...)          do { } while (0)       // :292
  #define wake_up_process(...)       do { } while (0)       // :293
  ```
- U-Boot 的 UBI 移植把 Linux 的"每设备后台线程"**替换成调用者上下文的同步 drain**：`void ubi_do_worker(struct ubi_device *ubi)`（`drivers/mtd/ubi/wl.c:528-557`，`#ifdef __UBOOT__`）：
  > /* call do_work, which executes exactly one work form the queue, including removeing it from the work queue. */
  （循环直到 `ubi->works` 空），调用点 `wl.c:628`、`wl.c:1037`。注意 `wl.c:88-99` 的 Linux 头文件包含（`<linux/kthread.h>` 等）整段在 `#ifndef __UBOOT__` 内。
- 周期性任务的替代品是主循环里跑的 cyclic 框架（`doc/develop/cyclic.rst` 原文）：
  > The cyclic infrastructure integrates cyclic_run(), the main function responsible for calling all registered cyclic functions, into the common schedule() function. This guarantees that cyclic_run() is executed very often...

### 2.11 MCUboot / OpenBLT：擦写同步，无异步机制

- MCUboot（`mcu-tools/mcuboot@main`）：`boot/bootutil/src/*.c` 中除 3 处注释 "FIXME: this might have to be updated for threaded sim"（`swap_move.c:37`、`swap_offset.c:38`、`swap_scratch.c:37`，指 host 端模拟器）外，**没有任何 thread/work 引用**；`flash_area_erase()` 在 swap 路径里同步调用（`boot/bootutil/src/bootutil_area.c:276`），随后同步 `flash_area_write()`。
- OpenBLT（`feaser/openblt@master`）：`Target/Source/<arch>/nvm.c` 里 `NvmWrite()`（`:79`）→（可选 hook）→ `FlashWrite(addr, len, data)`（`:109`）；`NvmErase()`（`:120`）→ `FlashErase(addr, len)`（`:150`）。都是"调用-返回 bool"的同步调用，由 XCP 命令处理器（`cop.c`）直接调用，无任何异步/线程层。

### 2.12 littlefs / FatFs：无后台线程，线程安全靠"用户提供的锁回调"

- littlefs（`littlefs-project/littlefs@v2.11.0`）：
  - 块设备回调本身就是同步语义（`lfs.h:173-181`）："Erase a block. A block must be erased before being programmed. The state of an erased block is undefined. Negative error codes are propagated to the user."；`sync` 回调 "Sync the state of the underlying block device."
  - 线程安全是可选的外部锁（`lfs.h:183-191`，`#ifdef LFS_THREADSAFE` 下的 `lock`/`unlock` 回调），并且 `LFS_THREADSAFE` 只是把公开 API 包一层：`lfs.c:5955-5961` 注释 "Here we can add tracing/thread safety easily / Thread-safe wrappers if enabled"。
  - 全仓库无 thread/wq 依赖。
- FatFs：官方 appnote "Re-entrancy" 节（<https://elm-chan.org/fsw/ff/doc/appnote.html>）原文：
  > The file operations of two tasks to the same volume is not thread-safe by default. FatFs can also be configured to make it thread-safe by an option FF_FS_REENTRANT. When a file function is called while the volume is being accessed by another task, the file function to the volume will be suspended until that task leaves the file function. ... To enable this feature, OS dependent synchronization control functions, ff_mutex_create/ff_mutex_delete/ff_mutex_take/ff_mutex_give, need to be added to the project.
  ——即 FatFs 的并发模型是"互斥量 + 调用者阻塞"，全文档没有后台线程/工作队列概念。

## 3. 重点章节二：(a) 提交-完成模型 vs (b) 后台家务 分类表

判定口径（按各栈自己的注释/文档，不按我们的猜测）：
- **(a) 提交-完成**：工作由"别处"执行是为了**上下文合法**（当前上下文不能睡眠 / 在 IRQ / 在软中断 / 不能做加密），调用者要么阻塞等待（completion/sem），要么拿完成回调；**动机是"这里不能做"，不是"这件事很久"**。
- **(b) 后台家务**：没有调用者在等；由内部状态触发（空闲、空间不足、脏块比例、周期性、事件）。

| 案例 | 归类 | 为什么 / 触发者 | 调用者等待？ | 锚点 |
|---|---|---|---|---|
| Linux blk-mq `blk_mq_run_work_fn` / `blk_mq_delay_run_hw_queue` | **(a) 的变体：延迟派发** | 注释直说 "We can't run the queue inline with interrupts disabled."；另外当提交者不在该 hctx 的 CPU 上时也要搬走 | 否（提交者返回，请求由块层继续推进） | `block/blk-mq.c:2217`、`:2194-2203`、`:2408-2415` |
| Linux blk-mq requeue（`blk_mq_requeue_work`） | (a) 的变体 | 驱动 requeue 后延迟重发 | 否 | `blk-mq.c:1441-1478`、`:1480-1491` |
| Linux mtdblock 的 `tr->background` | **(b)** | "Do background processing just once per idle period." | 否 | `mtd_blkdevs.c:133-146` |
| Linux UBI `bgt_thread` | **(b)** | PEB 被 put 回、需要 WL/擦洗；注释："The erasure is done asynchronously in context of the per-UBI device background thread" | **否**（`ubi_wl_put_peb` 不等待）；`ubi_wl_flush()` 是显式等待接口 | `wl.c:22-24`、`:554-563`、`:1429` |
| Linux JFFS2 `jffs2_gcd_mtdN` | **(b)** | 唤醒条件：有 pending 擦除块 / 有未检查节点 / 空闲块低于阈值 / very-dirty 块够多 | 否 | `background.c:27-45`、`nodemgmt.c:843-888` |
| **Linux f2fs flush 线程** | **(a)** | 每次需要写屏障的提交；队列里已有别人时入队交给线程，**调用者阻塞** `wait_for_completion`；队列空时 **inline 自己跑** | **是**（同步路径）| `segment.c:595`、`:611-618`（inline 快速路径）、`:623/633`（入队+唤醒）、`:637/644`（等待）、`:561/690`（线程） |
| Linux f2fs discard / GC / checkpoint | **(b)** | 周期性、空间压力、GC 策略 | 否 | `segment.c:1901/2287`；`gc.c:31/189/220`；`checkpoint.c:1906/1914` |
| Linux f2fs post-read wq | **(a)** | 只在有 encrypt/verity/compression 时创建，把解密/解压/校验从 bio 完成路径挪走 | 否（异步续跑） | `data.c:4178-4187`，使用点 `data.c:316`、`compress.c:1785` |
| Linux dm-crypt `kcryptd*` | **(a)** | 注释："Needed because it would be very unwise to do decryption in an interrupt context. ... They must be separated as otherwise the final stages could be starved by new requests which can block in the first stages due to memory allocation." | 间接（bio 完成链） | `dm-crypt.c:1849-1861` |
| Linux dm-thin pool worker | **(a)** | 延迟 bio 提交（与元数据提交串行） | 间接 | `dm-thin.c:431-439`、`:2988` |
| Linux dm-cache migration worker | **(b)** | 迁移/promote/demote 策略 | 否 | `dm-cache-target.c:460`、`:2520` |
| Linux dm-kcopyd | **(b)** | 异步拷贝引擎（供 snapshot/thin/cache） | 否（完成后回调） | `dm-kcopyd.c:212`、`:650`、`:937` |
| Linux NVMe 3 个 wq | **(b)-管理面** | AEN / keep-alive / reset / delete；三者用 flush 关系串行化 | 否（数据面 I/O 提交仍同步） | `nvme/core.c:105-115`、`:5051-5063` |
| Linux MMC `mmc_rescan` | **(b)** | 卡检测/延迟重扫 | 否 | `mmc/core.c:64-74`、`:2207` |
| Linux SDHCI `complete_wq` | **(a)** | 请求在 IRQ 中完成，收尾需要睡眠上下文 | 否（完成链继续走） | `sdhci.c:1548-1552`、`:3178`、`:4809` |
| **Linux SPI message pump** | **(a)** | `spi_async` 文档写明可在 "in_irq and other contexts which can't sleep" 调用，所以真正的传输必须由线程做；`spi_sync` 是"包装这个核心异步原语"的同步外壳（队列空时 inline） | 同步路径 **是**（completion）；异步路径否 | `spi.c:4403-4430`（`spi_async` 文档）、`:4498-4535`（`__spi_sync`）、`:1936-1941`、`:2056`、`:2047` |
| Linux pstore | **(a)** | timer 上下文里只置标志，读记录交给全局 `system_wq` | 否 | `platform.c:730-740` |
| Linux jbd2 `kjournald2` | **(b)** | 事务提交周期 / checkpoint（fs 侧的 wait 是日志语义，不是"提交长操作给 worker"） | 否（后台推进） | `journal.c:143-165`、`:261-263` |
| **Zephyr ESP32 flash 驱动** | **(a)** | 每次 flash API 调用都 `k_work_submit` + `k_sem_take`；动机是双核 marshalling（host CPU 拥有 flash 控制器） | **是**（`k_sem_take`） | `drivers/flash/flash_esp32.c:552-562`、`:591-608`、`:634-652` |
| NuttX `hpwork`/`lpwork` | **(a) 为主** | 官方定位："handle delayed processing from interrupt handlers" | 看调用者（`work_signal` 可等） | `include/nuttx/wqueue.h:47-61` |
| RT-Thread `rt_workqueue` | **(a) 为主** | `rt_workqueue_submit_work()` 提交 work 项（带延时 ticks），**没有完成通知/等待语义** | 否 | `components/drivers/ipc/workqueue.c:232`、`:297` |

**分类小结（关键观察）**

1. 在 **flash 器件驱动层**里，(a) 型 worker 只出现在两个样本上，且都不是因为"擦写毫秒级太长"：
   - Linux SPI message pump：因为 `spi_async` 允许在 IRQ 上下文调用（上下文合法性），并顺带解决多设备共用总线时的序列化与 RT 优先级（`sched_set_fifo`）。
   - Zephyr ESP32 flash 驱动：因为双核拓扑（remote CPU 必须把请求经 IPM 送进 host CPU）。
2. 真正"擦除很长，不要让调用者忙等"的诉求，成熟栈的两种解法是：
   - **UBI / JFFS2 型**：把擦除放进**后台家务线程**，调用者是 fire-and-forget（写路径只把 PEB 标记为待擦除），代价是需要 `ubi_wl_flush()` 这类显式同步点；
   - **mtdblock / spi-nor / ESP-IDF 型**：**仍然同步执行**，但 (i) 在可睡眠的上下文里执行（`BLK_MQ_F_BLOCKING`），(ii) 用 `cond_resched()` / `vTaskDelay()` 在长操作中间主动让出（ESP-IDF 还有分段上限）。
3. "提交-完成 + 调用者等待"的完整实现（与本项目形态最接近）在**非 flash 层**里反复出现，且注释都指向"上下文合法性"或"顺序保持"：f2fs flush、dm-crypt kcryptd、Zephyr ESP32 flash（拓扑）、Linux SPI pump。

**对照一句话（io_uring / aio 与内核 wq 的关系）**：io_uring 不复用内核 workqueue 子系统，而是自带一个"worker 线程池"（`io_uring/io-wq.c:1-6` 头注释原文 "Basic worker thread pool for io_uring"；每个 io_uring 实例 `io_wq_create()` `:1148`，内部分 bound/unbound 两类账目 `IO_WQ_ACCT_BOUND`/`IO_WQ_ACCT_UNBOUND` `:90`、`:1172`）来承接那些**必须阻塞**的操作；它存在的理由同样是"当前上下文不能做这件事"，而不是"操作很长"。（Linux 原生 aio 的具体回退路径本次未取证，见 §5。）

## 4. 反面证据：明确不用的框架 + 官方理由原文

### 4.1 Linux MTD：erase 明确声明为同步，异步回调机制被删除

- commit `884cfd9023ce`（Boris Brezillon，2018-02-12，v4.17）"mtd: Stop assuming mtd_erase() is asynchronous"：
  > None of the mtd->_erase() implementations work in an asynchronous manner, so let's simplify MTD users that call mtd_erase(). All they need to do is check the value returned by mtd_erase() and assume that != 0 means failure.
- commit `e7bfb3fdbde3`（同作者/同期）"mtd: Stop updating erase_info->state and calling mtd_erase_callback()"：
  > MTD users are no longer checking erase_info->state to determine if the erase operation failed or succeeded. Moreover, mtd_erase_callback() is now a NOP. We can safely get rid of all mtd_erase_callback() calls and all erase_info->state assignments. While at it, get rid of the erase_info->state field, all MTD_ERASE_XXX definitions and the mtd_erase_callback() function.
- 现状核验（v6.12）：`include/linux/mtd/mtd.h:30-34` 的 `struct erase_info` 只有 `addr/len/fail_addr`；`grep -n callback include/linux/mtd/mtd.h` **零命中**；`drivers/mtd/mtdcore.c`、`drivers/mtd/mtdchar.c`、`drivers/mtd/spi-nor/core.c` 对 `workqueue|work_struct|kthread|queue_work` 全部**零命中**。

### 4.2 Linux mtdblock：不持有自己的 worker，改为"在可睡眠的派发上下文里 inline 跑 + 主动让出"

- 代码里没有 kthread（v6.12 `mtd_blkdevs.c` 只命中函数名 `mtd_blktrans_work`）；`mtd_queue_rq()` 直接调用它（`:166-181`）。
- 声明"我可能睡眠"：`BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_BLOCKING`（`:332`）。
- 对"长操作饿死别人"的官方解法是 `cond_resched()`（`:161`）+ commit `ca6263a0c950`：
  > With some spi devices, the heavy cpu usage due to polling the spi registers may lead to netdev timeouts, RCU complaints, etc. This can be acute in the absence of CONFIG_PREEMPT. This patch allows to give enough breathing room to avoid those incorrectly detected netdev timeouts for example.

  （splat 显示该路径实际跑在 `Workqueue: kblockd blk_mq_run_work_fn` 上，链路 `mtd_queue_rq → mtd_blktrans_work → mtdblock_readsect → mtd_read → spi_nor_read → spi_mem_exec_op`。）

### 4.3 U-Boot：显式地把 workqueue/kthread 定义成空操作，并把 UBI 后台线程改成同步 drain

- `include/linux/compat.h`（master）原文：
  ```c
  #define kthread_should_stop(...)	0                        /* :247 */
  #define schedule_work(work)		do {} while (0)      /* :251 */
  #define INIT_WORK(work, fun)		do {} while (0)      /* :252 */
  struct work_struct {};                                            /* :254 */
  #define kthread_create(...)		__builtin_return_address(0)  /* :291 */
  #define kthread_stop(...)		do { } while (0)     /* :292 */
  #define wake_up_process(...)		do { } while (0)     /* :293 */
  ```
- `drivers/mtd/ubi/wl.c:528-557`（`#ifdef __UBOOT__`）新增 `ubi_do_worker()`，注释：
  > call do_work, which executes exactly one work form the queue, including removeing it from the work queue.
  在调用者上下文里循环 drain 整个 `ubi->works`；调用点 `wl.c:628`、`wl.c:1037`。
- 周期任务走主循环而不是线程：`doc/develop/cyclic.rst`
  > The cyclic infrastructure integrates cyclic_run(), the main function responsible for calling all registered cyclic functions, into the common schedule() function. This guarantees that cyclic_run() is executed very often...

### 4.4 NuttX：框架存在但"不是给存储用的"

- `include/nuttx/wqueue.h:47-61` 把 hpwork 的用途限定为"handle delayed processing from interrupt handlers"，并把 lpwork 定位为 "better suited for more extended processing (such as file system clean-up operations)"。
- 逐文件 grep 结果：`drivers/mtd/{smart.c,ftl.c,sector512.c,dhara.c,m25px.c,mtd_config.c,nvblk.c,mtd_rwbuffer.c}` 对 `work_queue|work_signal|work_cancel|HPWORK|LPWORK|kthread|pthread_create|task_create` **全部零命中**；SMART FS 的 GC 由写路径直接 inline 调用（`smart.c:4923/5149/5287` → `smart_garbagecollect()` `smart.c:4026`）。
- NuttX littlefs 移植层只用 `nxmutex`（`fs/littlefs/lfs_vfs.c:567` 等），NXFFS 初始化里没有线程/GC 线程（`fs/nxffs/nxffs_initialize.c` 零命中）。

### 4.5 Zephyr：flash / NVS / littlefs / storage 子系统不用 k_work

- 代码搜索（main）：`path:subsys/fs + k_work`=0、`path:subsys/storage + k_work`=0、`path:drivers/mtd + k_work`=0；`path:drivers/flash + k_work`=2（只有 `flash_esp32.c` 与 `flash_stm32wba_fm.c` 两个例外）。
- v4.1.0 源码实测：`drivers/flash/spi_nor.c` 用 `k_sem`（`:160/584/600`）；`subsys/fs/nvs/nvs.c` 只用 `k_mutex`（`:761/978/1012/1157/1192/1390/1400`）；`subsys/fs/littlefs_fs.c` 只用 `k_mutex`（`:93/98/927/992`）。

### 4.6 RT-Thread：workqueue 存在，但 FAL / DFS / MTD / block 层零引用

- 代码搜索（master）：`workqueue path:components/dfs`=**0**、`workqueue path:components/drivers/mtd`=**0**、`workqueue path:components/drivers/block`=**0**（对照：`workqueue path:components/drivers/wlan`=6，证明查询语法有效）。
- FAL：`src/fal.c`、`src/fal_flash.c`、`src/fal_rtt.c` 全部零命中（无 workqueue/线程/信号量）。
- SFUD：无 workqueue/线程，只有可选 SPI 总线锁回调 `spi->lock()` / `spi->unlock()`（`sfud/src/sfud.c:419-421`、`:450-452`、`:473-475`、`:503-505`、`:543-545`、`:604-606` …）。

### 4.7 ESP-IDF：官方措辞就是"在调用者上下文里让出"，不是"交给专用 task"

- `components/spi_flash/flash_ops.c:69-70`：
  > /* Limit number of bytes written/read in a single SPI operation,
  >    as these operations disable all higher priority tasks from running. */
- `components/spi_flash/esp_flash_api.c:640`：`// Don't lock the SPI flash for the entire erase, as this may be very long`
- `components/spi_flash/Kconfig`：
  - `SPI_FLASH_YIELD_DURING_ERASE`（default y）："This allows to yield the CPUs between erase commands. **Prevents starvation of other tasks.**"
  - `SPI_FLASH_ERASE_YIELD_DURATION_MS`（default 20）："If a duration of one erase command is large then it will yield CPUs after finishing a current command."
  - `SPI_FLASH_ERASE_YIELD_TICKS`（default 1）："Defines how many ticks will be before returning to continue a erasing."
  - `SPI_FLASH_WRITE_CHUNK_SIZE`（default 8192）："smaller value here ensures that cache (and non-IRAM resident interrupts) remains disabled for shorter duration."
  - `SPI_FLASH_BYPASS_BLOCK_ERASE`："This will be much slower overall in most cases, but improves latency for other code to run."
- 默认 `yield` 实现 = `vTaskDelay(...)`（`components/spi_flash/spi_flash_os_func_app.c:188-199`）。

### 4.8 无 OS 耦合的嵌入式文件系统：只用用户提供的锁回调

- littlefs（v2.11.0）：`lfs.h:183-191` 的 `lock`/`unlock` 回调（`#ifdef LFS_THREADSAFE`）+ `lfs.c:5955-5961` 的宏包装；块设备回调 `read/prog/erase/sync` 语义全部同步。
- FatFs（官方 appnote，<https://elm-chan.org/fsw/ff/doc/appnote.html>，"Re-entrancy" 节）：
  > The file operations of two tasks to the same volume is not thread-safe by default. FatFs can also be configured to make it thread-safe by an option FF_FS_REENTRANT. ... To enable this feature, OS dependent synchronization control functions, ff_mutex_create/ff_mutex_delete/ff_mutex_take/ff_mutex_give, need to be added to the project.
- SPIFFS（pellepl/spiffs，master）：`spiffs.h:189-195` 默认空的 `SPIFFS_LOCK(fs)` / `SPIFFS_UNLOCK(fs)` 宏；`:366` 文档 "Note: this function is not protected with SPIFFS_LOCK and SPIFFS_UNLOCK macros." —— 同样是"外部锁"，全仓库无后台线程。

### 4.9 Bootloader：MCUboot / OpenBLT 擦写全程同步

- MCUboot：`boot/bootutil/src/` 无任何线程/工作队列引用（唯一 3 处 "threaded sim" 注释指 host 模拟器：`swap_move.c:37`、`swap_offset.c:38`、`swap_scratch.c:37`）；`flash_area_erase()` 同步调用（`bootutil_area.c:276`）后同步 `flash_area_write()`。
- OpenBLT：`Target/Source/ARMCM4_STM32F4/nvm.c:79` `NvmWrite()` → `:109` `FlashWrite()`；`:120` `NvmErase()` → `:150` `FlashErase()`，返回 bool，由 XCP 命令处理器直接调用。

## 5. ⚠️ 未证实项（没有一手来源，或只拿到间接证据）

1. **NuttX 的 SPI 总线层是否有自己的 worker 线程**：`drivers/spi/spi.c` 在 master **不存在**（API 返回 404），我两次尝试取 `drivers/spi/` 目录清单都失败（gitbox 302/超时）。**未证实**（网上常见的"NuttX SPI 有 `spi_work` worker 线程"说法，本次没有拿到一手来源，因此不作为结论）。
2. **Zephyr `drivers/flash/flash_stm32wba_fm.c` 里 `k_work` 的用途**：只有 code search 命中（`path:drivers/flash + k_work` = 2），未读该文件源码，未归类。
3. **Zephyr ESP32 flash 驱动为什么需要 async**：代码结构显示是"host CPU 拥有 flash 控制器 + remote CPU 经 IPM 转发"（`flash_esp32.c:644-651`），但 **`CONFIG_ESP_FLASH_ASYNC` 的 Kconfig help 原文没取到**（推测的 `drivers/flash/Kconfig.esp32` 取回为空，未再定位），因此动机措辞没有官方文字支撑。
4. **Linux dm-crypt 的形态演进（老版每 CPU kcryptd 线程 → 新版 workqueue）**：没有定位到引入 workqueue 的 commit。只有：注释 `dm-crypt.c:1852-1860` 说 "The work is done per CPU global for all dm-crypt instances."（与现在"按 dm-crypt 设备分配 wq"的代码不一致，疑为**过时注释**），以及后续 commit 标题 `ed0302e83098` "dm crypt: make workqueue names device-specific"、`2285e1496dc6` "dm-crypt: export sysfs of all workqueues"。**演进细节未证实**。
5. **Linux mtdblock 在旧内核里是否有 per-device kthread**：`mtd_blkdevs.c` 的历史里确有 `891b7c5fbf61 mtd_blkdevs: convert to blk-mq`（2018-10-16, Jens Axboe），但其 commit message 只有 "Straight forward conversion, using an internal list to enable the driver to pull requests at will."，**没有说线程的去留**；"旧版有 `mtd_blktrans_thread`"这一说法本次**未取得一手确认**（已验证的只有：v6.12 里没有线程，且实际执行发生在 blk-mq 派发上下文，见 `ca6263a0c950` 的 splat）。
6. **ESP-IDF 是否还有别的机制承担 flash 操作**（例如 `components/spi_flash/esp_flash_spi_init.c`、总线锁持有者、Linux target 实现）：未逐一检查 `components/spi_flash/` 下其他文件。
7. **RT-Thread 的 BSP 层 SPI flash 驱动**（`drv_spi_flash.c` 之类）是否使用 workqueue：未检查；本次只验证了 `components/drivers/{ipc,dfs,mtd,block}` 与 FAL/SFUD 两个包。
8. **SPIFFS 的版本/commit**：抓到的是 `pellepl/spiffs@master` 的 `src/spiffs.h`，未记录具体 commit sha。
9. **被网络策略拦截、因此全程未使用的文档源**：`docs.zephyrproject.org`、`docs.espressif.com`（另加 `raw.githubusercontent.com`、`cdn.jsdelivr.net` 两个源码镜像不可达）。Zephyr / ESP-IDF 的结论全部改由源码 + 源码内嵌注释/Kconfig help 支撑，**没有引用官方文档页面**。
10. **Zephyr 的 GitHub code search 只覆盖默认分支（main）**，`path:... + k_work` 的 0 命中结论对 v4.1.0 是"以源码实测（NVS/littlefs/spi_nor/ESP32）复核过"的，但未对 v4.1.0 全仓库做等价搜索。
11. **NuttX 官方文档站**（`nuttx.apache.org` 的尝试路径 404）：未取到文档页面，wqueue 的定位改用 `include/nuttx/wqueue.h` 源码注释。
12. **U-Boot "单线程"的官方文字声明**：没找到（只在 `doc/develop/cyclic.rst` 里找到"周期函数在主循环 schedule() 里跑"的描述）。U-Boot 不用 wq 的证据来自 `include/linux/compat.h` 的宏空操作 + `#ifndef __UBOOT__` 的 Linux 头文件包含 + `ubi_do_worker()` 的同步 drain，**不是**某句官方声明。
13. **Linux 原生 aio（`io_submit`）在 buffered I/O 上是否回退到 worker**：本次**未取证**，因此 §3 末尾只对 io_uring（io-wq）下了结论，没有展开 aio。

## 附录：取源方法与可复现细节

**可用取源路径（本次实测）**

| 站点 | 结果 | 用途 |
|---|---|---|
| `git.kernel.org/.../plain/<path>?h=v6.12` | 200，快 | Linux 内核全部文件 |
| `api.github.com` + `gh` CLI（已登录） | 200，5000/h | 任意 GitHub 仓库单文件（`Accept: application/vnd.github.raw`）、`search/code`、`search/commits` |
| `codeload.github.com/<owner>/<repo>/tar.gz/refs/tags/...` | 200（但本次限速 ~35KB/s，>4MB 易被 `-m` 截断） | littlefs（279KB）、MCUboot（~1MB）小仓库 |
| `github.com/<o>/<r>/blob/<ref>/<path>` | 200，可解析 `react-app.embeddedData` JSON 的 `codeViewBlobLayoutRoute.StyledBlob.rawLines` 得到带行号源码 | 无 API 配额时的替代方案（本次用于 Zephyr v4.1.0） |
| `gitbox.apache.org/repos/asf?p=nuttx.git;a=blob_plain;f=<path>;hb=HEAD` | 200 | NuttX |
| `source.denx.de/u-boot/u-boot/-/raw/master/<path>` | 200（需 `-L`） | U-Boot 文档 |
| `elm-chan.org/fsw/ff/doc/appnote.html` | 200 | FatFs 官方文档 |
| `raw.githubusercontent.com`、`cdn.jsdelivr.net`、`docs.zephyrproject.org`、`docs.espressif.com` | 不可达/SSL 失败/被策略拦截 | — |

**版本锚点**

- Linux：`v6.12`（`?h=v6.12`），commit 引用按短 sha。
- Zephyr：`v4.1.0`（`kernel/system_work_q.c`、`drivers/flash/spi_nor.c`、`subsys/fs/nvs/nvs.c`、`subsys/fs/littlefs_fs.c`）；`drivers/flash/flash_esp32.c` 与 code search 为 `main`（逐条已标注）。
- ESP-IDF：`v5.4.1`。
- NuttX：`master @ 66703d09573e30f81083bb36ac3d58c50712facb`（`HEAD` 展开值）。
- RT-Thread / FAL / SFUD / MCUboot / OpenBLT / U-Boot：`master`（或 `main`）。
- littlefs：`v2.11.0`。

**关键 grep 口径（便于复核）**

- Linux：`grep -n "workqueue\|work_struct\|kthread\|queue_work\|schedule_work\|flush_work" <file>`；MTD 三个文件 + spi-nor 全部 0 命中。
- NuttX：`grep -n "work_queue\|work_cancel\|work_signal\|struct work_s\|HPWORK\|LPWORK\|kthread\|nxsem\|pthread_create\|task_create" <file>`。
- Zephyr / RT-Thread：GitHub code search `repo:<r>+path:<dir>+k_work|workqueue`，并做对照查询验证语法。
