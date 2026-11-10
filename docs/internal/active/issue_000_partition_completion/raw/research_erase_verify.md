# 擦后校验（Erase Verify）在成熟开源存储栈中的实践调研

> 调研方法：所有结论锚定一手源码（文件+行号）或官方文档原文。凡未能取得一手来源者，一律列入第 5 节，不做推测性表述。
>
> **覆盖范围（15 个对象）**：Linux 内核（MTD 核心 / mtdchar / SPI-NOR / NAND / SPI-NAND / CFI NOR / OneNAND / LPDDR / UBI / UBIFS / JFFS2 / mtdblock / MTD 测试模块）、littlefs、mtd-utils、U-Boot、ESP-IDF、MCUboot、flashrom、OpenOCD、STM32 HAL 与 ST EEPROM 模拟库、Nordic nrfx / nRF5 SDK、OpenBLT、Zephyr、NuttX、RT-Thread。
>
> **未覆盖**：ISO 26262、JEDEC、ST AN3969 / AN4894、各厂 NAND 应用笔记（均为网络策略所阻，见 5.3）。
>
> **实际可用的取源路径（实测）**：
> - `git.kernel.org` plain 视图 —— 可用（Linux 内核，支持 `?h=<tag>` 取历史版本）
> - `api.github.com` + `Accept: application/vnd.github.raw` 头 —— 可用（GitHub 文件原文）；**注意未认证配额仅 60 次/小时**
> - `source.denx.de/u-boot/u-boot/-/raw/<ref>/<path>` —— 可用（U-Boot）
> - `git.infradead.org` gitweb `blob_plain` 接口 —— 可用（mtd-utils），但连接不稳定
> - `linux-mtd.infradead.org/doc/` —— 可用（UBI 官方文档）
>
> **实测被拦截/不可用**：`docs.espressif.com`、`docs.zephyrproject.org`（按要求未尝试）；`raw.githubusercontent.com`（连接超时）；`github.com` 网页（超时）；`cdn.jsdelivr.net`（SSL 错误）。
>
> 调研日期：2026-09-19

---

## 1. 一句话结论

**没有共识**：擦后校验在成熟内核/RTOS 栈里是少数派，但在工具链侧（flashrom）却是默认行为。已证实的形态分五类：

0. **默认开启，每次擦除都做** —— **flashrom**（`erasure_layout.c:295-302`，`check_erased_range()` 逐块读回比对，失败即 `"ERASE FAILED!"` 并中止）。需要用 `-n/--noverify` 显式关闭，而手册原文说 **"Using this option is not recommended"**（`doc/classic_cli_manpage.rst:74-76`）。
1. **无条件、每次擦除都做，无任何开关** —— 三个实例，全部**不可关闭**：
   - **JFFS2**（`fs/jffs2/erase.c:311`，整个 Linux 里唯一一个）
   - **Zephyr NVS**（`subsys/fs/nvs/nvs.c:331-358`，每次扇区擦除后读回比对，失败返回 `-ENXIO`）
   - **OneNAND 批量擦除**（硬件校验，`onenand_base.c:2130`）
2. **默认关闭的开关控制** —— **ESP-IDF NVS**（`CONFIG_NVS_FLASH_VERIFY_ERASE`，默认 n，**且带重试** `CONFIG_NVS_FLASH_ERASE_ATTEMPTS` 默认 2）、**UBI**（debugfs `chk_io`，默认 0；老版本是 `CONFIG_MTD_UBI_DEBUG` + 模块参数）、**U-Boot `sf test`**（`CONFIG_CMD_SF_TEST`，默认 n）。
3. **只在"已经出问题了"的求证流程里做** —— **UBI `torture_peb()`** / **mtd-utils `mtd_torture()`**，触发条件是"上一次擦或写报了 EIO"，而不是"每次擦除"。
4. **只在格式化/重整/首次使用时做** —— **ST 官方 EEPROM 模拟库**（`VerifyPageFullyErased()`，在页自称 `ERASED` 时验证并强制擦除）、**NuttX NXFFS**（重整时逐块校验，不合格标坏块）、**OneNAND** 按擦除规模分档（≥2 块才校验）。**注意：它们的触发条件都不是"一次普通擦除之后"。**
5. **不校验**（多数派）—— Linux MTD 核心/mtdchar/SPI-NOR/NAND/SPI-NAND/LPDDR/mtdblock、UBIFS、littlefs、**ESP-IDF 主擦除 API**、**MCUboot 核心**、**OpenOCD**、**Zephyr `flash_area_erase`**、**NuttX MTD 核心/SMART**、**RT-Thread FAL/SFUD**、**STM32 HAL**、**nrfx/nRF5 SDK**、**OpenBLT**、mtd-utils `flash_erase`/`ubiformat`、U-Boot `sf erase`/`sf update`。

**触发时机**：主流答案是**"不在常规擦除路径上校验"**——Linux MTD 核心、mtdchar、SPI-NOR、NAND/SPI-NAND 主路径、littlefs、mtdblock、ESP-IDF 主擦除 API、MCUboot 核心、OpenOCD、U-Boot `sf erase`/`sf update`、mtd-utils `flash_erase`/`ubiformat` **全部不校验**。做了校验的，触发条件**都不是"每次擦除"**，而是以下四种之一：

- **坏块求证**（UBI `torture_peb()` / mtd-utils `mtd_torture()`）——触发条件是"上一次擦或写报了 EIO"
- **批量擦除**（OneNAND ≥2 块，`onenand_base.c:2385-2392`）——按擦除规模分档，单块擦除不校验
- **格式化 / 重整 / 首次使用**（**ST 官方 EEPROM 模拟库**的 `VerifyPageFullyErased()`；**NuttX NXFFS** 重整时的逐块校验）——见 2.10.2、2.14.2
- **用户主动调用的诊断命令**（U-Boot `sf test`、OpenOCD `flash erase_check`、内核 MTD 测试模块）
- **默认关闭的开关**（UBI `chk_io`、ESP-IDF NVS `CONFIG_NVS_FLASH_VERIFY_ERASE`）

**"格式化时校验一次"这个模式确实存在**（上面第三类），但要注意它的**精确形态**：ST 的校验发生在"**某页自称已擦除**"时（验证声明），NXFFS 的校验发生在"**卷看起来不对劲、决定整体重整**"时。**两者都不是"每次格式化都跑一遍"的固定流程**，而且 **Linux 官方的格式化工具 `ubiformat` 恰恰不校验**（见 2.3）。

**例外的例外是 flashrom**：它每次擦除都校验、默认开（见上）。

**一个必须注意的技术前提**：**"擦后值"不一定是 0xFF。** 这一点被**四个互相独立的实现**从不同角度印证：

| 实现 | 做法 | 锚点 |
|---|---|---|
| **flashrom** | `ERASED_VALUE(flash)` 宏，按 `FEATURE_ERASED_ZERO` 区分 0x00 / 0xFF | `include/flash.h:185` |
| **Zephyr NVS** | 比对值取自 `fs->flash_parameters->erase_value`（**从器件参数读，不硬编码**） | `subsys/fs/nvs/nvs.c:353` |
| **NuttX** | `CONFIG_NXFFS_ERASEDSTATE`（hex，默认 0xff）——**做成可配置项** | `fs/nxffs/Kconfig` |
| **littlefs** | 干脆声明"擦后状态未定义"，因此**自己不做校验** | `lfs.h:174-178` |

**任何要做擦后校验的设计，都必须先回答"擦后值是什么、由谁保证"。** 把 0xFF 硬编码进去，在一个擦后值为 0x00 的器件上就会得到 100% 的假阳性。

**最值得单列的一处官方陈述**（`Documentation/driver-api/mtd/spi-nor.rst:183-184`）——SPI-NOR 子系统的"新 flash 最低测试要求"里写着：

> **"If the flash comes erased by default and the previous erase was ignored, we won't catch it, thus test the erase again"**

这句话直接点名了擦后校验要解决的核心风险：**擦除可能被"静默忽略"**（不是返回错误，而是命令根本没生效），而且**新出厂 flash 本来就是全 0xFF**，所以第一次擦除即使无效也看不出来。但它的定位是**给驱动提交者的人工测试步骤**（用 `mtd_debug erase` + `mtd_debug read` + `sha256sum` 比对全 0xFF 的哈希），**不是运行时机制**。详见 2.1.12。

**失败处置**：四条规律覆盖几乎所有实现——
- **先重试再判死**。CFI NOR 重试 3 次（`MAX_RETRIES 3`），UBI 重试 3 次（`UBI_IO_RETRIES 3`），JFFS2 用 refile 回路，**ESP-IDF NVS 重擦 2 次**（`CONFIG_NVS_FLASH_ERASE_ATTEMPTS` 默认 2）。
- **判死后换区或标坏块/标无效**。JFFS2 → `bad_list` + 写 NAND OOB 坏块标记；littlefs → `goto relocate` 换下一块；UBI → `ubi_io_mark_bad()` + 消耗预留 PEB；**ESP-IDF NVS → 页状态置 `INVALID`**。
- **换无可换才是真事故**。UBI → `ubi_ro_mode()` 整设备只读；littlefs → 超级块不可 relocate 时 `LFS_ERR_NOSPC`。
- **没有坏块概念时就直接中止**。flashrom（NOR）→ `"ERASE FAILED!"` + 返回 -1，整个操作终止。

**"IO 失败"与"该区不可用"是否分开**：**分开了，但不是"一对平级专用码"的形式。** 四种做法：

- **单个专用码**：**littlefs `LFS_ERR_CORRUPT`（-84）**，文档原文 `"if the block should be considered bad"`，与泛用的 `LFS_ERR_IO`（-5）分家，消费端 `if (err == LFS_ERR_CORRUPT) goto relocate; return err;`。**Zephyr NVS `-ENXIO`** 同理，只表示"这次擦后校验没过"。**UBI 另有一套专用码 `UBI_IO_*`**（`ubi.h:89-111`），编码"这块区域处于什么状态"——但**用在读路径，不在擦除路径**。
- **约定既有 errno**：UBI 与 CFI NOR 在**擦除**路径上约定 `-EIO` 承载"块坏"语义（UBI 把这个约定写进了函数注释："If `-EIO` is returned, the physical eraseblock most probably went bad"），"暂时性失败"用 `-EAGAIN`/`-ENOMEM`/`-EBUSY`/`-EINTR` 承载，**其他一切 errno 则被 UBI 视为"我不知道发生了什么"→ 整设备只读**。
- **数据结构位置**：JFFS2 用 `bad_list` vs `erase_pending_list`；NuttX NXFFS 用**写在块头里的 `BLOCK_STATE_BAD`**（持久化）；littlefs 用 `goto relocate` 的**控制流**。
- **失败位置作为独立信息通道**：MTD 的 `instr->fail_addr`、JFFS2 的 `bad_offset`、STM32 HAL 的 `*SectorError`——都只回答"错在哪"，不回答"错得多严重"。

**注意一个倾向**：**没有一个框架把"该区不可用"这个判定，附加在"擦后校验失败"这个事件上。** 校验失败一律只报告"这次没成"，是否判死由外层状态机另行决定。Zephyr NVS 是最纯粹的例证——它无条件校验、返回专用码 `-ENXIO`，但**刻意不重试、不标坏块、不换扇区**。

**另有一处值得注意的语义维度**：UBI 会用**"错误发生的时间点"**来判定结构性损坏——同一个 bitflip，若发生在**刚擦完的块**上，判定为块坏（`io.c:428-434`，`"read problems on freshly erased PEB %d, must be bad"`）。这是调研中唯一利用该维度的实现。

**未找到**：`ERASE_IO_FAILED` / `REGION_UNUSABLE` 这样一对平级的专属枚举；以及任何**在擦除操作内部同时完成校验与语义分类**的实现——做了校验的（JFFS2 / UBI debug / OneNAND）与做了语义分类的（littlefs / UBI）是两拨不同的实现。

---

## 2. 逐框架对照表

图例：**verify** 列指"擦除返回成功后，再把数据读回来确认擦后值"。

| 框架 | 是否 verify | 触发时机 | 开关与默认值 | 失败处置 | 源码锚点 |
|---|---|---|---|---|---|
| **Linux MTD 核心** `mtd_erase()` | ❌ 否 | — | — | 透传驱动 errno + 设 `fail_addr` | `drivers/mtd/mtdcore.c:1459-1509` |
| **Linux mtdchar** MEMERASE | ❌ 否 | — | — | 直接返回 `mtd_erase()` 结果 | `drivers/mtd/mtdchar.c:921-955` |
| **Linux SPI-NOR** | ❌ 否 | — | — | 只轮询状态寄存器 WIP；超时 `-ETIMEDOUT` | `drivers/mtd/spi-nor/core.c:1821`、`:617-627`、`:714` |
| **Linux NAND** `nand_erase_nand()` | ❌ 否 | — | — | 读状态寄存器 FAIL 位；`fail_addr = ofs` | `drivers/mtd/nand/raw/nand_base.c:4524`、`:4598-4603` |
| **Linux SPI-NAND** `spinand_erase()` | ❌ 否 | — | — | `status & STATUS_ERASE_FAILED` → `-EIO` | `drivers/mtd/nand/spi/core.c:1165`、`:1188-1189` |
| **Linux OneNAND** 批量擦除（≥2 块） | ✅ **是（硬件校验）**，无条件 | 每次批量擦除（每批最多 64 块）后 | **无开关**（`CONFIG_MTD_ONENAND_VERIFY_WRITE` 不管这条路径） | `instr->fail_addr = addr`（精确到块）+ 返回 `-EIO` | `drivers/mtd/nand/onenand/onenand_base.c:2161`、`:2252-2256`、`:2130`、`:2140-2149` |
| **Linux OneNAND** 单块擦除（<2 块 / 4KB 页 / Flex-OneNAND） | ❌ 否 | — | — | 等状态后 `-EIO` | `onenand_base.c:2276`、`:2361`；分派点 `:2385-2392` |
| **Linux OneNAND** 写校验 | ✅ 是（软件读回 `memcmp`） | 每次写页 | `CONFIG_MTD_ONENAND_VERIFY_WRITE`，**默认 n** | `"verify failed"` 打印 + 错误返回 | `onenand_base.c:1606-1680`；`drivers/mtd/nand/onenand/Kconfig:11-18` |
| **Linux CFI NOR（AMD, 0002）** | ⚠️ **看芯片**：无状态寄存器的芯片 → ✅ 是（DQ 轮询即比对全 0xFF）；有状态寄存器 → ❌ 否（只查 DRB） | 每次块擦除 | 无（硬编码） | `-EIO`；发复位命令后最多重试 3 次 | `cfi_cmdset_0002.c:2510`、`:2517`、`:2572`、`:2594`；`chip_ready` 两分支在 `:831-859`；`MAX_RETRIES` 在 `:43` |
| **Linux CFI NOR（Intel, 0001）** | ❌ 否（只读状态寄存器） | — | — | 按 SR 位分派 `-EINVAL`/`-EROFS`/`-EIO`；特定位置试 | `drivers/mtd/chips/cfi_cmdset_0001.c:1948`、`:1993-2020` |
| **Linux UBI** `do_sync_erase()` | ⚠️ **是，但默认关** | 每次擦除（当旋钮打开时） | debugfs `chk_io`，**默认 0**；master 无 CONFIG 门控 | 校验不过返回 `-EINVAL` → 整个 UBI 设备转 R/O | `drivers/mtd/ubi/io.c:319`、`:351`、`:1445`、`:1452`；`debug.c:612`；`build.c:946` |
| **Linux UBI** `torture_peb()` | ✅ 是（擦后读回 + 写花样读回，3 轮） | **仅对疑似坏块**（写失败过的 PEB） | 由坏块处置流程驱动 | `-EIO` → 标记坏块 | `drivers/mtd/ubi/io.c:375`；`patterns[]` 在 `:369` |
| **Linux JFFS2** `jffs2_block_check_erase()` | ✅ **是，无条件** | **每一次块擦除完成后** | **无开关**，非 debug，非 CONFIG | `-EIO` → `bad_list`；NAND 上先试写 OOB 坏块标记；`-EAGAIN` → refile 重试 | `fs/jffs2/erase.c:311`、`:397`、`:402-407`、`:172-192` |
| **Linux UBIFS** | （复用 UBI 路径，无自有校验） | — | 同 UBI | 同 UBI | `drivers/mtd/ubi/kapi.c:620` `ubi_leb_erase()` |
| **Linux mtdblock** `erase_write()` | ❌ 否 | — | — | 擦除失败仅 `KERN_WARNING` | `drivers/mtd/mtdblock.c:44-64` |
| **littlefs** | ❌ 否（明确拒绝假设擦后值） | — | — | 由块设备驱动返回 `LFS_ERR_CORRUPT` | `lfs.c:276`（纯透传）；契约 `lfs.h:174-178` |
| **mtd-utils `ubiformat`**（格式化工具） | ❌ 否 | — | — | `errno == EIO` → 询问用户后标坏块；其他 errno → 中止 | `ubi-utils/ubiformat.c:458`（`flash_image`）、`:589`（`format`） |
| **mtd-utils `flash_erase`**（最常用的擦除工具） | ❌ 否 | — | — | 打印 `"MTD Erase failure"` 后 `continue`；**`main()` 无条件 `return 0`**，失败不上报退出码 | `misc-utils/flash_erase.c:310-312`、`:322` |
| **mtd-utils `mtd_torture()`** | ✅ 是（同 UBI torture_peb） | **仅在擦/写已报 EIO 之后** | 由错误触发 | 失败 → 标坏块 | `lib/libmtd.c:1065`、`:1081-1108` |
| **内核 MTD 测试套件** `erasetest()` | ✅ 是（擦后读回验 0xFF） | **仅在显式加载测试模块时** | 测试模块（`drivers/mtd/tests/`） | 打印 `"verifying all 0xff failed at %d"` | `drivers/mtd/tests/pagetest.c:267`、`:302-311` |
| **U-Boot** `sf erase` | ❌ 否 | — | — | 打印 `ERROR %d` / `OK` | `cmd/sf.c:346-388` |
| **U-Boot** `sf update` | ❌ 否（只有擦**前**的 memcmp，用于跳过不必要擦除） | — | — | 返回 `"erase"`/`"write"` 错误串 | `cmd/sf.c:180-211` |
| **U-Boot** `sf test` | ✅ 是（擦后读回验 0xFF + 写后读回验数据） | **仅当用户手动执行 `sf test`** | `CONFIG_CMD_SF_TEST`，**默认 n**（无 `default y`） | 打印 `"Check failed at %d"` / `"Verify failed at %d"` | `cmd/sf.c:481-508`、`:527-542`；`cmd/Kconfig:1778-1788` |

| **ESP-IDF** 主擦除 API | ❌ 否 | — | 仅状态轮询；`CONFIG_SPI_FLASH_CHECK_ERASE_TIMEOUT_DISABLED`（默认 n）只控超时 | 返回驱动错误，不重试不读回（**`ESP_ERR_FLASH_ERASE_FAIL` 不存在**） | `components/spi_flash/esp_flash_api.c:645-754`；`esp_flash_err.h:34-37` |
| **ESP-IDF** NVS | ✅ **是（可选）** | 每次擦页后，`esp_partition_read_raw` 按 32 字节读回比对 0xFF | **`CONFIG_NVS_FLASH_VERIFY_ERASE`，默认 n**；重试 `CONFIG_NVS_FLASH_ERASE_ATTEMPTS`，**默认 2**（1-10） | 重试到上限→返回错误→页标记 `INVALID`→向上传播不再重试 | `components/nvs_flash/src/nvs_partition.cpp:78-103`；`Kconfig:55-65`；`nvs_page.cpp:1170-1184` |
| **ESP-IDF** `esp_partition_erase_range` | ❌ 否 | — | `CONFIG_ESP_PARTITION_ERASE_CHECK`（默认 **y**）是**写前**检查，非擦后 | 纯透传 | `partition_target.c:125-146`；`partition_linux.c:587-597` |
| **MCUboot** 核心 | ❌ 否 | — | 无（`flash_area_erase_verify` 不存在） | 返回 rc，调用点 `assert(rc == 0)` | `boot/bootutil/src/bootutil_area.c:217-315`、`:276` |
| **MCUboot** ESP 移植 | ⚠️ 有读回但**是死代码** | 擦后逐字节查 0xFF | `VALIDATE_PROGRAM_OP`（**全仓无定义处**）&& 非加密模式 | `BOOT_LOG_ERR` + **`assert(0)`** | `boot/espressif/port/esp_mcuboot.c:490-497` |
| **flashrom** `-E` 擦除 | ✅ **是，默认开** | **每一个块擦除后**立即整块读回比对 | `-n/--noverify` 关闭；**默认开**（手册称关闭"not recommended"） | `msg_cerr("ERASE FAILED!\n")` + 返回 -1 | `erasure_layout.c:295-302`；`flashrom.c:560-575`；`cli_classic.c:1612` |
| **OpenOCD** 擦除 | ❌ 否 | — | 无 | `LOG_ERROR("failed erasing sectors...")` | `src/flash/nor/core.c:29-39` |
| **OpenOCD** `write_image` | ❌ 否（verify 硬编码 `false`） | — | `flash verify_image` 是独立的手工命令；无 `-verify` 选项 | 手工命令比对镜像 CRC | `src/flash/nor/tcl.c:448-449`；`core.c:982-987` |
| **OpenOCD** `flash erase_check` | 空白检查 | **仅用户手工执行** | 显式命令 | 打印 erased / not erased / unknown | `src/flash/nor/tcl.c:179-226` |

