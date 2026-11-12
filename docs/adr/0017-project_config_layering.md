# ADR-0017 (project_config_layering) — 工程配置分层：四类载体 + 多板片段机制

## 背景

配置现状：框架默认值集中 `lib/include/core/om_config.h`；用户覆写入口 `om_appcfg.h` 已存在（`OM_USE_APPCFG` 守卫引入），但启用需手工注入编译宏与 include path；板级配置分散在板数据 lua（构建资源）、板头 `bsp_*.h`（宏默认值）与板数据表（实例/引脚/中断），板头宏是否可覆写、从哪覆写无系统约定。多板工程（同一工程源码面向多块板构建）尚无工程侧承载机制。

## 考虑过的方案

1. **单头统一覆写（om_appcfg.h 一统）**：板宏也经 om_appcfg.h 覆写。否决：板宏定义在平台层头引入点，统一头使板宏可见性向框架/用户空间扩散（域泄露）。
2. **头内板段条件编译（boardcfg 内 `#if defined(OM_BOARD_RM_A)` 分段）**：否决——不是统一机制而是分流；且要求用户在用户空间书写板内宏名（板知识倒灌），无成熟框架采用此形态。
3. **生成聚合配置头（构建期生成 om_target_cfg.h 合并各层）**：弃选——guard 合并已足够，生成器边际收益（冲突检测）对当前宏面不成立。
4. **文件片段 + 构建选择（本决策）**：工程侧目录承载分策略文件，构建按板选择——Zephyr `prj.conf + boards/<board>.conf` 同构；合并语义沿用现有 `#ifndef` guard 链，零新机制。

## 最终决策

1. **四类配置载体**（职责与优先级）：
   - 命令行 `-D`（最高）——构建注入，临时调变
   - 工程片段：`<project>/cfg/om_appcfg.h`（框架层域 `OM_*`）+ `<project>/cfg/boards/<board>/om_boardcfg.h`（板层域：Ⅰ类策略宏/语义键）
   - 板默认值：`platform/bsp/boards/<board>/include/bsp_*.h`（板头 guard 宏定义默认）
   - 框架默认值：`lib/include/core/om_config.h`
2. **板身份宏**：板数据 lua `defines` 注入 `OM_BOARD_RM_A` / `OM_BOARD_RM_C` / `OM_BOARD_LP_MSPM0G3507`（编译级、三板统一、登记在 porting-guide）——机制基础件：boardcfg 板守卫、未来多板条件逻辑的唯一事实源。
3. **boardcfg 职责边界**：按板覆写板层**策略宏**（Ⅰ类：波特率/FIFO/日志口选择/DMA profile 等性能与用途类）；承载板身份守卫（首行 `#if !defined(OM_BOARD_<TARGET>) #error`——换板构建错误化，杜绝静默错配）；单板语境（片段按 preset board 自动选择，仅目标板片段被编译）；每片段只描述"这一块板"。
4. **板事实归属（不可覆写面）**：引脚映射、DMA 流/通道/中断分配、实例表（`bsp_*_data.c` 数据表 + Ⅱ类宏如 `USE_SERIAL_3`/`BSP_SERIAL_COUNT`）——板硬件模型，归框架板层；修改 = 板定义变更（复制板单元 DIY 流程），不在配置头能力内。
5. **语义键（跨板意图层）**：板头提供稳定接口宏（如 `BSP_LOG_BAUD`）映射到本板实际参数——用户片段只写意图键，不写板内名称；先例键 `BSP_LOG_SERIAL_NAME`。契约：板需提供该接口宏方可支持对应语义键。
6. **机制形态**：新构建规则 `oh_my_robot.project_cfg`（同 `board_assets` 读 `context.board_name`）自动发现工程片段——存在即注入 `-DOM_USE_APPCFG`/`-DOM_USE_BOARDCFG` 与片段目录 includedirs；不存在零成本。板片段目录形态：`cfg/boards/<board>/`（子目录——允许一板多文件）。
7. **多板工程模型**：一次构建一板（preset `board=`），工程携带 N 板片段；多板矩阵=循环构建（后续封装），机制不变。

## 影响

- 板头新增协约：每个 `bsp_*.h` 顶部（默认值定义前）挂 `#ifdef OM_USE_BOARDCFG #include "om_boardcfg.h" #endif` 钩子（分散各头——直接 include 单头的场景存在，聚合头不足以保证）；Ⅰ类宏统一 `#ifndef` guard 形态。
- 板数据 lua `defines` 字段启用（三板注入身份宏）。
- 用户可见行为：配置文件从"一个 om_appcfg.h"发展为 `cfg/` 目录（向后兼容：根置 `om_appcfg.h` + 手工 `-DOM_USE_APPCFG` 仍有效）。
- 文档：porting-guide 配置章重写（角色表/身份宏登记/覆写宏表/模板）；`docs/config/om_boardcfg.h.example` 模板。
- 边界声明：值域/单位校验无编译期强制（宏文本替换），靠覆写表文档 + review；跨域误用（appcfg 写板宏）机制不拦截、文档禁止。
- 后续推广项：rm-c/lp 板头钩子与 guard 补齐（本决策在 rm-a 试点落地）；语义键在接入日志口的板上推进（rm-c/lp 未接入日志口——补接入时按契约加键）。
