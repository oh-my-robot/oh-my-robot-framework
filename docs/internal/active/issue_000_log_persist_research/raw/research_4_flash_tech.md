# NOR Flash 上的环形/追加式日志与小型记录存储 —— 跨框架横切调研

调研日期：2026-09-15（所有 URL 于该日访问）
范围：不绑定任何 RTOS；优先一手来源（器件手册、项目官方文档/源码、维护者原话）。
标注约定：**[推算]** = 由已证实数字推导，非实测；**[未证实]** = 仅见于二手来源或无法找到一手出处。

---

## 事实与数据

### A. 器件时序与寿命

#### A1. Winbond W25Q128（外置 SPI NOR，128 Mbit / 16 MB）

| 参数 | 值 | 出处 |
|---|---|---|
| 4 KB 扇区擦除 tSE | 典型 45 ms / 最大 400 ms | 现场报告引用手册数值：https://www.hotmolts.com/post/my-spi-flash-chip-erased-fine-at-25c-and-failed-ev-eb8c328e-8050-49ea-972b-ed9f530e713f |
| 4 KB 扇区擦除 tSE（部分 datasheet 版本） | 典型 100 ms / 最大 400 ms | W25Q128FVAIG AC 表：https://www.alldatasheet.jp/html-pdf/932082/WINBOND/W25Q128FVAIG/26529/87/W25Q128FVAIG.html |
| 页编程 tPP（256 B） | 0.7 ms 典型 / 3 ms 最大 | 同上（W25Q128FV AC 表） |
| 32 KB 块擦除 tBE1 | 120 ms 典型 / 1600 ms 最大 | 同上 |
| 64 KB 块擦除 tBE2 | 150 ms 典型 / 2000 ms 最大 | 同上 |
| 擦除/编程挂起 tSUS | 20 µs 最大（"/CS High to next Instruction after Suspend"，注明为设计/表征保证值，非量产 100% 测试） | 同上 |
| 擦写寿命 | 100,000 次/扇区（最小） | https://www.datasheets.com/winbond/W25Q128JVS ；https://www.lcsc.com/product-detail/nor-flash_winbond-elec-w25q128jvsiq_C97521.html |
| 数据保持 | > 20 年 | 同上 |
| 接口带宽 | 133 MHz，Quad 等效 532 MHz，连续传输 66 MB/s；Continuous Read 8/16/32/64 字节 wrap，最少 8 个时钟寻址 | https://www.kynix.com/components/w25q128jvsiq-spi-flash-memory-datasheet-footprint-specification.html |

温度影响的现场实测（非厂商数据，但是少见的"典型值 vs 实测"对照）：W25Q128 数据记录仪
- 常温（~25 °C）扇区擦除稳定 ~50 ms；
- 机箱内温度 > 60 °C 时超过 150 ms，**有时到 300 ms**；
- 固件用 WIP 轮询 + 100 ms 超时 → 超时后误判擦除完成并写入**半擦除扇区**，因为 NOR 只能 1→0，结果新旧位混杂，表现为文件系统损坏；
- 作者结论：datasheet 的 typical "是 25 °C 的市场数字"，超时应按 max（400 ms）设计并再留一倍余量，且超时必须走错误路径而不是忽略。
出处：https://www.hotmolts.com/post/my-spi-flash-chip-erased-fine-at-25c-and-failed-ev-eb8c328e-8050-49ea-972b-ed9f530e713f

#### A2. Micron MT25QL256（另一主流外置 SPI NOR，用于交叉验证量级）

- 256 B 页编程：**0.5 ms 典型 / 5 ms 最大**
- 64 KB 扇区擦除：**0.7 s 典型 / 3 s 最大**
- 4 KB 子扇区擦除吞吐：**80 KB/s**（换算 ≈ 50 ms/4 KB）**[推算]**
- 编程吞吐 2 MB/s；擦写寿命 **100,000 次/扇区**；数据保持 20 年（典型）
出处：https://digilent.com/reference/pmod/pmodsf3/reference-manual （Pmod SF3 引用 MT25QL256ABA 手册值）；https://www.alldatasheet.co.nz/datasheet-pdf/pdf/1567078/MICRON/MT25QL01GBBB8ESF-0SIT.html

> 结论：外置 SPI NOR 的 **4 KB 扇区擦除在几十 ms 量级（典型 45–100 ms，最大 400 ms）**，**页编程在 0.5–0.7 ms 量级（最大 3–5 ms）**。两个厂商一致。

#### A3. 片内 NOR（Cortex-M 级 MCU）—— 与外置差一个数量级

| 器件 | 擦除 | 编程 | 出处 |
|---|---|---|---|
| STM32L4 | 2 KB 页擦除 **22 ms 典型** | 64 bit 双字 **82 µs 典型**；整 2 KB 页 20.9 ms(标准)/15.3 ms(快速) | https://www.st.com/resource/en/product_training/STM32L4_Memory_Flash.pdf |
| STM32L4（Zephyr 驱动取的超时值） | 最大 24.47 ms / 2 KB 扇区（驱动超时 25 ms） | — | https://github.com/zephyrproject-rtos/zephyr/blob/caa06c9c48b9f32c80e63ddf3ac1332aef74d35f/drivers/flash/flash_stm32.c |
| STM32F4 | 128 KB 扇区擦除：x8 时 2 s 典型 / **4 s 最大**；x16/x32 时 1.3 s/2.6 s | — | STM32F405/407 datasheet DocID022152（alldatasheet 镜像：https://www.alldatasheet.es/html-pdf/510595/STMICROELECTRONICS/STM32F407VE/205164/105/STM32F407VE.html ）；Zephyr 驱动注释 "STM32F4: maximum erase time of 4s for a 128K sector" |
| STM32 片内 flash 寿命 | **10,000 次擦除**（全温区，-40~+105/125 °C，多份 datasheet 一致） | — | STM32F103/F383/G071/H725 datasheet Table（alldatasheet 镜像）；https://www.edaboard.com/threads/write-cycles-in-stm32-internal-flash-memory.285483/ |
| nRF52840 | tERASEPAGE **最大 85 ms**（4 KB 页）；tERASEALL 最大 169 ms | tWRITE **最大 ~41–42.5 µs / 32 bit 字** | https://devzone.nordicsemi.com/f/nordic-q-a/124564/what-s-the-typical-flash-erase-and-write-time-on-nrf52840 |
| nRF52832 | tERASEPAGE **典型 2.05 ms / 最大 89.7 ms**（44 倍差） | tWRITE 典型 67.5 µs / 最大 338 µs | https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.nrf52832.ps.v1.1/nvmc.html |
| nRF51822 | 1 KB 页约可擦 **20,000 次**（Zephyr NVS 文档引用） | — | https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html |

关键补充事实：
- **Nordic 只规定最大值、不规定典型值**："We only specify maximum flash erase/write times in the nRF52840 datasheet, not typical values... In practice, most devices perform faster than the max spec."；**写/擦期间 NVMC 会停住 CPU**，一次页擦除可让从 flash 取指的代码停转最长 ~85 ms（有 ERASEPAGEPARTIAL 可切碎，但不适用于 UICR）。出处同上 DevZone。
- nRF52832 的 **typ 2.05 ms vs max 89.7 ms** 是"绝不能按典型值设计"的最有力单点证据。

#### A4. 擦除/编程中途掉电的物理后果（一手厂商文档）

- **Renesas KB**：掉电中断 NOR 的擦除或编程，被擦/写区域的数据可能变得不稳定并引发数据错误。
  https://en-support.renesas.com/knowledgeBase/21126034
- **ST AN4429 / NXP AN4521（C90FL 闪存）**：brownout 打断擦除序列后，块处于**取决于被打断在哪一步的非确定状态**：
  - 若在 program 步之后掉电 → 多页（含 ECC 位）仍为已编程态，全 0 不是合法 ECC 码字 → **不可纠正 ECC 错**；通常重新擦除即可恢复；
  - 若在 erase 步之后、compaction/软件编程步之前掉电 → 单元**过擦除（depleted）**，列漏电会抑制漏偏置，导致下一次擦除的起始 program 步失败，表现为"**这个块擦不掉了**"，需 `FlashDepletionRecover` 才能恢复。
  https://www.st.com.cn/resource/en/application_note/dm00103274-rpc56xx-and-spc56xx-c90fl-flash-recovery-in-case-of-brownout-during-flash-erase-operation-stmicroelectronics.pdf
- **Infineon KBA221246**：SPI NOR 的主阵列与"隐藏扇区"（OTP/寄存器位）的编程/擦除都可能被掉电打断；隐藏扇区**无法**用主阵列那套恢复手段恢复。
  https://community.infineon.com/gfawx74859/attachments/gfawx74859/nor-flash/3359/2/KBA_221246_WRR.pdf
- **S25FL-L 系列**：提供 **E_ERR（擦除错误）/ P_ERR（编程错误）** 状态位与 CLSR 清除命令；出错时 **WEL 位保持置位**以防误写。
  https://patchwork.ozlabs.org/project/linux-mtd/patch/20210301142844.1089385-2-yaliang.wang@windriver.com/
