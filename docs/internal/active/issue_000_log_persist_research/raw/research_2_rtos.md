# Zephyr 以外 RTOS / 嵌入式生态的持久化日志与 Flash 记录存储调研

调研范围：RT-Thread（ulog / EasyFlash / FlashDB）、ESP-IDF（esp_log / esp_coredump / NVS）、NuttX（syslog / ramlog / MTD / CRASHDUMP）、其他（MCUboot、SEGGER、通用 MCU 黑匣子库）。

来源约定：优先一手来源（官方文档、GitHub 源码、设计文档、Issue/PR 讨论）。所有网页抓取日期为 **2026-09-15**。每条事实后附出处 URL。无法证实的内容统一收敛到文末「存疑与未证实」。

## 事实

### 1. RT-Thread EasyFlash — LOG 模块

一手来源：`easyflash/src/ef_log.c`（master 分支）
https://raw.githubusercontent.com/armink/EasyFlash/master/easyflash/src/ef_log.c

- 定位：**无需文件系统**，日志直接顺序写 Flash。README 原文（中文）「Log 无需文件系统，日志可直接存储在 Flash 上」，适用于「小型的不带文件系统的产品」。资源占用「最低要求：ROM: 6K bytes RAM: 0.1K bytes」（英文段落写 RAM 0.2K，两处不一致）。
  出处：https://raw.githubusercontent.com/armink/EasyFlash/master/README.md
- **扇区头结构**：`LOG_SECTOR_HEADER_SIZE = 12` 字节，`LOG_SECTOR_HEADER_WORD_SIZE = 3` 个字。三个字的含义由枚举 `SectorHeaderIndex` 给出：
  `SECTOR_HEADER_MAGIC_INDEX` / `SECTOR_HEADER_USING_INDEX` / `SECTOR_HEADER_FULL_INDEX`。
- **魔术字**：`LOG_SECTOR_MAGIC = "0xEF30EF30"`，源码注释「magic code on every sector header」。
- **扇区状态值（关键手法）**：状态不是存在一个"状态字段"里，而是**按三个字中的哪一个被写成非 0xFF 来编码**（单向 1→0 写入，写后无需擦除即可区分）：

  | 状态 | magic 字 | using 字 | full 字 |
  |---|---|---|---|
  | EMPTY (`SECTOR_STATUS_MAGIC_EMPUT` = `0xFFFFFFFF`) | magic | 0xFFFFFFFF | 0xFFFFFFFF |
  | USING (`SECTOR_STATUS_MAGIC_USING` = `0xFEFEFEFE`) | magic | 0xFEFEFEFE | 0xFFFFFFFF |
  | FULL (`SECTOR_STATUS_MAGIC_FULL` = `0xFCFCFCFC`) | magic | 0xFEFEFEFE | 0xFCFCFCFC |

  源码头注释给出状态迁移关系：「State transition relationship: empty->using->full」以及「The FULL status will change to EMPTY after sector clean.」
- `write_sector_status()` 把 EMPTY 写在 header 偏移 0、USING 写在偏移 `sizeof(header)`、FULL 写在偏移 `sizeof(header) * 2` —— 即**状态推进 = 往下一个字写一个新值，永不回头**。
- `get_sector_status()` 用 `addr & (~(EF_ERASE_MIN_SIZE - 1))` 向下取整定位扇区头，读 3 个字；读失败或组合无法识别 → `SECTOR_STATUS_HEADER_ERROR`。
- **初始化 / 恢复流程**：`ef_log_init()` 断言 `LOG_AREA_SIZE % EF_ERASE_MIN_SIZE == 0` 且至少两个擦除单位，然后调用 `find_start_and_end_addr()` **扫描全部扇区头**，统计 `empty_sec_counts` / `using_sec_counts` / `full_sector_counts`，并用一个两状态环形模型 `cur_log_sec_state` 判断扇区序列是否合法。以下情况一律触发 `ef_log_clean()`（**擦掉整个日志区**）：
  - EMPTY 后面跟 USING 或 FULL，或 USING 后面跟 USING → 打印 `"Error: Log area error! Now will clean all log area.\n"`
  - 状态 2 下观察到 FULL→EMPTY → 同一句错误
  - 任何 `SECTOR_STATUS_HEADER_ERROR` → `"Error: Log sector header error! Now will clean all log area.\n"`
  - 扫描完 USING 扇区数不为 1 → `"Error: There must be only one sector status is USING! Now will clean all log area.\n"`
- **写指针定位（掉电后靠扫描，不靠元数据）**：`find_sec_using_end_addr()` 在扇区数据区**按 32 字节块扫描**并统计连续 `0xFF`：整块全 0xFF 则该扇区为空；尾部至少 4 字节 0xFF 则写结束位置在扇区内是字对齐的；否则判定该扇区已满。**没有任何记录级 CRC 或长度字段**——记录是裸的字对齐数据，边界完全靠"扫描 0xFF"推断。
- **追加写入 `ef_log_write(const uint32_t *log, size_t size)`**：要求字对齐，从 `log_end_addr` 开始写当前 USING/EMPTY 扇区的未擦除尾部；刚好写满则把该扇区标记 FULL 并继续。
- **环形覆盖**：换扇区时用 `get_next_flash_sec_addr()`；当"下一个扇区地址 == `log_start_addr`"时，**`log_start_addr` 一起前移**，即最旧扇区被回收重用。擦除该扇区后依次打 EMPTY、USING 标记，再写数据。
- **读回**：`log_index2addr()` 把逻辑字节索引换算成物理地址时，要按 `sector_num * LOG_SECTOR_HEADER_SIZE` 跳过每个扇区的 12 字节头，越过区域末尾时回绕到 `log_area_start_addr`。`ef_log_read()` 要求 `size % 4 == 0`，越界报 `"Error: index out of ranges, current using size is %d"`，超出部分截断并告警 `"Warning: Log read size out of bound. Cut read size.\n"`。
- `ef_log_clean()`：擦掉整个日志区，然后把首扇区标记 EMPTY→USING，其余扇区标记 EMPTY。

### 2. RT-Thread EasyFlash — ENV 模块（掉电保护的状态表范式）

一手来源：`easyflash/src/ef_env.c`（master 分支）
https://raw.githubusercontent.com/armink/EasyFlash/master/easyflash/src/ef_env.c

- **魔术字**：`ENV_MAGIC_WORD = 0x3034564B`（注释 `magic word(K, V, 4, 0)`，即 "KV40"）、`SECTOR_MAGIC_WORD = 0x30344645`（注释 `magic word(E, F, 4, 0)`，即 "EF40"）。偏移用 `ENV_MAGIC_OFFSET` / `ENV_LEN_OFFSET` / `ENV_NAME_LEN_OFFSET`。
- **ENV 节点头 `struct env_hdr_data`** 字段：`status_table[ENV_STATUS_TABLE_SIZE]`（注释「ENV node status」）、`magic`、`len`（注释「total length (header + name + value), must align by EF_WRITE_GRAN」）、`crc32`、`name_len`、`value_len`。
- **ENV 节点状态**（`env_status_t`）：`ENV_PRE_WRITE`、`ENV_WRITE`、`ENV_PRE_DELETE`、`ENV_DELETED`、`ENV_ERR_HDR`、终结符 `ENV_STATUS_NUM`。
- **扇区状态**分两组：
  - `enum sector_store_status`：`SECTOR_STORE_UNUSED` / `SECTOR_STORE_EMPTY` / `SECTOR_STORE_USING` / `SECTOR_STORE_FULL`
  - `enum sector_dirty_status`：`SECTOR_DIRTY_UNUSED` / `SECTOR_DIRTY_FALSE` / `SECTOR_DIRTY_TRUE` / `SECTOR_DIRTY_GC`
  另：`SECTOR_NOT_COMBINED 0xFFFFFFFF`、`FAILED_ADDR 0xFFFFFFFF`。
- **状态表的单向位编码**：`get_status` 的注释是「get the first 0 position from end address to start address」——即状态值靠**从表尾向表头找第一个 0 位**来解码，写状态只做 1→0 的位清除，因此不需要擦除。
- **必须避免重复写同一单元**：`write_status` 注释「write the status by write granularity」，理由是「some flash (like stm32 onchip) NOT supported repeated write before erase」。`set_status` 注释「the first status table value is all 1, so no need to write flash」——状态 0 完全不动 Flash。
- **写 ENV 的时序（掉电保护的骨架）**：`create_env_blob` 依次为「start calculate CRC32」→「write ENV header data」→「write key name」→「write value」→「change the ENV status to ENV_WRITE」，最后「trigger GC collect when current sector is full」。即**先置 PRE_WRITE，数据齐了再置 WRITE**。
- **上电恢复**：`check_and_recovery_env_cb` 给出语义：
  - 遇到 `ENV_PRE_WRITE` → 注释「the ENV has not write finish, change the status to error」，置为 `ENV_ERR_HDR`（放弃这次未完成的写）
  - 遇到 `ENV_PRE_DELETE` → 注释「recovery the prepare deleted ENV」以及 `"Found an ENV (%.*s) which has changed value failed. Now will recovery it."`（把旧值恢复回来）
  - `move_env` 附近注释「recovery check status when first reboot」；搬移失败会触发 GC 并重试。
- **磨损均衡 / GC**：V4.0 起 NG 模式原生支持磨损平衡与掉电保护（此前版本需要额外占用一个 Flash 扇区）。需求来自「空闲扇区数低于阈值（`EF_GC_EMPTY_SEC_THRESHOLD`）」；GC 把 FULL + DIRTY 扇区里的有效 ENV 搬走，再擦除该扇区并格式化为 EMPTY。
  出处（设计文档）：https://raw.githubusercontent.com/armink/EasyFlash/master/docs/zh/design.md
- 设计文档明确点出中间状态的存在理由：「准备写入、准备删除」这类中间状态正是为掉电保护设计的，状态存放在扇区与 ENV 头部，**在不擦除扇区数据的前提下单向修改**。文档同时说明：修改一个 ENV = 先把旧值标记删除并把扇区置 DIRTY，再写新值；写满的扇区转 FULL。
  注：该文档只写到 §3.2，**LOG 模块在文档中完全没有覆盖**（无日志区布局、无扇区状态机、无掉电处理描述），且 ENV 节点结构只以一张图 `ng_mode_data_structure` 引用，正文无字段级结构体。

### 3. RT-Thread FlashDB — KVDB / TSDB

版本标识：`fdb_def.h` 中 `FDB_SW_VERSION "2.2.99"`、`FDB_SW_VERSION_NUM 0x20299`，Apache-2.0。

一手来源：
- https://raw.githubusercontent.com/armink/FlashDB/master/inc/fdb_def.h
- https://raw.githubusercontent.com/armink/FlashDB/master/src/fdb_kvdb.c
- https://raw.githubusercontent.com/armink/FlashDB/master/src/fdb_tsdb.c
- https://raw.githubusercontent.com/armink/FlashDB/master/src/fdb_utils.c
- README 特性表（「支持**掉电保护**功能，可靠性高」「支持**磨损平衡**，延长 Flash 寿命」）：https://github.com/armink/FlashDB