| **STM32 HAL** `HAL_FLASHEx_Erase` | ❌ 否 | — | 无 | 只查 BSY/EOP/WRPERR/PGSERR 标志；返回 HAL 状态 | F4 `stm32f4xx_hal_flash_ex.c:160`、`stm32f4xx_hal_flash.c:551`；F1 `:157`；F0 `:157` |
| **ST EEPROM 模拟库** `VerifyPageFullyErased()` | ✅ **是（唯一"首次使用/格式化时校验"实例）** | **仅在 `EE_FLITF_Init()`（第 9 步）与 `EE_FLITF_Format()`** | 无开关 | **强制做一次真正的擦除** | `core/algo/eeprom_algo_flitf.c:1520`（定义）、`:852`（Init）、`:920`（Format） |
| **nrfx** `nrfx_nvmc_page_erase()` | ❌ 否（**无条件 `return 0`**） | — | `Kconfig.nrf` 零个 verify 选项 | 无（不表达失败） | `drivers/src/nrfx_nvmc.c:208-223` |
| **nRF5 SDK** `nrf_fstorage_erase()` | ❌ 否 | — | 无 | **总是 `NRF_SUCCESS`** | `nrf_fstorage_nvmc.c:150-174` |
| **OpenBLT**（STM32F4 目标） | ❌ 否 | — | 无 | — | 擦前空白检查 `FlashEmptyCheckSector`（`Target/Source/ARMCM4_STM32F4/flash.c:674`，调用 `:771`）；写后读回 `FlashWriteBlock`（`:651`） |

| **Zephyr** `flash_area_erase()` | ❌ 否，只调 `flash_erase()` | — | 无 | 无 | `subsys/storage/flash_map/flash_map.c:71-78`；契约 `include/zephyr/storage/flash_map.h:223-236` |
| **Zephyr** nRF/STM32/SPI-NOR 驱动 | ❌ 否 | — | 无 | `FLSR` 状态寄存器 `ERASE_FAIL` → `-EIO` | `drivers/flash/spi_nor.c:1039`、`:520-545`；`flash_stm32.c:166,185`；`soc_flash_nrf.c:221,333` |
| **Zephyr NVS** | ✅ **是，无条件、无开关** | **每一次扇区擦除**（`nvs_erase`/GC/erase-all） | **无** | 返回 **`-ENXIO`**，向调用方传播；不重试不标坏块 | `subsys/fs/nvs/nvs.c:331-358`（tag **v3.7.0**）；比对值取自 `flash_parameters->erase_value` |
| **Zephyr** littlefs 胶水层 | ❌ 否 | — | 无 | 无 | `subsys/fs/littlefs_fs.c:198-205` |
| **NuttX** MTD 核心/分区 | ❌ 否 | — | 无 | 只传驱动 rc | `include/nuttx/mtd/mtd.h:94`；`drivers/mtd/mtd_partition.c:219-244` |
| **NuttX NXFFS** | ✅ 是（**重新格式化时**逐块校验） | 挂载时"卷看起来不对劲"触发整体重整 | **无校验开关**；仅 `CONFIG_NXFFS_REFORMAT_THRESH`（int，**默认 20**）控触发 | 不合格块 → `nxffs_blkinit(..., BLOCK_STATE_BAD)` | `fs/nxffs/nxffs_reformat.c:207-224`、`:258`；触发 `nxffs_initialize.c:245-250` |
| **NuttX** SMART/smartfs | ❌ 否，**擦除返回值被丢弃**（`static void`） | — | `CONFIG_SMARTFS_ERASEDSTATE`（hex，0xff）仅哨兵值 | 无 rc 捕获 | `drivers/mtd/smart.c:2703-2729` |
| **RT-Thread** FAL | ❌ 否 | — | `FAL_USING_DEBUG` 仅控日志级别 | `LOG_E` 后返回 rc | `components/fal/src/fal_partition.c:490-518` |
| **RT-Thread** SFUD | ❌ 否 | — | 无 | `wait_busy()` WIP 轮询 | `components/sfud/src/sfud.c:453-497`、`:510-598` |

> 上表覆盖 Linux 内核及其官方周边（mtd-utils / U-Boot）、littlefs、ESP-IDF、MCUboot、flashrom、OpenOCD、STM32、nrfx、OpenBLT、Zephyr、NuttX、RT-Thread。
> **逐项取证索引**：2.1 Linux 内核 · 2.2 littlefs · 2.3 mtd-utils · 2.4 U-Boot · 2.5 OneNAND · 2.6 ESP-IDF · 2.7 MCUboot · 2.8 flashrom · 2.9 OpenOCD · 2.10 STM32 / ST EEPROM 模拟 · 2.11 Nordic nrfx / nRF5 SDK · 2.12 OpenBLT · 2.13 Zephyr · 2.14 NuttX · 2.15 RT-Thread。
>
> **未取得一手来源**：ISO 26262、JEDEC、ST AN3969 / AN4894、Micron/Infineon/Macronix 应用笔记——**详见第 5.3 节**，本报告对这些问题不作任何结论。

---

## 2.1 Linux 内核逐子系统详查（本节为本次调研的原始取证记录）

内核版本基准：`git.kernel.org/.../torvalds/linux.git` master 分支，2026-09-19 抓取。
本节所有行号即该次抓取的行号。

### 2.1.1 结论速览

| 子系统 | 做擦后读回校验? | 触发时机 | 开关 | 失败处置 |
|---|---|---|---|---|
| MTD 核心 `mtd_erase()` | **否** | — | — | 仅透传驱动错误码 + `fail_addr` |
| `mtdchar` MEMERASE/MEMERASE64 | **否** | — | — | 直接返回 `mtd_erase()` 的返回值 |
| SPI-NOR `spi_nor_erase()` | **否** | — | — | 只轮询状态寄存器 WIP 位；超时 → `-ETIMEDOUT` |
| NAND `nand_erase_nand()` | **否** | — | — | 读状态寄存器 FAIL 位；不读回阵列 |
| CFI NOR（AMD/Fujitsu cmdset 0002）| **是**（轮询即比对全 0xFF） | 每次块擦除 | 无（硬编码） | `-EIO`，最多重试 3 次 |
| CFI NOR（Intel/Sharp cmdset 0001）| **否** | — | — | 只读状态寄存器 |
| UBI `ubi_io_erase` 路径 | **是**，但被 debug 开关门控 | 每次擦除（当开关打开时） | debugfs `chk_io`，**默认 0** | 校验失败返回 `-EINVAL` → 整个 UBI 设备转 R/O |
| UBI `torture_peb()` | **是**（擦后读回 + 写花样读回） | 仅对**疑似坏块**做 torture 时 | 由坏块处置流程驱动 | `-EIO` → 标记坏块 |
| JFFS2 `jffs2_block_check_erase()` | **是**，**无条件** | **每一次**块擦除完成后 | **无开关**，非 debug | `-EIO` → 进 `bad_list`；NAND 上先试写 OOB 坏块标记 |
| UBIFS | 复用 UBI 的擦除路径 | — | 同 UBI | 同 UBI |

### 2.1.2 MTD 核心：`mtd_erase()` 不做任何校验

`drivers/mtd/mtdcore.c:1459-1509`。函数头注释（`:1459-1462`）明确定义了契约：

```
/*
 * Erase is an synchronous operation. Device drivers are epected to return a
 * negative error code if the operation failed and update instr->fail_addr
 * to point the portion that was not properly erased.
 */
```

实现只有三段：入参检查 → `master->_erase(master, &adjinstr)` → 把 `adjinstr.fail_addr` 换算回 `instr->fail_addr`（`:1498-1505`）。**没有任何 `mtd_read()` / `memcmp` / 全 0xFF 比对**。

关键接口语义：`include/linux/mtd/mtd.h:21`

```c
#define MTD_FAIL_ADDR_UNKNOWN -1LL
```

以及 `mtd.h:27` 的注释：`fail_addr = MTD_FAIL_ADDR_UNKNOWN` 表示 "the failure was not at the device level"。这是 MTD 层区分「设备级失败（能定位到具体偏移）」与「非设备级失败」的唯一机制——**它是"失败位置已知/未知"的区分，不是"IO 失败 vs 区块不可用"的区分**。

### 2.1.3 `mtdchar` MEMERASE：纯粹透传

`drivers/mtd/mtdchar.c:921-955`。`MEMERASE` / `MEMERASE64` 分支只做 `copy_from_user` 填充 `erase->addr/len`，然后 `:952` `ret = mtd_erase(mtd, erase);`。无读回、无重试、无坏块标记。

### 2.1.4 SPI-NOR：只轮询 WIP，不读回比对

`drivers/mtd/spi-nor/core.c:1821` `spi_nor_erase()`。
- 统一擦除的分支（`:1866-1891`）：循环 `spi_nor_erase_sector()` → `spi_nor_wait_till_ready()`。
- `spi_nor_sr_ready()`（`:617-627`）的实现是：

```c
int spi_nor_sr_ready(struct spi_nor *nor)
{
	int ret;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	return !(nor->bouncebuf[0] & SR_WIP);
}
```

读的是**状态寄存器**的 WIP（Write In Progress）位，**不是阵列数据**。整个 `spi_nor_erase()` 路径（`:1821-1905`）内不存在对擦除区域的 `memcmp`/全 0xFF 检查。

即：SPI-NOR 只能通过"等待超时"（`spi_nor_wait_till_ready_with_timeout()`，`:714`，返回 `-ETIMEDOUT`）感知擦除卡死，**无法感知"擦完了但没擦干净"**。

顺带一个值得注意的细节（`:714-739`）：超时这条路径的日志级别是 **`dev_dbg`** 而非 `dev_err`：

```c
	dev_dbg(nor->dev, "flash operation timed out\n");

	return -ETIMEDOUT;
```

即**在正常日志级别下，一次擦除超时是静默的**——只有返回值 `-ETIMEDOUT` 向上传递。`spi_nor_wait_till_ready()`（`:747`）本身只是用 `DEFAULT_READY_WAIT_JIFFIES` 调用上面这个带超时的版本。

### 2.1.5 NAND：读状态寄存器，不读回阵列

`drivers/mtd/nand/raw/nand_base.c:4524` `nand_erase_nand()`：
- 逐块：先 `nand_block_checkbad()`（`:4576`），坏块直接 `ret = -EIO` 且**不擦**（`:4580-4582`：`pr_warn("%s: attempt to erase a bad block at 0x%08llx\n")`）；
- 再 `nand_erase_op()`（`:4598`）；
- 失败时置 `instr->fail_addr = ofs`（`:4601`）后退出。

**没有擦后读回。**

**SPI-NAND（串行 NAND）同样不校验。** `drivers/mtd/nand/spi/core.c:1165` `spinand_erase()`：

```c
	ret = spinand_wait(spinand,
			   SPINAND_ERASE_INITIAL_DELAY_US,
			   SPINAND_ERASE_POLL_DELAY_US,
			   &status);

	if (!ret && (status & STATUS_ERASE_FAILED))
		ret = -EIO;
```

同样是**读状态寄存器**（`STATUS_ERASE_FAILED` 位），不读回阵列。三者（raw NAND / SPI-NAND / LPDDR）与 OneNAND 形成鲜明对照——**只有 OneNAND 走了硬件擦后校验这条路**。

需要特别澄清一个容易误读的点：`nand_check_erased_ecc_chunk()`（在 `:3077`、`:3149`、`:3215`、`:3298` 被调用）**不是擦后校验**。它的调用点全部在 `nand_read_page_*()` 系列里——是**读路径**遇到 ECC 报错时判断"这一页是不是其实是空的（被擦除但有 bitflip）"的补救逻辑。语义是"读到一个坏页，猜它本来是空页"，与"擦完确认擦干净"是两件事。

### 2.1.6 CFI NOR：两个命令集，一个做读回一个不做

同一个子目录下的两个驱动给出了直接的对照。

**AMD/Fujitsu（cmdset 0002）——做读回比对。** `drivers/mtd/chips/cfi_cmdset_0002.c:2510` `do_erase_oneblock()`：

```c
	map_word datum = map_word_ff(map);      /* :2517 */
	...
		if (chip_ready(map, chip, adr, &datum)) {   /* :2572 */
			if (cfi_check_err_status(map, chip, adr))
				ret = -EIO;
			break;
		}
```

`map_word_ff(map)` 是本 map 位宽下的"全 1"（即 0xFF）表示。

**但这里必须精确——`chip_ready()` 有两条分支，只有其中一条是真正的读回比对。** `cfi_cmdset_0002.c:831`：

```c
static int __xipram chip_ready(struct map_info *map, struct flchip *chip,
			       unsigned long addr, map_word *expected)
{
	struct cfi_private *cfi = map->fldrv_priv;
	map_word oldd, curd;
	int ret;

	if (cfi_use_status_reg(cfi)) {
		map_word ready = CMD(CFI_SR_DRB);
		/* For chips that support status register, check device ready bit */
		cfi_send_gen_cmd(0x70, cfi->addr_unlock1, chip->start, map, cfi,
				 cfi->device_type, NULL);
		curd = map_read(map, addr);
		return map_word_andequal(map, curd, ready, ready);
	}

	oldd = map_read(map, addr);
	curd = map_read(map, addr);

	ret = map_word_equal(map, oldd, curd);

	if (!ret || !expected)
		return ret;

	return map_word_equal(map, curd, *expected);
}
```

| 分支 | 条件 | 机制 | 是否算"擦后读回校验" |
|---|---|---|---|
| **状态寄存器分支** | `cfi_use_status_reg(cfi)` 为真 | 发 `0x70` 读状态寄存器，查 DRB（Device Ready Bit） | ❌ **不算**——和 SPI-NOR/NAND 一样是状态位 |
| **DQ 轮询分支** | 芯片**没有**状态寄存器 | 对阵列地址连读两次，检查 ① 两次值相同（无位翻转），② 当前值等于期望值（`map_word_ff`） | ✅ **算**——真的把阵列内容读回来跟全 0xFF 比 |

**所以准确的表述是：AMD CFI 命令集在「无状态寄存器的老式芯片」上，其擦除轮询本身就构成一次对全 0xFF 的读回比对；在有状态寄存器的芯片上则退化为状态位检查。** 这是"同一份驱动代码在不同硬件上校验强度不同"的一个实例。

失败处置：

```c
	if (ret) {
		/* reset on all failures. */
		map_write(map, CMD(0xF0), chip->start);
		if (++retry_cnt <= MAX_RETRIES) {   /* :2594, MAX_RETRIES == 3 (:43) */
			ret = 0;
			goto retry;
		}
	}
```

即：擦除失败（含上述比对失败）→ 发复位命令 → **重试最多 3 次**（`#define MAX_RETRIES 3`，`:43`）→ 仍失败才返回 `-EIO`。

**Intel/Sharp（cmdset 0001）——不读回。** `drivers/mtd/chips/cfi_cmdset_0001.c:1948` `do_erase_oneblock()` 在等待完成后执行：

```c
	/* We've broken this before. It doesn't hurt to be safe */
	map_write(map, CMD(0x70), adr);
	chip->state = FL_STATUS;
	status = map_read(map, adr);          /* :1993 —— 读的是状态寄存器 */

	/* check for errors */
	if (map_word_bitsset(map, status, CMD(0x3a))) {
```

`CMD(0x70)` 是"读状态寄存器"命令，因此 `map_read` 拿到的是 SR 而非阵列内容。错误分得很细（`:2000-2020`）：`0x30` → `-EINVAL`（命令序列错）、`0x02` → `-EROFS`（保护位）、`0x8` → `-EIO`（VPP 电压）、`0x20` 且还有重试次数 → 打印 `"block erase failed ... Retrying..."` 并 `goto retry`（`int retries = 3;`，`:1953`）、其余 → `-EIO`。

> 结论：Linux CFI 内部对"擦除失败如何分类"有非常明确的先例——**按失败原因分派不同 errno**（`-EINVAL` / `-EROFS` / `-EIO`），并且**只有 `-EIO` 被当作"块坏了"**。-EINVAL 表示"命令序列错"，-EROFS 表示"被保护"，都不重试、不标记坏块。

### 2.1.7 UBI：做了擦后校验，但被 debug 开关门控（默认关）

这是本次调研中**最接近本题要求**的一处，也是**最容易误读**的一处。

**校验函数的真名与位置。** 现行 mainline 中函数名为 `ubi_self_check_all_ff()`，定义在 `drivers/mtd/ubi/io.c:1445`：

```c
/**
 * ubi_self_check_all_ff - check that a region of flash is empty.
 * ...
 * This function returns zero if only 0xFF bytes are present at offset
 * @offset of the physical eraseblock @pnum, and a negative error code if not
 * or if an error occurred.
 */
int ubi_self_check_all_ff(struct ubi_device *ubi, int pnum, int offset, int len)
{
	...
	if (!ubi_dbg_chk_io(ubi))
		return 0;          /* :1452-1453  —— 开关关掉时直接返回“通过” */
	...
	err = mtd_read(ubi->mtd, addr, len, &read, buf);
	...
	err = ubi_check_pattern(buf, 0xFF, len);
	if (err == 0) {
		ubi_err(ubi, "flash region at PEB %d:%d, length %d does not contain all 0xFF bytes", ...);
		goto fail;
	}
```

> 注：你（提问方）提到的名字 `ubi_dbg_check_all_ff()` 是**旧名**。改名发生在 v3.0 与 v4.19 之间。实测（`git.kernel.org` plain 视图按 tag 抓取各版本 `drivers/mtd/ubi/io.c`）：
>
> | 版本 | 函数名 | 定义行 |
> |---|---|---|
> | v2.6.32 | `ubi_dbg_check_all_ff` | `io.c:1295` |
> | v3.0 | `ubi_dbg_check_all_ff` | `io.c:1408` |
> | v4.19 | `ubi_self_check_all_ff` | `io.c:1367` |
> | master (2026-09-19) | `ubi_self_check_all_ff` | `io.c:1445` |
>
> 按旧名检索现 mainline 会找不到。**函数体本身（`if (!ubi_dbg_chk_io(ubi)) return 0;` 门控 + 读回比对 0xFF）在两个时代是一致的**——改名没有改变行为。

**开关长什么样。** `drivers/mtd/ubi/debug.h:367-370`：

```c
static inline int ubi_dbg_chk_io(const struct ubi_device *ubi)
{
	return ubi->dbg.chk_io;
}
```

`chk_io` 是一个 **debugfs 运行时旋钮**，不是什么编译期 CONFIG。它由 `drivers/mtd/ubi/debug.c:612` 创建：

```c
	d->dfs_chk_io = debugfs_create_file("chk_io", mode, d->dfs_dir,
					    (void *)ubi_num, &dfs_fops);
```

**默认值 = 0（关）。** 依据：`drivers/mtd/ubi/build.c:946` 分配设备时用 `ubi = kzalloc_obj(struct ubi_device);`，`dbg` 结构体整体零初始化；`debug.c` 中**没有任何**给 `d->chk_io` 赋 1 的代码（全文件对 `chk_io` 的引用只有 debugfs 的读写回调 `:345-346`、`:448-449` 和创建处 `:612`）。所以除非用户手动 `echo 1 > /sys/kernel/debug/ubi/ubi0/chk_io`，这道校验永远走 `:1452-1453` 直接返回 0。

**关于 `CONFIG_MTD_UBI_DEBUG`——它已经不存在了。** 这一点值得单独说明，因为很多二手资料（包括问题描述里的措辞）仍按老结构理解。现行 mainline：

- `drivers/mtd/ubi/Makefile` 中 `debug.o` 是**无条件**编译的：

  ```
  ubi-y += vtbl.o vmt.o upd.o build.o cdev.o kapi.o eba.o io.o wl.o attach.o
  ubi-y += misc.o debug.o
  ```

- 全树 grep `CONFIG_MTD_UBI_DEBUG`：在 `io.c` / `debug.c` / `debug.h` / `ubi.h` / `build.c` / `wl.c` / `drivers/mtd/ubi/Kconfig` / `drivers/mtd/Kconfig` 中**零命中**。
- `drivers/mtd/ubi/Kconfig` 现存的 debug-ish 选项只剩 `MTD_UBI_FAULT_INJECTION`（`depends on FAULT_INJECTION_DEBUG_FS`，`default n`，`:108-110`）和 `MTD_UBI_NVMEM`。

即：**代码路径永远编进去，能不能生效完全由 runtime debugfs 旋钮决定，且默认不生效。**

**开关的历代形态**（这解释了为什么"哪个 CONFIG 控制"这个问题会有不同答案）：

| 时代 | 编译期门控 | 运行期门控 | 默认 |
|---|---|---|---|
| v2.6.32 / v3.0 | `CONFIG_MTD_UBI_DEBUG`（`drivers/mtd/ubi/Kconfig:55`，`bool "UBI debugging"`，`depends on SYSFS`）| 模块参数 `debug_chks`（位掩码），对应 `debug.h:84-85` 的 `UBI_CHK_GEN = 0x1, UBI_CHK_IO = 0x2` | 位掩码默认 0 |
| v4.19 | 已无 CONFIG 门控 | debugfs `chk_io` | 0 |
| master (2026-09) | **无** | debugfs `chk_io` | 0 |

v3.0 的 `drivers/mtd/ubi/debug.h:81` 对该位有一句直白的定义，正好注解了这段代码的意图：

```
 * UBI_CHK_IO: check writes and erases
```

即"**检查写和擦**"——与我们在 `io.c` 里看到的"写前查 0xFF / 写后读回比对 / 擦后读回比对 0xFF"三处校验完全对应。

**调用点。** `drivers/mtd/ubi/io.c:319` `do_sync_erase()`（static）：

