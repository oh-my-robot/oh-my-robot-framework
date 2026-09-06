# Flash / Bootloader / OTA 设计参考档案（锚点文档）

> **状态**：工作锚点——持续回读、校验、更新。本文档是 flash 驱动 + bootloader + OTA 方向的**调研与原则档案**，不是 ADR；决策落地为 ADR 时以 `P-xx` / `K-xx` 编号引用本文，不复述。
>
> **约定**：`P-xx` = 用户拍板的分层定界原则（不可走回头路）；`K-xx` = 从成熟开源框架提炼的考量（随调研更新）。新增条目只在文末"更新记录"追加并编号，**不重排历史编号**（防误伤引用）。原则性内容修订需在更新记录说明动机。
>
> **首版日期**：2026-09-06（素材：2026-09-05 方向定调会话 + 09-06 成熟框架调研）

---

## 1. 方向与开工序列（2026-09-05 定调）

近期目标：**IAP（在应用编程）bootloader + OTA 子系统**——用户区自写引导 + 在线升级（2026-09-06 纠名：方向词为 IAP；原记录 IAR 系误记，相应工具链条目删除）。开工序列（Flash 抽象为第一项）：

1. **Flash 设备抽象层**（片内 FlashDev：read/write/erase + geometry；drivers 层 + 平台适配器 + device_find 注册）——地基，零上层依赖
2. **Bootloader 最小工程**（IAP bootloader——基于 FlashDev，平台无关；片内双区布局；bootloader 日志 = 裁剪契约/早期滞留/零 OSAL 轮询三件套）
3. **OTA 状态机**（app 侧升级服务：分区/版本/校验/元数据小块——结构化独立存储，≠日志流）
4. **外部 Flash 批次**（W25Q 设备 + 磨损环形）+ **持久化日志后端**（同批共建；OTA 排障现场记录 = 头号消费场景）

## 2. 分层定界原则（P-xx——用户拍板，本方向架构的不可退让条款）

| 编号 | 原则 | 拍板日期 | 校验点（动代码前自问） |
|---|---|---|---|
| P-01 | Flash 驱动 = 设备服务抽象（PAL/设备模型族）；**片内 Flash 同样需要**；上层（bootloader/OTA/持久化）**严禁直接调用第三方库**，厂商驱动只出现在平台适配器内 | 2026-09-05 | 上层代码里出现 HAL_FLASH / driverlib 符号了吗？ |
| P-02 | 换芯片 = 换适配器：同一上层抽象，STM32F4 与 MSPM0 只差平台适配实现，上层零改动 | 2026-09-05 | 适配器新增/修改时上层文件动了吗？ |
| P-03 | 分区表抽象（boot/appA/appB/meta 布局）是**驱动上层语义**，与 FlashDev 同批或紧随，但不混入 FlashDev 设备层 | 2026-09-05 | 设备层头文件里出现"分区/boot/app"概念了吗？ |
| P-04 | 上层只表达"槽位 + 指针"；**remap / 介质映射 / bank 切换等细节 = 每板 bootloader 适配职责**（参考：MCUboot direct 模式、Zephyr 板级链接布局） | 2026-09-06 | 槽位语义代码里出现寄存器/remap/介质地址了吗？ |
| P-05 | **磨损环形 = 存储/日志层职责**（SFUD 之上、FAL 之下的边界同构）；不下沉进 FlashDev 设备层，不上浮到 bootloader | 2026-09-06 | FlashDev / bootloader 里出现环形/磨损/擦写均衡了吗？ |

## 3. 参考系统全景

| 参考 | 地位 | 主要借鉴面 |
|---|---|---|
| CMSIS-Driver Flash（ARM） | 事实标准设备 API | FlashDev 接口形状：read/write(program)/erase + GetInfo |
| Zephyr flash API + flash_map | 设备几何与逻辑分区分离范本 | 对齐/擦除语义参数化；分区 = 名字+设备+偏移+大小 |
| MCUboot（Zephyr 生态） | 升级策略集大成者 | swap/overwrite/direct 取舍；trailer 状态机；确认/回滚语义 |
| ESP-IDF OTA + otadata | A/B 双槽工程范本 | 元数据小块（双份轮转 + seq + CRC）；启动计数回滚 |
| RT-Thread FAL / SFUD / QBoot | 同竞赛生态，文件级参考 | FAL 分区表；SFUD NOR 驱动形态；下载→校验→搬移流程 |
| OpenBLT（Feaser） | 单区签名 + 后门 bootloader | 镜像内签名/后门标志；防砖通道 |