**通用分层**（`fdb_def.h`）：`struct fdb_db` 是所有库的基类（"inherit from fdb_db" 的 C 语言面向对象写法），字段含 `name`、`type`、`storage`（联合体：FAL 模式下是 `const struct fal_partition *part`，文件模式下是 `const char *dir`）、`sec_size`（「flash section size. It's a multiple of block size」）、`max_size`（「database max size. It's a multiple of section size」）、`oldest_addr`（「the oldest sector start address」）、`init_ok`、`file_mode`、`not_formatable`、`lock`/`unlock` 函数指针。**注意 `oldest_addr` 这个字段本身就是"环形保新"的元数据锚点。**

**状态表编码（磨损与掉电安全的共同基石）**，来自 `fdb_utils.c`：

- 源码头注释用一张表说明编码，`| write garn | status0 | status1 | status2 | status3 |`：
  - 1 bit 粒度：`0xFF` → `0x7F` → `0x3F` → `0x1F`（每推进一档多清一个 0 位）
  - 8 bit 粒度：`0xFF FF FF` → `0x00 FF FF` → `0x00 00 FF` → `0x00 00 00`
  - 32/64 bit 粒度同理，每档清零一个字
- 因为擦除态读出来是全 1，**推进状态永远只是把 1 写成 0**，不需要擦除即可原地推进。
- `_fdb_get_status` 注释「get the first 0 position from end address to start address」——解码靠**从表尾向表头找第一个 0**；实现上先 `--status_num` 再倒序扫。
- `_fdb_write_status` 先调 `_fdb_set_status` 算出该改哪个字节（`byte_index`），若返回 `SIZE_MAX` 说明状态表仍是全 1 态，注释「the first status table value is all 1, so no need to write flash」，**直接返回 `FDB_NO_ERR` 不写 Flash**；否则按 `FDB_WRITE_GRAN / 8` 字节写入。旁边注释：「some flash (like stm32 onchip) NOT supported repeated write before erase」。

**KVDB 的 Flash 结构**：

- 魔术字：`SECTOR_MAGIC_WORD = 0x30424446`（注释 `magic word(F, D, B, 1)`）、`KV_MAGIC_WORD = 0x3030564B`（注释 `magic word(K, V, 0, 0)`）。
- 扇区头 `struct sector_hdr_data`：`status_table.store[FDB_STORE_STATUS_TABLE_SIZE]`（「sector store status」）、`status_table.dirty[FDB_DIRTY_STATUS_TABLE_SIZE]`（「sector dirty status」）、`magic`、`combined`（「the combined next sector number, default: not combined」）、`reserved`、按 64/128/256 写粒度补齐的 `padding`。偏移宏 `SECTOR_STORE_OFFSET` / `SECTOR_DIRTY_OFFSET` / `SECTOR_MAGIC_OFFSET` 由零基址成员寻址算出；`SECTOR_HDR_DATA_SIZE = FDB_WG_ALIGN(sizeof(...))`。
- KV 节点头 `struct kv_hdr_data`：`status_table[KV_STATUS_TABLE_SIZE]`、`magic`、`len`（「KV node total length (header + name + value), must align by FDB_WRITE_GRAN」）、`crc32`（注释明确覆盖范围「KV node crc32(name_len + data_len + name + value)」）、`name_len`、`value_len`、padding。
- 状态枚举（`fdb_def.h`）：
  - `fdb_kv_status`：`FDB_KV_UNUSED` / `FDB_KV_PRE_WRITE` / `FDB_KV_WRITE` / `FDB_KV_PRE_DELETE` / `FDB_KV_DELETED` / `FDB_KV_ERR_HDR`，`FDB_KV_STATUS_NUM = 6`
  - `fdb_sector_store_status`：`FDB_SECTOR_STORE_UNUSED` / `_EMPTY` / `_USING` / `_FULL`，`..._NUM = 4`
  - `fdb_sector_dirty_status`：`FDB_SECTOR_DIRTY_UNUSED` / `_FALSE` / `_TRUE` / `_GC`，`..._NUM = 4`

**KVDB 的掉电保护时序（源码级）**：

- 写：先置 `FDB_KV_PRE_WRITE` → 写头部其余部分 → 写 name → 写 value → 最后置 `FDB_KV_WRITE`。**只有 `kv->crc_is_ok && kv->status == FDB_KV_WRITE` 才被 `find_kv_cb` 认作有效记录。**
- 未完成的写：扫描时注释「the KV has not write finish, change the status to error」→ 置 `FDB_KV_ERR_HDR`，迭代器返回 true 提前结束。
- 未完成的删：注释 `"Found an KV (%.*s) which has changed value failed. Now will recovery it."` → `move_kv()` 把旧值写回；恢复前先 `find_kv_no_cache` 检查，避免重复创建。
- 搬移时不破坏源数据：`move_kv` 先写新副本（PRE_WRITE → payload → WRITE）再删旧的，注释「prepare to delete the current KV」，因此搬到一半掉电旧记录仍然有效。
- 删除完成：先写 `PRE_DELETE` 并置 `db->last_is_complete_del = true`，成功写入 `FDB_KV_DELETED` 后才从 KV 缓存里摘除。
- **GC 被打断可续做**：GC 前先把 dirty 状态置为 `FDB_SECTOR_DIRTY_GC`，搬完 KV 后再把扇区重新格式化为 `SECTOR_NOT_COMBINED`。上电时 `check_and_recovery_gc_cb` 重新置 `gc_request` 并调用 `gc_collect()`，注释「resume the GC operate」。
- 初始化扫描 `_fdb_kv_load` 会置 `db->in_recovery_check = true`（注释「is in recovery check status when first reboot」）。扇区头损坏时 `check_sec_hdr_cb` 打印 `"Sector header info is incorrect. Auto format this sector"` 并重新格式化（除非 `not_formatable`）；若所有扇区都坏 → `"All sector header is incorrect. Set it to default."` 并调用 `fdb_kv_set_default`。
- 长度字段非法时（`read_kv`）：注释「not write, so reserved the info for current KV」，状态强制为 `FDB_KV_ERR_HDR` 并返回 `FDB_READ_ERR`；CRC 失败置 `crc_is_ok = false`。
- 已知未完成项（源码 TODO）：`//TODO Sector continuous mode`，以及注释「the write length is not written completely」所指的不完整块写。

**TSDB 的 Flash 结构与掉电保护**：

- 扇区魔术字 `SECTOR_MAGIC_WORD = 0x304C5354`（注释 `magic word(T, S, L, 0)`）。
- 扇区头 `struct sector_hdr_data`：store 状态表、对齐的 `magic`、对齐的 `start_time`、`end_info[]`（每项含 `time` / `index` / `status`，共两个）、`reserved`、`SECTOR_HDR_PADDING_SIZE` 补齐。
- **索引与数据从扇区两端相向增长**（源码注释：「the TSL's data is saved from sector bottom, and the TSL's index saved from the sector top」）：`empty_idx = addr + SECTOR_HDR_DATA_SIZE` 向上增长，`empty_data = addr + db_sec_size(db)` 向下增长，`sector->remain = sector->empty_data - sector->empty_idx`。
- TSL 节点（索引）`log_idx_data`：`status_table[TSL_STATUS_TABLE_SIZE]`、`fdb_time_t time`、以及（非 `FDB_TSDB_FIXED_BLOB_SIZE` 时）`log_len`、`log_addr`。**TSL 节点本身没有魔术字——魔术字只在扇区头**。固定 blob 模式下省略 `log_len`/`log_addr`，靠槽位反推：`tsl->addr.log = 扇区末尾 - (tsl_index_in_sector + 1) * FDB_WG_ALIGN(FDB_TSDB_FIXED_BLOB_SIZE)`。
- TSL 状态：`FDB_TSL_UNUSED` / `FDB_TSL_PRE_WRITE` / `FDB_TSL_WRITE` / `FDB_TSL_USER_STATUS1` / `FDB_TSL_DELETED` / `FDB_TSL_USER_STATUS2`，`FDB_TSL_STATUS_NUM = 6`。**其中两个 USER_STATUS 是留给应用自定义的**（注释「node status, @see fdb_log_status_t」）。
- 写时序 `write_tsl()`：先写索引状态 `FDB_TSL_PRE_WRITE`（sync=false，注释「write the status will by write granularity」）→ 写索引其余部分（从 `LOG_IDX_TS_OFFSET` 起）→ 写 payload（`_fdb_flash_write_align()`）→ 最后重写状态为 `FDB_TSL_WRITE`（sync=true）。全部成功后才推进 `cur_sec.end_idx` / `end_time` / `empty_idx` / `empty_data` / `remain` / `last_time`。
- **时间单调性约束**：`tsl_append()` 拒绝「less than or equal to the last save timestamp」的时间戳。超长记录报 `"This tsl will be dropped."`。
- 扇区收尾 `update_sec_status()`：当 `sector->remain < LOG_IDX_DATA_SIZE + FDB_WG_ALIGN(blob->size)` 时，用同样的 PRE_WRITE → data → WRITE 三步把上一个节点的索引和 `db->last_time` 写进第一个空闲的 `end_info` 槽，标记扇区 `FDB_SECTOR_STORE_FULL`，然后切到新扇区（EMPTY → USING，写入 `start_time`）。
- **掉电恢复**：`read_sector_info()` 若发现两个 `end_info` 都是 `FDB_TSL_PRE_WRITE`，源码写下 TODO「There is no valid end node info on this sector, need impl fast query this sector by fdb_tsl_iter_by_time」然后 `FDB_ASSERT(0)`——即**这个中间态在 TSDB 里是没有优雅恢复的**。
- 半写节点实际不可见：`read_tsl()` 把 `FDB_TSL_PRE_WRITE` 或 `FDB_TSL_UNUSED` 一律映射为 `log_len = db->max_len`、`addr.log = FDB_DATA_UNUSED`、`time = 0`；遍历遇到 `FDB_TSL_UNUSED` 即停止；`PRE_WRITE` 节点不刷新 `end_time`。遍历越界时 `"Error: this TSL (0x%08X) size (%u) is out of bound."`。
- **rollover（环形覆盖保新）**：`struct fdb_tsdb` 有 `bool rollover`（注释「the oldest data will rollover by newest data, default is true」），初始化时置真，可用 `FDB_TSDB_CTRL_SET_ROLLOVER` 改。扇区满时下一个地址是 `sector->addr + db_sec_size(db)`；到达顶部时由 `db->rollover` 决定回到地址 0，否则返回 `FDB_SAVED_FULL`（注释「not rollover」）。回绕后重新格式化扇区时会重算最旧位置，使新数据覆盖最旧数据；正向遍历从 `db_oldest_addr(db)` 开始，反向遍历从 `db->cur_sec.addr` 开始。
- TSDB 初始化错误串：`"Sector (0x%08X) header info is incorrect."`；同时出现多个 USING 扇区时 `"Warning: Sector status is wrong, there are multiple sectors in use."`；失败时若 `not_formatable` 返回 `FDB_READ_ERR`，否则 `tsl_format_all()` 全量重格式化并打印 `"All sector format finished."`。
- **文件模式**（可用于 Linux/仿真验证）：`FDB_KVDB_CTRL_SET_FILE_MODE`、`FDB_KVDB_CTRL_SET_MAX_SIZE`、`FDB_KVDB_CTRL_SET_NOT_FORMAT` 等控制字（`fdb_def.h` 中 `SET/GET SEC_SIZE 0x00/0x01`、`SET_LOCK 0x02`、`SET_UNLOCK 0x03`、TSDB rollover `0x04/0x05`、`GET_LAST_TIME 0x06`、`SET_FILE_MODE 0x09`、`SET_MAX_SIZE 0x0A`、`SET_NOT_FORMAT 0x0B`）。