```c
	err = mtd_erase(ubi->mtd, &ei);
	if (err) {
		if (retries++ < UBI_IO_RETRIES) {
			ubi_warn(ubi, "error %d while erasing PEB %d, retry", err, pnum);
			yield();
			goto retry;
		}
		ubi_err(ubi, "cannot erase PEB %d, error %d", pnum, err);
		dump_stack();
		return err;
	}

	err = ubi_self_check_all_ff(ubi, pnum, 0, ubi->peb_size);   /* :351 擦后校验 */
	if (err)
		return err;

	if (ubi_dbg_is_erase_failure(ubi)) {                        /* 故障注入 */
		ubi_err(ubi, "cannot erase PEB %d (emulated)", pnum);
		return -EIO;
	}
```

上层链路：`wl.c:432 ubi_sync_erase()` → `io.c:537 ubi_io_sync_erase()`（先 `self_check_not_bad`，必要时 `nor_erase_prepare`，再 `torture_peb`）→ `io.c:319 do_sync_erase()`。

**值得注意的语义副作用：** `ubi_self_check_all_ff()` 校验失败时返回的是 **`-EINVAL`**（`io.c:1476`），**不是 `-EIO`**。而下游 `wl.c` 的 `__erase_worker()` 对 `-EINVAL` 的处理是"我不知道怎么办" → **整个 UBI 设备切只读**。也就是说：在这个 debug 开关打开的情况下，一个块擦不干净会让整个文件系统掉进 R/O，而不是把那一个块标记成坏块。这是一个**真实存在但反直觉的行为**，读代码时需要留意。

**补充：UBI 的 `self_check` 家族是一个三处校验的体系，全部共用同一个 debug 门控。** 只盯 `do_sync_erase` 会漏掉全貌：

| 校验点 | 函数 | 位置 | 语义 | 失败返回 |
|---|---|---|---|---|
| 写之前 | `ubi_self_check_all_ff()` | `io.c:257-259` | "我要写的这块区域必须全是 0xFF" | `-EINVAL` |
| 写之后 | `self_check_write()` | `io.c:1380`，调用点 `io.c:303` | 读回并**逐字节**与源 buffer 比对 | `-EINVAL` |
| 写之后（尾段）| `ubi_self_check_all_ff()` | `io.c:351` 附近的尾部调用 `io.c:299-306` | 未写到的剩余部分必须仍是 0xFF | `-EINVAL` |
| 擦之后 | `ubi_self_check_all_ff()` | `io.c:351` | 整块必须全是 0xFF | `-EINVAL` |
| **分配 PEB 时**（延迟校验） | `ubi_self_check_all_ff()` | `wl.c:2149`，`ubi_wl_get_peb()`（`:2119`） | 从 free pool 取出一个块准备使用时，检查其**数据区**（`vid_hdr_aloffset` 到块尾）是否全 0xFF | `-EINVAL` + 打印 `"new PEB %d does not contain all 0xFF bytes"` |

最后一行是一个**重要的设计细节**：UBI 并非只在"擦除返回后"那一刻校验，而是在**这个块即将被投入使用**时再校验一次。也就是说，擦除校验的时点被**推迟到了使用点**——如果两次使用之间发生了什么事（长时间静置导致的电荷流失等），这个延迟校验能抓到，而贴身的即时校验抓不到。**这是"何时触发校验"这一问上一个值得注意的答案：可以选择在擦除点验，也可以选择在使用点验。**

`self_check_write()` 的文档注释（`io.c:1375-1377`）原文：

> This functions reads data which were recently written and compares it with the original data buffer - the data have to match. Returns zero if the data match and a negative error code if not or in case of failure.

三处全部以 `if (!ubi_dbg_chk_io(ubi)) return 0;` 开头。**注意：UBI 的"写前必须是 0xFF"检查在写路径上扮演了擦除校验的角色**——如果某个块擦得不干净，下一次写它时会在这里被抓到（前提是 `chk_io` 开着）。

### 2.1.8 UBI 的 torture test：真正语义完整的一处，但它只针对"疑似坏块"

`drivers/mtd/ubi/io.c:375` `torture_peb()` 是 UBI 里擦后校验用得最完整的地方。它做三件事（对应官方文档，见下）：

```c
	for (i = 0; i < patt_count; i++) {
		err = do_sync_erase(ubi, pnum);
		if (err)
			goto out;

		/* Make sure the PEB contains only 0xFF bytes */
		err = ubi_io_read(ubi, ubi->peb_buf, pnum, 0, ubi->peb_size);
		if (err)
			goto out;

		err = ubi_check_pattern(ubi->peb_buf, 0xFF, ubi->peb_size);
		if (err == 0) {
			ubi_err(ubi, "erased PEB %d, but a non-0xFF byte found", pnum);
			err = -EIO;
			goto out;
		}
		/* Write a pattern and check it */
		...
```

`patterns[] = {0xa5, 0x5a, 0x0}`（`io.c:369`）——擦除→读回比对全 0xFF→写花样→读回比花样，三轮。

**触发时机：仅当该 PEB 已被怀疑是坏块时**（写失败过的 PEB 被调度去 torture），不是每次擦除都做。官方文档 `http://www.linux-mtd.infradead.org/doc/ubi.html`（"Marking eraseblocks as bad"节，原文抓取于 2026-09-19）逐字如下：

> UBI marks physical eraseblocks as bad in the following 2 scenarios:
> - an **eraseblock write operation failed**, in which case UBI moves the data from this PEB to some other PEB (data recovery) and schedules this PEB for torturing;
> - the **erase operation failed with EIO error**, in which case the eraseblock is marked as bad immediately.
>
> The torturing is done in the background for the purpose of detecting whether the physical eraseblock is actually bad. …
> During the torturing UBI does the following:
> - erase the eraseblock;
> - **read it back and make sure it contains only 0xFF bytes**;
> - write test pattern bytes;
> - **read the eraseblock back and check the pattern**;
> - and so on for several patterns (0xA5, 0x5A, 0x00).
>
> The eraseblock is not marked as bad if it survives the torture test. However, a bit-flip during the torture test is a good reason to mark the eraseblock as bad. Please, refer to the `torture_peb()` function for detailed information.

**注意文档与代码的一处措辞差异：** 文档说"erase operation failed with EIO error → marked as bad immediately"，而实际上**带校验的那条路径**（`do_sync_erase` 里的 `ubi_self_check_all_ff`）返回的是 `-EINVAL`，落到 R/O 分支；只有不带校验的 `mtd_erase` 本身返回 `-EIO`（以及故障注入模拟的那条）才走"标记坏块"。代码里的判断在 `wl.c:1139-1145`：

```c
	if (err != -EIO)
		/*
		 * If this is not %-EIO, we have no idea what to do. Scheduling
		 * this physical eraseblock for erasure again would cause
		 * errors again and again. Well, lets switch to R/O mode.
		 */
		goto out_ro;
	/* It is %-EIO, the PEB went bad */
```

### 2.1.9 JFFS2：**无条件、每次擦除都做、无开关**的擦后校验

这是 Linux 里唯一一处"成熟文件系统对每一次擦除都做读回校验、且不受任何配置项控制"的实现。锚点：`fs/jffs2/erase.c`。

**校验函数** `jffs2_block_check_erase()`，`fs/jffs2/erase.c:311`。它有两条路径：

*快路径*（能用 `mtd_point` 直映射时，`:318-341`）——直接按 CPU 字长扫描：

```c
		wordebuf = ebuf-sizeof(*wordebuf);
		retlen /= sizeof(*wordebuf);
		do {
		   if (*++wordebuf != ~0)
			   break;
		} while(--retlen);
		mtd_unpoint(c->mtd, jeb->offset, c->sector_size);
		if (retlen) {
			*bad_offset = jeb->offset + c->sector_size - retlen * sizeof(*wordebuf);
			pr_warn("Newly-erased block contained word 0x%lx at offset 0x%08x\n",
				*wordebuf, *bad_offset);
			return -EIO;
		}
```

*慢路径*（`goto do_flash_read`，`:342-393`）——按 `PAGE_SIZE` 分块 `mtd_read()`，逐字比对：

```c
	jffs2_dbg(1, "Verifying erase at 0x%08x\n", jeb->offset);

	for (ofs = jeb->offset; ofs < jeb->offset + c->sector_size; ) {
		...
		ret = mtd_read(c->mtd, ofs, readlen, &retlen, ebuf);
		if (ret) {
			pr_warn("Read of newly-erased block at 0x%08x failed: %d. Putting on bad_list\n", ofs, ret);
			ret = -EIO;
			goto fail;
		}
		if (retlen != readlen) { ... ret = -EIO; goto fail; }
		for (i=0; i<readlen; i += sizeof(unsigned long)) {
			/* It's OK. We know it's properly aligned */
			unsigned long *datum = ebuf + i;
			if (*datum + 1) {                    /* != ~0UL，即 != 全 1 */
				*bad_offset += i;
				pr_warn("Newly-erased block contained word 0x%lx at offset 0x%08x\n",
					*datum, *bad_offset);
				ret = -EIO;
				goto fail;
			}
		}
		ofs += readlen;
		cond_resched();
	}
```

**"无开关"这一点是经过三重排查确认的**：

1. **编译期**：`fs/jffs2/erase.c` 全文中只有两个 `#ifdef`——`__ECOS`（`:33-72`，eCos 移植分支）和 `CONFIG_JFFS2_FS_XATTR`（`:273-280`，扩展属性）。**`jffs2_block_check_erase` 不在任何条件编译内。**
2. **挂载期**：`fs/jffs2/super.c:184-185` 的挂载选项只有两个——`compr`（压缩器）和 `rp_size`（预留池大小），**没有校验相关选项**。
3. **运行期**：无 sysfs/debugfs 旋钮。

**即：JFFS2 的擦后校验是强制且不可关闭的。**

**触发时机：每一次块擦除之后，无例外。** 这一点经过调用点穷举验证：`jffs2_mark_erased_block` 在 `fs/jffs2/erase.c` 中**只有一个调用点**（`:116`，位于 `jffs2_erase_pending_blocks()` 内），且 `nodemgmt.c` / `gc.c` / `scan.c` / `super.c` 中均**零命中**——不存在绕过校验的其他入口。

调用链：`jffs2_erase_pending_blocks()`（`:99`，擦除线程主循环）→ `jffs2_erase_block()`（`:28`）擦成功 → `jffs2_erase_succeeded()`（`:159`，只把 jeb 移到 `erase_complete_list`）→ 下一轮循环里 `jffs2_mark_erased_block()`（`:397`）→ 函数第一件事就是 `switch (jffs2_block_check_erase(c, jeb, &bad_offset))`。

**注意这里有一个精巧的架构分拆**：`jffs2_erase_block()` 里"物理擦除"和"校验"是**两个不同阶段**，中间通过 `erase_complete_list` 解耦。完整状态流转（`jffs2_erase_pending_blocks()`，`:99` 起）：

- `:113` `list_move(&jeb->list, &c->erase_checking_list);` —— 从 `erase_complete_list` 取出，标记为"正在校验"
- `:116` `jffs2_mark_erased_block(c, jeb);` —— 在这一步才真正做校验
- `:164`（`jffs2_erase_succeeded()`）`list_move_tail(&jeb->list, &c->erase_complete_list);` —— 物理擦除成功只是"进待校验队列"，**不代表擦除已被认可**
- `:483`（`refile` 路径）校验缓冲分配失败时退回 `erase_complete_list` 稍后重试

这样校验可以在擦除线程里独立进行，而不与物理擦除耦合。

**失败处置**（`fs/jffs2/erase.c:402-407`）：

```c
	switch (jffs2_block_check_erase(c, jeb, &bad_offset)) {
	case -EAGAIN:	goto refile;
	case -EIO:	goto filebad;
	}
```

- `-EAGAIN`（仅来自 `kmalloc(PAGE_SIZE)` 失败，`:351-353`）→ `refile`：移回 `erase_complete_list`，`jffs2_garbage_collect_trigger()`，**稍后重试**。
- `-EIO`（读失败 / 短读 / 发现非 0xFF 字）→ `filebad` → `jffs2_erase_failed(c, jeb, bad_offset)`（`:172`）。

`jffs2_erase_failed()` 再分一层（`:174-192`）：

```c
	/* For NAND, if the failure did not occur at the device level for a
	   specific physical page, don't bother updating the bad block table. */
	if (jffs2_cleanmarker_oob(c) && (bad_offset != (uint32_t)MTD_FAIL_ADDR_UNKNOWN)) {
		/* We had a device-level failure to erase.  Let's see if we've
		   failed too many times. */
		if (!jffs2_write_nand_badblock(c, jeb, bad_offset)) {
			/* We'd like to give this block another try. */
			... list_move(&jeb->list, &c->erase_pending_list); ...
			return;
		}
	}
	...
	c->bad_size += c->sector_size;
	list_move(&jeb->list, &c->bad_list);
```

即：**若能在 NAND 上成功写下 OOB 坏块标记，就进 `bad_list` 永久退役；若坏块标记都写不进去，就退回待擦队列再试一次。** 这是"结构性不可用"的判定与落实。

另外 `jffs2_erase_block()` 自身对**物理擦除直接失败**的处置（`:69-78`）也做了区分：`-ENOMEM` / `-EAGAIN` → 退回 `erase_pending_list` 重试；`-EROFS` → 只打 warning（`"Is the sector locked?"`）；其余 errno → `jffs2_erase_failed()`。

### 2.1.10 附带发现：内核把"擦除失败"当作可注入、可测试的一等公民

这条与"是否校验"无关，但说明了内核对这个问题的态度：**它选择用故障注入来测试擦除失败路径，而不是用读回校验来当场发现失擦。**

`drivers/mtd/mtdcore.c` 中带错误注入钩子的函数：

```c
ALLOW_ERROR_INJECTION(mtd_erase, ERRNO);            /* :1510 */
ALLOW_ERROR_INJECTION(mtd_read, ERRNO);             /* :1610 */
ALLOW_ERROR_INJECTION(mtd_write, ERRNO);            /* :1627 */
ALLOW_ERROR_INJECTION(mtd_block_markbad, ERRNO);    /* :2457 */
```

注意 **`mtd_erase` 与 `mtd_block_markbad` 都在列**——"擦除失败"和"标记坏块失败"都被认为需要被测试。

UBI 侧还有专门的配置（`drivers/mtd/ubi/Kconfig:107-115`）：

```
config MTD_UBI_FAULT_INJECTION
	bool "Fault injection capability of UBI device"
	default n
	depends on FAULT_INJECTION_DEBUG_FS
	help
	   This option enables fault-injection support for UBI devices for
	   testing purposes.

	   If in doubt, say "N".
```

配合 `io.c:355-358` 里 `ubi_dbg_is_erase_failure()` 模拟出来的 `"cannot erase PEB %d (emulated)"` → `-EIO`，可以完整地走一遍"擦除失败 → 标记坏块"的路径而不需要真的弄坏一块 flash。

> **这意味着：内核把"擦除可能悄悄失败"这件事的应对方式定为"把失败路径测透"，而不是"每次擦除都读回确认"。**

### 2.1.11 内核自己的 MTD 测试套件：把"擦后读回验 0xFF"当作**测试手段**

`drivers/mtd/tests/pagetest.c:267` 有一个专门的 `erasetest()`：

```c
static int erasetest(void)
{
	...
	pr_info("erasing block %d\n", ebnum);
	err = mtdtest_erase_eraseblock(mtd, ebnum);
	...
	pr_info("writing 1st page of block %d\n", ebnum);
	err = mtdtest_write(mtd, addr0, pgsize, writebuf);
	...
	pr_info("erasing block %d\n", ebnum);
	err = mtdtest_erase_eraseblock(mtd, ebnum);
	...
	pr_info("reading 1st page of block %d\n", ebnum);
	err = mtdtest_read(mtd, addr0, pgsize, twopages);
	...
	pr_info("verifying 1st page of block %d is all 0xff\n", ebnum);
	for (i = 0; i < pgsize; ++i)
		if (twopages[i] != 0xff) {
			pr_err("verifying all 0xff failed at %d\n", i);
			errcnt += 1;
			ok = 0;
			break;
		}

	if (ok && !err)
		pr_info("erasetest ok\n");
```

**这确认了内核的立场**：擦后读回验 0xFF 是**验证一个 MTD 驱动是否正常工作**的正当手段，但它被放在**测试模块**（`drivers/mtd/tests/`，需显式加载）里，而不是运行时擦除路径里。同目录下还有 `torturetest.c`、`stresstest.c`、`nandbiterrs.c` 等。

**这与 U-Boot 的 `sf test`、mtd-utils 的 `mtd_torture` 构成了同一个模式的三次重复**：擦后校验存在于**诊断/测试工具**中，不存在于常规擦除路径中。

### 2.1.12 ★ 最关键的一处官方陈述：SPI-NOR 文档**明文承认"擦除可能被静默忽略"**

`Documentation/driver-api/mtd/spi-nor.rst:183-184`，位于"Minimum testing requirements"一节的第 4 步（用 mtd-utils 验证擦除/读/写）之后。原文：

> **If the flash comes erased by default and the previous erase was ignored, we won't catch it, thus test the erase again::**

这句话之后给出的是一套**完整的手工擦后校验流程**：

```bash
# 第二次擦除（关键的一步）
root@1:~# mtd_debug erase /dev/mtd0 0 2097152
Erased 2097152 bytes from address 0x00000000 in flash

# 读回
root@1:~# mtd_debug read /dev/mtd0 0 2097152 spi_read
Copied 2097152 bytes from address 0x00000000 in flash to spi_read

# 与"已知的全 0xFF 内容"的哈希比对
root@1:~# sha256sum spi*
4bda3a28f4ffe603c0ec1258c0034d65a1a0d35ab7bd523a834608adabf03cc5  spi_read
c444216a6ba2a4a66cccd60a0dd062bce4b865dd52b200ef5e21838c4b899ac8  spi_test
```

文档把"擦后应该等于什么"这个**期望值**直接给出来了：`4bda3a28...` 就是这 2 MiB 的期望内容哈希。

> **本报告已独立验证这一点**：本地计算 2 MiB（2097152 字节）全 0xFF 内容的 SHA-256，结果为
> `4bda3a28f4ffe603c0ec1258c0034d65a1a0d35ab7bd523a834608adabf03cc5`，**与文档中擦除后 `spi_read` 的哈希逐字符一致**。
> 这确认了该步骤确实是在**比对擦后内容是否为全 0xFF**——即一次货真价实的擦后校验。

**为什么这段如此重要**——它是本次调研中唯一一处一手官方文档**明确点名了擦后校验要解决的那个具体风险**：

1. **"the previous erase was ignored"** —— 风险不是"擦除返回了错误"，而是**擦除被静默忽略了**（命令没生效、控制器吞掉了、时序不对）。
2. **"If the flash comes erased by default ... we won't catch it"** —— 新出厂的 flash **本来就是空的**，所以第一次擦除即使完全无效，读回也是全 0xFF，**根本看不出问题**。这正是"擦后校验"必须解决的场景，也正是单纯检查返回值的盲区。
3. **"thus test the erase again"** —— 处置办法是**再擦一次**，因为第二次擦除面对的是已经被写过的数据，此时读回才能区分"真擦了"和"没擦"。

**但要精确地看它的定位**：这段出现在 **"Minimum testing requirements"（提交新 flash 支持时的最低测试要求）** 一节里，是给**提交驱动的开发者**看的一套人工验证步骤，**不是运行时机制**。

> **即：SPI-NOR 子系统完全知道"擦除可能被静默忽略"这个风险，但它的应对方式是"让开发者手工验一遍"，而不是"在擦除路径上自动校验"。** 这大概是整份报告里最能说明"成熟栈如何权衡这件事"的一处证据。

**顺带佐证了本报告前几节的一个观察**：文档让开发者用的工具是 `mtd_debug`（mtd-utils）。实测确认 `misc-utils/mtd_debug.c`（401 行）中 `verify` / `0xff` / `memcmp` **三个关键词零命中**——它的 `erase`/`read`/`write` 全是单纯透传。**即：这套官方推荐的擦后校验流程，校验动作完全是靠 `sha256sum` 在用户态手工完成的，工具链本身不提供任何校验能力。**

---

## 2.2 littlefs：不做擦后校验，并且**明文解释了为什么不做**

littlefs 虽然没有出现在必查清单里，但它是本次调研中**对本题回答得最直接**的一个框架——因为它在 API 契约里把理由写成了文字。

### 2.2.1 它不做校验，而且给出了官方理由

`lfs.h:174-178`（`lfs_config.erase` 回调的文档注释）原文：

```c
    // Erase a block. A block must be erased before being programmed.
    // The state of an erased block is undefined. Negative error codes
    // are propagated to the user.
    // May return LFS_ERR_CORRUPT if the block should be considered bad.
    int (*erase)(const struct lfs_config *c, lfs_block_t block);
```

> **"The state of an erased block is undefined."**

这一句是整个调研里**唯一一处明文否认"擦后值可预期"的官方陈述**。它的逻辑后果很硬：**既然擦除后的状态由底层定义、littlefs 不做任何假设，那么 littlefs 就不可能自己去读回比对某个"擦后值"**。这解释了为什么 littlefs 把校验责任完全推给块设备驱动。

而实现层面完全吻合。`lfs.c:276`：

```c
static int lfs_bd_erase(lfs_t *lfs, lfs_block_t block) {
    LFS_ASSERT(block < lfs->block_count);
    int err = lfs->cfg->erase(lfs->cfg, block);
    LFS_ASSERT(err <= 0);
    return err;
}
```

