# ADR-0012：板瘦身——vendor 适配层 + 板数据契约

- 状态：已实施（CAN 试点，rm-a/rm-c，gnu-rm + armclang 验证通过）
- 日期：2026-07-30
- 参考：Zephyr devicetree（板=数据、驱动=通用）、RT-Thread BSP 模式

## 背景 (Context)

`platform/bsp/boards/<board>/source/peripherals/` 逐板复制外设实现。实测 rm-a/rm-c 两板 8 对外设文件 ~3000 行，**>2700 行（85-90%）逐字相同或仅数据不同**（CAN 96%、GPIO 99%、Serial 97%、SPI 95%、PWM 60% 已语义漂移），且已出现复制漂移 bug（rm-c `serial_cfg_s` 拼写错，虽在死代码块）。板数增长时实现按板线性复制，维护成本与漂移风险随板数膨胀。这些不是框架本体，是适配层——应收敛为"共享实现 + 板数据表"。

## 考虑过的方案 (Options)

- **方案 A：保持每板复制现状**——板数少时可行，随板数膨胀失控；已出现漂移 bug，否决。
- **方案 B：共享实现下沉到 lib/drivers**——lib/drivers 的 hal_*.c 已含全部通用状态机，但适配层含厂商 HAL 类型（`CAN_HandleTypeDef` 等），下沉会污染框架层，否决。
- **方案 C（采纳）：vendor 共享适配层 + 板数据契约**——适配层放 `platform/bsp/vendor/<V>/<family>/adapters/<periph>/`（与厂商 HAL 同族），板侧只留数据表；板 opt-in 引用适配层文件（永不经 vendor/chip sources 隐式编译）。
- **方案 D：板数据契约用链接期符号 vs 宏+extern**——链接期符号（`BSP_CAN_COUNT` 宏 + extern 表）配合三方钳制（实现侧 `#error`、数据 TU 编译期断言、审查），漏数据=响亮失败。

## 最终决策 (Decision)

- **共享适配层**：`platform/bsp/vendor/STM32/STM32F4/adapters/can/{bsp_can_f4.h,.c,_it.c}`——契约头（类型+家族常量+板数据 extern 声明）、共享实现（含 `OM_INIT_BOARD` 自注册）、ISR（两板逐字相同上移）。
- **板数据契约**（板 opt-in 后必须提供）：`BSP_CAN_COUNT` 宏（板 shim bsp_can.h）+ `gBspCan[]`/波特率表（psc=0 哨兵项）/引脚表/中断表（板 bsp_can_data.c）。
- **opt-in 铁律**：适配层实现文件进板 lua `selfreg_sources`（含 OM_INIT）、ISR 进 `override_sources`；**永不进 vendor/chip sources**。不用该外设的板零改动；lp-mspm0g3507（TI）不涉及。
- **契约守卫**：实现侧 `#ifndef BSP_CAN_COUNT #error`；数据 TU 编译期断言（C99 `typedef char ok[(cond)?1:-1]`，不用 `_Static_assert`——armclang 严格 C 拒绝非编译期常量初始化，如 `(BANK_LIST)[0]`）。
- **时钟数据化取舍**：GPIO/外设时钟使能宏无法数据化（是宏不是对象），用家族 switch-helper（F4 仅 8 端口 + CAN1/2；`#if defined(GPIOJ)` 按芯片守卫 F427/F407 差异）——Zephyr clock-control 查表的等价物，不增板数据负担。
- **lib/drivers 唯一改动**：`hal_can_register` 补 `hwInterface` 非空校验（防共享化后漏赋值在 can_open 处 NULL 解引用，症状晚）。
- **linkguard 按 vendor 符号检查（延后）**：防"漏引用适配层→静默无设备"场景（数据文件被自动 glob 但适配层未接线）。当前决策延后——防的是开发者半途应用配方错误而非框架缺陷，配方铁律 + review + 首测 device_find 可兜住；Zephyr 的 devicetree→驱动绑定构建期校验是对口先例，真实踩到后实现。

## 影响 (Consequences)

- **正面**：新增同族板 = 写板数据表（实例/波特率/引脚/中断 ~50 行）+ 三处 lua 引用，零实现复制；修复两板 CAN 的波特率/引脚差异统一由数据表达；重复实现 96% 收敛。
- **约束**：适配层文件 opt-in 引用（自注册/ISR 保活依赖 selfreg/override 直连）；板 shim 头保留 `USE_CAN*`/`BSP_CAN_COUNT`；数据表末尾须 psc=0 哨兵（extern 表不可 sizeof）。
- **已知静默场景（已记录，待 linkguard 扩展）**：板有数据但漏引用适配层 → 构建通过、设备静默缺席；按配方铁律 + review 兜底。
- **推广**：GPIO（99%）、Serial（97%，顺带修 rm-c `serial_cfg_s` 拼写 bug）、SPI（仅 rm-a，先建契约为未来板）、PWM（先对齐两板语义漂移再共享）。
- **构建侧**：`inputs.lua`/`board_assets.lua` 零改动（selfreg/override 三级合并已支持）；vendor/chip/board 的 sources/override_sources/selfreg_sources 键位即适配层的接线通道。