### 4. ESP-IDF NVS — 条目状态机（掉电安全的教科书案例）

一手来源（文档，v5.5.2 / master）：
- 英文：https://raw.githubusercontent.com/espressif/esp-idf/v5.5.2/docs/en/api-reference/storage/nvs_flash.rst
- 中文：https://github.com/espressif/esp-idf/blob/7ef4d6b76935d16b21367169ad62654baba3f46d/docs/zh_CN/api-reference/storage/nvs_flash.rst
源码：
- https://raw.githubusercontent.com/espressif/esp-idf/v5.5.2/components/nvs_flash/src/nvs_page.cpp
- https://raw.githubusercontent.com/espressif/esp-idf/v5.5.2/components/nvs_flash/src/nvs_page.hpp

**页结构（每页 = 1 个 4096 字节 flash 扇区）**

| 部分 | 大小 |
|---|---|
| 页头：State (4) + Seq. no. (4) + version (1) + Unused (19) + CRC32 (4) | 32 字节 |
| 条目状态位图（entry state bitmap） | 32 字节 |
| 条目 0..125，每个 32 字节 | 126 × 32 字节 |

条目取 32 字节是为了兼容 flash 加密硬件（「To be compatible with {IDF_TARGET_NAME} flash encryption」，加密以 32 字节块为单位）。**页头与条目状态位图永远不加密，只有条目本身加密。** version 字段每次格式修订递减一次，从 0xff 开始（0xff = version 1，0xfe = version 2，依此类推）。

**条目状态 = 每项 2 bit**，126 × 2 = 252 bit，位图剩最后 4 bit 未用：

- **Empty `2'b11`** — 「Nothing is written into the specific entry yet.」，字节全 0xff
- **Written `2'b10`** — 「A key-value pair (or part of key-value pair which spans multiple entries) has been written into the entry.」
- **Erased `2'b00`** — 「A key-value pair in this entry has been discarded.」，并且「Contents of this entry will not be parsed anymore.」

源码里 `EntryState` 还多两个成员：`ILLEGAL`（「only possible if flash is inconsistent」）与 `INVALID`（「entry is in inconsistent state (write started but ESB_WRITTEN has not been set yet)」）；表类型为 `CompressedEnumTable<EntryState, 2, ENTRY_COUNT>`，即 2 bit/项，与文档一致。

**核心掉电安全性质：状态转换只写 0**

文档原句：「Page state values are defined in such a way that changing state is possible by writing 0 into some of the bits.」，因此「it is not necessary to erase the page to change its state unless that is a change to the *erased* state.」——因为 NOR flash 只能 1→0，写 0 推进状态是**单向且不可逆**的，掉电只会落在旧状态或新状态，不会产生"半个状态"。

**页头 CRC32 故意排除 State 字段**：文档原句「CRC32 value in the header is calculated over the part which does not include a state value (bytes 4 to 28).」，保留区当前填 0xff。源码 `Header::calculateCrc32()` 用 `esp_rom_crc32_le`、种子 `0xffffffff`，覆盖范围从 `offsetof(Header, mSeqNumber)` 到 `offsetof(Header, mCrc32)`，即**只含 `mSeqNumber` 与 `mVersion`，不含 `mState` 也不含 `mCrc32` 自身**。这个设计的意义：状态可以随时用"写 0"推进而无需重算 CRC。

**页状态**：`UNINITIALIZED`（「All bits set, default state after flash erase.」）/ `ACTIVE`（「Page is initialized, and will accept writes.」）/ `FULL`（「Page is marked as full and will not accept new writes.」）/ `FREEING`（「Data is being moved from this page to a new one.」）/ `CORRUPT`（「Page was found to be in a corrupt and unrecoverable state.」+「Instead of being erased immediately, it will be kept for diagnostics and data recovery.」+「It will be erased once we run out out free pages.」）；另有仅存在于内存对象的 `INVALID = 0`（「Page object wasn't loaded from flash memory」）。源码中还有内部位名 `PSB_INIT` / `PSB_FULL` / `PSB_FREEING` / `PSB_CORRUPT` 与 `ESB_WRITTEN` / `ESB_ERASED`。

**FREEING 是瞬态，且恢复是"续做"而非"回滚"**：文档说「page should never stay in this state at the time when any API call returns.」，并给出恢复语义「In case of a sudden power off, the move-and-erase process will be completed upon the next power-on.」——**掉电发生在搬移中途时，下次上电把搬移做完**。

**日志结构式写入（log-structured）+ 原地失效**：更新一个键时「a new key-value pair is added at the end of the log and the old key-value pair is marked as erased.」，且「Invalidation of old values doesn't require immediate flash erase operations.」。

**磨损均衡**：文档明确「NVS component includes flash wear levelling by design」，并给出量化说法——页/条目布局「effectively reduces the frequency of flash erase to flash write operations by a factor of 126」（一页 126 条目）。回收方式：「Non-erased key-value pairs are being moved into another page so that the current page can be erased.」（文档没有把这个过程叫 "garbage collection"，但机制就是 GC。）

**撕裂写检测（源码级，很实用）**：
- `load()` 对 `UNINITIALIZED` 页会按 128 字一块通读整扇区，任何非 `0xffffffff` 的字意味着「page isn't as empty after all, mark it as corrupted」；注释给出性能理由「reading the whole page takes ~40 times less than erasing it」——**宁可多读也比误擦便宜**。
- 页头 CRC 不符 → `CORRUPT`；状态无法识别 → `CORRUPT`。
- 对 `ACTIVE` 页注释「we may have more data written to this page」，因此必须在页内找第一个未用条目；若掉电发生在条目中途，注释（原文含拼写错误）「the entry locacted via entry state table may actually be half-written」，检测手段是读条目首字：「this is easy to check by reading EntryHeader (i.e. first word)」，非 `0xffffffff` 的条目头被标为 `ERASED` 并调整计数。
- 变长条目必须整体校验：注释要求「check that all variable-length items are written or erased fully」，跨越不完整的区间一律擦除。
- 一致性检查 `checkHeaderConsistency(i)` 不过关就 `eraseEntryAndSpan(i)`；随后「search for potential duplicate item」，把更早的重复项擦掉，最后还要「check that last item is not duplicate」。
- 状态迁移合法性：`markFreeing()` 只能从 `FULL` 或 `ACTIVE` 进入，`markFull()` 只能从 `ACTIVE` 进入，否则返回 `ESP_ERR_NVS_INVALID_STATE`。

**错误码 / 容量约束**：`ESP_ERR_NVS_INVALID_STATE (0x110b)`（状态不一致，需重新 `nvs_flash_init` + `nvs_open`）、`ESP_ERR_NVS_NO_FREE_PAGES (0x110d)`（分区无空页，通常是分区被截断；处理方式是擦掉整个分区重新初始化）。**NVS 满了是报错，不是覆盖最旧数据。** 分区至少 3 页（12 KiB）且是 0x1000 的整数倍。

### 5. ESP-IDF esp_coredump — 崩溃现场写 Flash

一手来源（文档）：https://github.com/espressif/esp-idf/blob/v5.5-rc1/docs/en/api-guides/core_dump.rst
中文版：https://github.com/espressif/esp-idf/blob/v5.5-rc1/docs/zh_CN/api-guides/core_dump.rst

- **写 Flash 需要专用分区**：Type 必须为 `data`，SubType 必须为 `coredump`，分区名无特殊要求。官方示例：
  ```
  # Name,   Type, SubType, Offset,  Size
  nvs,      data, nvs,     0x9000,  0x6000
  phy_init, data, phy,     0xf000,  0x1000
  factory,  app,  factory, 0x10000, 1M
  coredump, data, coredump,,        64K
  ```
  开启 Flash 加密时需加 `encrypted` 标志；加密的 coredump 无法直接用 `idf.py coredump-info` 读，建议从设备侧读以自动解密。
- **分区容量估算公式（可直接照搬的思路）**：常量开销 **20 字节** + 每任务开销 **12 字节**（不含 TCB 和栈大小），即最小 ≈ `20 + 最大任务数 × (12 + TCB 大小 + 最大任务栈大小)` 字节。
- **两种记录格式**，由 `CONFIG_ESP_COREDUMP_DATA_FORMAT` 选择：
  - **ELF**（新设计推荐）：信息更全（含 CPU 寄存器与内存内容），存有崩溃应用镜像的 SHA256，校验支持 SHA256；体积更大。
  - **Binary**（兼容保留）：更小更快，校验只支持 **CRC32**。
- **ELF 格式内部结构**：
  - `PT_LOAD` 可加载段 → 存进程内存状态，即每个任务的 TCB 与栈转储
  - `PT_NOTE` 段 → 进程元数据；CPU 状态放在名为 `CORE`、类型 `NT_PRSTATUS` 的 note 里（每任务一个），寄存器格式为 GDB 可识别格式
  - 另有 ESP core dump information 段：版本控制信息（coredump 版本、应用镜像 SHA256 等）与扩展寄存器对
  - 镜像含 coredump 头（镜像大小、版本、任务数、TCB 大小）与校验和
- **读取工具链**：`espcoredump.py`（被 `idf.py coredump-info` / `idf.py coredump-debug` 包装）；`info_corefile` 打印崩溃任务寄存器/调用栈/任务列表/内存区域，`dbg_corefile` 生成 ELF 并拉起 GDB 会话。常用选项 `-c/--core`（coredump 文件路径，缺省从 flash 读）、`-t/--core-format`（`elf`/`raw`/`b64`）、`-o/--off`（coredump 分区偏移）、`-r/--rom-elf`（解析 ROM 函数回溯）。UART 输出时 coredump 以 Base64 编码夹在 `CORE DUMP START` / `CORE DUMP END` 标记之间（保存时必须去掉标记行）。

**对本项目的意义**：这是"panic 现场持久化"最成熟的工程范式，但它是**一次性覆盖式**（同一分区重写），不是环形；它靠"分区 + 头部 + 校验和"而不是文件系统。

### 6. ESP-IDF esp_log — 不自带落盘能力

- `esp_log` 默认输出到 UART0，官方提供的重定向手段是 `esp_log_set_vprintf(vprintf_like_t func)`，头文件注释说明它「can be used to redirect log output to some other destination, such as file or network.」，函数返回原来的日志处理函数；**回调必须是可重入的**，因为它可能被多个任务上下文并行调用。
  出处：https://github.com/espressif/esp-idf/blob/96f54947/components/log/include/esp_log_write.h
- 落盘需要用户自行拼装：挂载 SPIFFS → 以追加模式开文件 → 安装自定义 vprintf → 内部 `vfprintf` 并 `fflush()`。
- **已知踩坑**：用 `esp_log_set_vprintf` 把 `ESP_LOG` 写进 SPIFFS 会导致其他任务栈溢出（`***ERROR*** A stack overflow in task XXX has been detected.`）。
  出处：https://github.com/espressif/esp-idf/issues/13205