纯透传，**没有任何读回、比对、重试**。

### 2.2.2 但它是「两个码」问题最干净的先例

littlefs 没有做擦后校验，却在 API 里**专设了一个错误码来表达"这个块该被当作坏块"**。`lfs.h:72-73`：

```c
    LFS_ERR_IO          = -5,   // Error during device operation
    LFS_ERR_CORRUPT     = -84,  // Corrupted
```

`LFS_ERR_CORRUPT`（-84）的用途在 `lfs.h` 中被**两次**以同样的措辞定义：

- `lfs.h:170`（`prog` 回调）：`// May return LFS_ERR_CORRUPT if the block should be considered bad.`
- `lfs.h:177`（`erase` 回调）：`// May return LFS_ERR_CORRUPT if the block should be considered bad.`

"**if the block should be considered bad**" —— 这就是"该区结构性不可用"这一语义的专用表达。它与 `LFS_ERR_IO`（-5，"Error during device operation"，泛指本次操作出错）**明确分家**。

### 2.2.3 消费者端的处置：`relocate`（换下一区）

三处 `lfs_bd_erase()` 的调用点采用了**完全一致**的三岔分派。`lfs.c:2935`（`lfs_ctz_extend`）：

```c
        err = lfs_bd_erase(lfs, nblock);
        if (err) {
            if (err == LFS_ERR_CORRUPT) {
                goto relocate;
            }
            return err;
        }
```

`lfs.c:1996`（`lfs_dir_compact`）与 `lfs.c:3278`（`lfs_file_relocate`）同构。语义三分：

| 返回 | 含义 | 动作 |
|---|---|---|
| `0` | 擦成功 | 继续写 |
| `LFS_ERR_CORRUPT` | **这个块结构性不可用** | `goto relocate` → 放弃此块，`lfs_alloc()` 取**下一块**，循环重试 |
| 其他任何负值 | **本次 IO 失败** | `return err` 原样上报给上层/用户 |

`relocate` 标签的实体（`lfs.c:3012-3018`）：

```c
relocate:
        LFS_DEBUG("Bad block at 0x%"PRIx32, nblock);

        // just clear cache and try a new block
        lfs_cache_drop(lfs, pcache);
    }
}
```

整个函数体包在 `while (true)` 里（`lfs.c:2926`），所以 `relocate` 落到底后会重新 `lfs_alloc()` 拿到新块再试。`lfs_dir_compact` 的对应标签（`lfs.c:2096-2105`）措辞更明确：

```c
relocate:
        // commit was corrupted, drop caches and prepare to relocate block
        relocated = true;
        lfs_cache_drop(lfs, &lfs->pcache);
        if (!tired) {
            LFS_DEBUG("Bad block at 0x%"PRIx32, dir->pair[1]);
        }

        // can't relocate superblock, filesystem is now frozen
        if (lfs_pair_cmp(dir->pair, (const lfs_block_t[2]){0, 1}) == 0) {
            LFS_WARN("Superblock 0x%"PRIx32" has become unwritable",
                    dir->pair[1]);
            return LFS_ERR_NOSPC;
        }
```

**注意这里有一个"退无可退"的边界处理**：如果坏掉的是**超级块所在的块**（`pair == {0,1}`），不能再 relocate（没有地方可搬），于是打 `"Superblock ... has become unwritable"` 并返回 `LFS_ERR_NOSPC`。这是"结构性不可用"升级为"文件系统不可用"的临界点——**与 UBI 的 `out_ro` 是同一类设计**。

### 2.2.4 一处需要澄清的细节：`tired` 不是重试预算

读 `lfs_dir_compact` 时容易把 `tired` 误读成"重试次数用完了"。实际上 `lfs.c:1940-1949` 显示它是**磨损均衡**驱动的：

```c
static bool lfs_dir_needsrelocation(lfs_t *lfs, lfs_mdir_t *dir) {
    // If our revision count == n * block_cycles, we should force a relocation,
    // this is how littlefs wear-levels at the metadata-pair level. ...
    return (lfs->cfg->block_cycles > 0
            && ((dir->rev + 1) % ((lfs->cfg->block_cycles+1)|1) == 0));
}
```

`tired = true` 表示"本来这一轮就该搬家的（磨损均衡到期）"，所以出错时不必原地重试、直接搬。**littlefs 对 `LFS_ERR_CORRUPT` 没有次数上限**——`while (true)` 会一直换块直到 `lfs_alloc()` 报 `LFS_ERR_NOSPC`（没有可用块了）。这是一个值得注意的设计选择：**它靠"块池耗尽"而非"重试计数"来兜底。**

### 2.2.5 一处不一致：`lfs_migrate` 不做区分

`lfs.c:5874`（`lfs_migrate`）是唯一的例外——它不区分 `LFS_ERR_CORRUPT`，一律 `goto cleanup`：

```c
            err = lfs_bd_erase(lfs, dir1.head[1]);
            if (err) {
                goto cleanup;
            }
```

代码上方有一条注释（`lfs.c:5868-5869`）解释了这块的敏感性：

```c
            // Copy over first block to thread into fs. Unfortunately
            // if this fails there is not much we can do.
```

即：迁移路径上失败时"没什么可做的"，所以不做 relocate 重试。

### 2.2.6 小结

littlefs 的答案对本题最有价值，因为它**同时给出了否定和肯定**：

- **否定**：littlefs 自己不做擦后校验，并且给出了明确的、写进 API 契约的理由——"擦后状态未定义"。
- **肯定**：它用**一个专用错误码 `LFS_ERR_CORRUPT`** 把"该块结构性不可用"从"本次 IO 失败"里分离出来，并且消费端统一采用"换下一块重试"的处置。

**这是"两个语义分开"这一问上最清晰、最可直接对照的先例。**

---

## 2.3 mtd-utils：**格式化工具本身也不做擦后校验**（这是"只在格式化时做"这一设想的直接反例）

因为"只在格式化/首次使用时校验"是一个很自然的候选设计，所以专门查了 **Linux 官方的 flash 格式化工具** mtd-utils。取源：`git.infradead.org/mtd-utils.git`（gitweb `blob_plain` 接口）master 分支。

### 2.3.1 结论：`ubiformat` 擦完直接写，不读回

`ubi-utils/ubiformat.c` 里有两条擦除路径，**都没有擦后读回**：

| 路径 | 函数 | 起始行 | `mtd_erase` 调用行 | 失败后 `mark_bad` 行 | 写失败后 `mtd_torture` 行 |
|---|---|---|---|---|---|
| 带镜像刷写 | `flash_image()` | `:411` | **`:458`** | `:467` | `:517` |
| 纯格式化 | `format()` | `:545` | **`:589`** | `:598` | `:635` |

**路径一：`flash_image()`**。擦除段（`:458-470`）：

```c
		err = mtd_erase(libmtd, mtd, args.node_fd, eb);
		if (err) {
			if (!args.quiet)
				printf("\n");
			sys_errmsg("failed to erase eraseblock %d", eb);

			if (errno != EIO)
				goto out_close;

			if (mark_bad(mtd, si, eb))
				goto out_close;
			continue;
		}
```

擦成功后直接 `read_all()` 读源镜像 → `change_ech()` 改 EC 头 → `mtd_write()`（`:476-513`）。**中间不读回 flash。**

**路径二：`format()`**（`:545`）。结构与路径一同构：`mtd_erase()`（`:589`）失败且 `errno == EIO` → `mark_bad()`（`:598`）；成功后直接写 EC 头 `mtd_write()`。**同样不读回。**

### 2.3.2 失败处置：errno 分类 + 交互式确认 + 连续坏块熔断

`ubiformat` 的分类逻辑与内核 UBI 完全同构（`flash_image()` 路径 `:462-470`、`:513-521`；`format()` 路径 `:592-600`、`:631-639`）：

| 情形 | 处置 |
|---|---|
| `mtd_erase` 失败且 `errno != EIO` | **直接中止**（`goto out_free`）——非 EIO 的错误无法处理 |
| `mtd_erase` 失败且 `errno == EIO` | 调 `mark_bad()` 标记坏块，`continue` 下一块 |
| `mtd_write` 失败且 `errno == EIO` | 先调 `mtd_torture()` 求证；torture 也失败才 `mark_bad()` |

`mark_bad()`（`:381`）会**交互式询问用户**（`:386`：`if (!answer_is_yes("mark it as bad?"))`），并带一个连续坏块熔断器 `consecutive_bad_check()`（`:355`）——连续坏块超过 `MAX_CONSECUTIVE_BAD_BLOCKS` 就报 `"consecutive bad blocks exceed limit: %d, bad flash?"`（`:373`）并放弃。这对应内核侧的 `MTD_UBI_BEB_LIMIT`：**坏块多到一定程度就不是坏块问题，而是整片 flash 的问题。**

### 2.3.3 一处诚实的 `TODO`：`mark_bad` 不 torture

`ubiformat.c:380`：

```c
/* TODO: we should actually torture the PEB before marking it as bad */
static int mark_bad(const struct mtd_dev_info *mtd, struct ubi_scan_info *si, int eb)
```

即：**在"直接标记坏块"这条路径上，工具承认自己没有先做求证**。这是一个明确的、官方记录在案的自我批评，也是"先重试/求证再判死"这条规律的一个已知破例。

### 2.3.4 但 `mtd_torture()` 是内核 `torture_peb()` 的用户态翻版

`lib/libmtd.c:1065` `mtd_torture()`，与内核 `drivers/mtd/ubi/io.c:375` `torture_peb()` 几乎逐行对应：

```c
int mtd_torture(libmtd_t desc, const struct mtd_dev_info *mtd, int fd, int eb)
{
	...
	for (i = 0; i < patt_count; i++) {
		err = mtd_erase(desc, mtd, fd, eb);
		if (err)
			goto out;

		/* Make sure the PEB contains only 0xFF bytes */
		err = mtd_read(mtd, fd, eb, 0, buf, mtd->eb_size);
		if (err)
			goto out;

		err = check_pattern(buf, 0xFF, mtd->eb_size);
		if (err) {
			errmsg("erased PEB %d, but a non-0xFF byte found", eb);
			errno = EIO;
			goto out;
		}
		/* Write a pattern and check it */
		...
	}
	err = 0;
	normsg("PEB %d passed torture test, do not mark it a bad", eb);
```

注意那句 **`"erased PEB %d, but a non-0xFF byte found"`** 与内核 `ubi_io.c:396` 的 `"erased PEB %d, but a non-0xFF byte found"` **逐字相同**。这直接证明了二者的同源关系——**擦后读回校验这件事在 UBI 体系里是"坏块求证"流程的一部分，而不是常规擦除路径的一部分。**

`check_pattern()` 在 `lib/libmtd.c:1055`。

### 2.3.5 对本问的意义

这是**"擦后校验只在格式化时做"这一设想的直接反例**：Linux 官方的 flash 格式化工具，在擦除路径上**不校验**，只在"已经出问题了"的求证流程里校验。触发条件不是"格式化"，而是"**这次写/擦报了 EIO**"。

### 2.3.6 补充：`flash_erase` 工具**连擦除失败都不上报**（最极端的反面案例）

`misc-utils/flash_erase.c`（mtd-utils 里最常用的擦除工具）。逐块擦除的主循环：

```c
		if (mtd_erase(mtd_desc, &mtd, fd, eb) != 0) {
			sys_errmsg("%s: MTD Erase failure", mtd_device);
			continue;                       /* 打印后直接下一块 */
		}

		if (jffs2)
			clear_marker(mtd_desc, &mtd, fd, eb, cmlen, isNAND);
	}
	show_progress(offset, eb, eb_start, eb_cnt, mtd.eb_size);
out:
	bareverbose(!quiet, "\n");

	return 0;                                   /* :322 —— main() 的返回 */
```

**三个要点，全部是一手代码事实：**

1. **不做擦后校验**（全文件 grep `verify` **零命中**）。
2. **单块擦除失败只打印一条 `sys_errmsg` 然后 `continue`**，没有错误计数器（全文件无 `errcnt` 之类的累加变量）。
3. **`main()` 最后无条件 `return 0`（`:322`）**——也就是说，**即使有块擦除失败了，`flash_erase` 的退出码仍然是 0**。

第 3 点意味着：**调用 `flash_erase` 的构建脚本/产线脚本，无法通过退出码知道擦除是否真的成功。** 这是本报告里见到的"擦除结果不可信"的最极端形态——它不仅不校验，连已经拿到的失败信号都不往外传。

**顺带**：该工具会用 `mtd_is_bad()` 跳过坏块（`:286-298`），`-N/--noskipbad`（`:48`、`:69`）可以关掉这个跳过行为；NAND 上若坏块检查不可用则直接报错退出（`:295`，`"Bad block check not available"`）。

---

## 2.4 U-Boot：常规擦除不校验；校验藏在一个默认关闭的诊断命令里

取源：`source.denx.de/u-boot/u-boot` master（`-/raw/` 接口）。

### 2.4.1 `sf erase` —— 不校验

`cmd/sf.c:346` `do_spi_flash_erase()`，尾部（`:382-388`）：

```c
	ret = spi_flash_erase(flash, offset, size);
	printf("SF: %zu bytes @ %#x Erased: ", (size_t)size, (u32)offset);
	if (ret)
		printf("ERROR %d\n", ret);
	else
		printf("OK\n");
```

擦完直接打印 `OK`。**无读回。**

### 2.4.2 `sf update` —— 有一次 `memcmp`，但它不是擦后校验

`cmd/sf.c:180` `spi_flash_update_block()`：

```c
	/* Read the entire sector so to allow for rewriting */
	if (spi_flash_read(flash, read_offset, flash->sector_size, cmp_buf))
		return "read";
	/* Compare only what is meaningful (len) */
	if (memcmp(cmp_buf + start_offset, buf, len) == 0) {
		debug("Skip region %x+%x size %zx: no change\n",
		      start_offset, read_offset, len);
		*skipped += len;
		return NULL;
	}
	/* Erase the entire sector */
	if (spi_flash_erase(flash, read_offset, flash->sector_size))
		return "erase";
```

**这是一个容易误认的点**：这里确实"读了 flash 并 `memcmp`"了，但它是**擦除之前**的比对，目的是**"内容和目标一致就整个跳过擦除"**——一个省擦写次数（延长寿命）的优化。擦除之后**没有任何读回**。

> 值得注意的后果：如果擦除没擦干净，紧接着的 `spi_flash_write` 会写出一块脏数据，而 U-Boot 在这条路径上不会发现。这与 SPI-NOR 内核驱动"只轮询 WIP"的行为一致——**SPI-NOR 生态整体不假设"擦完就一定是 0xFF"能被验证**。

`sf update` 的命令帮助文本（`:637`）也印证了它只是 "erase and write"：

```
"sf update addr offset|partition len	- erase and write `len' bytes from memory\n"
```

### 2.4.3 `sf test` —— **有**擦后读回校验，但默认关闭且是破坏性诊断命令

`cmd/sf.c:481` `spi_flash_test()`，流程与前文的 torture 高度一致：

```c
	err = spi_flash_erase(flash, offset, len);
	if (err) {
		printf("Erase failed (err = %d)\n", err);
		return -1;
	}
	spi_test_next_stage(&test);

	err = spi_flash_read(flash, offset, len, vbuf);
	if (err) {
		printf("Check read failed (err = %d)\n", err);
		return -1;
	}
	for (i = 0; i < len; i++) {
		if (vbuf[i] != 0xff) {
			printf("Check failed at %d\n", i);
			...
			return -1;
		}
	}
```

`if (vbuf[i] != 0xff)` → `"Check failed at %d"` —— **这就是标准的擦后读回校验**。之后还做了写-读回比对（`:527-542`，`"Verify failed at %d"`）。

**开关与默认值**（`cmd/Kconfig:1778-1788`）：

```
config CMD_SF_TEST
	bool "sf test - Allow testing of SPI flash"
	depends on CMD_SF
	help
	  Provides a way to test that SPI flash is working correctly. The
	  test is destructive, in that an area of SPI flash must be provided
	  for the test to use. ...
```

- **没有 `default y`** → **默认关闭**。（对照：`CMD_SF` 本身是 `default y if DM_SPI_FLASH`，`:1774`。）
- Kconfig 帮助文本明说 **"The test is destructive"**。

**调度点**（`cmd/sf.c:617`）：

```c
	else if (IS_ENABLED(CONFIG_CMD_SF_TEST) && !strcmp(cmd, "test"))
```

**即：U-Boot 有擦后校验的实现，但它被定位成一个"用户主动调用的破坏性自检命令"，而不是擦除路径的一部分。**

---

## 2.5 OneNAND：Linux MTD 里**唯一无条件做擦后校验**的驱动，而且是**硬件校验**

这是本次调研中最出人意料的一处，也是 Linux MTD 树里唯一一个"常规擦除路径自带擦后校验"的实现。

### 2.5.1 批量擦除路径：擦完一批（最多 64 块）后无条件校验

`drivers/mtd/nand/onenand/onenand_base.c:2161` `onenand_multiblock_erase()`。在擦完一批块之后（`:2250-2256`）：

```c
		len -= block_size;
		addr += block_size;
		eb_count++;

		/* verify */
		verify_instr.len = eb_count * block_size;
		if (onenand_multiblock_erase_verify(mtd, &verify_instr)) {
			instr->fail_addr = verify_instr.fail_addr;
			return -EIO;
		}
```

`onenand_multiblock_erase_verify()`，`:2130`：

```c
static int onenand_multiblock_erase_verify(struct mtd_info *mtd,
					   struct erase_info *instr)
{
	struct onenand_chip *this = mtd->priv;
	loff_t addr = instr->addr;
	int len = instr->len;
	unsigned int block_size = (1 << this->erase_shift);
	int ret = 0;

	while (len) {
		this->command(mtd, ONENAND_CMD_ERASE_VERIFY, addr, block_size);
		ret = this->wait(mtd, FL_VERIFYING_ERASE);
		if (ret) {
			printk(KERN_ERR "%s: Failed verify, block %d\n",
			       __func__, onenand_block(this, addr));
			instr->fail_addr = addr;
			return -1;
		}
		len -= block_size;
		addr += block_size;
	}
	return 0;
}
```

**关键点：这是硬件校验，不是软件读回。** `ONENAND_CMD_ERASE_VERIFY` 是 OneNAND 命令集里的**一个独立硬件命令**（`onenand.c:421` 在地址译码 switch 中为它单独列了一 case），由 flash 芯片内部完成"擦后状态检查"，驱动只需发命令 + 等状态。这与 JFFS2 的"逐个读回比对"是**完全不同的实现层次**。

失败处置也很干净：`instr->fail_addr = addr;` 精确回填**是哪个块**校验失败，然后返回 `-EIO`。

### 2.5.2 触发时机是"按擦除规模"分档的——不是全做，也不是随机采样

`onenand_erase()` 的分派点，`:2385-2392`：

```c
	if (ONENAND_IS_4KB_PAGE(this) || region ||
	    instr->len < MB_ERASE_MIN_BLK_COUNT * block_size) {
		/* region is set for Flex-OneNAND (no mb erase) */
		ret = onenand_block_by_block_erase(mtd, instr,
						   region, block_size);
	} else {
		ret = onenand_multiblock_erase(mtd, instr, block_size);
	}
```

其中 `MB_ERASE_MIN_BLK_COUNT = 2`、`MB_ERASE_MAX_BLK_COUNT = 64`（`onenand.c:36-37`）。

| 路径 | 触发条件 | 是否校验 |
|---|---|---|
| `onenand_multiblock_erase()` | 非 4KB 页、非 Flex-OneNAND、且 `len >= 2 × block_size` | ✅ **无条件校验**（每批最多 64 块） |
| `onenand_block_by_block_erase()` | `len < 2 × block_size`、4KB 页芯片、Flex-OneNAND | ❌ **不校验**（`:2276`，只有 `ONENAND_CMD_ERASE` + 等状态） |

**这是"抽样 vs 全段"这一问题上一处真实的工程折中**：校验只在**批量擦除**时做。理由不难推断——批量擦除一次动 2~64 块，多一次硬件校验命令的边际成本很低；而单块擦除路径为了省那一次命令就不做。**代码里没有写这个理由**，此处属于从实现反推。

### 2.5.3 顺便：`CONFIG_MTD_ONENAND_VERIFY_WRITE` 是**写**校验，不是擦校验

`drivers/mtd/nand/onenand/Kconfig:11-18`：

```
config MTD_ONENAND_VERIFY_WRITE
	bool "Verify OneNAND page writes"
	help
	  This adds an extra check when data is written to the flash. The
	  OneNAND flash device internally checks only bits transitioning
	  from 1 to 0. There is a rare possibility that even though the
	  device thinks the write was successful, a bit could have been
	  flipped accidentally due to device wear or something else.
```

- 无 `default y` → **默认关闭**。
- 作用域是 `onenand.c:1606-1680` 的 `onenand_verify()` / `onenand_verify_oob()`（软件读回 `memcmp`，`:1642`、`:1665`），`#else` 分支把它们定义为常量 0（`:1677-1678`）。
- **这个 ifdef 不覆盖 `onenand_multiblock_erase_verify()`**（`:2130`）——后者在 ifdef 之外，因此**无论 CONFIG 开关如何都生效**。

**这段 Kconfig 帮助文本是本报告中最有价值的一段"官方理由"**，它把"为什么需要校验"说透了：

> The OneNAND flash device **internally checks only bits transitioning from 1 to 0**. There is a rare possibility that even though the device thinks the write was successful, a **bit could have been flipped accidentally due to device wear** or something else.

