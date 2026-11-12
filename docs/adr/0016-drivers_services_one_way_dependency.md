# ADR-0016：drivers → services 单向依赖（按服务逐个开放，先 log）

- 状态：已采纳（随 feature/log-service 落地）
- 日期：2026-08-20
- 参考：Zephyr（驱动广泛依赖 `subsys/logging`，`LOG_ERR` 遍地）、Linux（驱动 `pr_err`）、RT-Thread（驱动 `LOG_E`）——**成熟生态中驱动依赖日志接口是常态**

## 背景 (Context)

1. **services 的定位与依赖规则冲突**：services 是"通用服务层"（日志/配置/通信/诊断/文件系统），语义上服务**全体消费者**；但原依赖规则"services 与 drivers 对等互不依赖"禁止 drivers 依赖它——最大的消费群体被排除在外
2. **drivers 是日志的主要消费场景**：HAL 初始化失败、DMA 错误、外设超时——错误现场（`file:line`）在驱动内部；现有机制只有 return OmRet 或 vestigial errhandler，无框架级日志通道
3. **后端实现无合法归属层**：串口日志后端需同时见 `services/log/log.h`（接口）与 drivers 设备模型（设备）——"对等互不依赖"下 services/drivers 均不合法，platform 只依赖 kernel/third_party，只能落 samples/组合层

## 考虑过的方案 (Options)

- **维持对等互不依赖（否决）**：后端只能落 samples 示范，drivers 错误路径永远无日志通道——与业界（Zephyr/Linux/RT-Thread 均允许 drivers→logging）背离
- **services ↔ drivers 双向互依赖（否决）**：循环依赖风险；services 被驱动细节污染，破坏"服务语义项目无关"边界
- **日志接口下沉 kernel（否决）**：log 拆两半（kernel 极简路径 + services 完整服务）——与 ADR-0015 (log_service)"log 是 services 独立服务"定位冲突，重复设施
- **drivers → services 单向、按服务逐个开放（采纳）**：横切服务（log/config）应被全体消费者依赖；业务子系统之间（chassis ∥ gimbal 等）仍互不依赖；**依赖面逐个开放**——本次仅开放 `services/log/log.h`，config/comm/diagnostics/fs 被 drivers 依赖需另行决策，防止过早冻结未定型服务的 API

## 最终决策 (Decision)

- **依赖规则修订**：`drivers` 可**单向**依赖 `services` 的**服务接口**（公开头文件）；`services` 永不依赖 `drivers`；业务子系统之间、drivers 内部各模块之间仍互不依赖；`platform` 依赖规则不变（只能依赖 kernel/third_party——后端实现不落 platform）
- **依赖面按服务开放**：当前开放清单 = `services/log/log.h`；其余服务按需另议（新 ADR 或本 ADR 增补）
- **后端实现随依赖的驱动家族存放**（参照 Zephyr：log_backend_uart 在 drivers/console、log_backend_net 在网络栈——后端跟随它依赖的子系统，无集中目录）：串口日志后端落 `lib/drivers/src/peripheral/serial/`（与 hal_serial 同族——依赖其开放语义 SERIAL_O_NBLCK_TX/txFifo/溢出错误回调）；未来 flash/SD 后端随 flash 驱动家族存放；零驱动依赖后端（RAM 缓冲）落 `services/log/backends/`；接口与注册机制（`OmLogBackend` + 后端表）保持 **services 单一属主**（ADR-0015）
- **串口日志后端形态**：drivers 层提供工厂 `om_log_serial_backend_register(Device *serial_dev, const char *name, OmLogLevel level)`；组合层（app/samples）接线（device_find + open + register）

## 影响 (Consequences)

- **正面**：drivers 错误路径获得框架级日志通道（file/line 现场）；串口后端合法落位 drivers 层（依赖方向由文件归属决定）；与业界对齐（Zephyr/Linux/RT-Thread）
- **约束**：`services` 不得 include 任何 drivers 头（单向铁律，CI/审查按 include 方向把关）；drivers 仅依赖开放清单内的 services 公开接口，不触碰 services 内部实现；新服务开放需独立决策
- **兼容**：services 现有 API 零变化；drivers 现有代码零变化（规则开放不强制）；分层图语义更新（services 可被 drivers 依赖）
- **演进**：config 等服务的 drivers 依赖按需开放；各驱动家族随需求增补日志后端（串口 → flash/SD）