- **S25FL064L 数据损坏社区案例**："data in flash can get corrupted if a sudden power down happens during program or erase operations"。
  https://community.infineon.com/t5/Nor-Flash/S25FL064L-Data-Corruption/m-p/230198
- **DAC 2011 论文《Understanding the impact of power loss on flash memory》**（学术，非厂商）：掉电的影响非直觉 —— 掉电前写入时间更长并不必然降低错误率；**一次编程中途掉电可能破坏此前已成功编程的数据**；被打断的编程更易受读干扰与长期衰减影响；不完整的擦除使后续对该块的编程不可靠。
  https://dl.acm.org/doi/abs/10.1145/2024724.2024733

#### A5. 坏块管理：NOR 不需要

- **Infineon 官方社区答复**："there are no bad blocks to manage in any NOR flash"；"the system does not need to provide ECC to correct bit errors for any NOR flash"。
  https://community.infineon.com/t5/Nor-Flash/Does-NOR-Flash-need-Bad-Block-Management-Why/td-p/360001
- **Microchip（SST FAQ）**：出厂测试并"保证 100% 的 Flash 块非缺陷"；"**Bad block management is not done for NOR Flash memory because it is not required** since it is a very reliable memory"；NOR "在 datasheet 规定的最大写次数内不会磨损坏"。
  https://support.microchip.com/s/article/Memory---SST-FAQs
- 对比：littlefs 提供"坏块检测与绕行"（README），DESIGN.md 说明写失败时会"evict the bad block, allocate a new, hopefully good block, and repeat the write"；littlefs **自身不提供 ECC**。
  https://raw.githubusercontent.com/littlefs-project/littlefs/master/DESIGN.md
- 结论：片内 NOR 与外置 SPI NOR 都**不需要** NAND 式坏块表/ECC 管理；但仍必须处理"擦除/编程返回失败"这一异常路径（A4 已证明它会真实发生）。

### B. 记录布局经典手法

#### B1. Zephyr FCB（Flash Circular Buffer）—— 最贴近"环形日志"的官方组件

- **扇区头**写在扇区偏移 0，结构 `struct fcb_disk_area { fd_magic; fd_ver; fd_id; }`，即 **magic + 版本 + 单调序号（sequence id）**；magic 与擦除值（0xFF）的取反异或后存储，便于用"magic == 擦除值"判定"扇区未使用"。
- 源码注释：`fcb_sector_hdr_init` = "Initialize erased sector for use."；`fcb_sector_hdr_read` = "Checks whether FCB sector contains data or not."，**返回 0 = 扇区未用，返回 1 = 有数据**。
- `fcb_init` 扫描各扇区头的 `fd_id`，用 `FCB_ID_GT` 比较找出最旧/最新扇区。
  出处：https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/subsys/fs/fcb/fcb.c
- **记录结构**：官方文档 "Entries in the flash contain the length of the entry, the data within the entry, and checksum over the entry contents."；`fcb_append_finish()` "completes the writing of the entry by calculating the checksum."；遍历时 "It will skip over entries which don't have a valid checksum."
  https://docs.zephyrproject.org/latest/services/storage/fcb/fcb.html
- **记录不跨扇区（硬约束）**：`fcb_flash_read/write` 中 `if (off + len > sector->fs_size) return -EINVAL`；`#define FCB_MAX_LEN (0x3fffu)`，注释 `/**< Max length of element (16,383) */`。头文件本身**没有**写明理由。
  https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/include/zephyr/fs/fcb.h
- **回收**：`fcb_rotate()` — "Function erases the data from oldest sector. Upon that the next sector becomes the oldest."，并"Active sector is also switched if needed."
  https://docs.zephyrproject.org/latest/doxygen/html/group__fcb__api.html
- 掉电语义：勾选（finish）前的条目校验和不正确 → 遍历跳过 → 等价于"这次 append 丢失"。

#### B2. "记录不跨扇区"的**理由与代价**（一手出处）

- Pigweed `pw_kvs` 把这条写成显式不变量：**"An entry must be fully contained within a single sector."**
  理由原文：这是"**a direct consequence of the physical constraints of flash hardware, which can only be erased in fixed-size blocks (sectors). If an entry spanned two sectors, erasing one would corrupt the entry.**"
  配套事实：条目从不原地更新；GC 回收陈旧条目时**必须始终保证至少一个空闲已擦扇区**；同一 key 的多份拷贝按位相同并刻意放在不同扇区以对抗扇区级损坏。
  https://pigweed.dev/pw_kvs/disk_format.html
- 代价（**[推算]**，由上述约束直接推出）：
  1. **单条记录长度上限 = 扇区大小 − 头部开销**（FCB 取 16383 B）。对 4 KB 扇区，记录最大约 4 KB；对我们"一行文本几十~几百字节"完全够用。
  2. **扇区尾部会浪费**：变长记录若剩余空间放不下下一条，要么留空，要么写"填充/结束标记"记录（FCB 为此提供 `CONFIG_FCB_ALLOW_FIXED_ENDMARKER`）。
  3. 换来的是：**擦除一个扇区永远不会毁掉半条存活记录**；恢复扫描可以"按扇区推进 + 按记录 CRC 断链"，逻辑简单且可证明。

#### B3. Zephyr NVS —— 双向生长 + "先数据后元数据"

- 记录 = ATE（Allocation Table Entry，8 B 元数据）+ 数据；**元数据表从扇区末尾向前生长，数据从扇区头向后生长**。
- ATE 字段（源码）：`struct nvs_ate { uint16_t id; uint16_t offset; uint16_t len; uint8_t part; uint8_t crc8; }`；`part` 在"无擦除器件"支持中被复用为 `cycle_cnt`。
- 元数据 CRC "**only calculated over the metadata and only ensures that a write has been completed**"，可选 CRC-8（`CONFIG_NVS_ATE_CRC8`）或 CRC-24；数据可选 **CRC-32**（`CONFIG_NVS_DATA_CRC`），且"**The data CRC is not checked for a partial read, as it is stored at the end of the element data area.**"
- **写顺序：先数据，后元数据** —— "A write of data to nvs always starts with writing the data, followed by a write of the metadata."；因此上电时"没有元数据的数据被忽略"。
- 扇区数 `NVS_SECTOR_COUNT` "is at least 2, one sector is always kept empty to allow copying of existing data"。
- 特殊 ATE：close ATE（id=0xFFFF、len=0、指向最后写入的 ATE，标记扇区已关闭）、gc ATE、delete ATE；扇区状态：open / closed / write / empty。
- 防抖保护："NVS has a protection mechanism to avoid getting in a endless loop of flash page erases when there is limited free space."，此时直接报无空间。
  出处：https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html ；https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/dae79cefaabf63086946a48ccca4094f26f146c8/subsys/fs/nvs/nvs.c ；ATE 与扇区状态细节：http://www.zephyrproject.cn/develop/subsys/storage/nvs_analyze.html

#### B4. ESP-IDF NVS —— 状态位图 + "只写 0"的状态机

- 页（4 KB）= **页头（32 B：State 4 | Seq 4 | version 1 | Unused 19 | CRC32 4）+ 条目状态位图（32 B）+ 条目（32 B/条，126 条）**。条目 32 B 是为了兼容 flash 加密。
- 位图 **2 bit/条**：`Empty 2'b11`（未写，全 0xFF）→ `Written 2'b10` → `Erased 2'b00`（丢弃，不再解析）。
- 关键设计："Page state values are defined in such a way that **changing state is possible by writing 0 into some of the bits**." 因此改状态**不需要擦页**。页状态 Empty `0xFFFFFFFF` / Active `0xFFFFFFFE` / Full `0xFFFFFFFC` / Erasing `0xFFFFFFF8` / Corrupted `0x00000000`。
- 页头 CRC32 覆盖不含 state 的字节（4–28）；version 从 0xFF 每升级一次减一。
- **掉电恢复机制**：`mLoadEntryTable()` 中，若"数据已写入但状态位图尚未修改"（掉电在两者之间），通过读 EntryHeader 首字（**!= 0xFFFFFFFF**）检出该条目已被写过；条目 CRC32 不匹配则 `alterEntryState(...)` 丢弃。
- 官方韧性声明："one should be able to power off the device at any point and time and then power it back on. This should not result in loss of data, except for the new key-value pair if it was being written at the moment of powering off."
- 页状态 Erasing 的恢复："In case of a sudden power off, the move-and-erase process will be completed upon the next power-on."
  出处：https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html ；https://github.com/espressif/esp-idf/blob/v6.0.1/docs/en/api-reference/storage/nvs_flash.rst ；https://raw.githubusercontent.com/espressif/esp-idf/v5.5.2/components/nvs_flash/src/nvs_page.cpp

#### B5. MCUboot —— "魔术字最后写"的原子提交