翻译其要点：**器件自身只检查 1→0 的位翻转**，所以"器件报告成功"不等于"实际正确"，尤其在器件磨损后。**这条理由对擦除同样成立**（擦除是 0→1，器件同样只做有限检查），这正是 OneNAND 的批量擦除要额外发 `ERASE_VERIFY` 命令的根本原因。

### 2.5.4 在整棵 MTD 树里的位置

配合前面的 Kconfig 普查（见 2.1.7）：`drivers/mtd/{,nand/,nand/raw/,nand/onenand/,nand/spi/,spi-nor/,ubi/,chips/,lpddr/,parsers/}/Kconfig` 中 grep `verify` 的全部命中只有两处——`nand/raw/Kconfig:103`（与校验无关的措辞）和 `nand/onenand/Kconfig:11`（本节这条）。

**结论：整个 Linux MTD 树的编译期开关里，与"校验写入内容"相关的选项有且仅有一个，就是 OneNAND 这个，且默认关闭；它管的是写校验。擦后校验在整棵树里没有任何编译期开关。**

---

## 2.6 ESP-IDF：主擦除路径不校验，但 **NVS 有一条可选的擦后校验 + 重试** 路径

基准：`espressif/esp-idf` master @ `e4df0c12f70daf0a7958e586e223c519fa9a1576`。

### 2.6.1 主擦除 API：不校验

| API | 是否 verify | 锚点 |
|---|---|---|
| `esp_flash_erase_region()` / `esp_flash_erase_chip()` | ❌ 否 | `components/spi_flash/esp_flash_api.c:645-754` |
| `esp_partition_erase_range()`（target） | ❌ 否，纯透传，无日志无重试 | `components/esp_partition/partition_target.c:125-146` |

擦除时唯一的"检查"是**状态寄存器轮询**——`wait_idle()` 轮询 `host_status()` 的 WIP 位（`components/spi_flash/spi_flash_chip_generic.c:427-446`），从不读回 flash 内容。唯一相关开关是超时控制，与校验无关：

```
CONFIG_SPI_FLASH_CHECK_ERASE_TIMEOUT_DISABLED   # default n
```
（`components/spi_flash/Kconfig:275-279`；用法在 `spi_flash_chip_generic.c:196-206`）

**一条重要的否定发现**：**`ESP_ERR_FLASH_ERASE_FAIL` 这个错误码在 ESP-IDF 中不存在。** 全仓代码搜索 0 命中；`components/spi_flash/include/esp_flash_err.h:34-37` 里的 flash 错误码全集只有 `NOT_INITIALISED` / `UNSUPPORTED_HOST` / `UNSUPPORTED_CHIP` / `PROTECTED`（外加 ROM 的 `+1/+2`）。

### 2.6.2 ★ ESP-IDF NVS：**一个真正的擦后校验实现（可选，带重试）**

这是 ESP-IDF 里唯一一处真正的擦后读回校验。`components/nvs_flash/src/nvs_partition.cpp:78-103`：

```c
        // Validate the erase operation by reading back the erased area.
        const size_t buf_size = 32;
        uint8_t buf[buf_size];
...
            if (buf[i] != 0xFF)
            {
                err = ESP_ERR_NOT_FINISHED; // Verification failed.
                break;
            }
        if(err == ESP_OK) return ESP_OK; // Erase and verification succeeded do not need to attempt again.
```

**完整的条件编译结构**（本报告已独立抓取 `components/nvs_flash/src/nvs_partition.cpp` 核实）：

```c
esp_err_t NVSPartition::erase_range(size_t dst_offset, size_t size)
{
#ifndef CONFIG_NVS_FLASH_VERIFY_ERASE
    return esp_partition_erase_range(mESPPartition, dst_offset, size);   // 默认：纯透传
#else
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < CONFIG_NVS_FLASH_ERASE_ATTEMPTS; ++attempt)
    {
        esp_err_t err = esp_partition_erase_range(mESPPartition, dst_offset, size);  // 内层 err
        if (err != ESP_OK) { continue; }
        // Validate the erase operation by reading back the erased area.
        const size_t buf_size = 32;
        uint8_t buf[buf_size];
        ...
                if (buf[i] != 0xFF)
                {
                    err = ESP_ERR_NOT_FINISHED; // Verification failed.
                    break;
                }
        ...
        if(err == ESP_OK) return ESP_OK;
    }
    return err;   // ← 返回的是外层 err
#endif
}
```

读回用 `esp_partition_read_raw()`，按 **32 字节**分块比对 0xFF。

> **一个读代码时值得注意的细节（对第 4、5 问有意义）**：内层 `esp_err_t err`（`:74`）**遮蔽**了外层 `err`（`:64`），而函数末尾 `return err;` 引用的是**外层**那个——它被初始化为 `ESP_FAIL` 后，在内层作用域之外**再未被赋值**。因此"用尽所有尝试后返回该次的具体错误码"这一说法并不成立，**实际返回的是泛化的 `ESP_FAIL`**，而不是 `ESP_ERR_NOT_FINISHED`。
>
> 也就是说：**ESP-IDF NVS 在内部用 `ESP_ERR_NOT_FINISHED` 区分"校验未通过"，但这个区分在 API 边界上丢失了**，调用方拿到的是一个无法与"擦除命令本身失败"区分的 `ESP_FAIL`。这与第 3 节的讨论直接相关——一个框架**内部**做了语义区分，却**没有把它暴露出去**。

**开关与默认值**（`components/nvs_flash/Kconfig:55-65`）：

```
    config NVS_FLASH_VERIFY_ERASE
        bool "Enable verification of erase operations by reading back the erased data"
        default n
...
    config NVS_FLASH_ERASE_ATTEMPTS
        int "Total number of flash erase attempts in case the erase verification fails"
        range 1 10
        default 2
```

- **`CONFIG_NVS_FLASH_VERIFY_ERASE` 默认 n**（关闭）
- **`CONFIG_NVS_FLASH_ERASE_ATTEMPTS` 默认 2**，范围 1–10

**失败处置——这是"校验 + 重试"这一组合少见的完整实例**：

1. 校验失败返回 `ESP_ERR_NOT_FINISHED`；
2. 按 `NVS_FLASH_ERASE_ATTEMPTS` 重试（默认共 2 次尝试）；
3. 重试耗尽后返回最后一个错误；
4. `Page::erase()`（`nvs_page.cpp:1170-1184`）把该页状态置为 `INVALID`；
5. PageManager（`nvs_pagemanager.cpp:100-103`、`:118-121`、`:190-193`）把错误向上传播，**不再重试**。

**即：ESP-IDF NVS 的模型是"擦 → 读回验 0xFF → 不过就重擦（默认 2 次）→ 仍不过则把该页标记 INVALID"。** 这是本报告中"重试 + 状态标记"最完整的一条链路。

> **注意**：NVS 的 BDL 模式**不**走这条路径（`nvs_partition.cpp:171-173`，普通 BDL erase，无校验）。

### 2.6.3 两个容易误读的"check"——都不是擦后校验

**(a) `CONFIG_ESP_PARTITION_ERASE_CHECK`（默认 y）是"写前检查"，不是"擦后校验"。** `components/esp_partition/partition_linux.c:587-597`（主机端模拟）：

```c
#ifdef CONFIG_ESP_PARTITION_ERASE_CHECK
        // Check if address to be written was erased first
        if((~((uint8_t *)dst_addr)[x] & ((uint8_t *)src)[x]) != 0) {
            ESP_LOGW(TAG, "invalid flash operation detected");
            ret = ESP_ERR_FLASH_OP_FAIL;
            break;
        }
#endif // CONFIG_ESP_PARTITION_ERASE_CHECK
```

它检查的是"**要写的位是否处于已擦除状态**"（0→1 非法），在**写**的时候做，目的是抓"忘了先擦"的编程错误。Kconfig 在 `components/esp_partition/Kconfig:10-17`。这与 UBI 的 `ubi_io_write()` 里那句 `"The area we are writing to has to contain all 0xFF bytes"` 是同一思路。

**(b) OTA 不检查目标分区的内容，只有一个断言。** `components/app_update/esp_ota_ops.c:448-450`：

```c
            // must erase the partition before writing to it
            assert(it->need_erase == 0 && "must erase the partition before writing to it");
```

这是**状态机断言**，不是读回比对。

**(c) 写校验是独立的开关**：`CONFIG_SPI_FLASH_VERIFY_WRITE`（`components/spi_flash/Kconfig:115-124`，**默认 n**）在写后读回比对已写入的字节（`esp_flash_api.c:1048-1103`，调用点 `:1195-1201`）。它管的是**写**，不是擦。

---

## 2.7 MCUboot：核心不校验；唯一的读回在 ESP 移植层且**默认是死代码**

基准：`mcu-tools/mcuboot` main @ `c85af35b875d244cf91f8ec3caa04168fc7f1ec1`。

### 2.7.1 核心：`boot_erase_region()` 不校验

`boot/bootutil/src/bootutil_area.c:276-282`：

```c
            rc = flash_area_erase(fa, off, csize);
            if (rc < 0) {
                goto end;
            }
            MCUBOOT_WATCHDOG_FEED();
```

整个 `boot_erase_region()`（`:217-315`）内**没有任何读回**。`flash_area_erase_verify` 这类函数**不存在**（在 `boot/bootutil` 中 grep `erase_verify` = 0 命中）。

### 2.7.2 ★ 必须澄清的一点：`bootutil_buffer_is_erased` **不是**擦后校验

MCUboot 里出现多处"erasable/erased"判断，容易误读成擦后校验。**它不是。** `boot/bootutil/src/bootutil_public.c:196-215`：它读的是 **swap status / state 字节**，用于判断**状态机的当前状态**（"这个槽里的状态字节是空的还是有值的"），是**状态检测**而非**校验刚做的擦除**。

调用点：`swap_scratch.c:215`、`swap_move.c:169`、`swap_offset.c:254`、`swap_misc.c:197`、`bootutil_public.c:226,256,272`、`bootutil_loader.c:62`。

> **这正是本报告反复强调的那条分界线**：**"读回确认某区域是空的"** 与 **"核对某次擦除操作是否成功"** 是两件事。MCUboot 大量做的是前者。

### 2.7.3 ESP 移植层：唯一的读回，且被一个**全仓未定义**的宏关着

`boot/espressif/port/esp_mcuboot.c:490-497`：

```c
#if VALIDATE_PROGRAM_OP && !defined(CONFIG_SECURE_FLASH_ENC_ENABLED)
    for (size_t i = 0; i < len; i++) {
        uint8_t *val = (void *)(start_addr + i);
        if (*val != 0xff) {
            BOOT_LOG_ERR("%s: Erase at 0x%x Failed", __func__, (int)val);
            assert(0);
        }
    }
#endif
```

**这一段的性质需要说清楚**：

- 宏 `VALIDATE_PROGRAM_OP` 在**整个 mcuboot 仓库中没有任何定义处**（grep 只找到这一个使用点），在 esp-idf 中也搜不到定义。
- 因此**默认情况下这段代码不会被编译进去**——它实质上是"死代码"，除非使用者在编译时自行提供该宏。
- 且**失败处置是 `assert(0)`**（直接断言崩溃），不是返回错误码、不是标记坏块。

**结论：MCUboot 在生产路径上不做擦后校验，也没有为它提供可用的开关。**

---

## 2.8 ★ flashrom：本次调研范围内**唯一默认开启擦后校验**的成熟实现

基准：`flashrom/flashrom` main @ `2abdd70f648df42ece6928b63fc1f6668bd97f98`。

在本报告覆盖的全部对象中，**只有 flashrom 在擦除路径上默认带校验**。这一点必须单独强调，因为它与本报告其余所有结论方向相反。

> 限定：这是**本报告已取证范围内**的结论。第 5.3 节尚未覆盖的对象（Zephyr / NuttX / RT-Thread / STM32 / nrfx / OpenBLT）中若也存在同类实现，本结论需相应修正。

### 2.8.1 擦除后立刻读回比对，默认开启

`erasure_layout.c:295-302`：

```c
			if (erasefn(flashctx, start_addr, block_len)) {
				return -1;
			}
			if (flashctx->flags.verify_after_write
				&& check_erased_range(flashctx, start_addr, block_len)) {
				msg_cerr("ERASE FAILED!\n");
				return -1;
			}
```

**每擦一个块，立刻 `check_erased_range()` 整块读回比对**；不过就 `"ERASE FAILED!"` 并返回 -1。

### 2.8.2 默认值就是"校验开"

命令行默认值，`cli_classic.c:1612`：

```c
	flashrom_flag_set(context, FLASHROM_FLAG_VERIFY_AFTER_WRITE, !options.dont_verify_it);
```

即：**只有显式传 `-n/--noverify` 才会关闭**。默认 `dont_verify_it` 为假 → 校验开。

### 2.8.3 官方文档原文：关掉校验"不推荐"

`doc/classic_cli_manpage.rst`：

- `:74-76`
  > **-n, --noverify** — Skip the automatic verification of flash ROM contents after writing or erasing. Using this option is **not** recommended, you should only use it if you know what you are doing and if you feel that the time for verification takes too long.
- `:82`
  > This option is only useful in combination with ``--write`` or ``--erase``.
- `:65-68`
  > In case of erase errors it is even re-read completely. After writing has finished and if verification is enabled, the whole flash chip is read out and compared with the input image.

**"Using this option is not recommended"** —— 这是本次调研中唯一一处官方文档**劝阻**用户关闭擦后校验的表述。

### 2.8.4 ★ 一个关键实现细节：擦后值**不一定**是 0xFF

`include/flash.h:185`：

```c
#define ERASED_VALUE(flash)	(((flash)->chip->feature_bits & FEATURE_ERASED_ZERO) ? 0x00 : 0xff)
```

`check_erased_range()`（`flashrom.c:560-575`）：

```c
int check_erased_range(struct flashctx *flash, unsigned int start, unsigned int len)
{
	...
	memset(cmpbuf, erased_value, len);
	ret = verify_range(flash, cmpbuf, start, len);
```

**flashrom 显式处理了"擦后值是 0x00 而不是 0xFF"的芯片**（`FEATURE_ERASED_ZERO`）。也就是说：**要做擦后校验，就不能把 0xFF 硬编码为"擦后值"——它得是可以配置的。**

这与 littlefs 那句 `"The state of an erased block is undefined"`（见 2.2.1）是同一个技术事实的两种应对：littlefs 选择"不假设、因此不校验"，flashrom 选择"把假设做成一个可按芯片配置的常量"。

### 2.8.5 快照

| 项 | 值 |
|---|---|
| 是否 verify | ✅ **是** |
| 触发时机 | **每一次块擦除之后**（`erasure_layout.c:295-302`） |
| 开关 | `FLASHROM_FLAG_VERIFY_AFTER_WRITE`；命令行 `-n/--noverify` |
| **默认值** | **开（校验）** |
| 失败处置 | `msg_cerr("ERASE FAILED!\n")` + 返回 -1（中止） |
| 写校验 | 同样默认开；整片读回与输入镜像比对 |

---

## 2.9 OpenOCD：擦除不校验；空白检查是一个**独立的手工命令**

基准：`openocd-org/openocd` master @ `8056a09f1d065bbb5207a44d4bbdd39d2e6f729d`。

### 2.9.1 擦除：只调驱动，不校验

`src/flash/nor/core.c:29-39`：

```c
int flash_driver_erase(struct flash_bank *bank, unsigned int first,
		unsigned int last)
{
	int retval;

	retval = bank->driver->erase(bank, first, last);
	if (retval != ERROR_OK)
		LOG_ERROR("failed erasing sectors %u to %u", first, last);

	return retval;
}
```

无读回。

### 2.9.2 `write_image` 的 verify 被硬编码为 `false`

`src/flash/nor/tcl.c:448-449`：

```c
	retval = flash_write_unlock_verify(target, &image, &written, auto_erase,
		auto_unlock, true, false);
```

最后两个参数中的 `false` 就是 verify 位。**OpenOCD 的 `flash write_image` 没有 `-verify` 选项**——手册定义（`doc/openocd.texi:6131`）为：

```
@deffn {Command} {flash write_image} [erase] [unlock] filename [offset] [type]
```

（同一签名在 v0.12.0 `:5632`、v0.11.0 `:5275` 一致。）校验被拆成**另一个需要用户手动敲的命令** `flash verify_image`（`:6168`）。当它被调用时，比对的是**写入的镜像缓冲区**，而不是擦后状态（`core.c:982-987`）：

```c
		if (retval == ERROR_OK) {
			if (verify) {
				/* verify flash sectors */
				retval = flash_driver_verify(c, buffer, run_address - c->base, run_size);
			}
		}
```

### 2.9.3 空白检查：`flash erase_check`，手工触发

`flash erase_check` 命令（`src/flash/nor/tcl.c:179-226`）调用驱动的 `erase_check`（`core.c:331-380`、`:382-430`），打印每个扇区的 "erased / not erased / unknown"。

**但 OpenOCD 刻意不去缓存这个结果**——`src/flash/nor/core.h:33-42` 的注释原文：

```c
	 * Indication of erasure status: 0 = not erased, 1 = erased,
	 * other = unknown.  Set by @c flash_driver::erase_check only.
	 *
	 * This information must be considered stale immediately.
	 * Don't set it in flash_driver::erase or a device mass_erase
```

> **"Don't set it in `flash_driver::erase`"** —— OpenOCD 明确禁止在擦除路径上顺手把"已擦除"状态标记为真，理由是 **"This information must be considered stale immediately."**

这是一条很值得玩味的官方立场：**"擦除完成后宣称该区域是空白的"这个信息，被认为立刻就会过期，因此不值得在擦除路径上生成。** 它从侧面说明为什么很多栈不把擦后校验的结果当作可长期信赖的状态。

### 2.9.4 快照

| 操作 | 是否 verify | 触发 |
|---|---|---|
| `flash erase_sector` / `write_image erase` | ❌ 否 | — |
| `flash write_image` | ❌ 否（verify 硬编码 `false`） | — |
| `flash verify_image` | 比对**写入的镜像**，非擦后状态 | **用户手工执行** |
| `flash erase_check` | 空白检查 | **用户手工执行** |

---

## 2.10 ★ STM32：厂商 HAL 不校验，但厂商**自己的 EEPROM 模拟库有"首次使用/格式化时校验"**

这一节直接回答了第 2 问中"**是否只在格式化/首次使用时校验**"——**找到了一个真实先例。**

### 2.10.1 STM32Cube HAL：**不校验**，只轮询状态标志

| 函数 | 锚点 | 机制 |
|---|---|---|
| `HAL_FLASHEx_Erase`（STM32F4） | `stm32f4xx_hal_flash_ex.c:160` | 发擦除命令 → `FLASH_WaitForLastOperation` → **只轮询 BSY / EOP / WRPERR / PGSERR 标志** |
| `HAL_FLASHEx_Erase`（STM32F1） | `stm32f1xx_hal_flash_ex.c:157` | 同上 |
| `HAL_FLASHEx_Erase`（STM32F0） | `stm32f0xx_hal_flash_ex.c:157` | 同上 |
| `FLASH_WaitForLastOperation` | `stm32f4xx_hal_flash.c:551` | 状态寄存器轮询 |
| `HAL_FLASHEx_Erase_IT`（中断版） | F4 `:231`、F1 `:317`、F0 `:238` | 同样只查标志 |

**没有读回。** 且 **HAL 中不存在 LL 层的 flash 驱动**——F4/L4/U5/H5 的 HAL driver `Inc` 目录下均无 `*_ll_flash.h`。

> 关键区分：`FLASH_WaitForLastOperation` 轮询的是 `BSY`/`EOP`/`WRPERR`/`PGSERR`，**这是"操作是否完成、控制器是否报错"，不是"擦后内容是否正确"**。这正是本报告一以贯之要区分的那条线。

擦除主体的原文（F4，`stm32f4xx_hal_flash_ex.c:189-205`）：

```c
        FLASH_Erase_Sector(index, (uint8_t) pEraseInit->VoltageRange);

        /* Wait for last operation to be completed */
        status = FLASH_WaitForLastOperation((uint32_t)FLASH_TIMEOUT_VALUE);

        /* If the erase operation is completed, disable the SER and SNB Bits */
        CLEAR_BIT(FLASH->CR, (FLASH_CR_SER | FLASH_CR_SNB));

        if (status != HAL_OK)
        {
          /* In case of error, stop erase procedure and return the faulty sector*/
          *SectorError = index;
          break;
        }
```

`FLASH_WaitForLastOperation` 的实质（F4 `stm32f4xx_hal_flash.c:566-596`）：

```c
  while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY) != RESET) { … return HAL_TIMEOUT; }
  if (__HAL_FLASH_GET_FLAG(FLASH_FLAG_EOP) != RESET) { __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP); }
  if (__HAL_FLASH_GET_FLAG((FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | \
                            FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR)) != RESET)
  { FLASH_SetErrorCode(); return HAL_ERROR; }
```

**失败处置**：返回 `HAL_ERROR` 并通过 `*SectorError` 回填**出错的扇区序号**（这是"失败位置"，与 MTD 的 `fail_addr` 同构），**但不读回 flash 内容**。

### 2.10.2 ★ ST 官方 EEPROM 模拟库：**有** `VerifyPageFullyErased()`——但它校验的是**"页自称已擦除"这个声明**，不是"我刚做的擦除"

仓库：`STMicroelectronics/stm32-util-eeprom-emulation`（ST 官方，README tag 2.1.0）。