- 官方限制：`esp_log_write()` 不附加颜色/时间戳/tag 格式；普通日志宏「should not be used from an interrupt」（`esp_log_write.h`）。

**结论**：ESP-IDF 的日志持久化没有官方组件，属于"用户自建"，且官方 issue 表明在日志回调里做阻塞文件 IO 是危险的。

### 7. RT-Thread ulog — 多后端日志服务

一手来源：RT-Thread 官方手册源文件
https://raw.githubusercontent.com/RT-Thread/rtthread-manual-doc/master/ulog/ulog.md
官方文档站：https://www.rt-thread.io/document/site/programming-manual/ulog/ulog/

- **三层架构**：前端（暴露 `syslog` 与 `LOG_X` 两套 API）→ 核心（「format[s] and filter[s] the logs passed by the upper layer, and then generate log frames」）→ 后端（「files, consoles, log servers」）。前后端解耦：任何后端只要实现并注册即可用。
- **后端接口 `struct ulog_backend`** 字段：`name[RT_NAME_MAX]`、`support_color`、函数指针 `init` / `output` / `flush` / `deinit`、`rt_slist_t list`。
  - `output` 是「back-end specific output function」，**每个后端必须实现**
  - `init` 在注册时调用，`deinit` 在 `ulog_deinit` 时调用，二者可选
  - **`flush` 是给"内部带缓存的后端"准备的**（例如带 RAM 缓存的文件系统），正常由 `ulog_flush` 在断言或 hardfault 时调用
  - 注册/注销：`ulog_backend_register(backend, name, support_color)` / `ulog_backend_unregister(backend)`
  - 控制台后端示例 `console_be.c` 只设置 `console.output`。
- **同步 vs 异步**：默认同步。异步模式下日志「cached first, and then handed to the log output thread」，用户 API 不变。
  - 优点：调用方不被慢后端阻塞；每线程栈开销可能下降；**ISR 日志能到达所有后端**——同步模式下 ISR 日志「directly output to the console, and output to other backends is not supported」
  - 缺点：需要额外缓冲区和专用输出线程，资源占用更高
  - 配置项：异步缓冲区大小（默认 **2048**）、「Enable async output by thread.」、异步输出线程栈大小（默认 **1024**）、线程优先级（默认 **30**）
  - 可用 idle 线程替代：`rt_thread_idle_sethook(ulog_async_output)`；但官方警告 idle 线程栈需按后端调整，且**需要挂起的后端（Flash、网络）在 idle 线程里可能不可用**
- **过滤，优先级顺序**：全局静态 > 全局动态 > 模块静态 > 模块动态
  - 全局静态级别：`ULOG_OUTPUT_LVL`（menuconfig）；低于静态级别的日志**根本不进 ROM**
  - 模块静态级别：文件内的 `LOG_LVL` 宏（定义在 `#include <ulog.h>` 之前，与 `LOG_TAG` 并列）
  - 全局动态：`ulog_global_filter_lvl_set(level)`，命令 `ulog_lvl <level>`
  - 按模块/标签动态：`ulog_tag_lvl_filter_set(const char *tag, rt_uint32_t level)`，命令 `ulog_tag_lvl <tag> <level>`
  - 按标签全局过滤：`ulog_global_filter_tag_set(const char *tag)`，命令 `ulog_tag [tag]`
  - 按关键词全局过滤：`ulog_global_filter_kw_set(const char *keyword)`，命令 `ulog_kw [keyword]`
  - 级别常量：`LOG_LVL_ASSERT` / `LOG_LVL_ERROR` / `LOG_LVL_WARNING` / `LOG_LVL_INFO` / `LOG_LVL_DBG`、`LOG_FILTER_LVL_SILENT`、`LOG_FILTER_LVL_ALL`；用 `ulog_filter` 命令查看当前状态
  - **动态过滤需要 menuconfig 里「Enable runtime log filter.」，该项默认关闭**
- **异常场景必然丢日志，所以有 `ulog_flush`**：异步模式有缓冲，部分后端内部也有缓存，因此「hardfault and assertion」会丢待输出日志。`void ulog_flush(void)` 在输出异常信息时被调用，把缓存中的记录冲给后端。`ASSERT(expression)` 内部会调用它；也用于通过 `rt_assert_set_hook(rtt_user_assert_hook)` 安装的断言钩子；CmBacktrace 的 `cmb_println` 在有 ulog 时映射为 `ulog_e(...)` 后接 `ulog_flush`。
- 其他开关：`Enable ulog`、`Enable ISR log.`、`Enable assert check.`、`The log's max width.`、`Enable console backend.`、`Enable syslog format log and API.`

**与 Flash 后端的结合及已知缺陷**：ulog 官方提供 `ulog_easyflash` 软件包，把 ulog 日志经 EasyFlash 落到 Flash（前端 ulog + 后端 ulog_easyflash + 底层 EasyFlash 是社区推荐栈）。**但该组合的掉电行为有实测缺陷**：论坛提问指出写 log 过程中掉电，上电后会提示 `Error: Log area error! Now will clean all log area.`，20 个日志扇区中**只要 1 个扇区出错就擦掉全部日志**，且当时没有"掉电保护开关"；建议的改进方向是改为只识别/处理单个出错扇区，而非全擦。
出处（论坛帖，本次抓取返回 HTTP 468 未能直接打开，仅据搜索摘要引用）：https://club.rt-thread.org/ask/question/d21ca483a5cfbca7.html
**独立佐证**：`ef_log.c` 源码中确实存在该字符串与"全擦"逻辑（见 §1），故机制成立。
注意：ulog_easyflash 是否在 EasyFlash LOG 之上另加了记录级帧头/CRC，本次未取得其源码，标为未证实。

### 8. NuttX — syslog / ramlog / MTD / CRASHDUMP

一手来源：
- https://raw.githubusercontent.com/apache/nuttx/master/Documentation/implementation/syslog.rst
- https://raw.githubusercontent.com/apache/nuttx/master/Documentation/debugging/coredump.rst
- RAMLOG 驱动源码：https://github.com/apache/nuttx/blob/master/drivers/syslog/ramlog.c
- RAMLOG Kconfig：https://github.com/apache/nuttx/blob/c0e27fba41800640c12268d992f024545593457f/drivers/syslog/Kconfig

**SYSLOG 通道抽象**

```c
typedef CODE int (*syslog_putc_t)(int ch);
typedef CODE int (*syslog_flush_t)(void);

struct syslog_channel_s
{
  syslog_putc_t sc_putc;    /* Normal buffered output */
  syslog_putc_t sc_force;   /* Low-level output for interrupt handlers */
  syslog_flush_t sc_flush;  /* Flush buffered output (on crash) */
};
```

- 通过 `int syslog_channel(FAR const struct syslog_channel_s *channel);` 安装，文档说明这是「a non-standard, internal OS interface」，应用不可用，可反复调用来切换通道。默认所有系统日志去 `/dev/console`。
- 初始化分两阶段：`int syslog_initialize(enum syslog_init_e phase);`，phase 为 `SYSLOG_INIT_EARLY` 与 `SYSLOG_INIT_LATE`，分别由 `up_initialize()` 与 `nx_start()` 调用。
- **各通道的掉电/持久化能力差异很大**：
  - **RAMLOG**：reset 时即完全可用；纯 RAM 环形缓冲
  - **控制台通道**：串口配置好后 `up_putc()` 可用
  - **其他通道**：在「SYSLOG channel device has been initialized」之前，输出被直接丢弃
- 注意 `sc_flush` 的注释就是「Flush buffered output (on crash)」——**NuttX 把"崩溃时刷缓冲"做进了通道接口的原生语义**。另文档提到 `CONFIG_SYSLOG_BUFFER` 的崩溃时冲刷「has not yet been implemented」。

**文件通道（落盘的正规做法）**

- `int syslog_file_channel(FAR const char *devpath);`，由 `CONFIG_SYSLOG_FILE` 启用，是 `syslog_dev_initialize()` + `syslog_channel()` 的薄封装。
- 关键限制：**目标文件系统必须已经挂载**，因此该通道「cannot be supported during the boot-up phase」，必须在挂载完成后由板级代码调用；调用之前产生的输出不会进文件。
- 文件已存在则**追加到末尾**，不存在则创建。
- **中断级输出会丢失**，除非开启中断缓冲（`CONFIG_SYSLOG_INTBUFFER` + `CONFIG_SYSLOG_INTBUFSIZE`）。
- 字符设备通道由 `CONFIG_SYSLOG_CHAR` 启用，需 `CONFIG_SYSLOG_DEVPATH` 指定完整路径；强制输出「always goes to the bit-bucket」（即中断级输出直接丢弃）。

**RAMLOG（环形缓冲，但不持久）**

- 定位：当常规串口输出不可用时支撑调试输出的驱动，把调试输出存进 RAM 里的 FIFO/环形缓冲（类似管道但带日志特性）；**能从中断处理程序接收调试输出**，这是通用字符设备 SYSLOG 通道做不到的。
- 配置项：
  - `CONFIG_RAMLOG` 启用驱动
  - `CONFIG_RAMLOG_CONSOLE` 把控制台输出重定向到 RAM（需 `CONFIG_DEV_CONSOLE`）
  - `CONFIG_RAMLOG_SYSLOG` 把调试输出重定向到 RAM，可用 NSH 的 `dmesg` 命令查看
  - `CONFIG_RAMLOG_BUFSIZE` 环形缓冲大小，默认 **1024**
  - `CONFIG_RAMLOG_NONBLOCKING` 读空时返回 0（被解释为 EOF）；不开会让 NSH 的 `dmesg` 命令卡死
  - **`CONFIG_RAMLOG_OVERWRITE` 溢出时覆盖环形缓冲以保留最新日志**（正是黑匣子语义的开关）
  - `CONFIG_RAMLOG_CRLF`、`CONFIG_RAMLOG_NPOLLWAITERS`（默认 4）、`CONFIG_RAMLOG_POLLTHRESHOLD`、`CONFIG_RAMLOG_BUFFER_SECTION`（缓冲区所在段，要求 boot 时不被初始化）
- 注册为字符驱动，暴露 open/close/read/write/ioctl/poll；API：`ramlog_register(devpath, buffer, buflen)`、`ramlog_syslog_register(void)`、`ramlog_putc()`、`ramlog_write()`；ioctl 含 `FIONREAD`、`BIOC_FLUSH`、`SYSLOGIOC_SETRATELIMIT`/`SYSLOGIOC_GETRATELIMIT`。`dmesg` 命令**导出并清空**缓冲。
- **文档中没有任何关于 Flash 持久化或跨重启保留的描述**：RAMLOG 只在 RAM 里；要持久必须走文件通道 + 已挂载文件系统。

**CRASHDUMP —— "panic 现场 RAM 暂存后复位转存"的教科书表述**

一手来源（Kconfig 与 API）：
- https://github.com/apache/nuttx/blob/master/boards/Kconfig
- 示例实现：https://raw.githubusercontent.com/apache/nuttx/389a1d4fda15672cceaf6f8c85cfc4a1851181c4/boards/arm/cxd56xx/common/src/cxd56_crashdump.c