- image trailer 布局：Swap status（每扇区一条记录，按 flash 最小写尺寸对齐，factor 3 对应一次交换的三次数据搬移）| 可选密钥 | Swap size（4 B）| Swap info（1 B，bit0–3 = swap type）| Copy done（1 B）| Image OK（1 B）| **MAGIC（16 B）**。
- `boot_magic_decode()` 返回 `BOOT_MAGIC_GOOD`（完全匹配）/ `BOOT_MAGIC_BAD`（存在但损坏）/ `BOOT_MAGIC_UNSET`（全 0xFF，即已擦除）。
- **提交点最后写**："the commit point (the bootmagic) is written atomically and last"；掉电后 bootloader 第一步就是"Inspect swap status region; is an interrupted swap being resumed? Yes: Complete the partial swap operation"。
- 交换开始前**必须先把 trailer 擦干净**（否则写入被硬件拒绝）；STM32U5 SBSFU 的 `"15 status write fails performing the swap"` 正是"忘了擦 trailer 所在扇区"导致的实战故障。
- trailer 会占用 slot 空间："for 128 slot sectors with a 4-byte alignment, it would become 1536 B"。
  出处：https://raw.githubusercontent.com/mcu-tools/mcuboot/master/docs/design.md ；https://github.com/mcu-tools/website-mcuboot/blob/5b8a4449b0e6207d1f9a3f88243f39ebae866c5c/_documentation/design.md ；https://community.st.com/stm32-mcus-security-36/stm32u5-sbsfu-wrn-15-status-write-fails-performing-the-swap-using-custom-flash-layout-163346

#### B6. LittleFS —— 双份 + CRC + revision 的元数据对

- "Metadata pairs are the backbone of littlefs." — "These are small, two block logs that allow atomic updates anywhere in the filesystem."
- "**Atomicity (a type of power-loss resilience) requires two parts: redundancy and error detection.**"
- 校验方式："Error detection can be provided with a checksum, and in littlefs's case we use a 32-bit CRC."；"**Note that littlefs doesn't maintain a checksum for each entry.**"；"What we can do instead is group multiple entries into a commit that shares a single checksum."
- 提交点："It's only when the commit's checksum is written" 之后，"the compacted entries and revision count become committed and readable"；新旧用 revision count + **序列算术**比较。
- 掉电语义："we still have the original entries if we lose power during the append."；"if we lose power we still have everything in our original block."
- **代价**："in the worst case a small log costs **4x** the size of the original data."；"each metadata entry has an effective storage cost of 4x the original size."
- 额外机制（SPEC.md）：32 位 tag 中的 **valid bit** + 可选 **FCRC（forward CRC，上一个 commit 对"下一段 commit 区域被擦除时"的校验）**，用于区分钟"下一个 commit 区域是被擦过还是写了一半就掉电"；全局状态以 XOR 差值分散在各 metadata pair 中，使跨目录操作（如 move）也原子。
- littlefs **does not provide ECC**；所有块都失败时返回 "out of space"。
  出处：https://raw.githubusercontent.com/littlefs-project/littlefs/master/DESIGN.md ；https://raw.githubusercontent.com/littlefs-project/littlefs/v2.9.1/SPEC.md

#### B7. FlashDB TSDB / EasyFlash —— 状态表 + 索引数据对向生长

- 每个扇区以 `struct sector_hdr_data` 开头，含 magic 与状态表；TSDB 的 magic 是 `0x304C5354`（'T','S','L','0'），并记录时间范围（首个 start 节点时间戳、最后 end 节点时间戳/索引、`end_info[2]`）。
- KV 头 `struct kv_hdr_data` 含 **crc32 + status_table**；TSDB 日志节点 `struct log_idx_data` 含 status、timestamp、length、address。所有头/数据按 `FDB_WRITE_GRAN` 对齐。
- 共享的 **"Status Tables" 机制**用于在不每次擦整个扇区的前提下跟踪数据生命周期，并提供掉电安全。
- 存储布局：**TSL 索引从扇区头向尾生长，TSL 数据从扇区尾向头生长**；定义 `FDB_TSDB_FIXED_BLOB_SIZE` 时可用数学方式直接算出日志地址以省空间。
- 追加路径 `fdb_tsl_append()`：检查当前扇区空间 → 不够则 `format_sector` 格式化新扇区 → 写 TSL 索引与数据；TSL 状态由状态机管理。
  出处：https://deepwiki.com/armink/FlashDB/2.2-time-series-database-(tsdb) ；https://deepwiki.com/armink/FlashDB/2-architecture ；https://gitcode.com/smartdao/FlashDB/blob/master/src/fdb_tsdb.c

#### B8. 裸环形日志的参考实现（与我们目标最接近，含设计取舍讨论）

参考实现（W25Q64，64 KB 日志区，4 KB 扇区）：
- 区域头 8 B 存持久 `write_ptr`（最新）/ `read_ptr`（最旧）；
- 记录 = 4 B 时间戳 + 2 B `data_len` + 2 B `crc16` + 载荷，单条最大 128 B；CRC16 用种子 0xFFFF、反射多项式 0xA001，覆盖除 CRC 字段外的整条记录；
- **写顺序刻意固定：先提交完整数据包，最后写指针**。原文效果：数据中途掉电由 CRC 拦截丢弃；指针中途掉电则旧指针仍有效，新字节下次被覆盖；
- 回绕：`write_ptr` 回到区域起点，`read_ptr` 前进一个扇区，并擦除目标扇区；
- **擦除提前量（erase-ahead）**："若当前扇区即将写满，系统会提前触发对下一扇区的擦除操作"；并建议"懒擦除"/空闲主循环后台预擦，避免长擦除卡在实时写入路径上；
- RAM 环形缓冲批量刷写；指针更新与 flash 写期间短暂关中断；初始化时若指针越界或数据异常则格式化日志区并重置指针（黑匣子可接受的降级）。
  出处：https://www.21ic.com/a/1007742.html

#### B9. 追加写与"写 1→0"的原子性边界

- 物理前提（littlefs DESIGN.md 原文）："writing to flash requires two operations: erasing and programming."；"**Programming (setting bits to 0) is relatively cheap and can be very granular.**"；"Erasing however (setting bits to 1), requires an expensive and destructive operation which gives flash its name."；"Writing to flash is destructive."
  https://raw.githubusercontent.com/littlefs-project/littlefs/master/DESIGN.md
- **边界**：单次页编程**不是**相对掉电的原子操作 —— 它可能留下部分已编程、部分仍为 1 的字节（A4 的厂商/学术来源均证实"掉电会破坏正在写的数据，甚至破坏此前已写好的数据"）。因此"靠硬件保证只写 1→0"**不足以**得到原子记录。
- **工程结论**：原子性必须由"**提交标记最后写 + 校验**"来构造，业界的四种等价做法：
  1. 记录校验和最后写（FCB 的 `fcb_append_finish`）；
  2. 元数据/长度最后写（Zephyr NVS 先数据后元数据）；
  3. 状态位最后写（ESP-IDF NVS 位图；FlashDB 状态表）；
  4. 魔术字最后写（MCUboot trailer；littlefs commit CRC）。

### C. 恢复扫描

#### C1. 扫描的通用算法（三个独立来源一致）

1. 读区域头/扇区头 → magic 校验（FCB：magic == 擦除值 → 扇区未使用；MCUboot：magic 全 0xFF → UNSET）；
2. 用扇区单调序号（FCB `fd_id`）或页 seq（ESP-IDF NVS）排序，确定最旧/最新；
3. 从最旧扇区开始按记录长度推进，逐条算 CRC；**CRC 断链即停/丢弃该条**（FCB："It will skip over entries which don't have a valid checksum."）；
4. 遇到**全 0xFF 的记录头判空**，此处即写游标；
5. 没有元数据的数据被忽略（Zephyr NVS）；半写记录被丢弃（ESP-IDF NVS 靠首字 != 0xFFFFFFFF 检出）。

#### C2. 扫描耗时与分区大小的关系

- 朴素做法是 **O(分区字节数)**：必须顺序读完整个分区才能重建游标。**[推算]** 按外置 SPI NOR 有效读 10 MB/s 估：64 KB → ~6.5 ms，1 MB → ~105 ms，4 MB → ~420 ms；按片内 flash 有效读 20 MB/s 估：64 KB → ~3 ms，1 MB → ~50 ms。（有效读速率因命令开销/等待周期而异，未找到通用一手数据，**速率假设为粗估**）
- **更快的办法（扇区头索引）**：只读每扇区头（8 B 级）→ O(扇区数 × 头长)，定位最新扇区后**只全扫该扇区** → O(1 个扇区)。64 KB/4 KB = 16 个扇区，头读取仅 ~128 B。FCB 的 `fcb_sector_hdr_read` 与 NVS 的扇区扫描正是这个抓手。
- **跳查表/索引的一致性风险**：
  - 扇区头有效不等于记录区完好 → 仍必须逐条 CRC 兜底，"跳过扇区头就跳过整扇区"会放过坏数据；
  - "最新扇区头已写、记录尚未写"的窗口（扇区头是扇区被激活时第一个写的）→ 必须能回退到前一扇区；
  - 一旦在 flash 上维护"最新位置索引/检查点"，这个索引本身又成了需要原子提交的元数据（回到 B9 的问题）——**这是"用扫描换一致性"的根本权衡**。