| 项 | 锚点 |
|---|---|
| `VerifyPageFullyErased()` 声明 | `core/algo/eeprom_algo_flitf.c:473` |
| 定义 | `core/algo/eeprom_algo_flitf.c:1520` |
| 调用点 | `:918-925`，在 `case PAGE_STATE_ERASED:` 分支内；另见 `EE_FLITF_Init()` 第 9 步（`:852`）与 `EE_FLITF_Format()`（`:920`） |
| 判定方式 | 整页读回，按 8/16 字节单元与 `EE_PAGESTAT_ERASED`（`0xFFFFFFFFFFFFFFFF`）比对；不等或 ECC 错 → `EE_ERROR_ALGO` |
| 失败处置 | 调用方**强制执行一次真正的擦除**（`:855` / `:923`） |

函数自身的文档注释（`core/algo/eeprom_algo_flitf.c:1518-1526`）：

```c
/**
  * @brief  Verify if specified page is fully erased.
  * @param  address page address
  * @retval ee_status
  *           - @ref EE_ERROR_ALGO : if Page not erased
  *           - @ref EE_OK    : if Page erased
  */
static ee_status VerifyPageFullyErased(uint32_t address)
```

**准确的语义是"验证一个声明，然后修复"**——触发条件是**某个页的状态自称是 `ERASED`**，此时去读回确认这个声明是否属实；不属实就强制擦一遍。调用点原文（`:918-925`）：

```c
        case PAGE_STATE_ERASED:
          if (VerifyPageFullyErased(GetPageAddress(page)) != EE_OK)
          {
            /* If Erase operation was failed, a Flash error code is returned */
            if (Page_SetState(page, PAGE_STATE_ERASED) !=  EE_OK)
```

**三个必须说清的限定：**

1. **它不是"擦完立刻读回"。** 校验发生在一个页**自称已擦除**的时刻（初始化/格式化的条件擦除分支），不是紧跟在一次擦除之后。
2. **只在条件擦除路径上做。** `EE_CONDITIONAL_ERASE`（先验证再决定是否擦）会走这个校验；**`EE_FORCED_ERASE`（无条件强制擦）跳过它**。
3. **常规擦除路径不校验。** `Page_SetState(PAGE_STATE_ERASED)`（`:1820`）执行擦除后**不读回**。

### 2.10.3 必须澄清：AN2594 里的 "ERASED" **不是**读回校验的结果

AN2594（EEPROM emulation in STM32F10x，Doc ID 13718 Rev 3）§2.1 原文：

```
        Each page has three possible states:
         ERASED: the page is empty.
         RECEIVE_DATA: the page is receiving data from the other full page.
         VALID_PAGE: the page contains valid data and this state does not change until all
              valid data are completely transferred to the erased page.
```

**这里的 "ERASED" 是一个写在页头里的状态标记，不是"读回确认内容全 0xFF"的结果。** 页状态机靠这个标记工作，而标记本身是否属实，在 AN2594 的流程里**不被验证**。

- AN2594 §3.3 的 `EE_Init()` 确实会"用页状态检查完整性并修复"，但 Table 2 给出的动作只有 `Erase Page1 and mark Page0 as VALID_PAGE` / `Erase both pages and format page0`——**没有验证步骤**。
- AN2594 的 `EE_VerifyPageFullWriteVariable`（参考实现 `eeprom.c:586`、`:610`）是**写之前的可用空间检查**，与擦后校验无关。

> **这一条是本报告中最容易误引的点**：一个名为 "ERASED" 的状态常量看起来像"已校验为空"，实际上只是一个**未经验证的自我声明**。ST 的 X-CUBE/官方库后来补的 `VerifyPageFullyErased()` 正是为了补上"这个声明到底可不可信"这一环——**但补的位置是"使用前验证声明"，而不是"擦除后立即校验"。**

---

## 2.11 Nordic nrfx / nRF5 SDK：**完全不校验**，且错误码本身就不携带信息

| 层 | 函数 | 锚点 | 行为 |
|---|---|---|---|
| nrfx | `nrfx_nvmc_page_erase()` | `drivers/src/nrfx_nvmc.c:208-223`（`return 0` 在 `:223`） | 仅当地址不对齐时返回 `-EACCES`；否则执行擦除、轮询 `nrf_nvmc_ready_check`，然后**无条件 `return 0`** |
| Zephyr 适配 | `soc_flash_nrf.c` `erase_op` | `:415`（`(void)nrfx_nvmc_page_erase(...)`） | **返回值被显式丢弃**；只有对齐检查（`-EINVAL`）与 POFWARN 路径会报错（擦除入口 `:221`） |
| nrfx 配置 | `drivers/flash/Kconfig.nrf`（56 行） | — | **"verify" 零命中** |
| nRF5 SDK fstorage 后端 | `nrf_fstorage_erase()` → `nrf_fstorage_nvmc.c:150-174` | — | 逐页调 `nrf_nvmc_page_erase`，然后**无条件 `return NRF_SUCCESS`** |
| nRF5 SDK DFU | `nrf_dfu_flash.c:144-165` | — | 会传播 `nrf_fstorage_erase()` 的结果，失败时 `NRF_LOG_WARNING("nrf_fstorage_erase() failed…")` |

**Nordic 的擦除 API 在最底层就没有表达失败的能力**——`nrfx_nvmc_page_erase` 无条件返回 0，SDK 层无条件返回 `NRF_SUCCESS`（Zephyr 适配层甚至连这个返回值都丢掉）。

这与第 5 问直接相关：**在这里，"本次 IO 失败"与"该区不可用"连区分的载体都不存在。** Nordic 的擦除接口在签名上就无法携带失败信息。

（NVMC 是片上 flash 控制器，Nordic 的设计前提应是"片上 flash 不会擦失败"。这一点代码和文档中均未找到明文说明，**属推断**。）

---

## 2.12 OpenBLT：擦除不校验；但**擦前**有空白检查、**写后**有读回

仓库：`feaser/openblt`。

| 目标 | 擦除后校验 | 擦前空白检查 | 写后读回 |
|---|---|---|---|
| `ARMCM4_STM32F4` | ❌ 无 | ✅ `FlashEmptyCheckSector`（`Target/Source/ARMCM4_STM32F4/flash.c:674`，调用点 `:771`） | ✅ `FlashWriteBlock`（`:651`） |
| `ARMCM3_STM32F1` | ❌ 无 | ❌ **无**（`Target/Source/ARMCM3_STM32F1/flash.c:289` `FlashErase`，调用 `:342`） | ✅ `:704` |

**OpenBLT 的亮点是"擦前空白检查"**——`flash.c:770-778` 原文：

```c
      /* no need to erase the sector if it is already empty */
      if (FlashEmptyCheckSector(sectorIdx) == BLT_FALSE)
      {
        /* keep the watchdog happy */
        CopService();
        /* set the sector to erase */
        eraseInitStruct.Sector = sectorIdx;
```

即：**擦之前先确认该扇区是不是已经是空的，是就整个跳过擦除**（省寿命）。这与 U-Boot `sf update` 的 `memcmp` 跳过、UBI 的 `ubi_io_write()` 写前 0xFF 检查属于同一类优化，**但都不是擦后校验**。

擦除失败处置：`HAL_FLASHEx_Erase != HAL_OK` → 返回 `BLT_FALSE`，循环中止。

> **顺带说明**：`FlashEmptyCheckSector` **不能**替代擦后校验——它检查的是"**擦之前**是不是空的"，而擦后校验要回答的是"**擦之后**是不是真的空了"。前者通过不代表后者会通过。
>
> 另注：`FlashVerifyChecksum`（`:385`）是 NVM 校验和，与擦除无关。

---

## 2.13 ★ Zephyr：`flash_area_erase` 不校验，但 **NVS 每次擦除扇区都无条件校验**

### 2.13.1 flash_map API 与各驱动：不校验

| 对象 | 是否 verify | 锚点 |
|---|---|---|
| `flash_area_erase()` | ❌ 否，只调 `flash_erase()` | `subsys/storage/flash_map/flash_map.c:71-78` |
| API 文档 | **未提及校验** | `include/zephyr/storage/flash_map.h:223-236` |
| `drivers/flash/spi_nor.c` | ❌ 否；`FLSR` 状态寄存器的 `ERASE_FAIL` → `-EIO` | 擦除 `:1039`；状态检查 `:520-545` |
| `drivers/flash/flash_stm32.c` | ❌ 否 | `:166`、`:185` |
| `drivers/flash/soc_flash_nrf.c` | ❌ 否 | `:221`、`:333` |
| `drivers/flash/flash_page_layout.c` | ❌ **根本没有擦除代码** | 全文件 |
| littlefs 胶水层 `lfs_api_erase()` | ❌ 否（转 `flash_area_flatten`） | `subsys/fs/littlefs_fs.c:198-205` |

`flash_area_erase()` 的 API 契约原文（`include/zephyr/storage/flash_map.h:226-228`）：

> "Erase given flash area range. Area boundaries are asserted before erase request. API has the same limitation regard erase-block alignment and size as wrapped flash driver."

**契约里没有一个字提到"校验"或"擦后值"。**

### 2.13.2 ★ Zephyr NVS：**每次擦扇区都做，无条件，无开关**

基准：**tag `v3.7.0`**（见 5.4 节关于当前 `main` 的说明）。`subsys/fs/nvs/nvs.c:331`：

```c
/* erase a sector and verify erase was OK.
 * return 0 if OK, errorcode on error.
 */
static int nvs_flash_erase_sector(struct nvs_fs *fs, uint32_t addr)
...
	rc = flash_flatten(fs->flash_device, offset, fs->sector_size);   /* :347 */
	if (rc) { return rc; }
	if (nvs_flash_cmp_const(fs, addr, fs->flash_parameters->erase_value,
				fs->sector_size)) {                      /* :353 */
		rc = -ENXIO;                                                 /* :355 */
	}
```

`nvs_flash_cmp_const()`（`:264-293`）把 flash **读进缓冲区**，遇到第一个 ≠ `erase_value` 的字节就返回 1——**确实是一次真读回比对**。

**三个关键点：**

1. **触发时机是每一次扇区擦除**，调用点：`:746`（`nvs_erase`）、`:901`/`:905`/`:948`（GC）、`:986`（erase-all，遍历所有扇区）。
2. **没有开关，没有默认值**——不像 ESP-IDF NVS 那样需要 `CONFIG_*`，Zephyr NVS 是无条件做的。
3. **★ 比对的值不是硬编码的 `0xFF`，而是 `fs->flash_parameters->erase_value`**——从 flash 参数里取的"本器件擦后值"。

> **第 3 点值得单独强调**：这与 flashrom 的 `ERASED_VALUE(flash)`（见 2.8.4）、NuttX 的 `CONFIG_NXFFS_ERASEDSTATE`（见 2.14.3）是同一个设计要点。**三个独立实现都不约而同地把"擦后值"做成了从器件参数读取、而非硬编码 0xFF。** 这与 littlefs 那句 "The state of an erased block is undefined" 互相印证：**擦后值确实不能假定为 0xFF。**

**失败处置**：返回 `-ENXIO`，向调用方传播。**不重试、不标记坏块、不切换扇区。**

### 2.13.3 flash_simulator：Zephyr 用"注入失败"而非"读回校验"来测擦除

`drivers/flash/flash_simulator.c` 提供了多种擦除失败建模：

| 机制 | 锚点 | 行为 |
|---|---|---|
| 按单元注入 | `:315-323`（`flash_simulator_erase_unit_cb_t erase_cb`） | 回调可返回错误 |
| 写次数超阈值 → **磨损耗尽** | `:349-355` | **擦除静默返回 0**（不报错！） |
| 双重写入检测 | `:226-231` | `-EIO` |
| 显式擦除要求 | `:275-279` | — |

配置项：`CONFIG_FLASH_SIMULATOR_CALLBACKS`、`CONFIG_FLASH_SIMULATOR_DOUBLE_WRITES`、`CONFIG_FLASH_SIMULATOR_EXPLICIT_ERASE`、`CONFIG_FLASH_SIMULATOR_STATS`。

**注意"磨损耗尽后擦除静默返回 0"这一条**——这是模拟器刻意建模的一种真实故障模式：**擦除操作报告成功，但实际上没擦**。这正是擦后校验要抓的场景，而 Zephyr 的应对是在模拟器里建模它、在 NVS 里校验它。

---

## 2.14 NuttX：MTD 核心不校验；**NXFFS 在重新格式化时校验并标坏块**

### 2.14.1 MTD 核心与分区：不校验

| 对象 | 锚点 | 行为 |
|---|---|---|
| `MTD_ERASE` 宏 | `include/nuttx/mtd/mtd.h:94` | 透传 |
| `mtd_partition.c` | `:219-244`、`:467-476` | 只传驱动返回码 |

### 2.14.2 ★ NXFFS：**重新格式化时**逐块校验，不合格标坏块

这是本次调研中**第二个"格式/挂载时校验"的实例**（第一个是 ST 的 EEPROM 模拟库，见 2.10.2）。

`fs/nxffs/nxffs_reformat.c:258` 的函数注释原文：

> "Erase and reformat the entire volume. **Verify each block and mark improperly erased blocks as bad.**"

实际检查（`:207-217`）：

```c
      /* This is a properly formatted, good NXFFS block. Check that the
       * block data payload is erased. */
              size_t erasesize = nxffs_erased(&blkptr[SIZEOF_NXFFS_BLOCK_HDR], blocksize);
              good = (blocksize == erasesize);
```

不合格则（`:222-224`）：

```c
              nxffs_blkinit(volume, blkptr, BLOCK_STATE_BAD);
              modified = true;
```

**触发时机**：在挂载时，如果"好块/未格式化块"的数量越过阈值，或统计失败——即**"这个卷看起来不对劲，我把它整体擦一遍并逐块确认"**。触发逻辑在 `fs/nxffs/nxffs_initialize.c:245-250`、`:281`。

**开关**：**没有专门的校验开关**。不存在 `CONFIG_NXFFS_VERIFY`（在 `fs/nxffs/*.c`、`nxffs.h`、`Kconfig` 中**零命中**）。行为只受 `CONFIG_NXFFS_REFORMAT_THRESH`（int，**默认 20**）控制——那是"什么时候触发整体重整"的阈值，不是"要不要校验"的开关。

`fs/nxffs/Kconfig` 的完整符号表：`FS_NXFFS`（默认 n）、`NXFFS_SCAN_VOLUME`（n）、`NXFFS_NAND`（n）、`NXFFS_REFORMAT_THRESH`（int，20）、`NXFFS_PREALLOCATED`（y）、`NXFFS_ERASEDSTATE`（hex，**0xff**）、`NXFFS_PACKTHRESHOLD`（32）、`NXFFS_MAXNAMLEN`（255）、`NXFFS_TAILTHRESHOLD`（8192）。

### 2.14.3 两个**不是**擦后校验的近似物（容易误引）

| 符号 | 实际作用 | 锚点 |
|---|---|---|
| `MTDIOC_ERASESTATE` ioctl | **一次性探测**"本器件的擦后值是什么字节"，不是校验 | `include/nuttx/mtd/mtd.h:79-81` |
| `nxffs_verifyblock()` | 只检查块头 magic + `BLOCK_STATE_GOOD`，**不读数据** | `fs/nxffs/nxffs_block.c:65-110` |
| `nxffs_wrverify()` | 向前扫描找连续已擦除字节，用来确定**写入起点** | `fs/nxffs/nxffs_write.c:849`，调用点 `:156` |
| `mtd_config.c` 的读回 | 在**擦除之前**读回以保留数据，不是擦后校验 | `:594`（`MTD_BREAD`）、`:604`（`MTD_ERASE`） |

`MTDIOC_ERASESTATE` 定义原文（`include/nuttx/mtd/mtd.h:79-81`）：

```c
#define MTDIOC_ERASESTATE   _MTDIOC(0x000a) /* IN:  Pointer to uint8_t
                                             * OUT: Byte value that represents the
                                             *      erased state of the MTD cell */
```

> **注意**：你（提问方）提到的 `mtd_erase_state` **函数**在本次核查中**未找到**——`include/nuttx/mtd/mtd.h`、`drivers/mtd/mtd_partition.c`、`mtd_config.c`、`smart.c`、`ramtron.c`、`drivers/mtd/Kconfig` 及 NXFFS 源码中均无该符号。唯一相关的是上面这个 `MTDIOC_ERASESTATE` **ioctl**。详见 5.4。

### 2.14.4 SMART 文件系统：**擦除的返回值被直接丢弃**

`drivers/mtd/smart.c:2703-2729`：函数为 `static void smart_erase_block_if_empty(...)`（**返回 void**），内部执行：

```c
	MTD_ERASE(dev->mtd, block, 1);
```

**没有任何 rc 捕获。** 即：SMART 层连"擦除是否报错"都不关心。`CONFIG_SMARTFS_ERASEDSTATE`（hex，默认 `0xff`）只是哨兵值，不是校验开关（`fs/smartfs/Kconfig:14-19`）。`fs/smartfs/smartfs_smart.c` 中**根本没有擦除调用**。

---

## 2.15 RT-Thread：FAL 与 SFUD 均不校验

| 组件 | 锚点 | 行为 |
|---|---|---|
| FAL `fal_partition_erase()` | `components/fal/src/fal_partition.c:490-518`（擦除调用 `:510`）、`:527-530` | 纯透传；失败仅 `LOG_E("Partition erase error! ...")` 后返回 rc |
| FAL 配置 | — | **`FAL_USING_DEBUG` 只控日志级别，无 verify 选项** |
| SFUD `sfud_chip_erase()` | `components/drivers/spi/sfud/src/sfud.c:453-497` | 结束时 `wait_busy(flash)`（WIP 状态轮询），**不读回数据** |
| SFUD `sfud_erase()` | `sfud.c:510-598` | 同上，逐扇区 + `wait_busy` |

FAL 擦除实现原文（`fal_partition.c:510`）：

```c
    ret = flash_dev->ops.erase(part->offset + addr, size);
    if (ret < 0)
    {
        LOG_E("Partition erase error! Flash device(%s) erase error!", part->flash_name);
    }
    return ret;
```

**RT-Thread 生态（FAL + SFUD）在本报告覆盖范围内没有任何擦后校验。**

---

## 3. 重点：「单次 IO 失败」与「该区结构性不可用」是否分成两个码

**结论：找到了先例，且有两种不同形态；但"一对平级的专用枚举"这一形态确实没有先例。**

分两种形态：

- **形态 A —— 专用码**：littlefs 定义了 **`LFS_ERR_CORRUPT`（-84）**，文档措辞是 `"if the block should be considered bad"`，与泛指的 `LFS_ERR_IO`（-5）明确分家（详见 2.2）。这是最接近"两个语义分开"的先例。
- **形态 B —— 复用既有 errno 做约定 + 调用方状态机**：UBI、CFI NOR、JFFS2 都走这条。它们不为"块坏"新造码，而是**约定某个既有 errno 承载该语义**（UBI/CFI 用 `-EIO`），再用"暂时性错误码组"（`-EAGAIN`/`-ENOMEM`/`-EBUSY`/`-EINTR`）表达"本次 IO 没成"。

**没有任何框架定义了类似 `ERASE_IO_FAILED` / `REGION_UNUSABLE` 这样一对平级的专属枚举。** 也没有任何框架把这对区分**挂在擦除操作自身上**——littlefs 的 `LFS_ERR_CORRUPT` 虽然专用，但它的语义由**块设备驱动**决定并返回，littlefs 自己是消费方。

下面是逐例。

### 3.1 littlefs：专用码 `LFS_ERR_CORRUPT` + 换块重试（形态 A 的代表）

完整取证见 2.2 节，此处只提炼与本题直接相关的三点：

1. **两个语义由两个码承载**：`LFS_ERR_IO = -5`（"Error during device operation"，本次操作失败）与 `LFS_ERR_CORRUPT = -84`（"Corrupted"）。后者在 `lfs.h:170` 和 `lfs.h:177` 被两次解释为 `"if the block should be considered bad"`。
2. **消费端三分支**（`lfs.c:2935`、`1996`、`3278` 三处同构）：

   ```c
        err = lfs_bd_erase(lfs, nblock);
        if (err) {
            if (err == LFS_ERR_CORRUPT) {
                goto relocate;      // 结构性不可用 → 换下一块
            }
            return err;             // 本次 IO 失败 → 原样上报
        }
   ```

3. **换块重试无次数上限**，靠块池耗尽（`lfs_alloc()` 返回 `LFS_ERR_NOSPC`）兜底；且对**超级块**另设临界点——不可 relocate，直接 `LFS_ERR_NOSPC` + `"Superblock ... has become unwritable"`（`lfs.c:2107-2110`）。

**关键限定：这个区分不是 littlefs 在擦除时"测"出来的，而是块设备驱动"报"出来的。** littlefs 自身不做任何擦后读回（`lfs.c:276` 纯透传）。

### 3.2 UBI：最完整的三分类先例

`drivers/mtd/ubi/wl.c:1129-1146`，`__erase_worker()` 对 `ubi_sync_erase()` 的返回值做三级分派：

```c
	ubi_err(ubi, "failed to erase PEB %d, error %d", pnum, err);

	if (err == -EINTR || err == -ENOMEM || err == -EAGAIN ||
	    err == -EBUSY) {
		int err1;

		/* Re-schedule the LEB for erasure */
		err1 = schedule_erase(ubi, e, vol_id, lnum, wl_wrk->torture, true);
		...
		return err;
	}

	spin_lock(&ubi->wl_lock);
	wl_entry_destroy(ubi, e);
	spin_unlock(&ubi->wl_lock);
	if (err != -EIO)
		/*
		 * If this is not %-EIO, we have no idea what to do. Scheduling
		 * this physical eraseblock for erasure again would cause
		 * errors again and again. Well, lets switch to R/O mode.
		 */
		goto out_ro;

	/* It is %-EIO, the PEB went bad */
	if (!ubi->bad_allowed) {
		ubi_err(ubi, "bad physical eraseblock %d detected", pnum);
		goto out_ro;
	}
	...
	ubi_msg(ubi, "mark PEB %d as bad", pnum);
	err = ubi_io_mark_bad(ubi, pnum);
```

