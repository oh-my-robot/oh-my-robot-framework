# SEGGER RTT（外部库，仅供适配使用）

**来源**：[github.com/SEGGERMicro/RTT](https://github.com/SEGGERMicro/RTT)（官方仓库，原样快照）
**许可**：BSD 三句版，版权与条件见同目录 `LICENSE.md`——分发须保留版权声明
**状态**：未修改源码（仅 `SEGGER_RTT_Conf.h` 为最小配置壳——空体走 `SEGGER_RTT_ConfDefaults.h` 全部默认值）

## 目录内容

| 文件 | 说明 |
|---|---|
| `SEGGER_RTT.c / .h` | 核心实现与接口（官方原版） |
| `SEGGER_RTT_Conf.h` | 配置壳（本仓库唯一"修改"——自定义覆盖：**`BUFFER_SIZE_UP=4096`** 见下） |
| `SEGGER_RTT_ConfDefaults.h` | 官方默认配置（3 上/3 下通道、UP 1024B、NO_BLOCK_SKIP=0） |
| `LICENSE.md` | BSD 许可文本（分发必须保留） |

**`BUFFER_SIZE_UP=4096`（订正）**：压测实证（2026-09-04 21 格矩阵）——默认 1024B 时宿主读一轮停顿即触发 3-5% 稳态丢包（环形余量仅 ~25ms）；4096B 下 225KB/s 满速率零丢，与 Zephyr 默认一致。代价 +3KB RAM。

## 使用契约（框架内）

- **注入方式**：`build/rules/selfreg.lua` 的 `SELFREG_FILES` 直接把 `SEGGER_RTT.c` 编译进 binary（第三方库无静态库抽取风险；`third_party/` 因此不在任何 `tar_*` 聚合内）。
- **消费者**：`lib/services/log/backends/rtt_backend.c`（日志 RTT 后端）。
- **仅通道 0**：`SEGGER_RTT_Init()`/`_DoInit()` 只配置上向通道 0（挂默认缓冲）；通道 1..MAX-1 描述符零态——写出将写坏内存，禁止使用（后端注册即锁死通道 0）。
- **首写自动初始化**：`SEGGER_RTT_Write` 含 `INIT()`（acID 魔数守卫）；无需显式 `SEGGER_RTT_Init()`。
- **升级纪律**：升级时整体替换 `SEGGER_RTT.c/.h` + 两个 Conf 文件，保留本 README 的契约核对（通道 0 行为、默认配置）；重大变化（API 变更/默认配置变更）需同步后端与架构文档。

## 目录豁免

`third_party/` 是"非框架作者代码"的边界：不进 framework 的 clang-format/命名/评审门（CI format 扫描范围为 `lib`/`platform`/`samples`），不改写内部风格。