- **实测的"线性查找"代价**（真实数据，非推算）：Zephyr settings/NVS 的 issue #45591 记录：约 30 个 key、启动读 ~100 个 key、每次启动写 5 个小计数器，第 20 次重启时测得
  - settings 加载 98 次、平均 **32056 µs/次**；
  - 键查找的 NVS 读 1692 次、平均 **943 µs/次**；值探测 1692 次、平均 836 µs；值读 1563 次、平均 74 µs；
  - 单次启动共 **735,614 次 ATE 读**；启动时间从 2340 ms 涨到 3789 ms。
  - 官方结论句："**Currently, reading an NVS value takes averagely linear time with respect to the number of ATEs.**"
  https://github.com/zephyrproject-rtos/zephyr/issues/45591
- littlefs 的另一类"扫描"代价（维护者原话）：块分配/GC 扫描是 **O(n²)**（n = 文件系统块数），"**It's technically related to total filesystem size, which is arguably worse.**"
  https://github.com/littlefs-project/littlefs/issues/1054

### D. 掉电窗口分析

| 窗口 | 物理后果 | 业界处置 |
|---|---|---|
| **擦除中途掉电** | 被擦扇区内容**非确定**（取决于停在哪一步）：C90FL 会出现不可纠 ECC 错，或过擦除导致后续编程失败（"擦不掉的块"）；SPI NOR 主阵列一般可"重擦"恢复，但**隐藏扇区（OTR/寄存器）无法恢复**。掉电前后的"擦除成功"判断本身不可靠 | 上电时对可疑扇区无条件重擦（ST/NXP AN4429/AN4521）；SPI NOR 用 E_ERR/P_ERR 状态位 + CLSR，且出错时 WEL 保持置位（S25FL-L）；**擦除超时按 datasheet max 设计**（Zephyr STM32 驱动 4 s/25 ms 超时；W25Q128 现场报告 100 ms 超时导致半擦除写入） |
| **写记录中途掉电** | 只能把 1 写成 0，未写完的字节仍是 0xFF 或部分为 0 → 记录内容撕裂；且可能波及此前已写好的数据（DAC 2011） | 记录级 CRC + **提交标记最后写**：FCB 在校验和写完前遍历会跳过；Zephyr NVS 先数据后元数据（"没有元数据的数据被忽略"）；ESP-IDF NVS 数据先写、状态位图后写，上电用首字检出未标记条目 |
| **元数据提交中途掉电** | 指针/魔术字可能半写 | 魔术字最后写并整体校验（MCUboot，`BOOT_MAGIC_UNSET` = 全 0xFF）；**双份 + revision + CRC**（littlefs metadata pair："if we lose power during the append, we still have the original entries"）；**只写 0 的状态迁移**（ESP-IDF NVS 页状态可从 Active 0xFFFFFFFE 直接降到 Corrupted 0x00000000，无需擦除）；FlashDB 状态表 |

**参考语义对照（官方原文）**
- littlefs README："designed to handle random power failures"；每个文件操作有 "strong copy-on-write guarantees"；掉电时 "the filesystem will fall back to the last known good state"；文件改动"直到 sync 或 close 才提交"。
  https://raw.githubusercontent.com/littlefs-project/littlefs/master/README.md
- **SPIFFS 的已知弱点（ESP-IDF 官方文档原文）**：
  - "**When the chip experiences a power loss during a file system operation it could result in SPIFFS corruption.**"（恢复靠 `esp_spiffs_check`）
  - "It is not a real-time stack. One write operation might take much longer than another."
  - "SPIFFS is able to reliably utilize only around **75%** of assigned partition space."
  - 空间不足时可能触发反复 GC 扫描，"每次调用持续数秒"；不检测也不处理坏块；不支持目录。
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/spiffs.html
- SPIFFS 内部一致性模型（源码）：页头含 object id / span index / flags；`spiffs_check_luconsistency` / `spiffs_check_pgconsistency`（每页需 4 bit 工作内存，"the working memory might not fit all pages so several scans might be needed"）/ `spiffs_check_objidconsistency`；某些损坏只能删除（`index bad %i, cannot mend!`）。挂载时 `spiffs_obj_lu_scan` **允许一个未擦块**（"might be powered down during an erase"），多于一个 magic 缺失的块则挂载失败。
  https://github.com/DimmKirr/spiffy/blob/140f1a3d10a3a70dd524e83448a9b9bd47d5545e/src/spiffs_check.c ；https://nuttx-forge.org/nuttx/nuttx-mirror/raw/commit/b5507ea9a243400072396966448237a0e667f6d3/fs/spiffs/src/spiffs_check.h
- SPIFFS 已不再维护；Espressif 官方对比表把掉电韧性标为：FatFS "最大弱点"、SPIFFS "**Partial**"、LittleFS "**Yes (integrated)**"、"designed as fail-safe"。
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/file-system-considerations.html

### E. 磨损与寿命

- **量级**：外置 SPI NOR **10^5** 次/扇区（W25Q128JV、MT25QL 均为 100,000 次最小，20 年保持）；片内 flash **10^4** 次（STM32 全系列 10 kcycles；nRF51822 页约 20,000）。**两者差一个数量级**，同一套日志逻辑在片内更容易磨穿。
  出处见 A1/A2/A3。
- **顺序环形轮转为什么天然均匀**：回绕一次，每个扇区恰好被擦一次；擦除计数在所有扇区上均等，**无需任何动态磨损均衡算法**。"每次回绕每扇区擦一次，因此擦除计数均匀分布；扇区越多磨损均衡越好，且不需要 compaction"。
  https://github.com/ouyanglingle/simple-kvdb/blob/master/README.md ；https://stackoverflow.com/questions/43020407 (Flash Memory Management) ；https://my.oschina.net/emacs_7986337/blog/19547948
- 与"就地更新热点"的对比：原地反复写同一地址会迅速耗尽寿命 —— 这是追加式环形的根本动机；littlefs 额外提供**动态**磨损均衡（"a statistical wear leveling algorithm"，每次挂载从随机偏移启动分配器）与 `block_cycles`（建议 100–1000 次擦除后搬移元数据块）；littlefs DESIGN.md 明确其磨损均衡只覆盖 **dynamic blocks**（静态/冷数据不搬）。
- **坏块/坏扇区要不要管**：NOR 不需要（A5）。但要区分"**坏块**"（不需要管理）与"**单次擦除/编程失败**"（必须处理，见 A4）。
- **寿命预算公式（Zephyr NVS 官方文档原文）**：
  `SECTOR_COUNT * SECTOR_SIZE * PAGE_ERASES / (NS * (DS+8))` **分钟**，
  其中 NS = 每分钟存储请求数，DS = 数据字节数，PAGE_ERASES = 页可擦次数；结果不理想时"增加 SECTOR_COUNT 或 SECTOR_SIZE"。
  官方算例：nRF51822（页 1024 B，约 20,000 次擦除），每次状态写占 12 B（8 B 元数据 + 4 B 数据），"the device should last about 171 * 20,000 minutes, or about 6.5 years"。
  https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
- **等价简化形式 [推算]**：因为"每扇区每轮转擦一次"，总可写记录数 = (分区字节数 ÷ 每条记录占用字节) × 擦写寿命，**与扇区大小无关**（扇区大小只影响擦除延迟与 RAM 缓冲需求）。

### F. 实测数据汇总（含测试条件）

| 测量项 | 数值 | 测试条件 | 出处 |
|---|---|---|---|
| FlashDB TSDB 追加（外置 NOR） | **250 TSL/s，4.00 ms/条** | W25Q64 SPI NOR | https://raw.githubusercontent.com/armink/FlashDB/master/README.md |
| FlashDB TSDB 查询（外置 NOR） | 平均 **1.77 ms/次** | 同上 | 同上 |
| FlashDB TSDB 追加（片内） | **2684 TSL/s，0.37 ms/条** | STM32F2 片内 flash | 同上 |
| FlashDB TSDB 查询（片内） | 平均 **0.11 ms/次** | 同上 | 同上 |
| FlashDB 代码占用 | `fdb_tsdb.o` ro code 1160 B / ro data 236 B；`fdb_kvdb.o` ro code 4584 B | STM32F4，IAR 8.20 | 同上 |
| W25Q128 扇区擦除（常温） | ~50 ms，每次稳定 | 现场数据记录仪 | hotmolts 报告 |
| W25Q128 扇区擦除（>60 °C） | >150 ms，有时 300 ms | 同上 | 同上 |
| W25Q128 扇区擦除 datasheet | 45 ms typ / 400 ms max（部分版本 100/400） | 25 °C | A1 |
| 片内页擦除（nRF52832） | typ **2.05 ms** / max **89.7 ms** | datasheet，per page | A3 |
| Zephyr NVS 单次读（键查找） | 平均 943 µs | 30 个 key、大量小写入、第 20 次重启 | Zephyr #45591 |
| Zephyr NVS 单次写读（值） | 平均 74 µs | 同上 | 同上 |
| littlefs 追加吞吐劣化 | 318 kB/s → **90 kB/s** | W25N01GV（2 Gbit，2 KB 页，128 KB 块），lookahead 128，每块 sync 一次；作者确认应用层**没有**发起任何读，却观测到 ~274 页/s 的读（GC 扫描） | littlefs #1054 |
| littlefs sync 耗时 | ~11 ms | 同上 | 同上 |
| littlefs 小文件 open/write/close | 300 ms – 2 s（~70–150 B） | PJRC 论坛，QSPI flash | https://forum.pjrc.com/index.php?threads/littlefs-performance-issue-for-qspi-flash-from-1-54-beta10-to-1-56.70467/page-2#post-308885 |
| littlefs 挂载 | 1600 ms | ESP32，分区大小未说明 —— **未必可外推** | https://forum.arduino.ru/t/fajlovuyu-sistemu-zapilil-na-esp32/19644/113 |
| Write amplification | littlefs 1.3x vs SPIFFS 2.8x | 中文对比测试（二手，**未证实**） | https://www.eepw.com.cn/zhuanlan/202409/345275.html |
| SPIFFS 修复耗时 | 510 s（分区 ~4896 KB） | 中文对比测试（二手，**未证实**） | https://www.21ic.com/a/1002093.html |
| SPIFFS 掉电后挂载成功率 | 10 次随机掉电中 4/10 成功挂载 | 二手（**未证实**） | https://wenku.csdn.net/column/3anc1qgjy5 |

