# OM 框架移植向导

本文档说明将 oh-my-robot-framework 移植到新 MCU / 新 RTOS 时，必须提供的配置入口及其职责。

## 配置入口全景

```
include 顺序:  om_port_compiler.h → om_port_hw.h → om_osal_portdef.h → om_config.h
                           │              │                │                │
                        编译器检测      中断原语        OS 强制参数      功能裁剪
```

| 入口 | 谁写 | 性质 | 位置 | 触发方式 |
|---|---|---|---|---|
| `om_osal_portdef.h` | OS 端口适配者 | **强制** | `platform/osal/<os>/` | `osal_config.h` 直接 `#include` |
| `om_bsp_portdef.h` | BSP 作者 | **预留**（当前空） | `platform/bsp/boards/<board>/` | 待定 |
| `om_appcfg.h` | 应用作者 | **可选**（框架层域 `OM_*`） | `<project>/cfg/` | 自动发现（`oh_my_robot.project_cfg` 规则注入 `OM_USE_APPCFG`） |
| `om_boardcfg.h` | 应用作者 | **可选**（板层域策略宏） | `<project>/cfg/boards/<board>/` | 自动发现（同规则注入 `OM_USE_BOARDCFG`） |

**配置覆写优先级**（自高至低）：命令行 `-D` > 工程片段（appcfg/boardcfg）> 板默认（框架板头 guard 块）> 框架默认（`om_config.h`）。**四类载体与板事实/策略切分见 ADR-0017 (project_config_layering)。**

## 1. `om_osal_portdef.h` — OS 端口强制定义

### 何时需要

每次将框架适配到新的 RTOS（或更换 RTOS 版本）时，OS 端口适配者必须在对应 `platform/osal/<os>/` 目录下创建此文件。

### 必须提供的内容

以 FreeRTOS 为例：

```c
// platform/osal/freertos/om_osal_portdef.h

#ifndef OM_OSAL_PORTDEF_H
#define OM_OSAL_PORTDEF_H

/* Event Flags 可用位宽 — FreeRTOS 使用 uint32_t 的低 24 位 */
#ifndef OM_OSAL_EVENT_FLAGS_USABLE_MASK
#define OM_OSAL_EVENT_FLAGS_USABLE_MASK   0x00FFFFFF
#endif

/* 对齐 FreeRTOSConfig.h 的默认值 — 若项目的 config 不同，通过 -D 覆写 */
#ifndef OM_OSAL_PRIORITY_MAX
#define OM_OSAL_PRIORITY_MAX              32u
#endif

#ifndef OM_OSAL_TASK_NAME_MAX
#define OM_OSAL_TASK_NAME_MAX             16u
#endif

/* sync 加速后端能力声明 — OS 端口自报'我能做什么' */
#ifndef OM_SYNC_ACCEL_CAP_COMPLETION
#define OM_SYNC_ACCEL_CAP_COMPLETION      1
#endif

#endif
```

### 职责边界：能力声明 vs. 策略开关

`om_osal_portdef.h` 中的 `CAP_*` 宏声明的是**能力**——OS 端口能做什么。应用层通过 `om_config.h` 的 `OM_SYNC_ACCEL` **策略开关**决定要不要用。

```
om_osal_portdef.h  →  CAP_COMPLETION = 1    (FreeRTOS 能做 CAS+TaskNotify)
om_config.h        →  OM_SYNC_ACCEL = 1      (应用要开加速)
completion.c       →  两者皆为 1 时走加速路径，否则回退 reference
```

应用开发者只需要关心 `OM_SYNC_ACCEL` 一个开关。如果开了但端口不支持——`#if` 检查会 fall through 到 reference，不会出错。

### 工作机制

```
osal_config.h:
  #include "om_osal_portdef.h"   ← 若文件不存在 → 编译错误

osal_event.h:
  #ifndef OM_OSAL_EVENT_FLAGS_USABLE_MASK
  #error "..."                   ← 端口定义文件存在但值缺失 → 编译错误
  #endif
```

- **xmake 构建**：构建系统自动将 `platform/osal/<os>/` 加入 include path
- **手动工程**（CCS/IAR 等）：需手动将该目录加入 include path
- 值可通过 `-D` 编译选项覆盖（`#ifndef` 保护）

### 如何验证

配置完成后，编译 OSAL 层目标。若通过，说明所有强制参数已就位。

## 2. `om_bsp_portdef.h` — BSP 端口定义（预留）

当前版本 BSP 级无强制参数。`om_port_hw.h` 已覆盖中断原语。本文件为未来扩展预留（如 Flash 起始地址、内存布局差异等）。