- `CONFIG_BOARD_CRASHDUMP`：使能后 `up_assert` 在调用 `exit` 之前（断言失败）或在死循环之前（hardfault）回调 `board_crashdump`。官方对其目的的描述（Kconfig）：尽可能多地保存故障信息然后复位系统；因为内存可能已被破坏，所以要**把机器状态保存到一个"下次复位后能够由正常环境写入更精细存储"的地方**——这正是"RAM 暂存 + 复位后转存"的权威表述。
- 子选项：`BOARD_CRASHDUMP_NONE`（默认）、`BOARD_CRASHDUMP_CUSTOM`（「Enable Crash Dump with custom method… only work with board_crashdump api」）。
- API 签名（由补丁可见其演进）：`void board_crashdump(uintptr_t currentsp, FAR void *tcb, FAR const uint8_t *filename, int lineno);`
- **板级实例（CXD56xx）**：`cxd56_crashdump.c` 填一个 `fullcontext_t`（timestamp、flags、寄存器、用户/中断栈信息、任务名、pid、文件名/行号），内存来自 `up_backuplog_alloc("crash", ...)`（在 `CONFIG_CXD56_BACKUPLOG` 下）否则用 `malloc`；并提供 `board_reset_on_crash()` 从中断处理程序经看门狗复位。配套 `crashdump.h` 定义 `CRASHLOG_SIZE 1024`。
- RX65N 变体用 standby RAM 保存 crashdump（`RX65N_SAVE_CRASHDUMP`「SBRAM Save Crashdump」，依赖 `RX65N_SBRAM`）。

**Core Dump（ELF 格式，可落到 MTD/块设备）**

- 格式：ELF core —— `Elf_Ehdr`（`e_type = ET_CORE`，`e_machine` 为目标架构）→ `Elf_Phdr[]`（每个内存区域一个 `PT_LOAD`）→ 各内存区域原始内容 → 末尾一个 `PT_NOTE` 段：`Elf_Nhdr`（`n_type = COREDUMP_MAGIC`）→ 名字 `"NuttX"`（`n_namesz = COREDUMP_INFONAME_SIZE`）→ `coredump_info_s`（版本、时间戳、进程名）。
- `CONFIG_BOARD_COREDUMP_COMPRESSION` 开启时整体用 **LZF** 压缩，后缀从 `.core` 变 `.lzf`。
- **保存目标可选**：`BOARD_COREDUMP_SYSLOG`（到 syslog）、`BOARD_COREDUMP_BLKDEV`（块设备）、`BOARD_COREDUMP_MTDDEV`（MTD 设备）、`BOARD_COREDUMP_MEMDEV`（内存设备），路径由 `BOARD_COREDUMP_DEVPATH` 指定；另有 `BOARD_COREDUMP_FULL`、`BOARD_COREDUMP_OVERWRITE`、`BOARD_COREDUMP_BASE64STREAM`。
- 支持「restoration from persistent storage on block or MTD devices」：`CONFIG_SYSTEM_COREDUMP_RESTORE` + `CONFIG_SYSTEM_COREDUMP_DEVPATH`；恢复时校验 ELF 结构并从 note 段提取元数据。
- **滚动保留策略**：恢复出的 coredump 文件命名 `<version>-<timestamp_hex>.<suffix>`（例：`NuttX-10.1.0-6720C67E.lzf`），**只保留 `maxfile` 个，最旧的先删**。
- 分析：`tools/coredump.py`（hex → 二进制并解 LZF），然后 `arm-none-eabi-gdb -c elf.core nuttx`（工具链需新于 11.3）。

**小结**：NuttX **没有**为日志提供 Flash 通道；持久化路径是"文件通道 + 文件系统"或"coredump 到 MTD/块设备"；RAMLOG 是纯 RAM 环形缓冲（可覆盖保新）。MTD 层是存储抽象，不是日志通道。

### 9. MCUboot — image trailer / swap 状态（掉电安全的另一范式）

一手来源：
- 设计文档：https://git.trustedfirmware.org/plugins/gitiles/mirror/mcuboot.git/+/f84cc4b309bd9c53d878505693349c953b86fdca/docs/design.md
- 镜像 trailer 与 Flash 布局（第三方整理，交叉参考）：https://deepwiki.com/STMicroelectronics/stm32-mw-mcuboot/7.2-image-trailer-and-flash-layout
- 变砖缺陷与修复：https://github.com/mcu-tools/mcuboot/pull/2100

**image trailer 结构**（每个 slot 末尾，从槽尾向前排布）：

| 字段 | 大小 | 用途 |
|---|---|---|
| **Magic** | 16 字节（`boot_img_magic[]`） | trailer 有效性标识 |
| **Image OK** | 1 字节 + padding | 镜像确认标志 |
| **Copy Done** | 1 字节 + padding | 拷贝完成标志 |
| **Swap Info** | 1 字节 + padding | 低 4 位 = swap 类型，高 4 位 = 镜像号 |
| **Swap Size** | 4 字节 + padding | 需要交换的总字节数 |
| **Encryption Keys** | 各 16 字节（可选） | primary / secondary 槽密钥 |
| **Swap Status** | `BOOT_MAX_IMG_SECTORS * min-write-size * 3` | 逐扇区交换进度（用于中断后续做） |

- magic 由 4 个 32 位值组成：`0xf395c277`、`0x7fefd260`、`0x0f505235`、`0x8079b62c`。`boot_magic_decode()` 的判定：
  - `BOOT_MAGIC_GOOD` — 完全匹配，存在有效 swap 元数据
  - `BOOT_MAGIC_BAD` — 被破坏（任何其他非擦除值）
  - `BOOT_MAGIC_UNSET` — 已擦除（全 `0xFF`）
  - `BOOT_MAGIC_ANY` / `NOTGOOD` — 用于决策表匹配的逻辑控制值
- 标志值：`BOOT_FLAG_SET`（`0x01`）、`BOOT_FLAG_UNSET`（`0xFF`，擦除态）、`BOOT_FLAG_BAD`（被破坏）；`boot_flag_decode()` **只在精确等于 `0x01` 时返回 SET**。
- **`copy_done` 只在升级/回滚的全部扇区拷贝完成后才置位；`image_ok` 用于把 test 镜像确认为永久镜像。** MCUboot 用两个槽的 trailer 状态匹配 `boot_swap_tables` 决策表，决定操作类型（NONE / TEST / PERM / REVERT）。

**swap status 区 —— "用多次单向写编码一个多值状态"的经典技巧**

- swap status 区是逐个独立写入的单字节记录（补齐到 flash 最小写单位）。**由于 flash 记录不可覆盖，每个扇区索引使用 3 条记录来编码 4 个状态**：

  | 状态 | 记录值 |
  |---|---|
  | 0 | `0xff 0xff 0xff` |
  | 1 | `0x01 0xff 0xff` |
  | 2 | `0x01 0x02 0xff` |
  | 3 | `0x01 0x02 0x03` |

  这与 EasyFlash / FlashDB 的"状态表位图"、NVS 的"2 bit 条目状态"是同一思想的不同实现：**在只能 1→0 的介质上，用"已清掉几个 0"来表示进度。**
- **中断恢复**：若 bootloader 在交换中途复位，镜像可能是不连续的。bootutil 通过 image trailer 定位对应的 swap status 区（决策表把 trailer 内容——primary / scratch / secondary 槽的 magic 与 copy-done 取值——映射到 status 区的来源位置），从而知道从哪个扇区继续或回滚。
- `swap_info` 字段是为修「中断的 revert 导致双重交换」缺陷（issue #480）而加入的，使 MCUboot 能判断该恢复哪种交换类型；交换时 MCUboot **先把 trailer 写入 scratch 区，让 swap 类型在 primary trailer 被擦除之前就持久化**——"先立遗嘱再动手"的标准手法。

**反面教材：决策表 + 中间态落盘 = 变砖（PR #2100 / issue #1966）**

- 场景：swap-move 模式下，一次升级留下未确认（不可用）的镜像在 primary 槽，于是安排回滚。回滚开始时 `fixup_revert` 重写 secondary 槽的 trailer，使这次回滚看起来像一次永久升级。如果**在写 trailer magic 的过程中掉电**（`boot_write_magic`），magic 处于半写状态 → `BOOT_MAGIC_BAD`。
- 下次启动的状态组合：
  - primary 槽：magic=good、copy-done=set、image-ok=unset
  - secondary 槽：magic=bad、copy-done=unset、image-ok=set
- **这个组合在 swap 决策表里没有任何对应项**，于是 MCUboot 既不回滚也不做别的，直接尝试启动那个不可用的 primary 镜像 → 设备变砖（除非能用外部手段重刷 secondary 槽）。
- 修复思路：只要 primary 槽的 copy-done 已置位且 image-ok 未置位，就无条件执行回滚，**不理会 secondary 槽 trailer 的 magic 状态**。因为 copy-done 只在升级/回滚完整完成后才置位，这个组合足以保证"存在一次未确认的升级，需要回滚"。讨论中还指出该修复需同步更新文档（状态描述已与代码不符），并建议用故障注入测试覆盖升级/回滚路径。
- **衍生教训**：STM32U5 SBSFU 移植中曾出现 `[WRN] 15 status write fails performing the swap`，原因是 flash 驱动擦除时没有相对 Bank 2 重定位页号，导致 primary 槽末尾扇区从未被擦除、trailer 写入被拒。说明 **trailer 区必须保证可擦可写**，否则整个恢复逻辑失效。
  出处：https://community.st.com/stm32-mcus-security-36/stm32u5-sbsfu-wrn-15-status-write-fails-performing-the-swap-using-custom-flash-layout-163346

### 10. SEGGER — RTT / SystemView：有环形覆盖，但只在 RAM

一手来源：
- post-mortem 模式：https://www.segger.com/products/development-tools/systemview/technology/post-mortem-mode/
- single-shot 模式：https://www.segger.com/products/development-tools/systemview/technology/single-shot-recording/
- 用户手册 UM08027：https://doc.segger.com/UM08027_SystemView.html

- **没有任何 target 侧 Flash 持久化**。SystemView 把事件格式化后交给 RTT，存在 **target RAM 缓冲**里。官方页只讨论 RAM 缓冲（「External RAM can be used for the SystemView buffer」，建议 ≥ 8 kByte），**全文未提及写 flash、跨断电保留**。
- **single-shot 模式**：用于目标不支持 RTT 或无 J-Link 的场合；应用调 `SEGGER_SYSVIEW_Start()` 开始、缓冲满或 `SEGGER_SYSVIEW_Stop()` 结束；保留的是**最先记录的事件**。典型应用每秒产生约 5–15 kB 记录数据，建议缓冲 ≥ 8 kB。
- **post-mortem 模式（最接近黑匣子）**：连续记录，缓冲满后**覆盖较旧事件，读出来的是最新记录的事件**——「When the target buffer is filled older events are overwritten and reading the buffer provides the latest recorded events.」官方推荐用于分析崩溃前发生了什么。要求调试探针能连接且**不复位目标、不修改 RAM**（「without resetting it or modifying the RAM」），这反过来确认数据是留在 RAM 里的。
- 因为是环形缓冲，读出时可能要分两段：「Since the SystemView buffer is a ring buffer, the data might have to be read in two chunks.」官方给的步骤是先从 `pBuffer + WrOff` 读到缓冲末尾，再补上 `pBuffer` 到 `pBuffer + RdOff - 1`，存为 `.SVdat` 或 `.bin`。需要 sync 包才能解析回绕内容，建议 sync 周期约等于缓冲大小 / 16。
- 配置宏：`SEGGER_SYSVIEW_RTT_BUFFER_SIZE`、`SEGGER_SYSVIEW_BUFFER_SECTION`（可把缓冲放到专用链接段甚至外部 RAM）、`SEGGER_SYSVIEW_POST_MORTEM_MODE`、`SEGGER_SYSVIEW_SYNC_PERIOD_SHIFT`。读出时需要的地址与写偏移通常是 `_SEGGER_RTT.aUp[1].pBuffer` 与 `_SEGGER_RTT.aUp[1].WrOff`。
- **持久化只发生在主机侧**（SystemView 应用可把记录存成文件，之后无需 J-Link / 目标即可分析）。