**设计可用的三个关键量**
1. **一次擦除期间能攒多少日志** = 日志速率 × 擦除最坏时长。外置 W25Q128 取最坏 400 ms：1 条/秒 → 0.4 条；10 条/秒 → 4 条；100 条/秒 → 40 条（80 B/条 → 3.2 KB RAM 缓冲）。片内 nRF52840 取 85 ms 上限或 STM32F4 取 1.3–4 s（后者在单 bank 下等于 CPU 停转数秒）。
2. **"写满一扇区"与"擦一扇区"同量级**：4 KB 扇区装 51 条 80 B 记录，写它需 51 × 0.7 ms ≈ 36 ms，而擦除典型 45 ms。**所以 erase-ahead 必须提前约"一整个扇区的写入时间"启动**，只提前几条记录是不够的。
3. **擦写错峰的可行手段**（按代价从低到高）：
   - 外置 SPI NOR 的擦除**是器件自持的**，CPU 不阻塞，可直接后台发起 + 轮询 WIP（Zephyr 的"写必须让出 CPU"约束在此主要是驱动实现问题）；
   - **Erase/Program Suspend**：W25Q128 tSUS **20 µs max**（挂起后即可读其它扇区）；兼容器件 Puya P25Q128L 给出 TESL 30 µs、恢复后需 ≥200 µs 才能再次挂起、resume-to-suspend 需 ≥100 µs 才能让编程有进展 —— 即"挂起让路"可行但会拖慢擦除；
   - **片内双 bank RWW**：STM32 双 bank 允许"在 bank1 取指、在 bank2 擦除"，中断也能在另一 bank 执行；但**不允许 write-while-write**（不能一边擦一个 bank 一边写另一个 bank）。
     https://www.st.com/resource/en/application_note/dm00266999-stm32f7-series-flash-memory-dual-bank-mode-stmicroelectronics.pdf ；https://www.manualslib.com/manual/3725861/St-Stm32g474rct6.html?page=107
   - 若都不行：RAM 环形缓冲 + 把擦除放到低优先级后台任务。

### G. 要不要文件系统

**反对用 FS 承载日志（一手论据）**
- littlefs 维护者 geky 在 issue #1054 的原话：
  > "Honestly, if your logging speed is mission critical, and you're putting in the effort to make the hack in [#564] work, **I would consider not storing the log in a file and instead reserving a fixed amount of raw flash to hold the log. The speed in the chips datasheet is the maximum speed, and any filesystem will necessarily be slower.** You could still store the log size/offset in a file to benefit from power-loss resilience."
  https://github.com/littlefs-project/littlefs/issues/1054
- 同一 issue 的实测劣化与根因：块分配/GC 扫描 **O(n²)**，"It sounds like you're running into #75, the issue being block allocation/gc ultimately scales O(n²) where n is the number of blocks in the filesystem."；`lfs_file_size` 与 GC 扫描同价（都要遍历所有块）；官方缓解建议只有"加大 lookahead_size"（"no benefit to a lookahead larger than block_count/8"）与"加大 block_size"。
- **ESP-IDF 官方**：NVS 库"**not suitable for logging or other use cases with frequent, large data updates**"；SPIFFS "tends to slow down when exceeding around 70% of the dedicated partition size"；FatFS "The biggest weakness is its low resilience against sudden power-off events"，且"each write in the FatFS involves full erase of the area to be written"。
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/file-system-considerations.html
- SPIFFS 官方"partial"掉电语义 + 需 `esp_spiffs_check`（见 D 节）。
- 目录元数据开销：littlefs 元数据最坏 **4x** 成本（DESIGN.md）；SPIFFS 只能可靠利用 **75%** 分区（ESP-IDF 官方）。

**支持用 FS 的论据**
- 开箱即得的**掉电原子性**：littlefs 保证操作级原子（"fall back to the last known good state"），所有 POSIX 风格的 remove/rename 在掉电下原子。
- 自带**动态磨损均衡**、**坏块检测与绕行**、**RAM 严格有界**（"RAM consumption does not change as the filesystem grows"，无无界递归，动态内存仅限可静态提供的缓冲区）。
- 免自研：目录/文件抽象、可扩展、周边生态（FAT 还能被 PC 直接读）。
- littlefs 的 RAM 其实不大（Zephyr 默认 cache 64 × 6 + lookahead 32 ≈ 608 B；Moddable 实测 16 B→512 B 四参数换来显著性能提升只花 2 KB RAM）。
  https://pigweed.googlesource.com/third_party/github/zephyrproject-rtos/zephyr/+/c180afec119a12c3031c5b24011e70491a72f0cb/subsys/fs/Kconfig.littlefs ；https://raw.githubusercontent.com/Moddable-OpenSource/moddable/efb7dcbd397b153ec27fb00265fee58e9b65e3fc/documentation/files/files.md
- littlefs README 也承认："SPIFFS is noted as likely faster than littlefs on small memories such as the internal flash on microcontrollers."

**折中（社区常见）**：日志区用**裸分区环形**（无 FS 元数据、无 GC 扫描、追加延迟确定），其它数据（配置/证书/固件）仍用 littlefs。geky 的原话正是这个折中："You could still store the log size/offset in a file to benefit from power-loss resilience."

### H. CRC 选型与开销

- **碰撞/漏检概率**（随机错误模型）：
  - CRC-16：≈ **2^-16 = 1.53e-5**（检出率 99.9984%）
  - CRC-32：≈ **2^-32 = 2.33e-10**，比 CRC-16 低 65536 倍
  https://stackoverflow.com/questions/64056932
- **确定性保证**（与宽度同等重要）：
  - CRC-16-CCITT (0x8810)：可检**全部 1 位与 2 位错（HD=3）**、32,751 数据位以内的**全部 3 位错**、全部奇数位错、**长度 ≤16 位的全部突发错**、17 位突发的 99.997%、≥18 位突发的 99.998%。
  - CRC-32：32 位突发保护；保证 HD=3（对 ≤~2 k 块更高）；但"保证检出任意 2 位错"仅在**文件长度 + CRC ≤ 2^32 − 1 位**时成立。
  - 多项式选择与宽度同等重要："**incorrect polynomial selection can make a CRC worse than Fletcher**"；对短码字（嵌入式记录远短于 2^k 位），CRC 可优于 1/2^k。出处：CMU Maxino 硕士论文 http://users.ece.cmu.edu/~koopman/thesis/maxino_ms.pdf ；IEEE《Choosing a CRC & specifying its requirements for field-loadable software》 https://ieeexplore.ieee.org/abstract/document/4702857
- **记录级 vs 扇区级**：
  - 记录级 CRC 是"**逐条断链**"的前提 —— FCB "It will skip over entries which don't have a valid checksum."；没有记录级校验，一条坏记录会把整扇区甚至整个分区判废。
  - 扇区级 CRC 只能整块丢弃，且**扇区头本身也需要 CRC**（否则扇区头半写会导致整个扇区被误判）。
  - Zephyr NVS 的分层做法值得借鉴：**元数据用短 CRC（CRC-8/CRC-24，只保证"这次写完成了"）+ 数据用可选 CRC-32（只在完整读取时校验）**。
    https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
  - ESP-IDF NVS 两层都用 CRC-32：页头 CRC32 覆盖字节 4–28；条目 CRC32 覆盖除 CRC 字段外的整个条目。