## 4. 跨框架考量（K-xx）

| 编号 | 考量 | 出处 | 对 OMR 的含义 |
|---|---|---|---|
| K-01 | 设备层只回答"芯片几何与擦写语义"：读可任意，**擦除整扇区、写有 program 粒度与对齐约束**；接口显式暴露 geometry，不默认 | CMSIS / Zephyr / FAL | FlashDev 必须带 geometry 查询（capacity/扇区布局、program 粒度、erase_value）；总线映射基址**不进几何**（2026-09-06 裁决 A） |
| K-02 | 物理设备 ≠ 逻辑分区，分层放置：分区 = 静态描述表（名字/设备/偏移/大小），上层只认名字与偏移，永不裸地址 | Zephyr flash_map / FAL | 分区表独立成模块；换芯片不动分区表 |
| K-03 | NOR 只能擦后写、原地更新 = 自毁 → **staging 槽位先行**是全部方案的共同骨架：staging → 校验 → 激活 | 全部 | 任何升级路径必须先落到非启动区，校验通过前不得触碰运行区 |
| K-04 | 激活 = 翻转一个小子段指针/状态，**不是搬移**；搬移只发生在 bootloader 提交阶段（swap）而非每次升级 | ESP otadata | A/B + last-good 指针优先于"每次升级全量搬移" |
| K-05 | meta 掉电安全 = **双份轮转 entry + seq + magic + CRC**：半写 entry 因 CRC 失败被忽略，读一个 entry 即得真相 | ESP otadata / MCUboot trailer | boot 决策必须"读到一个小结构就够"，不在启动时解析整镜像 |
| K-06 | intent 先行：状态机**每推进一步先写状态再动手**（journal 思想）；每步幂等、掉电任意点可判定 | MCUboot trailer | OTA/swap 状态机推进准则 |
| K-07 | **bootloader 是升级事实源**；app 无权自改启动决策（它可能已坏）。状态语义只有三种：pending（试一次）→ confirm（正式）→ 回退 | ESP / MCUboot | 确认接口语义定死；app 只做"我活了，确认" |
| K-08 | **自动回滚窗口**：pending 镜像在 N 次启动或看门狗超时内未确认 → 自动回退旧镜像/旧槽位 | ESP | 联调"刷了新固件起不来且人不在"也可自愈 |
| K-09 | 整镜像校验先于激活；**镜像头自校验 + 带结构版本号 + 预留字段**（防新旧 bootloader/镜像双向错位，可优雅拒绝） | MCUboot / ESP | 镜像头契约 = bootloader 第一交付物 |
| K-10 | 传输与安装解耦；**续传点由接收侧记录**（慢通道断了重来是灾难） | mcumgr / ESP | 协议面（块传输/重传/续传）与安装面（staging 写入）分离 |
| K-11 | bootloader 薄：无动态内存、无复杂驱动栈、启动路径确定性；**策略由构建/配置期固定**，运行时不做策略切换 | MCUboot / OpenBLT | log 三件套支撑"bootloader 也有日志"；每 binary 单策略 |
| K-12 | 保留**后门进入通道**（串口握手/按键/超时/升级标志），单区方案在镜像无效时仍能收固件——防砖 | OpenBLT | bootloader 必须能主动进入下载模式，不能只被动等 |
| K-13 | 权力分离：**写 staging = 下载方职责**（app 内 OTA 服务或外部下载器），**提交（翻转指针/执行 swap）= bootloader 权力** | ESP / MCUboot 流程一致 | OTA 状态机归属据此划分：app 写 staging + 标 pending；bootloader 决策执行 |
| K-14 | 并发锁模型：锁粒度 = 设备（物理片/擦写引擎）粒度，**无"设备内子锁"先例**；两派 = API/驱动内建可睡眠锁（ESP-IDF 全局互斥、MTD chip/controller 双锁、Zephyr spi-nor k_sem 全操作排他）vs 调用方纪律（Zephyr flash API 无锁、FAL 零互斥、CMSIS 隐含单飞行）；**ms 级写擦用可睡眠锁度过、不关中断**（ESP 禁 cache/禁中断系其 cache 架构被迫，非普遍）；FAL 被社区在 port 层补锁 = 纪律模型在 RTOS 并发下会漏的实证；ESP 承认 v4.0 起并发读可读新旧混合数据 | ESP-IDF / Linux MTD / Zephyr spi-nor / FAL | 双层锁落地（v1）：device 锁管本片时序；共享总线锁仅传输瞬间（Linux spi-nor 同款）；写/擦异步化（单飞行 slot + 完成源），read 同步保留（F-04/F-06 v1 修订——v0"可睡眠互斥全排他同步"经真机实测废弃：忙等饿死低优线程） |
| K-15 | 设备对象判据：设备 = 物理"片/擦写引擎"（锁与擦除的根）；**bank 一般不构成设备判据**——Zephyr 整片一设备（F4 驱动作者明示权衡过拆两设备而不做）、MCUboot 槽位可连排同 bank 且 bootutil 不感知 bank、FAL 分区绑定单设备不可跨；要跨片/bank 并发的自然表达 = 拆设备或 mtd_concat 拼合，**非子锁**；ESP 每物理片一 chip、外片因 cache 禁 APP | Zephyr / MCUboot / FAL / ESP-IDF / Linux MTD | F407 双 bank = 一个 flash0，bank 入几何区域表；拆设备路径留口（F-05） |
| K-16 | 完成源边界与事件范式裁决（2026-09-06 三方调研收敛）：**flash 框架 API 对调用者保持同步，异步完成源压驱动/后端用信号量吸收**——Linux 2018 删 mtd_erase 假异步（"None of the mtd->_erase() implementations work asynchronously"）；Zephyr flash async PR（HAL_IT+IRQ）被否（作者自承"称 async 有误导性"——调用线程仍阻塞）；CMSIS SignalEvent 规格完备但实际消费近零（ARM 自家 FileSystem 把 READY 事件写进无人读取的字段，等待一律 GetStatus 轮询）；**bootloader 生态（MCUboot/OpenBLT/ST ROM/TI BSL/NXP）全为同步阻塞 + 轮询 + 拆片 + 主机长超时**（OpenBLT erase 超时默认 10s、ST AN3155 承认秒级静默、MCUboot 喂狗在扇区间且默认容忍 5 分钟）；无 OS 复用手法 = 编译期把 OS 原语裁剪为空转（U-Boot compat.h 样板） | Linux MTD（commit 884cfd90）/ Zephyr（PR #93173）/ CMSIS + ARM fs_nor_media / MCUboot loader+bootutil_area / OpenBLT / ST AN3155 / TI SLAU887 / U-Boot compat.h | FlashDev 保持"同步 API + 域 worker + 后端完成源可插拔（EOP 中断主路径/轮询退化，D-07）"；**事件不升框架 API**（提交者是任务时同步即充分；能耗用阻塞+tickless 解决而非事件化）；bootloader 复用：带 OS 用同步等待原语 / 裸机 OM_FLASH_SYNC_ONLY + osal 空桩；"同步擦写 + 让出"是框架下沉原语 |

