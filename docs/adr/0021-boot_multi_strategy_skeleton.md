# ADR-0021：多策略 Boot 系统骨架（direct 起步 + 片内双槽布局 + 镜像头契约）

- 状态：已决策（2026-09-07，Q-01..06/08..10 拍板）
- 日期：2026-09-07
- 参考：ADR-0022 (osal_none_bare_metal)、reference_design_notes.md 的 K-03..13/K-17/K-18/K-20、storage_landscape.md ER-4

## 背景 (Context)

IAP bootloader + OTA 方向（四步开工序列）步骤 ② 的**设计前置**拍板。前置事实：

1. 步骤 ① FlashDev 已落地并真机验证（PR #70）——槽/meta 全部消费其几何语义，设备层零新增职责（P-01/P-03）。
2. 分层定界 P-01..05 与调研档案 K-01..16 已就位；`multi_strategy_boot_design.md` 草案列出 11 项开放决策点（Q-01..11）。
3. 框架定位 = **通用嵌入式框架**（非竞赛专属）——策略取舍不得以单一应用场景为据。
4. 真机实测（flash_dev_design.md §8）：F427 RWW 有效——跨 bank 布局使"写 staging 期间执行侧照跑"成立；同 bank 擦写仍 stall。

## 考虑过的方案 (Options)

### 提交策略（Q-01）

- **direct-xip（翻指针）起步，四策略同骨架**：提交动作 = meta 翻指针 + 启动计数回退；swap/overwrite/ram-load 为同骨架后续提交动作（构建期开关，K-11 单 binary 单策略）。**采纳。** 理由：① 骨架与策略正交（草案 §1）——首实现选 direct 因参考板 rm-a 双 bank 可 XIP 形态 + 提交动作最简，最先验证骨架正交性；② 换策略不换骨架，swap（远程设备回滚）/overwrite（面积紧）均为一等支持面，按需求落地。
- swap 起步：回滚最强但提交动作最复杂，首版周期拉长，且不改变骨架形态——延后为同骨架第二提交动作。**否决（首版）。**
- overwrite 起步：最简但无回滚，片内双 bank 形态浪费。**否决（首版）。**

### 片内布局（Q-02/Q-03）

- **rm-a/F427：bootloader 64K（4×16K 扇区）@bank1 头 + A = bank1 余量 ~960K + B = bank2 1MB + meta 独立区**。**采纳。** 依据：① A/B 跨 bank = RWW 安全区（运行 A 写 B 不 stall，真机实测）；② 扇区对齐零碎化；③ 无 factory 区——后门兜底（K-12）。
- **meta 独立区（与镜像生命周期解耦）+ 双份轮转（CRC+seq，K-05）**。**采纳。** 槽尾（MCUboot trailer 风格）否决——片内槽尾写时机与 staging 完成耦合，复杂度无对价。

### 镜像头契约与校验链（Q-04/Q-05）

- **契约单头文件共享（bootloader 与 app 两工程 include 同一份），数值随布局实现期定**。**采纳。**
- **校验链可配置**：镜像头带 digest 算法 ID、摘要区按算法预留、bootloader 侧校验模块按构建期选型编译；CRC32 起步（沉淀进框架算法库），SHA-256+ECDSA 记演进——契约版本化保证旧侧遇新算法优雅拒绝（K-09/K-17）。**采纳。**

### 确认/回退窗口（Q-06）

- **启动计数 N 次**（app confirm 停表；无看门狗依赖；掉电重启计数仍推进）。**采纳。** 依据：direct 系自动回退的行业标准（ESP otadata，K-18）；swap 落地时回退语义随策略天然获得（K-18），骨架不重复造回退机制。看门狗超时否决——app 侧外设依赖与框架现状冲突。

### 后门与下载方（Q-08/Q-09）

- **串口握手后门起步**（零 OSAL 串口轮询三件套已备，K-12 防砖）；按键/上电超时等多触发源记演进。**采纳。**
- **下载方两路都留（K-13）：bootloader 后门直写先行**（与串口握手同通道，服务现场灌固件与防砖）；app 内 OTA 服务归步骤 ③（与版本/校验/元数据规划合并）。**采纳。**

### remap/介质映射接口（Q-10）

- **随板适配实现期定**：生态无"remap 框架 API"先例（K-20——槽 = 静态表 + 跳转；bank swap/MMU 为板适配跳转前动作）；分区表抽象（P-03）随步骤 ② 前置落地。**采纳。** 现在抽象否决——无实现支撑的提前抽象（YAGNI）。

## 最终决策 (Decision)

- **步骤 ② 首版 = direct 单策略 bootloader**：骨架（读 meta → 判状态 → 校验 → 提交 → 启动 → 确认/回退）与提交策略正交；swap/overwrite/ram-load 为同骨架后续提交动作（构建期开关）。
- **布局（rm-a/F427）**：bootloader 64K @ bank1 头；A = bank1 余量；B = bank2；meta 独立区双份轮转；无 factory。
- **镜像头契约**：单头文件共享；digest 算法 ID + 摘要区预留；CRC32 起步、SHA-256+ECDSA 演进；校验模块构建期选型。
- **回退**：启动计数 N 次（direct 语义），app confirm 停表。
- **后门**：串口握手起步；下载方两路都留、bootloader 后门直写先行。**2026-09-07 补充（K-21）**：后门协议按**命令组形态**设计（组 ID + 载荷=镜像 + 块/续传，一组一命令 boot_upload 起步）——为通信/命令服务（services 层演进项，与 log v4 shell 规划合并）的**裁剪前身**；UART = 第一介质后端；boot 组为高危命令组（载荷覆写镜像），访问门禁（握手时序/口令/窗口）为设计约束，门禁位置实现期定。
- 决策记录索引：Q-01..10 拍板结论在 `multi_strategy_boot_design.md` §5；演进项（swap/overwrite 落地、SHA-256+ECDSA、多触发源后门、通信/命令服务、app OTA）在 §5.1。

## 影响 (Consequences)

- 正面：骨架先行 + 策略正交 = 换策略不动骨架（一次投资覆盖四策略形态）；A/B 跨 bank 吃 RWW 红利（写 staging 期间业务照跑）；meta 独立区与镜像生命周期解耦；校验链可配置不留升级债（用户要求：CRC32 与 SHA-256+ECDSA 类保留可配置能力）。
- 行为：bootloader 与 app 由镜像头契约 ABI 耦合（版本化、优雅拒绝）；app 只 confirm 不翻指针（K-07/K-13）；升级由人/通道触发，bootloader 语境提交。
- 约束：首版无自动回滚的 swap 级能力（direct 靠启动计数）；无 factory 区——镜像双坏时靠串口后门收固件；bootloader 区大小预算 64K（rm-a 参考值）。
- 兼容：FlashDev 零改动（槽/meta 消费其几何语义）；分区表（P-03）作为步骤 ② 前置新模块落地，归 drivers 上层语义；ER-4 由本决策承接（storage_landscape.md 标记）。
- 后续：ADR-0022 (osal_none_bare_metal) 承载 bootloader 运行时形态决策；步骤 ② 动码前补 Q-05 数值（契约头 ABI 具体值随布局实现期定）。