- **开销**：
  - CPU：STM32 有**硬件 CRC 单元**（固定多项式 **0x4C11DB7**，即"CRC-32 (Ethernet)"多项式；F4 等固定功能件初值固定 0xFFFFFFFF 且不反转，实际等价 **CRC-32/MPEG-2**），**32 位字 4 个 AHB/HCLK 周期 = 1 HCLK/字节**（16 位字 2 周期、8 位 1 周期，都是 1 周期/字节）。软件查表 CRC16 约几周期/字节。相对一次页编程（SPI NOR 0.7 ms、STM32L4 双字 82 µs），**CRC 的 CPU 成本可忽略**。
    https://www.manualslib.com/manual/3904501/St-Rm0090.html?page=86 ；https://stackoverflow.com/revisions/30ae128e-8fd6-43b0-a01e-bac61fa7b80d/view-source
  - Flash 空间：CRC16 占 2 B，CRC32 占 4 B。对 80 B 记录是 2.5% vs 5%。
  - **漏检期望值 [推算]**（随机错误模型）：若全生命周期写 8.19e6 条记录（见下一节算例），CRC16 期望漏检 ≈ 8.19e6 × 1.53e-5 ≈ **125 条**；CRC32 ≈ 8.19e6 × 2.33e-10 ≈ **0.002 条**。若"一条日志被静默篡改"代价高，多花 2 B 换 5 个数量级是划算的。（注意：真实 flash 错误以卡位/撕裂为主，突发 ≤16 位 CRC16 可 100% 检出，上式为**悲观上界**）
  - 兼容性提示：STM32 硬件 CRC 是 MPEG-2 变体，要算 zlib/以太网 CRC32 需软件实现（或补齐初值/反转）。

---

## 手法对比表

| 方案 | 扇区/页头 | 记录结构 | 元数据提交方式 | 掉电语义 | 记录跨扇区 | 恢复扫描代价 | 磨损均衡 | RAM |
|---|---|---|---|---|---|---|---|---|
| **Zephyr FCB** | magic + version + 单调 id | len + data + checksum（finish 时写） | 扇区头（首写）+ 条目校验和（末写） | 未 finish 的条目不合法 → 丢弃该条 | **禁止**（`off+len > fs_size` → -EINVAL；`FCB_MAX_LEN`=16383） | O(扇区 + 条目)，可用扇区头加速 | 环形轮转（`fcb_rotate` 擦最旧） | 极小 |
| **Zephyr NVS** | 无独立扇区头；close ATE 标记 | ATE 8 B(id/off/len/part/crc8) + data；元数据从尾向前、数据从头向后 | 先写数据后写元数据；可选数据 CRC32 | 没有元数据的数据被忽略 | 记录在扇区内偏移（隐含不跨） | O(扇区)，且**读时间线性于 ATE 数**（实测 943 µs/次查找） | 环形轮转 + GC 拷贝，>2 扇区且恒留 1 空 | 极小 |
| **Zephyr ZMS** | empty ATE 存 cycle_cnt | ATE 16 B(crc8/cycle_cnt/len/id + inline 或 offset) | 同 NVS + 周期计数 | 同 NVS | 同 | 线性 | 无擦除器件专用 | 小 |
| **ESP-IDF NVS** | 页头 32 B(State/Seq/version/CRC32) | 32 B/条(NS/Type/Span/ChunkIndex/CRC32/Key16/Data8)，2 bit 状态位图 | **数据先写、状态位后写**；状态迁移只写 0 | 位图未更新由条目首字 != 0xFFFFFFFF 检出并丢弃 | 不适用 | 线性扫页 | 页轮转 + GC | 小 |
| **LittleFS** | metadata pair（双块日志） | 32 位 tag + 32 位 CRC（commit 级，非条目级）；可选 FCRC + valid bit | **双份 + CRC + revision（序列算术）** | **操作级原子**：回落到上次已知良好状态；remove/rename 也原子 | 不适用 | 挂载快（O(元数据)），但**块分配/GC 扫描 O(n²)**；`lfs_file_size` 同价 | 动态（随机分配起点）+ `block_cycles` | 有界（Zephyr 默认约 608 B） |
| **SPIFFS** | 页头(objid/span/span_ix/flags) | 页级 | **无原子提交**，单份元数据 | 可能损坏；需 `esp_spiffs_check`；官方标 "Partial" | 不适用 | 挂载 O(n)；check 需多趟扫描（每页 4 bit 工作内存） | 内置静态 | 小 |
| **FlashDB TSDB** | 扇区头 magic(0x304C5354)+状态表+时间范围 | 索引从扇区头长、数据从扇区尾长 | 状态表（避免为每次更新擦扇区） | 状态表保证幂等/掉电安全 | 记录受扇区容量约束 | 线性（实测 W25Q64 追加 4 ms/条、查询 1.77 ms） | 扇区轮转 | 宣称"几乎为 0" |
| **裸环形（参考实现）** | 区域头 8 B 存 write/read 指针 | 时间戳 4 + len 2 + crc16 2 + payload | **数据包先写、指针后写** | 数据掉电 → CRC 拦；指针掉电 → 旧指针仍有效 | 由"记录 ≤ 扇区"约束 | 线性；**可优化为"只读扇区头 + 只扫最新扇区"** | 环形轮转天然均匀 | 可做到极小（仅缓冲） |
| **pw_kvs** | — | Magic(32) + Checksum(如 CRC16) + Alignment(8) + KeyLen(8) + ValueSize(16) + **TransactionID(32)** | 条目自带 magic + checksum + 事务 ID | 扫描重建，最高事务 ID 胜出；ID 回绕不处理 | **显式禁止**（"must be fully contained within a single sector"；理由：跨扇区则擦一个会毁掉条目） | 启动全扫重建 | 需恒留 1 空扇区 | 小 |
| **MCUboot trailer** | — | Swap status 逐扇区记录 + swap size/info/copy done/image OK | **16 B MAGIC 最后写**（原子提交点） | GOOD/BAD/UNSET（全 0xFF）；上电先续做未完成 swap | 不适用 | 读 trailer | 不适用 | 小 |

---

## 对本项目的量化启示

### 约束回顾
C11 嵌入式框架；Cortex-M4 级 MCU；片内 NOR，未来外置 SPI NOR；器件层已有 flash 抽象（擦/写/读，**写必须让出 CPU**）；分区表 v2（注册表+句柄，分区内偏移读写擦）。目标：黑匣子语义（环形覆盖保新）的持久化日志后端，单条记录为一行文本（几十~几百字节），分区几十 KB ~ 几 MB。

### 1. 选型结论：裸分区环形，不要引入文件系统

三条独立的一手论据都指向同一结论：
1. **littlefs 维护者自己的建议**：日志这种关键路径"reserving a fixed amount of raw flash to hold the log"，因为"any filesystem will necessarily be slower"（#1054）；
2. **ESP-IDF 官方**把 NVS 标为"not suitable for logging"，SPIFFS 只有 partial 掉电韧性且只能可靠利用 75% 分区；
3. 我们的目标语义是**覆盖保新**（黑匣子），而文件系统语义是**持久不丢**（POSIX）—— 语义本来就不匹配，用 FS 是付了 COW/GC/目录元数据的代价买一个不需要的保证。

但要**保留 FS 的可取部分**：littlefs 的"双份 + CRC + revision"和 MCUboot 的"魔术字最后写"是可以在裸分区里**按需复刻**的，成本只有几个字节。

### 2. 记录布局建议（综合 B1/B3/B9）

```
[分区头/无]                       ← 建议不存全局指针，靠扫描重建，避免额外的元数据提交问题
[扇区头] magic(4) | ver(1) | seq(4) | hdr_crc(2)    ← 首写，全 0xFF 表示未使用
[记录 0] magic(2) | len(2) | seq(4) | crc(4) | payload(...)
[记录 1] ...
...
[扇区尾全 0xFF]                                     ← 扫描到此即写游标
```
- **记录头 12 B（CRC32）或 10 B（CRC16），16 B 对齐**：magic 用于快速识别、len 用于推进、seq 用于排序、crc 用于断链。
- **记录不跨扇区**（照抄 pw_kvs 的理由）：这使得"擦一个扇区永不毁掉半条存活记录"。代价是单条上限 = 扇区大小 − 头；对"几十~几百字节"的记录，**4 KB 扇区留 4 KB 上限绰绰有余**；2 KB 片内页也够（留 2 KB）。
- **提交顺序**：payload → 然后写记录头（含 len 与 crc）→ 最后写扇区头的 seq 变化（若采用）。或者等价地：**crcc 覆盖整个记录，头最后写**，这样"头未写"的状态天然是全 0xFF（或 magic 不匹配），扫描时判为空。
- **不要持久化 write_ptr/read_ptr**（与 21ic 参考实现相反）：指针本身是需要原子提交的元数据，多一个掉电窗口。改为"扫描重建游标"——代价是上电扫描，见第 4 点。
- **终止/填充记录**：扇区尾部放不下下一条时，写一条 len=0 的结束标记（FCB 有 `FCB_ALLOW_FIXED_ENDMARKER` 同类机制），避免"空白区"与"半写"混淆。

### 3. 容量与寿命粗算

设：记录头 16 B，载荷 64 B（一行文本典型值）→ **每条占用 80 B**；乐观情形载荷 256 B → 272 B/条。