## 5. 更新记录（追加式，不重排编号）

- 2026-09-06：首版。素材 = 09-05 定调会话（P-01..03、五步序列）+ 09-06 成熟框架调研（K-01..13）；同日拍板 P-04、P-05（多策略设计讨论产出）。
- 2026-09-06（同日追加）：K-14/K-15（并发锁模型与设备对象判据，F-04/F-05 调研产出）；F 系列定论（F-01..05）落地为 `flash_dev_design.md` v0 定稿。
- 2026-09-06（同日追加，v1）：真机验证（rm-a/F427）驱动异步重构——同步忙等饿死低优线程实测（FreeRTOS 持续就绪压制 log LOW 线程）+ 多片并发/read 语义调研（双层锁：device 锁管本片、总线锁仅传输瞬间；read 业界保持同步）→ F-04 修订为异步写/擦 + 同步读（F-06）；K-14 含义列同步；`flash_dev_design.md` v1 定稿。
- 2026-09-06（同日追加）：K-16（完成源边界与事件范式裁决：事件不进框架 API、完成源差异留后端、bootloader 同步阻塞+拆片+主机超时、U-Boot 裁剪样板——Linux/Zephyr/CMSIS/MCUboot/OpenBLT/ST/TI/U-Boot 六方证据）；D-07 强化为"事件主路径、轮询退化"（`flash_dev_impl_design.md` §6.1）。

## 6. 关联文档

- `boot_system_multi_strategy_design.md`（同目录）——多策略 boot 系统设计草案，决策点 `Q-xx`，落地时以本文件 P/K 编号为约束锚点
- `flash_dev_design.md`（同目录）——Flash 设备抽象 v0 定稿（F-01..F-05），开工序列步骤 ① 接口文档
- `storage_landscape.md`（同目录）——存储系统形态锚点：芯片特性谱系 → 软件义务分层推导、可擦存储族 + 随机器件族目标形态、演进留口 ER-xx