三档语义如下表。**注意区分维度不是"错误严重程度"，而是"这个错误告诉了我们什么"**：

| 档 | 错误码 | UBI 的解读 | 动作 | 该区后续可用性 |
|---|---|---|---|---|
| 1 | `-EINTR` `-ENOMEM` `-EAGAIN` `-EBUSY` | 这次没做成，**原因在系统侧，不在 flash** | `schedule_erase(...)` 重新入队，稍后再擦 | **不变**，仍是好块 |
| 2 | `-EIO` | **这个 PEB 坏了** | `ubi_io_mark_bad()` 写 EC 头标记；消耗预留 PEB；`bad_peb_count++`、`good_peb_count--` | **永久退役**，不再进 free pool |
| 3 | 其他一切 errno（含 `-EINVAL`、`-EROFS`）| **我不知道发生了什么** | `out_ro:` → `ubi_ro_mode(ubi)` **整个 UBI 设备切只读** | 停止一切写入，人工介入 |

第 2 档的判据是**硬编码**的——`if (err != -EIO) goto out_ro;`。UBI 没有为"块不可用"发明新码，而是**直接约定 `-EIO` 就是 bad block 的信号**。这个约定在函数注释里被显式写死，`drivers/mtd/ubi/io.c:313-317`：

```c
/**
 * do_sync_erase - synchronously erase a physical eraseblock.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to erase
 *
 * This function synchronously erases physical eraseblock @pnum and returns
 * zero in case of success and a negative error code in case of failure. If
 * %-EIO is returned, the physical eraseblock most probably went bad.
 */
```

**这是整个调研中最重要的一条证据**：一个经过 20 年生产检验的 flash 管理层，把"该区不可用"这件事的表达方式定为 **`-EIO` 这一个约定 + 文档注释**，而不是新造错误码。

#### 3.2.1 重要限定：UBI **另有**一套专用码 `UBI_IO_*`（只是不用于擦除）

上面的"复用 errno"结论需要补一个重要的限定：**UBI 确实有一套自己定义的、与 errno 无关的状态码**，只不过用在**读路径**而不是擦除路径。

`drivers/mtd/ubi/ubi.h:89-111`：

```c
/*
 * Error codes returned by the I/O sub-system.
 *
 * UBI_IO_FF: the read region of flash contains only 0xFFs
 * UBI_IO_FF_BITFLIPS: the same as %UBI_IO_FF, but also there was a data
 *                     integrity error reported by the MTD driver
 *                     (uncorrectable ECC error in case of NAND)
 * UBI_IO_BAD_HDR: the EC or VID header is corrupted (bad magic or CRC)
 * UBI_IO_BAD_HDR_EBADMSG: the same as %UBI_IO_BAD_HDR, but also there was a
 *                         data integrity error reported by the MTD driver
 *                         (uncorrectable ECC error in case of NAND)
 * UBI_IO_BITFLIPS: bit-flips were detected and corrected
 *
 * Note, it is probably better to have bit-flip and ebadmsg as flags which can
 * be or'ed with other error code. But this is a big change because there are
 * may callers, so it does not worth the risk of introducing a bug
 */
enum {
	UBI_IO_FF = 1,
	UBI_IO_FF_BITFLIPS,
	UBI_IO_BAD_HDR,
	UBI_IO_BAD_HDR_EBADMSG,
	UBI_IO_BITFLIPS,
};
```

**这套码的价值在于它编码的是"这块区域处于什么状态"，而不是"这次调用失败没有"**：

| 码 | 语义 |
|---|---|
| `UBI_IO_FF` | **这块是空的**（全 0xFF）——这不是错误，是一个状态 |
| `UBI_IO_FF_BITFLIPS` | 块是空的，但读的时候有 bitflip（NAND 上正常） |
| `UBI_IO_BAD_HDR` | 头部（magic 或 CRC）坏了 |
| `UBI_IO_BAD_HDR_EBADMSG` | 头部坏了 **且** 有不可纠正的 ECC 错误 |
| `UBI_IO_BITFLIPS` | 读成功，但纠正了 bitflip |

**这与 littlefs 的 `LFS_ERR_CORRUPT` 是同一思路的两个实例**：都用一个专用码来表达"这块区域处于什么状态"，而不是只报"这次操作失败没失败"。

**并且 UBI 作者自己反思过这个设计的形态**（上面注释最后一段）：他们认为 bit-flip 与 ebadmsg 更适合做成**可 OR 的标志位**，但因为调用方太多、改动风险大而作罢。**这是一处罕见的、由内核作者亲笔写下的关于"错误语义该如何编码"的设计权衡记录。**

**UBI 里还有第二套专用码**——`ubi_eba_copy_leb()`（LEB 搬运/scrub）的返回码，`ubi.h:113-136`：

```c
/*
 * Return codes of the 'ubi_eba_copy_leb()' function.
 *
 * MOVE_CANCEL_RACE: canceled because the volume is being deleted, the source
 *                   PEB was put meanwhile, or there is I/O on the source PEB
 * MOVE_SOURCE_RD_ERR: canceled because there was a read error from the source
 *                     PEB
 * MOVE_TARGET_RD_ERR: canceled because there was a read error from the target
 *                     PEB
 * MOVE_TARGET_WR_ERR: canceled because there was a write error to the target
 *                     PEB
 * MOVE_TARGET_BITFLIPS: canceled because a bit-flip was detected in the
 *                       target PEB
 * MOVE_RETRY: retry scrubbing the PEB
 */
enum {
	MOVE_CANCEL_RACE = 1,
	MOVE_SOURCE_RD_ERR,
	MOVE_TARGET_RD_ERR,
	MOVE_TARGET_WR_ERR,
	MOVE_TARGET_BITFLIPS,
	MOVE_RETRY,
};
```

这套码值得逐条看，因为它回答的正是"失败该怎么分类"：

| 码 | 它编码的维度 |
|---|---|
| `MOVE_CANCEL_RACE` | **这次失败不是存储的错**，是并发导致的（卷被删、PEB 被占用） |
| `MOVE_SOURCE_RD_ERR` | 失败在**源**侧（读） |
| `MOVE_TARGET_RD_ERR` | 失败在**目标**侧（读） |
| `MOVE_TARGET_WR_ERR` | 失败在**目标**侧（写） |
| `MOVE_TARGET_BITFLIPS` | 目标侧有 bitflip（可能是好块，但值得注意） |
| **`MOVE_RETRY`** | **明确的"重试"语义**——这是"暂时性失败"被单独编码的实例 |

**所以 UBI 的实际情况是：它为"读结果"（`UBI_IO_*`）和"搬运结果"（`MOVE_*`）都定义了专用的、带文档注释的小型枚举，唯独"擦除结果"没有——擦除路径复用了 errno 约定。**

这个对比本身就是本节最有价值的发现：**"给一个操作定义一套带注释的专用语义码"在 UBI 里是标准做法，不是异类；擦除是那个没被覆盖到的例外。**

另外注意 `UBI_IO_FF` 的判定点 `ubi_io_read_ec_hdr()`（`io.c:709`，判定逻辑 `:742-753`）：

```c
		if (ubi_check_pattern(ec_hdr, 0xFF, UBI_EC_HDR_SIZE)) {
			/* The physical eraseblock is supposedly empty */
			if (verbose)
				ubi_warn(ubi, "no EC header found at PEB %d, only 0xFF bytes", pnum);
			...
			return UBI_IO_FF;
		}
```

即 **UBI 在 attach（挂载）时会读每一个 PEB 的 EC 头，并把"全 0xFF"分类为"这个 PEB 是空的"**。这是"何时做擦除状态检查"这一问题上的第三个时点：**挂载时全盘扫一遍**。但要精确：这**不是**在确认"我的擦除成功了没有"，而是在**分类**——"这块是空的 / 有数据 / 坏了"。

#### 3.2.2 另一个限定：UBI 会用"错误发生在什么时候"来判定结构性损坏

`drivers/mtd/ubi/io.c:428-434`（`torture_peb()` 收尾）：

```c
	if (err == UBI_IO_BITFLIPS || mtd_is_eccerr(err)) {
		/*
		 * If a bit-flip or data integrity error was detected, the test
		 * has not passed because it happened on a freshly erased
		 * physical eraseblock which means something is wrong with it.
		 */
		ubi_err(ubi, "read problems on freshly erased PEB %d, must be bad",
			pnum);
```

**同一个 bitflip，发生在擦除刚完成之后 ⇒ 判定为块坏。** 这与前文引用的 UBI 官方文档一致（"a bit-flip during the torture test is a good reason to mark the eraseblock as bad"）。

**这是一个重要的语义提示：判定"结构性不可用"时可以借助"错误发生的时间点"**——同样的 IO 错误，刚擦完就出现，和写入了大量数据之后才出现，含义完全不同。这是本次调研中唯一一处明确利用该维度的实现。

**结构性佐证——预留块池。** UBI 把"块变坏"当作**预期内的正常事件**而非异常：`drivers/mtd/ubi/Kconfig:31-45` 定义 `MTD_UBI_BEB_LIMIT`（"Maximum expected bad eraseblock count per 1024 eraseblocks"，`default 20`），注释原文：

> To put it differently, if this value is 20, UBI will try to reserve about 1.9% of physical eraseblocks for bad blocks handling.

也就是说 UBI 在编译期/挂载期就为坏块预留了配额，坏块是被**容量规划**吸收的，不是致命错误。第 3 档（R/O）才是真正的事故。

### 3.3 CFI NOR（AMD cmdset）：用 errno 区分失败原因，只有 `-EIO` 意味着块坏

`drivers/mtd/chips/cfi_cmdset_0001.c:2000-2020`（Intel 命令集的状态寄存器判读）给出了同构的分类法：

| 状态位 | errno | 含义 | 是否重试 |
|---|---|---|---|
| `(chipstatus & 0x30) == 0x30` | `-EINVAL` | bad command sequence（命令序列错，软件 bug）| 否 |
| `chipstatus & 0x02` | `-EROFS` | Protection bit set（写保护）| 否 |
| `chipstatus & 0x8` | `-EIO` | bad VPP（电压异常，硬件）| 否 |
| `chipstatus & 0x20` 且 `retries--` | 保持 `ret` | 一般性擦除失败 | **是**，`goto retry`（最多 3 次）|
| 其余 | `-EIO` | 块擦除失败 | 否 |

代码原文（`:2009-2019`）：

```c
		} else if (chipstatus & 0x20 && retries--) {
			printk(KERN_DEBUG "block erase failed at 0x%08lx: status 0x%lx. Retrying...\n", adr, chipstatus);
			DISABLE_VPP(map);
			put_chip(map, chip, adr);
			mutex_unlock(&chip->mutex);
			goto retry;
		} else {
			printk(KERN_ERR "%s: block erase failed at 0x%08lx (status 0x%lx)\n", map->name, adr, chipstatus);
			ret = -EIO;
		}
```

同样地：`-EINVAL`（软件 bug）与 `-EROFS`（保护）**都不重试、也不认为块坏了**；只有 `-EIO` 被当作块级故障。

### 3.4 JFFS2：用"返回值 + bad_offset + 专门的 bad_list"区分

JFFS2 的做法比 UBI 更"物理"一层——它把"该区不可用"**落实为一个数据结构位置**：

- `c->bad_list`（`fs/jffs2/erase.c:197`）+ `c->bad_size`（`:196`）：结构性不可用的块进这里，永久不再参与分配。
- `c->erase_pending_list` 退回（`:183`、及 `:51`/`:80`）：`-EAGAIN`/`-ENOMEM` 这类"这次没成"的进这里，**下轮还会被擦**。
- `bad_offset` 出参（`uint32_t *bad_offset`）：校验失败时回填**具体是哪个字节坏的**，这个信息被直接用于写 NAND OOB 坏块标记（`jffs2_write_nand_badblock(c, jeb, bad_offset)`，`:179`）。

三种失败路径的对照：

| 触发 | 返回 | 落到哪 | 后续 |
|---|---|---|---|
| `kmalloc` 失败（校验缓冲）| `-EAGAIN` | `erase_complete_list`（refile）| 稍后重新校验 |
| 读回发现非 0xFF 字 / 读失败 / 短读 | `-EIO` | `jffs2_erase_failed()` → 尝试写 NAND 坏块标记 | 成功则 `bad_list` 永久退役；标记都写不进则退回 `erase_pending_list` 再试 |
| `mtd_erase()` 直接返回 `-ENOMEM`/`-EAGAIN` | 同码透传 | `erase_pending_list` | 重试 |
| `mtd_erase()` 直接返回 `-EROFS` | — | 打 warning `"Is the sector locked?"` 后走 `jffs2_erase_failed()` | — |

注意最后一行：**JFFS2 对 `-EROFS` 的处理其实相当"粗暴"**——它不认为写保护是暂时状态，仍然走坏块流程（只是多打一条日志）。这是一个可以对照的负面例子：errno 分类做得比 UBI 粗。

### 3.5 关于这一问的总体判断

归纳出四种形态，按"分离得有多干净"排序：

1. **专用错误码**（最干净）：三个实例——
   - **littlefs `LFS_ERR_CORRUPT = -84`**，文档明写 `"if the block should be considered bad"`，与泛用的 `LFS_ERR_IO = -5` 并存。消费端 `if (err == LFS_ERR_CORRUPT) goto relocate; return err;` 三分支。
   - **Zephyr NVS `-ENXIO`**：擦后校验不过时返回，与底层 IO 错误码可区分（`subsys/fs/nvs/nvs.c:355`）。
   - **ESP-IDF NVS `ESP_ERR_NOT_FINISHED`**：内部用于"校验未通过"——**但这个区分在 API 边界上丢失了**（见 2.6.2 末尾），因此是三者中最弱的一个。
2. **约定单一 errno 承载"块坏"语义**：UBI 与 CFI NOR 都用 `-EIO` 表示"这个 PEB/块坏了"（UBI 甚至在 `io.c:313-317` 的函数注释里把这条约定写成规格：`"If %-EIO is returned, the physical eraseblock most probably went bad."`）。"暂时性失败"由 `-EAGAIN`/`-ENOMEM`/`-EBUSY`/`-EINTR` 这组通用码承载。**第三种情况"其他一切 errno"被 UBI 当作"我不知道发生了什么"→ 整设备 R/O。**
3. **数据结构位置承载"结构性不可用"**（JFFS2：`bad_list` vs `erase_pending_list`）。错误码只负责"这一次失败了"，是否永久退役由列表迁移决定。
4. **失败位置作为独立信息通道**（MTD 的 `instr->fail_addr` + `MTD_FAIL_ADDR_UNKNOWN`；JFFS2 的 `bad_offset` 出参）。`mtd.h:21,27` 明确说明 `MTD_FAIL_ADDR_UNKNOWN` 意味着 "the failure was not at the device level"。**注意：这是 MTD 层唯一一处把"失败性质"单独编码的地方，但它区分的是"能否定位到具体偏移"，不是"IO 错 vs 块坏"**——不要误引。
5. **为特定操作定义带文档注释的小型专用枚举**——**UBI 是这方面的正面样板**：读结果用 `UBI_IO_*`（`ubi.h:89-111`），搬运结果用 `MOVE_*`（`ubi.h:113-136`），后者含明确的 `MOVE_RETRY`（"重试"语义）并按"哪一侧/哪种操作出错"分类。**也就是说"给操作定义语义码"在 UBI 里是标准做法——唯独擦除没有这么做，擦除复用 errno 约定。**

**横向对照表（按"这一问"的维度）：**

| 框架 | 「本次 IO 失败」怎么表达 | 「该区结构性不可用」怎么表达 | 二者如何区分 | 不可用后的动作 |
|---|---|---|---|---|
| **littlefs** | 任意其他负值；泛用码 `LFS_ERR_IO = -5` | **专用码 `LFS_ERR_CORRUPT = -84`** | `if (err == LFS_ERR_CORRUPT)` | `goto relocate` → 换下一块；超级块则 `LFS_ERR_NOSPC` |
| **UBI** | `-EINTR`/`-ENOMEM`/`-EAGAIN`/`-EBUSY` | **约定 `-EIO`** | `if (err == -EIO)` 走标坏块分支；重试类码走 `schedule_erase` 分支 | `ubi_io_mark_bad()` + 消耗预留 PEB |
| **UBI（第三种）** | — | — | `if (err != -EIO) goto out_ro;` —— **"其他一切码"被当作"未知故障"** | **整设备 R/O** |
| **CFI NOR（Intel）** | `chipstatus & 0x20` 且有重试次数 | `-EIO` | 按状态位分派 | 重试 3 次后返回 `-EIO` |
| **JFFS2** | `-ENOMEM`/`-EAGAIN` → 退回待擦队列 | `-EIO` + `bad_offset` 出参 | 返回值 + **列表归属**（`bad_list` vs `erase_pending_list`） | 写 NAND OOB 坏块标记 → `bad_list`；标记写不进则退回重试 |
| **mtd-utils `ubiformat`** | `errno != EIO` → 中止整个操作 | `errno == EIO` | `if (errno != EIO) goto out_free;` | 询问用户 → `mtd_mark_bad()` |
| **UBI**（搬运/scrub 路径，**另一套码**） | `MOVE_RETRY` = 明确的"重试"；`MOVE_CANCEL_RACE` = "不是存储的错，是并发" | `MOVE_SOURCE_RD_ERR` / `MOVE_TARGET_RD_ERR` / `MOVE_TARGET_WR_ERR` / `MOVE_TARGET_BITFLIPS`（**按"哪一侧、哪种操作"分类**） | 独立的 6 值枚举 `ubi.h:113-136` | 由调用方按码分派 |
| **UBI**（读路径，**第三套码**） | 负 errno | `UBI_IO_BAD_HDR` / `UBI_IO_BAD_HDR_EBADMSG` | 独立的 5 值枚举 `ubi.h:89-111`；`UBI_IO_FF` 编码"这块是空的"（状态而非错误） | 由调用方按码分派 |
| **ESP-IDF NVS** | 由底层 `esp_partition` 传播的 errno | 内部用专用码 `ESP_ERR_NOT_FINISHED`（**校验未通过**）；重试耗尽后把页状态置 `INVALID` | 靠**重试次数**天然分离：重试是"暂时"，置 `INVALID` 才是"结构性" | 页标记 `INVALID`，由 PageManager 向上传播错误，不再重试。**但 API 边界上返回的是泛化的 `ESP_FAIL`，这个区分没有暴露给调用方**（见 2.6.2 末尾） |
| **Zephyr NVS** | 底层 errno / `-EIO` | 专用码 **`-ENXIO`**（**校验未通过**） | `-ENXIO` 与底层 IO 错误码可区分 | **不重试、不标坏块、不换扇区**，直接向上传播 |
| **NuttX NXFFS** | MTD 层 errno | **块头状态 `BLOCK_STATE_BAD`**（写在块头里，持久化） | 失败块被**改写块头**为 BAD——是持久化的结构性判定，不是返回码 | 块头标记 `BLOCK_STATE_BAD`，该块永久退出使用 |
| **nrfx / nRF5 SDK** | **无**（`nrfx_nvmc_page_erase` 无条件返回 0；SDK 恒返回 `NRF_SUCCESS`） | **无** | **无区分**——接口签名不携带失败信息 | 无 |
| **flashrom** | 擦除/校验任一失败 → 返回 -1 中止 | **不区分**（没有坏块概念，NOR 通常无坏块） | 不区分 | 中止整个操作 |
| **Linux MTD 层** | 任意 errno | **无专门表达**（只有 `fail_addr != MTD_FAIL_ADDR_UNKNOWN` 表示"能定位到具体偏移"） | 不区分 | 由上层决定 |
| **Linux SPI-NOR / NAND** | `-ETIMEDOUT` / 状态寄存器 | 无区分 | 不区分 | 上层（文件系统）决定 |

**两条跨框架的共性规律：**

- **所有框架都拒绝在"擦除返回失败"当口直接下结论说块坏了。** 共同模式是"**先重试，重试耗尽/求证之后才升级为结构性判定**"：CFI 用 `MAX_RETRIES 3`（`cfi_cmdset_0002.c:43`，`cfi_cmdset_0001.c:1953` 的 `retries = 3`）；UBI 用 `UBI_IO_RETRIES 3`（`ubi.h:66`）+ torture test；JFFS2 用 refile 回路。UBI 的 torture test 是极致形态——"擦→读回验 0xFF→写花样→读回验花样→换花样再来"，**主动求证**这个块是不是真不行了，只有 torture 也过不去才落 `bad_list`。littlefs 是例外：它对 `LFS_ERR_CORRUPT` **不做次数限制**，靠块池耗尽兜底。
- **"块坏"被当作容量规划内的正常事件，而不是异常。** UBI 为此设了编译期配额 `MTD_UBI_BEB_LIMIT`（默认 20/1024，即预留约 1.9% 的 PEB）。真正的事故是"没块可换"（UBI 的 `out_ro`、littlefs 的 superblock unwritable）。

**明确未找到的：**