| 分区 | 介质 | 每轮转条数 | 寿命（擦写次数） | 总可写条数 | @1 条/秒 | @1 条/分钟 |
|---|---|---|---|---|---|---|
| 64 KB | 片内（2 KB 页，**10 k** 次） | 819 | 10,000 | 8.19e6 | **94.8 天** | 15.6 年 |
| 64 KB | 外置 W25Q128（4 KB 扇区，**100 k** 次） | 819 | 100,000 | 8.19e7 | **2.6 年** | 156 年 |
| 256 KB | 外置（100 k 次） | 3,276 | 100,000 | 3.28e8 | **10.4 年** | 623 年 |
| 1 MB | 外置（100 k 次） | 13,107 | 100,000 | 1.31e9 | **41.5 年** | 2492 年 |
| 64 KB（256 B 载荷，272 B/条） | 外置（100 k 次） | 240 | 100,000 | 2.4e7 | 277 天 | 45.7 年 |

**记住这个口径**：总可写条数 = (分区字节数 ÷ 每条占用字节) × 擦写寿命。**分区大小与寿命成正比，扇区大小不影响寿命**（扇区只影响擦除延迟与缓冲需求）。
**最重要的两个数字**：① 片内 64 KB 分区，@1 条/秒 只能撑 **~3 个月**；② 换成外置 SPI NOR 立刻变 **~2.6 年**（10 倍），扩到 256 KB 变 **~10 年**。**"片内 vs 外置"和"分区大小"是两个比任何算法优化都有效的杠杆。**
**黑匣子容量**：64 KB 分区只存得住**最近 819 条**（80 B/条）——这是"最近 N 条"的实际承诺值，需要在文档/接口里讲清楚。

### 4. 恢复扫描的量化与优化

- **朴素全扫 [推算]**（按有效读 10 MB/s 外置 / 20 MB/s 片内粗估）：64 KB → 6.5 ms / 3 ms；1 MB → 105 ms / 50 ms。
- **优化后**（只读扇区头 + 只全扫最新扇区）：16 个扇区头 × 8 B = 128 B + 1 个扇区 4 KB ≈ **4.1 KB**，亚毫秒级，且**与分区大小基本无关**。这是本设计应该采取的默认路径。
- **优化引入的一致性风险（必须显式处理）**：
  1. 扇区头说"有效"但记录区被破坏 → 仍然要逐条 CRC 兜底，**不能因为跳过了扇区头就跳过整扇区校验**；
  2. "最新扇区头已写、记录尚未写"的窗口 → 扫到空扇区时必须能回退到前一扇区并把它当作活动扇区；
  3. 若为了更快而在 flash 上缓存"最新位置索引"，这个索引本身又变成需要原子提交的元数据 —— **本设计的建议是不缓存，永远靠扫描**（成本已在第 2 点被压到亚毫秒）。
- **扫描期间再掉电是安全的**（只读，无写），这点优于任何"挂载时修复"的 FS（SPIFFS 的 check 可能修改 flash，littlefs 的 GC 也会写）。
- 反面教材参考 Zephyr #45591：如果实现成"每次查找都线性扫所有元数据"，启动时间会随写入量线性恶化（实测 2340 ms → 3789 ms / 20 次重启）。

### 5. 擦写时序与错峰

| 场景 | 阻塞时长 | 期间能攒多少日志（80 B/条） | 处置 |
|---|---|---|---|
| 外置 W25Q128 4 KB 扇区擦除（典型 45 ms） | 器件自持，CPU 只需轮询 WIP；但**同扇区不可读写** | @1/s：0.05 条；@100/s：4.5 条（360 B） | 提前一个扇区启动 erase-ahead；RAM 环形缓冲 |
| 外置 W25Q128 扇区擦除（**最坏 400 ms**） | 同上 | @1/s：0.4 条；@100/s：**40 条（3.2 KB）** | 缓冲 ≥ 一条扇区容量的日志量；**不要用 typical 值做超时** |
| 外置擦除挂起 Erase Suspend | **tSUS ≤ 20 µs** 后即可读其它扇区 | 挂起期间可读写别的扇区，但擦除被拖慢 | 实时性紧张时的应急手段；注意 resume 后需 ≥100–200 µs 才能再次挂起（同规格器件数据） |
| 片内 nRF52840 4 KB 页擦除 | **CPU 停转 ≤ 85 ms** | @1/s：0.085 条；@100/s：8.5 条 | 必须靠 RAM 缓冲 + 后台任务 |
| 片内 STM32L4 2 KB 页擦除（22 ms typ） | CPU 停转 | @1/s：0.022 条 | 可接受；页小、擦除快是片内唯一的优势 |
| 片内 STM32F4 128 KB 扇区擦除（1.3–4 s） | **CPU 停转数秒** | @100/s：130–400 条（10–32 KB） | **单 bank 下不可接受**；必须双 bank RWW 或改用外置 |
| 页编程 tPP（256 B，0.7 ms typ / 3 ms max） | 阻塞 0.7–3 ms/次 | — | 多条记录合批到同一页可摊薄；**不要每条日志一次页编程** |

**最关键的错峰洞察**：4 KB 扇区装 51 条 80 B 记录，写满需要 51 × 0.7 ms ≈ **36 ms**，与一次扇区擦除（典型 45 ms）**同量级**。所以 erase-ahead 的调度必须**提前约"一整个扇区的写入时间"**（而不是提前几条记录）启动预擦除；否则擦除会正好卡在写满的那一刻。

**"写必须让出 CPU"的具体含义（按介质不同）**：
- **片内**：CPU 真的停转（nRF NVMC 实测语义），只能靠 RAM 缓冲把日志"接住"；
- **外置 SPI NOR**：擦除是器件自持的（发出命令后器件自己干），CPU 只是**不能阻塞等**——驱动应做成"发起 + 状态机轮询 WIP（或挂起）+ 完成回调"，而不是自旋等待。这正好匹配我们器件层"写必须让出 CPU"的抽象：把它实现为**异步提交 + 完成通知**，日志 API 就可以是非阻塞的。

### 6. CRC 选型建议

- **记录级用 CRC-32，扇区头用 CRC-16/CRC-8**（分层，参考 Zephyr NVS 的 ATE 用 CRC-8、数据用 CRC-32）。
- 理由（用本项目的数字算）：8.19e6 条的全生命周期里，CRC16 的随机错误漏检期望 **~125 条**，CRC32 **~0.002 条**；代价只是每条多 2 B（80 B 记录上 2.5% → 容量 819 条 → 800 条，少 2.3%）。**2.3% 容量换 5 个数量级可信度，值得。**
- CPU 代价可忽略：STM32 硬件 CRC 单元 1 HCLK/字节（≈64 周期算完 64 B），相对 0.7 ms 的页编程是 **1e-5 量级**。若用软件，务必用**查表法**（256 项表 1 KB，或 16 项 nibble 表 64 B）。
- 注意：STM32 硬件 CRC 是 **CRC-32/MPEG-2**（poly 0x4C11DB7、init 0xFFFFFFFF、不反转），**不等价于 zlib CRC32**。若日志要导出到 PC 用 zlib 校验，要么软件实现，要么在协议里写明用 MPEG-2 变体。
- 若资源极度受限退到 CRC-16：选 **CRC-16/CCITT (0x1021)** 或 **CRC-16/MODBUS (0xA001，反射)**，二者都能 100% 检出全部 ≤16 位突发错（真实 flash 卡位/撕裂错误的主流形态），比"随机错误 2^-16"的实际表现好得多。

### 7. 其他对本项目的具体启示

1. **不要持久化指针**：交给扫描（第 4 点已把扫描压到亚毫秒）；持久化指针 = 多一个需要原子提交的元数据。
2. **不要用"典型擦除时间"做超时**：nRF52832 typ 2.05 ms vs max 89.7 ms（44 倍）、W25Q128 现场 25 °C 50 ms vs 60 °C 300 ms。超时至少要覆盖 datasheet max 并留余量，且**超时必须走错误路径**（不写半擦除扇区）。
3. **擦除后要判"擦干净"**：Zephyr NVS 的 `nvs_flash_erase_sector` 擦完会与 `erase_value` 逐字节比较；W25Q128 现场故障正是"没验证擦除结果"造成的。
4. **擦除失败要能降级**：NOR 无坏块表，但单次擦除/编程失败真实存在（A4）。至少要做到：擦除失败 → 标记该扇区跳过 → 用剩下的扇区继续环形（容量下降但不死）。littlefs 的 `LFS_ERR_CORRUPT` / "evict the bad block" 是现成的语义参考。
5. **保持"至少一个已擦扇区"不变式**：FCB/NVS/pw_kvs 都依赖它做轮转；我们的环形也要保证"下一个扇区永远是已擦干净的"，这正是 erase-ahead 要维持的。
6. **扇区头写 seq 要有单调性且能处理回绕**：pw_kvs 明确不处理 32 位事务 ID 回绕（4.3e9 次 vs 1e5 寿命，先磨损坏）；我们若用 32 位 uint 回绕比较（`(int32_t)(a - b) > 0` 的序列算术，littlefs 同款），同样安全。
7. **RAM 预算可以做到很小**：裸环形只需一个扇区大小的写缓冲（4 KB）或按记录缓冲（几百 B）+ 未刷盘记录队列；对比 littlefs 默认约 608 B（cache 64×6 + lookahead 32）——但 littlefs 每次 append + sync 要重写整块，我们的裸环形只需追加。
8. **分区表 v2 的契合点**：环形日志所需的原语只有"分区内偏移读/写/擦"，与我们已经有的抽象完全匹配；唯一的额外要求是**擦除/编程的异步完成通知**（因为"写必须让出 CPU"），应在后端接口里显式建模（发起 → 完成回调/信号），否则上层会因为自旋等待而阻塞。