## 3. `om_appcfg.h` — 应用配置覆写（框架层域）

### 何时使用

当 `om_config.h` 提供的默认功能裁剪（`OM_USE_*`）或优化开关（`OM_SYNC_ACCEL`）需要调整时，应用作者在工程目录创建 `<project>/cfg/om_appcfg.h`（构建规则自动发现并注入 `OM_USE_APPCFG`——零命令参数）。

### 板层覆写（`om_boardcfg.h`）

板级策略宏（见"板事实 vs 策略"说明）在 `<project>/cfg/boards/<board>/om_boardcfg.h` 覆写——**模板见 `docs/config/om_boardcfg.h.example`**；职责边界与守卫语法即模板内注释。换板（preset `board=`）时构建自动选择对应片段目录。

### 板身份宏（登记表）

板数据 lua `defines` 注入，编译级全局；boardcfg 守卫与多板条件逻辑的事实源：

| 板 | 宏 |
|---|---|
| rm-a-board | `OM_BOARD_RM_A` |
| rm-c-board | `OM_BOARD_RM_C` |
| lp-mspm0g3507 | `OM_BOARD_LP_MSPM0G3507` |

### rm-a 覆写宏表（策略类——Ⅰ类）

| 宏 | 默认 | 含义 | 备注 |
|---|---|---|---|
| `BSP_LOG_BAUD`（语义键） | `115200u` | 日志口波特率（意图键——boardcfg 覆写本键即生效） | 板内映射 usart6 |
| `BSP_LOG_SERIAL_NAME` | `"usart6"` | 日志口实例名 | 板事实约束：须为已配置实例 |
| `BSP_SERIAL6_BAUD` | `BSP_LOG_BAUD` | 6 号串口（=日志口）实例波特率 | 适配器消费点；CLI `-D` 仍可直覆 |
| `BSP_SERIAL6_TXBUFSZ` | `1024` | 6 号串口 TX FIFO | ≥ 最大消息长度保完整 |
| `RM_A_SERIAL37_PROFILE` | 1 | 串口 3/7 DMA 方案包 | 0/1 见板头注释 |

板【事实】类（引脚/DMA stream/中断/实例表/`USE_SERIAL_3`/`BSP_SERIAL_COUNT`）不可经配置头覆写——修改 = 板定义变更（复制板单元 DIY 流程）。

### 示例

```c
// <project>/om_appcfg.h

/* 关闭不需要的 HAL */
#undef OM_USE_HAL_CAN

/* 开启 sync 加速后端 */
#define OM_SYNC_ACCEL 1

/* 自定义日志级别：log 服务就绪后经 services/log/log.h 的 OM_LOG_LEVEL_* 配置
 * （om_config.h 的 OM_USE_LOG / OM_LOG_* 宏），appcfg 阶段无级别常量 */
```

### 工作机制

```c
// om_config.h 头部（默认值定义之前）:
#ifdef OM_USE_APPCFG
#include "om_appcfg.h"
#endif
```

- **前置语义**：用户宏先于默认值生效——默认宏均为 `#ifndef` 守卫（仅当未定义时设置），appcfg 直接 `#define` 覆写即可，**无需 `#undef` 再重定义**（后置写法会与默认值宏重定义冲突）
- 默认不启用（未定义 `OM_USE_APPCFG` 时不做任何额外 include）
- 启用方式（二选一）：① 工程 `cfg/om_appcfg.h` 存在——`oh_my_robot.project_cfg` 规则自动注入 `-DOM_USE_APPCFG` 与 include path（推荐）；② 编译选项手工 `-DOM_USE_APPCFG` + 自保 include path（legacy）
- **片段书写约定**：`om_appcfg.h` 与 `om_boardcfg.h` 均须自带声明式守卫（`#ifndef` 骨架见模板）——`om_boardcfg.h` 尤其必要：bsp 聚合头（`bsp.h`）与直接包含（`bsp_serial.h`）并存时，同一 TU 会对板头重复包含本文件（同值宏重复定义虽无碍，非幂等内容/分歧值会冲突）

## 移植检查清单

- [ ] `platform/osal/<os>/om_osal_portdef.h` 已创建且包含所有强制参数
- [ ] `om_osal_portdef.h` 所在目录已在 include path 中
- [ ] `osal_config.h` 编译通过（确认 `#include "om_osal_portdef.h"` 找到了文件）
- [ ] FreeRTOSConfig.h 中的值与 `om_osal_portdef.h` 中的值一致（或通过 `-D` 覆写）
- [ ] 编译 OSAL + sync 目标，无 error