**对本项目的意义**：SystemView 的 post-mortem 模式与"环形覆盖保新"语义完全一致，但它止步于 RAM，掉电即失——**这正好是我们做 Flash 持久化后端要补上的那一环**。它的"读出来分两段"、"需要 sync 包"是环形缓冲读出的通用工程细节，值得在设计导出协议时参考。

### 11. 其他通用 "MCU 黑匣子" 开源实现（材料有限，未展开）

- **mxv3a/cyclic-data-log**（MIT，Arduino C++ 库）：在 ESP32 / STM32 / BW16 上将用户自定义 struct 写入持久内存；README 明确「The log adds the entry to the persistent memory and remembers all entries even after a reset.」以及满了之后「If the log is full the oldest entry will be overwritten.」（环形覆盖保新）。读取按相对索引：最旧为 `0`，最新为 `-1`。配置靠 `DataLog.h` 里的三个宏 `EEPROM_MAX_SIZE`（默认 4096）、`LOG_START_ADDRESS`（默认 0）、`LOG_END_ADDRESS`（默认 `EEPROM_MAX_SIZE`）；超出 `LOG_START_ADDRESS`/`LOG_END_ADDRESS` 之外的区域库不触碰。
  出处：https://raw.githubusercontent.com/mxv3a/cyclic-data-log/refs/heads/main/README.md
  **README 未描述记录内部布局、校验和、掉电原子性，也未说明复位后如何定位写入位置**（未证实）。且它基于 EEPROM 抽象而非裸 flash。
- **CmBacktrace**：Arm Cortex-M 故障诊断库，捕获寄存器快照、栈回溯与故障类型（HardFault / BusFault 等），可经串口或 Flash 输出；与 ulog 有集成钩子（见 §7）。**本次仅得搜索摘要，未抓取一手 README，细节未证实。**
- **blackbox-logger（martinbudden）**：`no_std`、无堆分配的 Betaflight 兼容黑匣子飞行数据记录器，基于 Nicholas Sherlock 的 Blackbox 实现，Rust 编写。
  出处：https://raw.githubusercontent.com/martinbudden/crate-blackbox-logger/refs/heads/main/README.md
- **Trice（rokath，MIT）**：MCU 上的 C 追踪/日志库，支持 ring / double-buffer 模式；主要传输是 SEGGER RTT、UART、USB 或自定义字节 sink，**flash 分区仅被提及为可能的辅助 sink**，不是专用的 flash 环形日志器。
- **ChronoLog（Hamas888，MIT）**：ESP32 / STM32 / nRF52，但输出走 UART；**flash 文件日志与环形缓冲被列为 roadmap 未实现项。**

**小结**：通用生态里"MCU 上的 flash 环形黑匣子日志库"很少有既开源又完整覆盖"掉电原子性 + 磨损 + 可导出"三者的成熟实现；真正把掉电安全做扎实的是 EasyFlash / FlashDB / NVS 这类**存储库**，以及 MCUboot 这类**元数据状态机**。

## 各实现的掉电安全手法对比

### 表 A：横切五问

| 实现 | ① 存文本还是结构化 | ② 满了怎么办 | ③ 掉电恢复靠扫描还是元数据 | ④ 磨损如何处理 | ⑤ 用文件系统吗，为什么 |
|---|---|---|---|---|---|
| **EasyFlash LOG** (`ef_log.c`) | **裸二进制**字对齐 blob，不假设内容（文本/二进制都行，由上层定） | **环形覆盖最旧**：`log_start_addr` 随写指针前移，最旧扇区被擦后重用 | **纯扫描**：扫扇区头判状态序列合法性 + 扫 0xFF 定位写指针；**无记录级 CRC**；判为非法就全擦 | 扇区级轮转天然均摊（每扇区轮流擦）；**无擦除计数** | **明确不用**——README「无需文件系统」，面向无 FS 的小产品，省 ROM/RAM |
| **EasyFlash ENV** (`ef_env.c`) | **结构化 KV**（name + value + CRC32） | GC：空扇区低于 `EF_GC_EMPTY_SEC_THRESHOLD` 时，搬走 FULL+DIRTY 扇区的有效 ENV 再擦除 | **元数据**：扇区头 `store`/`dirty` 状态表 + ENV 节点状态表 + CRC32；扫描时按状态决定恢复动作 | "写平衡"：扇区级轮转 + GC 搬移；状态表单向位编码使推进状态不必擦除 | **不用**（同 LOG，同属 EasyFlash 库） |
| **FlashDB KVDB** (`fdb_kvdb.c`) | **结构化 KV**（magic + len + crc32 + name_len + value_len） | GC 搬移（`FDB_SECTOR_DIRTY_GC` 标记 + 可续做）；空间不足时报错 | **元数据**：扇区头状态表 + KV 节点状态表 + CRC32；`PRE_WRITE` → 置 `ERR_HDR` 丢弃，`PRE_DELETE` → 搬回旧值 | 扇区状态推进（EMPTY→USING→FULL）不擦除；GC 回收 FULL+DIRTY 扇区；**源码未见 erase_count 字段** | 可选：FAL 裸 flash 模式（默认）或 `file_mode`（Linux/仿真验证用） |
| **FlashDB TSDB** (`fdb_tsdb.c`) | **结构化时序**：索引（time + status + log_len/addr）与数据分离存放 | **`rollover` 默认 true → 最新覆盖最旧**；置 false 则返回 `FDB_SAVED_FULL` | **元数据**：扇区头 store 状态表 + `end_info[2]`；`PRE_WRITE` 节点映射为 `FDB_DATA_UNUSED` 不可见；**两 `end_info` 均 PRE_WRITE 时源码是 `FDB_ASSERT(0)`，无优雅恢复** | 同 KVDB（扇区级轮转 + `oldest_addr` 跟踪）；另靠索引/数据相向增长压缩空间 | 同 KVDB（支持文件模式） |
| **ESP-IDF NVS** | **结构化 KV**（字符串 / blob） | **不覆盖，报错**：`ESP_ERR_NVS_NO_FREE_PAGES (0x110d)`，需擦整个分区重初始化 | **元数据为主 + 扫描兜底**：页状态 + 2 bit 条目状态位图 + 3 层 CRC32；撕裂条目靠读 `EntryHeader` 首字判定；`UNINITIALIZED` 页按 128 字通读校验是否真空白 | **页轮转 + 126 条目/页，把擦除次数降低 126 倍**；`FREEING` 状态做搬移回收；官方称「wear levelling by design」 | **不用**——直接管理 4096 字节扇区，无 FS 层 |
| **esp_coredump** | **结构化二进制**：ELF（`PT_LOAD` 内存段 + `PT_NOTE` 元数据）或 Binary 格式 | **单份覆盖**（同一 `coredump` 分区重写），非环形 | **元数据**：coredump 头（大小/版本/任务数/TCB 大小）+ **CRC32 或 SHA256** 校验和；ELF 版还存应用镜像 SHA256 | **无磨损处理**（崩溃是低频事件） | **不用**——直接写 `data,coredump` 专用分区，省 FS 依赖 |
| **esp_log** | 格式化**文本**（tag + level + 时间戳） | 无内建策略；落盘靠用户自建（文件轮转需自己写） | **无**（本身不持久化） | 无 | **必须用**——官方无持久化组件，唯一路径是靠 `esp_log_set_vprintf` 接 FS（如 SPIFFS）并 `fflush` |
| **NuttX SYSLOG 文件通道** | 格式化**文本** | 无内建策略（文件只追加，轮转交给 FS/用户） | **无**（依赖 FS 自身的一致性保证） | 交给 FS | **必须用**——且要求文件系统**已挂载**，因此「cannot be supported during the boot-up phase」 |
| **NuttX RAMLOG** | 格式化**文本** | `CONFIG_RAMLOG_OVERWRITE` 可选**覆盖保新**；不开则丢弃 | **无**（RAM 掉电即失） | **无**（RAM 无擦写寿命问题） | **不用**（纯内存字符设备）；`dmesg` 导出并清空 |
| **NuttX CRASHDUMP** | 结构化现场：`fullcontext_t`（寄存器/栈/任务名/pid/文件行号） | N/A（单次） | **靠 RAM 暂存 + 复位后由正常上下文转存** | 无 | **不用**——就是要避开"故障时还依赖复杂子系统" |
| **NuttX Core Dump** | **结构化二进制**：ELF core（`ET_CORE` + `PT_LOAD` + `COREDUMP_MAGIC` note），可 LZF 压缩 | 落到 FS 时**只保留 `maxfile` 个，最旧的先删** | **元数据**：ELF 结构 + note 段 `coredump_info_s`（版本/时间戳/进程名），恢复时校验 | 无 | **可选**——可到 syslog / 块设备 / MTD 设备 / 内存设备 |
| **MCUboot trailer** | **元数据状态位**（非数据日志） | N/A | **元数据**：trailer magic + `copy_done` + `image_ok` + `swap_info`；swap status 区逐扇区记录进度，可续做 | **无**——trailer 区反复原地写是已知弱点（STM32U5 案例里因扇区未擦除导致写入被拒） | **不用**——bootloader 阶段不可能依赖 FS |
| **SEGGER SystemView** | **结构化二进制**事件流（经 RTT） | **post-mortem 模式环形覆盖，保留最新**；single-shot 保留最先 | **无**（RAM 掉电即失） | 无 | **不用**（target 侧根本无持久化；持久化只在主机侧存 `.SVdat`） |
| **cyclic-data-log** | **用户自定义 struct**（结构化） | **环形覆盖最旧** | **未描述**（README 未说明复位后如何定位写位置） | 未描述（基于 EEPROM 抽象） | **不用**（直接操作 EEPROM 地址区间） |

### 表 B：掉电安全的具体手法归类