---

## 存疑与未证实

1. **W25Q128 的 tSE 典型值有 45 ms 与 100 ms 两种说法**。45 ms 来自现场报告转述，100 ms 来自 W25Q128FVAIG datasheet AC 表；两者最大都是 400 ms。**设计按 400 ms 最大即可**，典型值分歧不影响结论。**未找到 Winbond 官方 PDF 原文直接确认 45 ms。**
2. **W25Q128JV 的 100,000 次擦写寿命**：来自多个分销商/datasheet 聚合站（datasheets.com、LCSC、Kynix、GlobalSpec）一致引用，但**未取到 Winbond 官方 PDF 原文**。MT25QL 的 100,000 次有 alldatasheet 上的 Micron 手册镜像支撑，可信度更高。
3. **SPI NOR 的"相邻扇区被擦除中断波及"**：DAC 2011 论文提到掉电可破坏此前已成功编程的数据、论坛提到 Micron 文档有"adjacent page issue"，但**没有任何厂商应用笔记明确记录 NOR 主阵列相邻扇区被擦除中断破坏**。本报告未断言这一点。
4. **SPI NOR 的有效读带宽（10 MB/s 假设）是我为估算扫描耗时取的粗估**，未找到通用一手数据。实际值取决于 SPI 时钟、单线/四线、命令开销、驱动实现。**所有基于它的扫描耗时数字都应视为 [推算]，需在目标板上实测。**
5. **片内 flash 读带宽（20 MB/s 假设）**同样是粗估，取决于等待周期配置。
6. **SPIFFS 的具体劣化数字**：510 s 修复耗时、4/10 挂载成功率、littlefs 1.3x vs SPIFFS 2.8x 写放大 —— 均出自中文对比文章（21ic / EEPW / CSDN），**未见原始测试代码与测试条件，标注为未证实**。ESP-IDF 官方文档的定性结论（"partial"、"可能损坏"、"75% 可用"、"可能数秒的 GC 扫描"）有官方出处，可信。
7. **littlefs 挂载 1600 ms**（ESP32）出自论坛单一来源，**分区大小未知**，可能包含大分区/碎片化因素，未必可外推；littlefs 官方宣称挂载代价是 O(元数据)。
8. **nRF52832 的 tWRITE typ 67.5 µs / max 338 µs 与 tERASEPAGE typ 2.05 ms / max 89.7 ms** 来自搜索结果对 nRF52832 PS v1.1 的转述，未直接读取 PDF 原文。数量级（typ 与 max 相差 40 倍以上）与 nRF52840 的"只给 max"策略一致，方向可信，**精确值待核**。
9. **Puya P25Q128L 的 TESL 30 µs / TERS ≥200 µs / TPRS ≥100 µs** 是兼容器件数据，**不是 Winbond W25Q128 的规格**；W25Q128 只确认 tSUS = 20 µs max。
10. **"擦除挂起会拖慢擦除"** 是机制推断（挂起/恢复有最小间隔要求，见第 9 条），**未找到定量数据**。
11. **本报告"容量/寿命/扫描耗时"表中的所有具体数值均为 [推算]**，输入量（记录大小 80 B/272 B、日志速率 1 条/秒等）是为演示口径而设的示例，实际项目需代入真实参数。
12. **未找到**任何一手来源给出"环形日志上电扫描耗时"的直接实测数据（各框架文档都只描述算法，不给时间）。第 4 点的耗时数字全部是推算。

---

## 主要来源清单

器件与厂商文档
- Winbond W25Q128FV AC 特性（alldatasheet 镜像）：https://www.alldatasheet.jp/html-pdf/932082/WINBOND/W25Q128FVAIG/26529/87/W25Q128FVAIG.html
- W25Q128JV 规格汇总：https://www.datasheets.com/winbond/W25Q128JVS ；https://www.kynix.com/components/w25q128jvsiq-spi-flash-memory-datasheet-footprint-specification.html
- Micron MT25QL（Pmod SF3 引用）：https://digilent.com/reference/pmod/pmodsf3/reference-manual
- STM32L4 Flash 产品培训：https://www.st.com/resource/en/product_training/STM32L4_Memory_Flash.pdf
- STM32F405/407 datasheet（alldatasheet 镜像）：https://www.alldatasheet.es/html-pdf/510595/STMICROELECTRONICS/STM32F407VE/205164/105/STM32F407VE.html
- ST AN4429（C90FL brownout 恢复）：https://www.st.com.cn/resource/en/application_note/dm00103274-rpc56xx-and-spc56xx-c90fl-flash-recovery-in-case-of-brownout-during-flash-erase-operation-stmicroelectronics.pdf
- ST AN4826（STM32F7 双 bank RWW）：https://www.st.com/resource/en/application_note/dm00266999-stm32f7-series-flash-memory-dual-bank-mode-stmicroelectronics.pdf
- Nordic nRF52840 擦写时间：https://devzone.nordicsemi.com/f/nordic-q-a/124564/what-s-the-typical-flash-erase-and-write-time-on-nrf52840
- Nordic nRF52832 NVMC：https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.nrf52832.ps.v1.1/nvmc.html
- Infineon NOR 坏块管理：https://community.infineon.com/t5/Nor-Flash/Does-NOR-Flash-need-Bad-Block-Management-Why/td-p/360001
- Infineon KBA221246（WWR 掉电）：https://community.infineon.com/gfawx74859/attachments/gfawx74859/nor-flash/3359/2/KBA_221246_WRR.pdf
- Microchip SST FAQ（NOR 坏块）：https://support.microchip.com/s/article/Memory---SST-FAQs
- Renesas KB（NOR 掉电影响）：https://en-support.renesas.com/knowledgeBase/21126034

框架文档与源码
- Zephyr FCB 概览：https://docs.zephyrproject.org/latest/services/storage/fcb/fcb.html
- Zephyr FCB 源码：https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/subsys/fs/fcb/fcb.c ；头文件：https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/include/zephyr/fs/fcb.h
- Zephyr NVS：https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
- Zephyr ZMS：https://docs.zephyrproject.org/latest/services/storage/zms/zms.html
- Zephyr NVS 性能问题 #45591：https://github.com/zephyrproject-rtos/zephyr/issues/45591
- ESP-IDF NVS：https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html ；源码：https://raw.githubusercontent.com/espressif/esp-idf/v5.5.2/components/nvs_flash/src/nvs_page.cpp
- ESP-IDF SPIFFS：https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/spiffs.html
- ESP-IDF 文件系统对比：https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/file-system-considerations.html
- LittleFS DESIGN.md：https://raw.githubusercontent.com/littlefs-project/littlefs/master/DESIGN.md
- LittleFS SPEC.md：https://raw.githubusercontent.com/littlefs-project/littlefs/v2.9.1/SPEC.md
- LittleFS README：https://raw.githubusercontent.com/littlefs-project/littlefs/master/README.md
- LittleFS 追加性能 #1054（含维护者原话）：https://github.com/littlefs-project/littlefs/issues/1054
- LittleFS 每次写触发擦除 #581：https://github.com/littlefs-project/littlefs/issues/581
- FlashDB README（性能表）：https://raw.githubusercontent.com/armink/FlashDB/master/README.md
- FlashDB 架构与 TSDB：https://deepwiki.com/armink/FlashDB/2-architecture ；https://deepwiki.com/armink/FlashDB/2.2-time-series-database-(tsdb)
- Pigweed pw_kvs 磁盘格式：https://pigweed.dev/pw_kvs/disk_format.html
- MCUboot design.md：https://raw.githubusercontent.com/mcu-tools/mcuboot/master/docs/design.md
- SPIFFS check 源码：https://github.com/DimmKirr/spiffy/blob/140f1a3d10a3a70dd524e83448a9b9bd47d5545e/src/spiffs_check.c

CRC
- CRC 漏检概率讨论：https://stackoverflow.com/questions/64056932
- Maxino 硕士论文（CRC 性能对比）：http://users.ece.cmu.edu/~koopman/thesis/maxino_ms.pdf
- IEEE《Choosing a CRC & specifying its requirements for field-loadable software》：https://ieeexplore.ieee.org/abstract/document/4702857
- STM32 CRC 单元：https://www.manualslib.com/manual/3904501/St-Rm0090.html?page=86

实测/现场报告（二手，已标注）
- W25Q128 高温擦除变慢：https://www.hotmolts.com/post/my-spi-flash-chip-erased-fine-at-25c-and-failed-ev-eb8c328e-8050-49ea-972b-ed9f530e713f
- 裸环形日志参考实现（21ic）：https://www.21ic.com/a/1007742.html
- PJRC 论坛 littlefs 小写入延迟：https://forum.pjrc.com/index.php?threads/littlefs-performance-issue-for-qspi-flash-from-1-54-beta10-to-1-56.70467/page-2#post-308885
