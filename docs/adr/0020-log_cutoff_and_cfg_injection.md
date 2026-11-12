# ADR-0020：日志裁剪零副作用契约与工程配置注入全单元化

- 状态：已实施（feature/log-ring）
- 日期：2026-09-05
- 参考：ADR-0017 (project_config_layering)、ADR-0019 (log_unified_ring)

## 背景 (Context)

`OM_USE_LOG=0`（服务级裁剪——最小配置/bootloader 前期）实证审计发现两类缺陷：

1. **公共 API 裁剪面裸露**：`log.h`/`rtt_backend.h`/`log_serial_backend.h` 无 `#if OM_USE_LOG`
   守卫——类型/宏/声明全暴露，而实现全数 `#if` 隔离 → 裁剪构建下外部代码（样例/组合层）
   编译通过、**链接期 undefined reference**（三个 verify 目标实证）——信号滞后（编译期
   报错优于链接期幽灵接口）。
2. **工程配置注入 binary-only**：`project_cfg.lua` 的 `if target:kind() ~= "binary" then
   return end` 使 144/367 编译单元（drivers/osal/HAL/FreeRTOS 等库源）未达
   `OM_USE_APPCFG` 宏——**库源配置与 binary 不一致**：`log_serial_backend.c` 的
   `#if OM_USE_LOG` 锋利边守卫（此前修复）从未真正以 0 编译过——配置分层的"所有源码
   统一可见"语义（ADR-0017）在库目标上不成立。

## 考虑过的方案 (Options)

### 裁剪行为

- **接口整体 `#if` 消失（采纳）**：类型无条件（外围结构体引用不受扰）；函数声明
  `#if`（调用=编译期错误——信号清晰）；调用宏 no-op（`OM_LOG_*`→`((void)0)`、
  `OM_LOG_MODULE` 不生成实例）——Zephyr `CONFIG_LOG=n` 同款业界标准。
- **运行时 stub 返回错误**：接口常在（返回 OM_ERR_*）——保留幽灵接口 + 链接面复杂度，
  与"打日志无失败路径"哲学相悖；**否决**。
- **全删接口（声明/类型都 `#if`）**：外围结构体（如 `OmLogBackend`/`OmLogStats`）被
  组合层引用——纯删除造成下游编译面破坏；**否决**（类型保留是契约一部分）。

### 注入范围

- **全部编译单元（采纳）**：配置分层语义（ADR-0017）= 所有源码一致可见工程配置；
  库单元执行守卫/配置宏才真实生效。挂法 = 各目标**定义处** `add_rules`——实测
  （2026-09-05）：库目标脚本尾部 `_t:add_rules` 不触发 `on_config`、binary 侧规则
  `{public=true}` 传播亦不生效；与 context 规则同法（逐 target 定义处挂接）。
- **binary-only（现状，否决）**：库源配置漂移（本审计的锋利边形同虚设即其后果）。
- **仅 defines 不扩 includedirs**：宏源=appcfg 头（`OM_USE_APPCFG` 触发包含）——
  扩展必须一对（defines+includedirs）；**否决**半套。

## 最终决策 (Decision)

- **裁剪契约（`OM_USE_LOG=0` 零副作用）**：
  - 类型（`OmLogLevel/OmLogModule/OmLogBackend/OmLogStats`）无条件——外围可引用
  - 函数声明（`om_log_*`/`om_rtt_backend_register`/`om_log_serial_backend_register`
    等）随 `#if OM_USE_LOG` 消失——调用=编译期错误
  - 调用宏 no-op（`((void)0)`；`OM_LOG_MODULE` 不生成实例）；组合层（后端接线/记录
    调用）由使用方 `#if`（样例即教学模板）
  - 实现全部 `.c` `#if` 隔离——开关关闭零 RAM/栈/ROM
- **注入全单元化**：`project_cfg.lua` 移除 binary gate；lib（13 库）/platform
  （osal/sync/ipc/bsp/freertos tar_os）/板（3 板 tar_board）目标定义处挂接规则；
  规则内缓存清理 `os.rmdir` 加 `os.isdir` 存在性守卫（xmake 3.1 对不存在目录视作
  致命——CI 实证 exit 255）；CI 配置 `xmake f -c` 改 `xmake f`（clean checkout 无需）。

## 影响 (Consequences)

- 正面：裁剪=编译期信号 + 调用宏 no-op（三种使用面全零副作用）；库源与 binary 配置
  一致——锋利边类守卫真实生效（`log_serial_backend.c` 修复首次以 `OM_USE_LOG=0`
  编译验证）；`OM_USE_LOG=0` 全目标构建绿（框架+样例双零副作用）。
- 行为变化：全量重编一次（注入扩展到库目标）；未来新增库/板目标**必须**定义处挂接
  `oh_my_robot.project_cfg`（约定写入规则与 lib/xmake.lua 注释——漏挂=该单元配置漂移，
  构建期无告警——由 review 与规则内注释承担）。
- 约束：样例（组合层示范）承担 `#if` 教学义务；`OM_LOG_MODULE` no-op 后直接引用
  `_om_log_module` 的手写代码须自行守卫（fatal 组合点先例）。
- 兼容：`OM_USE_LOG=1`（默认）行为完全不变（host 三目标 + 双工具链回归绿）。