| 手法 | 代表实现 | 机制要点 |
|---|---|---|
| **状态表单向位编码** | EasyFlash ENV、FlashDB KVDB/TSDB | 状态值靠"从表尾往前找第一个 0 位"解码；推进状态 = 清 0；状态 0（全 1）时根本不写 flash |
| **按位置编码状态（多字占位）** | EasyFlash LOG | 三个 header 字，EMPTY/USING/FULL 分别对应"写到第几个字"，永不回头 |
| **多次单向写编码多值状态** | MCUboot swap status | 每个扇区索引 3 条记录编码 4 个状态（`ff ff ff` → `01 ff ff` → `01 02 ff` → `01 02 03`） |
| **2 bit 条目状态位图 + 页状态位** | ESP-IDF NVS | 页状态转换"只写 0"；页头 CRC 故意排除 State 字段（只覆盖 bytes 4–28），使状态可推进而无需重算 CRC |
| **PRE_WRITE → 数据 → WRITE 三段式** | EasyFlash ENV、FlashDB KVDB/TSDB、NVS 追加式写 | 只有终态被认作有效；中途掉电的 `PRE_WRITE` 记录在扫描时被标为 `ERR_HDR` / 映射为 `UNUSED` |
| **未完成删除的恢复（搬回旧值）** | EasyFlash ENV、FlashDB KVDB | `PRE_DELETE` 状态 → 上电时 `move_kv()` 把旧值写回；恢复前检查避免重复 |
| **先写新副本再删旧的** | FlashDB KVDB `move_kv` | 搬移中掉电时旧记录仍有效，最坏情况是存在两份而不会丢失 |
| **瞬态状态"下次上电续做"** | NVS `FREEING`、FlashDB `SECTOR_DIRTY_GC` | 官方语义：`FREEING` 绝不在 API 返回时停留；掉电后"move-and-erase 会在下次上电完成" |
| **CRC 分层校验** | NVS（页头 / 条目 / 变长数据三层）、FlashDB（节点 CRC32）、esp_coredump（CRC32 或 SHA256） | 页头 CRC 可独立校验而不受条目区影响；变长数据单独 CRC |
| **撕裂写检测靠读"首字"** | NVS `load()` | 条目首字非 `0xffffffff` 即说明该条目被写过 → 结合条目状态位图判断是否半写 |
| **宁可多读也不误擦** | NVS `load()` | 注释理由：读整页比擦一次快约 40 倍，所以 `UNINITIALIZED` 页要通读校验真空白 |
| **扫描 0xFF 定位写指针（无记录头）** | EasyFlash LOG | 按 32 字节块扫连续 0xFF；代价是无记录级完整性保证 |
| **先立遗嘱再动手** | MCUboot swap | 交换前先把 trailer 写进 scratch 区，使 swap 类型在 primary trailer 被擦除前已持久化 |
| **RAM 暂存 → 复位 → 正常上下文转存** | NuttX CRASHDUMP（CXD56xx 用 `up_backuplog_alloc` 专段，RX65N 用 SBRAM） | 官方 Kconfig 原文即"保存到下次复位后能写入更精细存储的地方" |
| **崩溃时冲刷缓冲** | NuttX `syslog_channel_s::sc_flush`（注释 "Flush buffered output (on crash)"）、ulog `ulog_flush` | 把"异常时冲刷"做进接口的原生语义 |

## 可借鉴 / 应避免

### 本项目约束回顾（用于本节判断）

C11 嵌入式框架；Cortex-M4 级 MCU；片内 NOR flash；已有器件层 flash 抽象（**写必须让出 CPU**）、零状态分区表 v2（注册表 + 句柄）、多后端 log 服务（**独立日志线程**、per-backend 按模块过滤）。目标：**黑匣子语义（环形覆盖保新）的持久化日志后端**，掉电安全、可主动拉取导出、panic 现场 RAM 暂存后复位转存。

### 可借鉴

**1. 记录帧自带长度 + CRC，且用"预写/完成"两段状态位**
FlashDB 的 KV/TSL 与 NVS 条目都是这个套路：先落一个"未完成"状态，数据写完再翻成"已完成"，扫描时只有终态才算数。对我们的日志帧：帧头写 `len + crc32 + flags`，`flags` 里留一个"有效"位，**数据区全部写完并回读校验后再置该位**。因为 NOR 只能 1→0，这个位天然是不可逆的单向提交。这样恢复了"单帧原子性"，而不必依赖 EasyFlash LOG 那种"扫 0xFF 猜边界"的启发式（见应避免第 1 条）。

**2. 状态推进不要重算 CRC —— 学 NVS 的"CRC 排除状态字段"**
NVS 页头 CRC32 **只覆盖 bytes 4–28，故意排除 4 字节 State 字段**。因为状态要被反复"写 0"推进，若 CRC 覆盖状态，每推进一次都要重算重写 CRC，既多写又引入新的撕裂窗口。我们的帧头若同时有 CRC 和状态位，应让 **CRC 只覆盖"数据 + 长度"这些不变部分，状态位在 CRC 覆盖范围之外**。

**3. 环形覆盖以"扇区/块"为单位轮转，而不是逐记录原地覆盖**
EasyFlash LOG 的环形是扇区粒度：`log_start_addr` 只在"下一个扇区追上最旧扇区"时才前移。这天然对齐 flash 擦除粒度（片内 NOR 必然是整块擦），且掉电时**最多损失一个扇区**而不是把整个环形结构搞乱。配合零状态分区表 v2，我们的日志后端应以分区内的块为单位维护一个"最旧块"水位。

**4. 用 FlashDB 的 `oldest_addr` 思路显式记录"最旧"锚点**
`struct fdb_db` 里的 `oldest_addr`（「the oldest sector start address」）就是环形保新的元数据锚点，读写双向遍历都靠它（正向从 `oldest_addr` 开始，反向从 `cur_sec` 开始）。**导出功能应该以这个锚点为准，而不是"从头扫"**——这正是"可主动拉取导出"的高效实现路径。

**5. 用 TSL 的 `USER_STATUS1/USER_STATUS2` 实现"已导出"标记**
FlashDB TSDB 的状态枚举里特意留了两个**用户自定义状态**（`FDB_TSL_USER_STATUS1` / `FDB_TSL_USER_STATUS2`）。我们可以照搬：给每条日志帧一个应用级状态位，标记"已导出/未导出"。导出不必删除数据、也不必改写数据区，只推进一个单向状态位即可——既不破坏环形语义，也不额外磨损数据区（且状态位推进本身走得是"只清 0"的低磨损路径）。

**6. 记录用单调序号而非时间戳做排序键 —— 规避 FlashDB TSDB 的坑**
FlashDB TSDB 强制"时间戳必须严格大于上一条"（拒绝 `<= last_time` 的写入）。这在设备复位后 RTC 丢失、时钟回退时会**直接拒写日志**，对黑匣子是不可接受的。我们的帧头用**单调递增序号 + 可选时间戳**：序号用于排序与环形定位，时间戳只作参考信息。

**7. panic 路径分层：中断里只做 RAM 暂存，转存交给日志线程**
NuttX 的 `CONFIG_BOARD_CRASHDUMP` 把这套做法写成了官方语义：因为"内存可能已被破坏"，所以先**把机器状态保存到一个下次复位后能由正常环境写入更精细存储的地方**。这与我们"器件层 flash 写必须让出 CPU"的约束完全一致，也是我们"panic 现场 RAM 暂存后复位转存"的权威依据。可直接照搬的两个板级实现细节：
- CXD56xx 用 `up_backuplog_alloc("crash", ...)` 申请**一块独立备份 RAM 段**（`CONFIG_CXD56_BACKUPLOG`，`CRASHLOG_SIZE 1024`），而不是普通堆；
- RX65N 用 **standby RAM（SBRAM）**——掉电/复位不丢的 RAM 区，这是比普通 SRAM 更强的"暂存"介质（`RX65N_SAVE_CRASHDUMP`）。
panic 写入路径上只记录 `fullcontext_t` 那样的固定小结构（寄存器组、栈指针、任务标识、故障点位），**不格式化字符串**。

**8. 崩溃时必须有"冲刷"语义**
异步模式 + 后端内部缓存 = 异常时丢日志，这是 ulog 明确承认的问题，`ulog_flush()` 就是为此存在的；NuttX 更把它做进了通道接口（`sc_flush` 注释 "Flush buffered output (on crash)"）。我们的 log 服务是独立日志线程 + 有界队列，**必须有等价的 flush 原语**，并与 panic 钩子对接：断言/HardFault 时先 flush 队列，再走 RAM 暂存。

**9. 异步输出的参数与禁忌可以直接参考 ulog**
ulog 的异步模式给出了可用的经验值（缓冲 2048、输出线程栈 1024、优先级 30），并且官方明确警告：**改用 idle 线程时，需要挂起的后端（Flash、网络）不可用**。我们的"独立日志线程"路线正对应 ulog 的 "async output by thread"，是正确选择；同时它反证了"不要在 idle 里做 flash 写"。

**10. 分区容量按最坏情况估 —— 参考 esp_coredump 的公式**
esp_coredump 明确定义了"常量开销 20 字节 + 每任务开销 12 字节（不含 TCB 和栈）"，并给出最小值公式。我们的黑匣子若也要存任务现场，应显式给出"单帧最大长度 × 期望条数 × 安全系数"的估算公式，并把最坏情况写进设计文档，而不是拍脑袋定分区大小。

**11. 崩溃转储复用成熟容器格式（ELF）而非自造**
esp_coredump 与 NuttX 都选了 **ELF core 格式**（`e_type = ET_CORE` + `PT_LOAD` 内存段 + `PT_NOTE` 元数据段），好处是直接用 `gdb` 和现成脚本分析，并有 CRC32/SHA256 校验。若我们的 panic 转存要存寄存器/栈，**不要自造二进制格式**；即使不完整兼容 ELF，也应保留"魔数 + 版本 + 校验和 + 可扩展 note 段"的结构。

**12. 多副本轮转元数据，而不是单点覆盖**
MCUboot 的教训（见应避免第 4 条）与 NVS 的页轮转共同指向同一个结论：**元数据（写指针、水位、已导出位置）不要单点原地反复写**。应做成"多副本 + 序号选新"，例如 N 个槽位轮流写，读取时选序号最大且 CRC 正确的那个。

### 应避免

**1. 不要"一个扇区出错就擦掉整个日志区"**
这是 EasyFlash LOG 的实测缺陷：`find_start_and_end_addr()` 遇到任何扇区状态序列非法、扇区头读不出、或 USING 扇区数不为 1，就打印 `"Error: Log area error! Now will clean all log area."` 并 `ef_log_clean()`。对**黑匣子语义这是灾难性的**——恰恰在最需要保留现场的时候把全部历史抹掉。我们必须反过来设计：**逐扇区/逐块隔离，坏块只丢自己，能读出的部分一律保留**，并且在导出时明确告知"这里有 N 块损坏"。这是本次调研里最直接、最该吸取的反面教训。

**2. 不要用"扫描 0xFF 猜记录边界"来确定日志尾部**
EasyFlash LOG 的写指针定位是 32 字节块扫描 + "尾部至少 4 字节 0xFF"的启发式，**没有记录级 CRC 或长度字段**。源码未声明任何合法性保证；单条记录写到一半掉电时的截断位置由该启发式决定。我们用显式 `len` 字段 + CRC，扫描时逐帧推进（帧头 CRC 不过就停在上一帧末尾），把"日志尾部在哪里"变成一个**可验证**的问题。

