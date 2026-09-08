# ADR-0022：裸机运行时形态（osal-none 端口——同步面保真 + 单执行流）

- 状态：已决策（2026-09-07，Q-07/Q-11 拍板）
- 日期：2026-09-07
- 参考：ADR-0021 (boot_multi_strategy_skeleton)、reference_design_notes.md 的 K-11/K-16/K-19

## 背景 (Context)

bootloader 运行时形态决策（Q-07）及其衍生的框架缺口（Q-11）：

1. bootloader 生态事实（K-16 六方证据）：MCUboot/OpenBLT/ST ROM/TI BSL/NXP 全为同步阻塞 + 轮询 + 无 RTOS 内核；RTOS 内核出现在 bootloader 属个别例外。
2. 框架 osal 目前只有 FreeRTOS 端口（`platform/osal/freertos`）；D-08（flash_dev_impl_design.md）只设计了裁剪语义（OM_FLASH_SYNC_ONLY + 空桩方向），无实现落点。
3. K-16 原表述"无 OS 复用手法 = 编译期把 OS 原语裁剪为空转（U-Boot compat.h）"——经拍板讨论细化：空转宏为历史妥协，同步面保真为现代做法（K-19，XRobot/LibXR 先例）。
4. 裸机形态是 os 轴的合法取值（换 RTOS 不碰架构核心的分层哲学；无 RTOS = os=none）。

## 考虑过的方案 (Options)

### 运行时形态（Q-07）

- **裸机直跑（无 RTOS）**。**采纳。** 理由：① 被修复者不能依赖被修复者——bootloader 的存在意义 = 系统坏了还能救，若它依赖同一套 RTOS/框架初始化，那套初始化坏了它同样起不来；② 提交期确定性（秒级擦写/搬移期间无调度抢占）；③ 生态共识（六方成熟 loader 全裸机，K-16）；④ 框架裁剪形态的验收场——同一套 API（FlashDev/日志三件套）两种形态（完整 OS / 裁剪直跑）。
- RTOS 上运行：开发体验一致，但违背自举依赖原则与生态共识。**否决。**

### 同步面语义（Q-11a）

- **同步面保真**：mutex = 0/1 互斥开关（配合 irq_lock 临界区）、sem = 生产消费计数、completion 直通；等待语义由 idle 刷新驱动（XRobot RefreshTimerInIdle 同款）——基于 osal 的 sync/async 组件在裸机行为一致（K-19）。**采纳。** 依据：mutex/sem 语义与抢占无关（FreeRTOS 协作模式/Zephyr coop 佐证）——它们表达资源/事件状态 + 临界区纪律，单执行流下不塌缩；驱动正确性跨形态保持。
- U-Boot compat.h 全量空转宏：最简但裸机上可写出"看似线程安全实为无意义"的伪同步代码。**否决。**

### 任务面落地范围（Q-11b）

- **首版单执行流**：thread 创建返回 NOT_SUPPORTED；async 随 OM_FLASH_SYNC_ONLY 编除（FlashDev 同步直跑）；协作式多任务（独立栈 + 显式/阻塞让出 + 软定时器）记演进。**采纳。** 理由：bootloader 是单流程，不需要调度；协作多任务应由未来裸机业务需求驱动落地（YAGNI），不被 bootloader 工程绑架。
- 首版即协作多任务（XRobot 裸机形态完整复刻）：bootloader 场景用不上，周期拉长。**否决（首版）。**

### osal-none 落点

- **正式 osal-none 端口**（`platform/osal/none`，与 freertos 端口并列，os= 配置可选）。**采纳。** 裁剪形态成为框架一等公民；本地桩（host_osal.c 先例）否决——裁剪形态无正式落点，bootloader 代码的 osal 调用面依赖随工程携带的桩，技术债。

## 最终决策 (Decision)

- **bootloader 运行时形态 = 裸机直跑**：代码与 app 同一套框架 API（FlashDev 同步等待原语 + 日志裁剪三件套），经编译期裁剪落地。
- **新增 osal-none 端口**（`platform/osal/none`）：
  - 时间面（sleep/now：SysTick/DWT 轮询）与中断面（irq_lock：真关中断）保留；
  - 同步面保真：mutex = 0/1 互斥开关、sem = 计数、等待由 idle 刷新驱动（不提供空转伪同步）；
  - 线程面：thread 创建返回 NOT_SUPPORTED；
  - async 随 OM_FLASH_SYNC_ONLY 编除（FlashDev 同步直跑，D-08）。
- **协作式多任务记演进**（触发条件 = 框架裁剪形态的裸机业务需求出现；XRobot 裸机形态为参考，K-19）。

## 影响 (Consequences)

- 正面：裁剪形态成为框架一等公民（os=none 合法取值）；bootloader 与 app 共享代码路径（同一 API 两种形态）；同步面保真使基于 osal 的 sync 组件（completion/sem 族）在裸机上可正常运行——保证基于 sync/async 的驱动功能正确性跨形态一致；无"空转伪同步"技术债。
- 行为：bootloader 工程 = os=none 配置 + OM_FLASH_SYNC_ONLY + 日志三件套裁剪；osal-none 端口内不可用原语显式返回 NOT_SUPPORTED（编译期可检）。
- 约束：首版单执行流——无任务调度（bootloader 单流程不受影响）；协作多任务落地前，裸机业务须自行分时（演进项）。
- 兼容：FreeRTOS 端口零改动；kernel-core OS 无关约定（CLAUDE.md）背书无 OS 形态；`platform/` 新增 osal/none 目录与构建选择。
- 后续：osal-none 端口实现随步骤 ② bootloader 工程落地（第一个消费者与验收场）；K-16 含义列已修订指向本 ADR 与 K-19。