- **没有**任何一个框架定义 `ERASE_IO_FAILED` / `REGION_UNUSABLE` 这样**一对平级**的专属枚举。littlefs 的 `LFS_ERR_CORRUPT` 与 Zephyr NVS 的 `-ENXIO` 都是**单个**专用码，其"对立面"仍是泛用的通用错误码，不是一个专属的"IO 失败"码。
- **没有**任何一个框架把"擦后校验失败"与"擦除命令本身失败"编成两个不同的、**专用于擦除**的公开错误码对。
- **没有**框架在**擦除操作内部**同时完成"校验"与"把该区判定为结构性不可用"两件事：
  - **做了校验但不判定结构性**：JFFS2（**判定在外层**，靠 `bad_list`）、UBI debug（校验失败返回 `-EINVAL` → 反而整设备 R/O）、OneNAND（只回填 `fail_addr`）、**Zephyr NVS（只返回 `-ENXIO`，不对该扇区做任何结论）**。
  - **做了结构性判定但不校验**：littlefs（`LFS_ERR_CORRUPT` 由驱动给，littlefs 自己不校验）、UBI（`-EIO` 来自 `mtd_erase`，不是来自读回）。
  - **两者都做的只有两个"外挂"路径**：UBI 的 `torture_peb()` 与 ESP-IDF NVS 的 `erase_range()`——但它们都不在常规擦除路径上（前者只在坏块求证时触发，后者默认关闭）。

**一个值得注意的倾向**：Zephyr NVS 是唯一一个"**无条件校验 + 专用错误码对外**"的组合，但它**刻意不把该扇区判定为坏**（不重试、不标坏块、不换扇区，直接向上传播）。也就是说：**"我这次没擦干净"与"这块不可用"之间，它选择只报告前者、把判定权留给上层。**

---

## 4. 反面证据：明确不做 verify 的框架及官方理由

### 4.1 Linux MTD 核心：把契约写成"驱动返回错误码"，校验责任外推

`drivers/mtd/mtdcore.c:1459-1462` 的函数头注释是整条链路的规格说明，它**只**规定了失败如何表达，对"擦后要不要读回"完全沉默：

```
/*
 * Erase is an synchronous operation. Device drivers are epected to return a
 * negative error code if the operation failed and update instr->fail_addr
 * to point the portion that was not properly erased.
 */
```

（原文保留了 `epected` 这个拼写错误。）可以推断的设计意图：MTD 核心是**多驱动共用的一道薄适配层**，它无法知道底层是 NOR/NAND/OneNAND/CFI 中的哪一种，也就无法假设"读回比对 0xFF"在任何介质上都成立。所以校验被留给上层（JFFS2 自己做）或 debug 钩子（UBI 的 `chk_io`）。

### 4.2 SPI-NOR：注释只承诺"有问题就返回错误"

`drivers/mtd/spi-nor/core.c:1818-1820`：

```
/*
 * Erase an address range on the nor chip.  The address range may extend
 * one or more erase sectors. Return an error if there is a problem erasing.
 */
```

实现里"problem"的唯一检测手段是状态寄存器 WIP 位 + 超时（见 2.1.4）。**没有任何读回比对。**

### 4.3 NAND：`nand_erase_nand()` 的收尾注释是"Return more or less happy"

`drivers/mtd/nand/raw/nand_base.c:4613`，函数退出前：

```c
	/* Return more or less happy */
	return ret;
```

这是内核里少见的坦诚注释，直白承认 NAND 擦除的成功判定是**启发式**的：只读状态寄存器的 FAIL 位，不读回阵列。

**为什么 NAND 不做读回校验——有代码层面的硬理由。** NAND 的"已擦除"状态在物理上就不可靠：擦除后立即读回可能读到 bitflip，而这**不代表擦除失败**。内核对此的应对不是"擦后校验"，而是**在读取侧兜底**：`nand_check_erased_ecc_chunk()`（声明于 `include/linux/mtd/nand.h:1139`）的作用是"这一页 ECC 报错了，但它可能是被擦除的空页只是有 bitflip，帮我确认一下"。它的**全部四个调用点都在读路径上**：

| 调用点行号 | 所在函数 | 函数起始行 |
|---|---|---|
| `:3077` | `nand_read_subpage()` | `:2991` |
| `:3149` | `nand_read_page_hwecc()` | `:3103` |
| `:3215` | `nand_read_page_hwecc_oob_first()` | `:3176` |
| `:3298` | `nand_read_page_syndrome()` | `:3242` |

**没有任何一处在 `nand_erase_nand()` 里。** 这是一个重要的反例：在 NAND 上，"读回全 0xFF"这件事被内核判定为**读语义**（纠正 bitflip 的容错过程），而不是**擦除语义**（通过/不通过的判据）。

> 对我们的直接含义：**如果底层的"擦后值"本身可能带 bitflip，那么"读回比对"就必须带容错阈值，否则会产生假阳性。** 内核的处理方式是把阈值放在 `nand_check_erased_ecc_chunk(..., int threshold)` 的参数里（`include/linux/mtd/nand.h:1139-1142`）。

### 4.4 MTD block（`mtdblock.c`）：经典的"擦完直接写"，失败只打印不校验

`drivers/mtd/mtdblock.c:44` `erase_write()`：

```c
	/*
	 * First, let's erase the flash block.
	 */
	erase.addr = pos;
	erase.len = len;

	ret = mtd_erase(mtd, &erase);
	if (ret) {
		printk (KERN_WARNING "mtdblock: erase of region [0x%lx, 0x%x] "
				     "on \"%s\" failed\n",
			pos, len, mtd->name);
		return ret;
	}

	/*
	 * Next, write the data to flash.
	 */
```

擦除成功就直接进入写流程，**中间没有读回**。这里也顺带说明了 `mtdblock` 模块的定位（`mtdblock.c:361`：`"Caching read/erase/writeback block device emulation access to MTD devices"`）。

### 4.5 littlefs：**给出了明文理由**的"不做"

见 2.2.1。`lfs.h:174-178` 对 `erase` 回调的契约：

```c
    // Erase a block. A block must be erased before being programmed.
    // The state of an erased block is undefined. Negative error codes
    // are propagated to the user.
```

> **"The state of an erased block is undefined."**

**这是整份调研里唯一一处成文的"为什么框架自己不做擦后校验"**：既然擦后状态由底层定义、框架不做假设，框架就不可能拿某个固定值去比对。

### 4.6 U-Boot：`sf update` 的 `memcmp` 是"省擦除"，不是"验擦除"

见 2.4.2。`cmd/sf.c:192-198` 在**擦除之前**读回并 `memcmp`，一致就整块跳过擦除（省擦写寿命），不一致才擦。**擦完不读回。** 这是最容易被误读成"U-Boot 做了擦后校验"的一段代码。

### 4.7 其余"明确不做"的清单

| 对象 | 锚点 | 失败处置 |
|---|---|---|
| Linux MTD 核心 | `mtdcore.c:1459-1509` | 透传 errno + `fail_addr` |
| Linux mtdchar | `mtdchar.c:921-955` | 透传 |
| Linux SPI-NOR | `spi-nor/core.c:1821` | WIP 轮询 + 超时 |
| Linux NAND | `raw/nand_base.c:4524` | 状态寄存器 |
| Linux CFI Intel | `cfi_cmdset_0001.c:1948` | 状态寄存器分派 |
| Linux LPDDR | `lpddr/lpddr_cmds.c`（全文 grep `verify`/`0xff` **零命中**） | — |
| Linux mtdblock | `mtdblock.c:44-64` | `KERN_WARNING` |
| Linux UBIFS | `ubi/kapi.c:620` → UBI 路径 | 同 UBI |
| littlefs | `lfs.c:276` | 交给驱动返回 `LFS_ERR_CORRUPT` |
| U-Boot `sf erase` | `cmd/sf.c:346-388` | 打印 `ERROR %d` / `OK` |
| U-Boot `sf update` | `cmd/sf.c:180-211` | 返回 `"erase"`/`"write"` |
| mtd-utils `ubiformat` | `ubi-utils/ubiformat.c:458-470` | errno 分类 + 询问用户 |
| mtd-utils `flash_erase` | `misc-utils/flash_erase.c:310-322` | 打印后忽略，**退出码始终 0** |
| ESP-IDF 主擦除 API | `spi_flash/esp_flash_api.c:645-754` | 返回驱动错误；**`ESP_ERR_FLASH_ERASE_FAIL` 不存在** |
| MCUboot 核心 | `boot/bootutil/src/bootutil_area.c:276` | 返回 rc，调用点 `assert(rc == 0)` |
| OpenOCD 擦除 | `src/flash/nor/core.c:29-39` | `LOG_ERROR("failed erasing sectors...")` |
| **Zephyr** `flash_area_erase` | `subsys/storage/flash_map/flash_map.c:71-78` | 无 |
| **NuttX** MTD 核心 | `drivers/mtd/mtd_partition.c:219-244` | 只传驱动 rc |
| **NuttX SMART** | `drivers/mtd/smart.c:2703-2729` | **返回值被丢弃**（函数签名是 `static void`） |
| **RT-Thread** FAL | `components/fal/src/fal_partition.c:510` | `LOG_E` 后返回 rc |
| **RT-Thread** SFUD | `components/sfud/src/sfud.c:453-497` | `wait_busy()` WIP 轮询 |
| **STM32 HAL** | `stm32f4xx_hal_flash_ex.c:160` | `HAL_ERROR` + `*SectorError` 回填扇区号 |
| **nrfx** | `drivers/src/nrfx_nvmc.c:208-223` | **无条件 `return 0`** |
| **nRF5 SDK** | `nrf_fstorage_nvmc.c:150-174` | **恒返回 `NRF_SUCCESS`** |
| **OpenBLT** STM32F4 擦除 | `Target/Source/ARMCM4_STM32F4/flash.c:778` | `BLT_FALSE`，循环中止 |

### 4.8 一处值得注意的"官方自我批评"

`ubi-utils/ubiformat.c:380`：

```c
/* TODO: we should actually torture the PEB before marking it as bad */
static int mark_bad(const struct mtd_dev_info *mtd, struct ubi_scan_info *si, int eb)
```

即：mtd-utils 承认自己在"直接标记坏块"这条路径上**跳过了求证步骤**。这是"先重试/求证再判死"这条规律的已知破例，且由官方注释记录在案。

### 4.9 OpenOCD：**明令禁止**在擦除路径上记录"已擦除"状态

`src/flash/nor/core.h:33-42` 的注释原文：

```c
	 * Indication of erasure status: 0 = not erased, 1 = erased,
	 * other = unknown.  Set by @c flash_driver::erase_check only.
	 *
	 * This information must be considered stale immediately.
	 * Don't set it in flash_driver::erase or a device mass_erase
```

**"Don't set it in `flash_driver::erase`"** —— OpenOCD 明确禁止驱动在擦除路径上顺手把"该区域已擦除"这个状态位标记为真，理由是：

> **"This information must be considered stale immediately."**（这个信息必须被视为立刻就会过期。）

**这是本次调研中唯一一处直接针对"擦除后宣称空白"这一行为的官方反对意见**，而且它反对的不是"校验"本身，而是**"把校验结果当成可长期信赖的状态存下来"**。它从侧面说明了为什么很多栈选择不在擦除路径上做校验：**即时的校验结果只在那一个瞬间有意义。**

（对比：OpenOCD 提供了 `flash erase_check` 这个**手工**空白检查命令，`src/flash/nor/tcl.c:179-226`。）

### 4.10 关于"官方理由"的总体说明

调研中收集到的**成文理由**一共四处，方向各不相同，值得并列：

| 来源 | 原文 | 立场 |
|---|---|---|
| **littlefs** `lfs.h:174-178` | "The state of an erased block is undefined." | **不做**校验的理由 |
| **OneNAND** `drivers/mtd/nand/onenand/Kconfig:14-18` | "The OneNAND flash device internally checks only bits transitioning from 1 to 0." | **要做**校验的理由（器件自身检查不完备） |
| **OpenOCD** `src/flash/nor/core.h:38` | "This information must be considered stale immediately." | **不去记录**擦除状态的理由 |
| **flashrom** `doc/classic_cli_manpage.rst:74-76` | "Using this option is not recommended" | **反对关闭**校验的理由 |
| **SPI-NOR** `Documentation/driver-api/mtd/spi-nor.rst:183-184` | "If the flash comes erased by default and the previous erase was ignored, we won't catch it, thus test the erase again" | **承认风险**、但把校验定位为人工测试步骤 |

**除上述五处外，Linux 内核 MTD 树里没有任何一处注释明确写出"我们刻意不做擦后校验，因为……"。** 4.1–4.4 中给出的理由均为**从代码契约与实现反推**，已在相应位置标注为推断，请勿当作一手陈述引用。

---

## 5. ⚠️ 未证实项

本节列出**明确未能取得一手来源**的条目。凡是本报告正文中出现的结论，都有文件+行号或官方原文支撑；下面这些是**没有**支撑、因此**不作任何结论**的问题。

所有子调研均已归队并整合完毕。

### 5.1 Linux 内核部分（本次已充分取证，以下为残留缺口）

| # | 未证实项 | 原因 |
|---|---|---|
| 1 | `ubi_dbg_check_all_ff` → `ubi_self_check_all_ff` 改名的**具体 commit 与版本** | 只逐 tag 实测确认了 v3.0 用旧名、v4.19 用新名，**未定位到确切的那一次提交**。区间：v3.0 之后、v4.19 之前。 |
| 2 | 内核 MTD 为何**不**在通用擦除路径做校验的**官方成文理由** | 全树未找到任何注释或文档明文写出该理由。4.1–4.4 中给出的理由均为**从代码契约与实现反推**，非一手陈述。 |
| 3 | OneNAND 为何**只**在批量擦除路径校验、单块路径不校验的**官方理由** | 代码中无注释说明。2.5.2 中的推断（"批量擦除摊薄成本"）**属于推测**。 |
| 4 | `CONFIG_MTD_ONENAND_VERIFY_WRITE` 是否会间接影响 `onenand_multiblock_erase_verify()` | 已确认该函数在该 ifdef 作用域（`onenand.c:1606-1680`）之外，故**推断**其无条件生效；未找到文档确认。 |
| 5 | SPI-NOR 是否在**某些特定控制器驱动**（`drivers/mtd/spi-nor/controllers/`）中做了擦后校验 | **未逐个检查**该子目录。仅检查了 `spi-nor/core.c` 与 Kconfig。 |
| 6 | `libmtd.c` 的 `mtd_torture()` 是何时引入的 | 未查 git 历史。 |
| 7 | ~~mtd-utils `flash_erase` 是否做校验~~ | **已解决**（见 2.3.6）：首次尝试时 `git.infradead.org` 连接超时，重试后成功取得 `misc-utils/flash_erase.c`，结论为**不校验且不上报失败退出码**。 |
| 8 | `drivers/mtd/tests/` 下除 `pagetest.c` 外，`torturetest.c`/`stresstest.c` 等是否也含擦后校验 | **未逐个检查**，只读了 `pagetest.c`。 |
| 9 | `UBI_IO_*` 这套码的**完整调用方清单** | 只读了 `io.c` 中的主要使用点，未穷举所有消费方。 |

### 5.2 ESP-IDF / MCUboot / flashrom / OpenOCD

| # | 未证实项 | 原因 |
|---|---|---|
| 1 | ESP-IDF 是否存在"`esp_flash_erase_region` 读回已擦区域并报错"这一模式 | **反向确认不存在**：全仓无此读回；`ESP_ERR_FLASH_ERASE_FAIL` 符号全仓 0 命中。但**仅针对当前 master**；更早的 release 分支未逐一检查。 |
| 2 | ESP-IDF ROM 内的擦除实现是否做读回 | **无法证实**：`components/esp_rom/**` 只有链接桩，ROM 源码不可得（如 ESP32-S3/C3 的 `rom_esp_flash_erase_region`）。 |
| 3 | ESP-IDF `esp_flash_erase_chip` 的"chip erase check" | 已定位为 `wait_idle` 状态轮询 + `CONFIG_SPI_FLASH_CHECK_ERASE_TIMEOUT_DISABLED`；**未找到任何读回芯片擦除区域的代码**。 |
| 4 | MCUboot 的 `VALIDATE_PROGRAM_OP` 应由谁定义 | 全 mcuboot 树只有 1 个使用点、**无定义处**，esp-idf 中也搜不到；**未找到声明其为"用户自定义宏"的文档**。 |
| 5 | MCUboot `loader.c`（2501 行）/ `swap_scratch.c`（1149 行）是否存在罕见的擦后读回 | 基于 grep（`0xff`/`erased_val`/`flash_area_erase`）判断为无，**未逐行通读**。 |
| 6 | flashrom `-E` 与显式 `-v <file>` 同时使用时是否被拒绝 | **未证实**；只确认了 `--verify`/`--noverify` 互斥（`cli_classic.c:842,849`）与调度顺序 `erase_it` 先于 `verify_it`（`:1626-1632`）。 |
| 7 | OpenOCD 各驱动的 `erase_check` 实现，是真实读数据还是依赖控制器硬件空白检查 | **未逐一检查**各 nor 驱动；核心 `default_flash_*` 实现确为真实内存读取。 |

### 5.3 STM32 / nrfx / OpenBLT / 应用笔记 / 功能安全

| # | 未证实项 | 原因 |
|---|---|---|
| 1 | **AN3969**（STM32F40x/41x EEPROM 模拟）是否有擦后校验要求 | **未取得**。`st.com` 对该 PDF 返回 HTTP 567 + HTML 中间页（7,351 字节，非 PDF）。未找到 GitHub 镜像（搜到的 `paradajz/libemueeprom` 无 `.c` 实现，`Tazmania0/EEPROM` 404）。 |
| 2 | **AN4894 / X-CUBE-EEPROM 文档本身** | **未取得**。代码侧已确认（官方 `STMicroelectronics/stm32-util-eeprom-emulation`，README tag 2.1.0），但**该仓库中不含 "AN4894" / "X-CUBE-EEPROM" 字样**，故"这份代码对应 AN4894"这一映射**未证实**。 |
| 3 | **ISO 26262** 是否强制要求擦后校验 | **未证实**。`iso.org` 返回 HTTP 403，未取得任何权威一手或二手来源。**本报告没有任何证据支持"ISO 26262 强制要求擦后校验"这一说法。** |
| 4 | **JEDEC**（JESD22 / JESD47 耐久性测试流程）是否定义擦后校验步骤 | **未证实**。`jedec.org` 返回 HTTP 403。 |
| 5 | **Micron / Infineon(Spansion) / Macronix 应用笔记**中"擦除失败 ⇒ 标记坏块"的原文 | **未证实**。`micron.com` 根域可达（200）但猜测的技术文档 URL 返回站点 HTML 404 页（273,595 字节 HTML，非 PDF），文档被 JS/注册墙挡住。**"擦除失败 ⇒ 标坏块"这条规则在本报告中的一手依据只有 Linux/UBI 源码，没有厂商文档。** |
| 6 | `VerifyPageFullyErased()` 被调用的**完整时机清单** | 已确认 `PAGE_STATE_ERASED` 分支（`:918-925`）、`EE_FLITF_Init()` 第 9 步（`:852`）、`EE_FLITF_Format()`（`:920`）；**未穷举所有调用路径**。 |
| 7 | Nordic NVMC 是否可能擦除失败、以及"不会失败"是否为设计前提 | **未找到任何明文的官方说明**。2.11 节中的推断已明确标注为推断。 |

### 5.4 Zephyr / NuttX / RT-Thread

| # | 未证实项 | 原因 |
|---|---|---|
| 1 | **Zephyr NVS 在当前 `main` 分支的行为** | `subsys/fs/nvs/nvs.c` 在 `ref=main` 下返回 **HTTP 404**（raw 路径与 API contents 两种取法各试两次），而在 `ref=v3.7.0` 下返回 200。NVS 现在位于何处（是否已移到独立模块仓库或改名）**未证实**——`zephyrproject-rtos/nvs` 的 `src/nvs.c` 同样 404。**2.13.2 节的全部 NVS 结论只锚定 tag `v3.7.0`，不能直接当作当前 main 的行为。** |
| 2 | **NuttX `mtd_erase_state` 函数** | **未找到该符号**。已核查 `include/nuttx/mtd/mtd.h`、`drivers/mtd/mtd_partition.c`、`mtd_config.c`、`smart.c`、`ramtron.c`、`drivers/mtd/Kconfig` 及 NXFFS 源码，均无。唯一相关的是 `MTDIOC_ERASESTATE` **ioctl**（`mtd.h:79`）。**无法运行全仓代码搜索**（GitHub code-search API 对本 IP 限流/需鉴权），因此**不能排除**它存在于未抓取的 `arch/` 或 `drivers/mtd/*` 其他文件中。 |
| 3 | **`CONFIG_NXFFS_VERIFY`** | **确认不存在**——在 `fs/nxffs/*.c`、`fs/nxffs/nxffs.h`、`fs/nxffs/Kconfig` 中零命中。`fs/nxffs/Kconfig` 的完整符号表已在 2.14.2 列出。**是否在树内别处（如 `fs/Kconfig`）另有一个同名符号，未证实。** |
| 4 | NuttX `mtdconfig` 的 `erasestate` 字段由谁初始化 | **未证实**——只在 `mtd_config.c` 中见到字段与宏，未找到赋值处。 |
| 5 | RT-Thread 的 littlefs 移植路径 | **未取得一手源码**——路径猜测均 404。 |
| 6 | Zephyr / NuttX / RT-Thread 各驱动"未找到读回"这一结论的完备性 | 受网络限制**未能拉取整仓 tarball**（codeload 速率约 28 KB/s），所有"未发现读回"的驱动结论**基于已抓取的具体文件，而非穷举全树搜索**。基准为 2026-09-19 的 `main`/`master`。 |