**3. 不要做"决策表式"的恢复逻辑（MCUboot 变砖模式）**
MCUboot 用 trailer 状态组合去匹配 `boot_swap_tables` 决策表。当掉电落在一个**决策表未覆盖的中间态组合**时（primary: magic=good/copy-done=set/image-ok=unset，secondary: magic=bad/copy-done=unset/image-ok=set），它既不回滚也不启动备用，直接把设备变砖（PR #2100 / issue #1966）。教训：**恢复逻辑要么是全序状态机且明确处理每个中间态，要么设计成"任何未知组合都有安全默认值"**；绝不能让"没匹配上"等价于"什么都不做，继续往下走"。

**4. 不要让元数据/Trailer 区成为没有磨损处理的单点**
MCUboot 的 trailer 反复原地写、且没有磨损均衡，是公认弱点；STM32U5 SBSFU 案例里还因为 flash 驱动没重定位 Bank 2 的页号，导致该扇区从未被擦除、trailer 写入直接被拒（`15 status write fails performing the swap`）。**任何"要反复改写的固定位置"都必须有轮转方案**，并且要有"写入被拒"的错误处理路径（不能假设写一定成功）。

**5. 不要在中断/panic 路径里直接写 flash**
三重证据一致：NuttX 明确要求 coredump 延后到"正常上下文"再写（`board_crashdump` 只暂存 RAM）；ESP-IDF 文档明确普通日志宏「should not be used from an interrupt」；`fdb_utils.c` 与 `ef_env.c` 都注释「some flash (like stm32 onchip) NOT supported repeated write before erase」——flash 写的时序约束比 RAM 严得多。这与我们"器件层 flash 写必须让出 CPU"的约束是同一条道理。**panic 钩子里只允许写 RAM 暂存区，禁止碰 flash。**

**6. 不要在日志输出回调里做阻塞文件 IO + fflush**
`esp_log_set_vprintf` 写 SPIFFS 导致其他任务栈溢出的官方 issue（#13205）就是活教材。同步落盘会拖垮实时性，而在回调里申请栈/做阻塞 IO 会直接引发故障。**独立日志线程 + 有界队列 + 批量落盘**是正确架构，且队列满时必须有明确的丢弃策略（且要可统计、可上报）。

**7. 不要指望 KV 库的默认"满了"行为等于黑匣子**
NVS 满了是 `ESP_ERR_NVS_NO_FREE_PAGES` **报错**，不覆盖最旧数据。如果直接拿 KV 语义的库当黑匣子用，会在日志写满的那一刻开始静默丢新日志（或报错），恰好丢失最需要的新数据。**"环形覆盖保新"必须显式实现并显式测试**（FlashDB TSDB 的 `rollover` 默认 true 是少数默认就对的选择，但它带时间戳单调性约束，见可借鉴第 6 条）。

**8. 不要把"文件系统"当成掉电安全的替代品**
NuttX 的文件通道要求文件系统**已挂载**，因此「cannot be supported during the boot-up phase」——启动早期与崩溃时刻都指望不上；esp_log 落盘同样要挂 FS 并 `fflush`。文件系统解决的是"组织与导出"，**不解决"掉电原子性"**；对黑匣子这种"必须在任意时刻可写、且写入点极可能伴随异常"的场景，直接在分区上做记录布局（如 NVS / esp_coredump 那样）更可控。

**9. 异步模式不要忘记 flush**
ulog 的官方文档直接承认：异步模式有缓冲、部分后端内部也有缓存，所以「hardfault and assertion」会丢日志。**没有 flush 原语的异步日志服务，在黑匣子场景下是不合格的。**

**10. 不要假设"写入一定成功"**
EasyFlash 的按写粒度写入、MCUboot 的 trailer 写失败、`_fdb_write_status` 只在必要时才写 flash——这些都指向同一个工程现实：**flash 写可能被拒、可能只能写一次、可能需要先擦除**。我们的日志后端必须检查每一次器件层写的返回值，并在失败时把该记录标记为无效而不是让环形结构继续往前走。

## 存疑与未证实

以下条目**未能由一手来源证实**，或存在来源冲突，**不应作为设计依据**：

1. **第三方博客给出的 FlashDB 扇区状态枚举与源码不符（判定为错误，已弃用）**
   某 CSDN 博客称 FlashDB 有 `SectorStatus_t`：`SECTOR_STATUS_UNUSED = 0xFFFFFFFF`、`PREPARE = 0xFFFFFF00`、`ACTIVE = 0xFFFF0000`、`FULL = 0xFF000000`、`GC = 0x00000000`，并称扇区头含 `erase_count`（擦除次数）。
   **与源码冲突**：`fdb_kvdb.c` / `fdb_def.h` 中实际是两组各 4 个状态——`fdb_sector_store_status`（`UNUSED`/`EMPTY`/`USING`/`FULL`）与 `fdb_sector_dirty_status`（`UNUSED`/`FALSE`/`TRUE`/`GC`），**且源码中未出现 `erase_count` 字段**。本报告一律以源码为准，该博客内容不采用。
   出处（不采信）：https://blog.csdn.net/u014727709/article/details/162606097

2. **FlashDB 官方在线文档未能抓取到正文**
   文档站 `http://armink.gitee.io/flashdb/#/zh-cn/` 与 `https://armink.github.io/FlashDB/#/zh-cn/design` 均为客户端渲染，抓取只得到 "Loading ..." 占位符，**未取得 "设计" 章节正文**。因此本报告中 FlashDB 的机制描述**全部来自源码**（`fdb_def.h` / `fdb_kvdb.c` / `fdb_tsdb.c` / `fdb_utils.c`），而非官方文档。

3. **NVS 页状态的数值位模式未从源码确认**
   文档只给出性质描述（"changing state is possible by writing 0 into some of the bits"），未给数值。源码中数值定义在 `nvs_constants.h` 的 `NVS_CONST_PAGE_STATE_*` 宏，但本次对 `v5.5.2` 与 `master` 两个 ref 的抓取均返回 **HTTP 404**（该文件路径可能已变更）。因此 **`ACTIVE`/`FULL`/`FREEING` 的具体数值（如是否形如 `0xFFFFFFFE`/`0xFFFFFFFC`/`0xFFFFFFF8`）未证实**；`nvs_page.hpp` 中只确认了 `INVALID = 0` 与内部位名 `PSB_INIT`/`PSB_FULL`/`PSB_FREEING`/`PSB_CORRUPT`。

4. **EasyFlash 源码注释自相矛盾处（以 `#define` 数值为准）**
   - `ef_log.c` 中枚举名有拼写错误：`SECTOR_STATUS_MAGIC_EMPUT`（源文件如此，应为 EMPTY）
   - `fdb_kvdb.c` 中扇区头字段的注释写 "magic word(`E`, `F`, `4`, `0`)"，但同一文件的 `#define SECTOR_MAGIC_WORD 0x30424446` 注释是 "magic word(`F`, `D`, `B`, `1`)" —— **注释互相矛盾**
   - `fdb_def.h` 中 `fdb_kv.magic` 字段注释写 "magic word(`K`, `V`, `4`, `0`)"，而 `KV_MAGIC_WORD = 0x3030564B` 按字节序是 "KV00" —— 同样不一致
   本报告在引用时以 `#define` 的十六进制数值为准。

5. **EasyFlash LOG 单条记录半写时的截断行为属机制推断，未经故障注入验证**
   `ef_log.c` 用「32 字节块扫描 + 尾部 ≥4 字节 0xFF」的启发式定位写指针，且**没有记录级 CRC / 长度字段**。据此推断"半写记录会被截断到该启发式判定的边界"，但**源码未声明任何保证，也无官方故障注入测试数据佐证**。这是推断，不是已证实的行为。

6. **ulog_easyflash 是否在 EasyFlash LOG 之上加了记录级帧头/CRC —— 未证实**
   本次未取得 `ulog_easyflash` 软件包源码，无法确认它是否补上了 EasyFlash LOG 缺失的记录级完整性校验。这直接影响"RT-Thread 生态的 ulog 落盘方案到底能不能识别半写记录"这个判断。

7. **RT-Thread 论坛帖未能直接抓取**
   `https://club.rt-thread.org/ask/question/d21ca483a5cfbca7.html` 本次抓取返回 **HTTP 468**，仅能通过搜索摘要引用其内容（"写 log 过程中掉电，上电后提示 Log area error 并清空全部日志"）。
   **缓解措施**：该行为已在 `ef_log.c` 源码中得到独立确认（存在 `"Error: Log area error! Now will clean all log area.\n"` 字符串与 `ef_log_clean()` 调用路径），故**机制结论成立**，但该帖的具体措辞、发帖人、官方是否回复等细节未证实。

8. **CmBacktrace 是否能写 Flash —— 未证实**
   仅得搜索摘要称其"可经串口或 Flash 输出"，**未抓取一手 README 或源码**。本报告未将其作为设计依据。

9. **cyclic-data-log 的掉电原子性与写位置恢复机制 —— 未证实**
   README 只声明"跨复位保留"与"满了覆盖最旧"，**未描述记录内部布局、校验和、复位后如何定位写入位置**。且其底层是 EEPROM 抽象（默认 4096 字节区间），与裸 flash 的擦除粒度约束不同，参考价值有限。

10. **SEGGER RTT 本身是否有 Flash 后端 —— 未证实**
    已确认 SystemView 官方技术页与 UM08027 只描述 target RAM 缓冲 + 主机侧文件保存，**未发现任何 target flash 持久化能力**。但 RTT 组件自身（独立于 SystemView）是否存在某种 flash 输出模式，本次未单独核查。

11. **NuttX "backup log" 机制的通用性 —— 未证实**
    `up_backuplog_alloc()` / `CONFIG_CXD56_BACKUPLOG` 目前只在 **CXD56xx 板级**代码中出现；该机制是否是通用 API、能否移植到 Cortex-M4 等其他架构，**未证实**。同样，RX65N 的 SBRAM 方案依赖该芯片特有的 standby RAM。

12. **EasyFlash 设计文档未覆盖 LOG 模块（来源缺口）**
    `docs/zh/design.md` 只写到 §3.2，**LOG 模块完全没有文档**（无日志区布局图、无扇区状态机说明、无掉电处理描述），且 ENV 节点结构仅以一张图 `ng_mode_data_structure` 引用，正文无字段级结构体。因此本报告中 **EasyFlash LOG 与 ENV 的全部结构描述均来自源码**，没有官方设计文档可交叉验证。

13. **esp_coredump 的 ELF 格式未逐字段核对**
    ELF/Binary 两种格式的段结构、note 命名（`CORE`/`NT_PRSTATUS`、`COREDUMP_MAGIC`）描述来自官方文档，**未逐字段比对 `components/espcoredump` 源码**；分区大小公式（20 + 12 字节）亦为文档值，未经实测。

14. **未展开的子项**
    - **RT-Thread `ulog` 的 syslog 模式细节**：仅知其存在（menuconfig "Enable syslog format log and API"），格式分 PRI/Header/TAG/Content 四部分，未深入。
    - **NuttX MTD 层本身**：本报告确认它是存储抽象而非日志通道，但未展开其坏块管理、擦除计数等能力——若我们的黑匣子要复用类似抽象层，需另行调研。
    - **ArduPilot / PX4 的日志实现**：按任务约定（"如 ArduPilot 之外的通用库"）未纳入本次范围。

---

*调研完成日期：2026-09-15。所有 URL 抓取日期同此。标注「未证实」的条目请勿作为设计依据。*
