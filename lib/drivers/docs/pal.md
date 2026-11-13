# 外设抽象层（PAL）

PAL 契约按外设分布在 `drivers/peripheral/<periph>/pal_*.h`：GPIO、SPI、PWM、CAN、Flash、Serial。契约只描述框架需要的能力，不暴露芯片厂商类型或寄存器。

- 框架实现位于 `drivers/src/peripheral/`。
- 平台适配位于 `platform/bsp/`，负责实现 PAL 契约中的 ops。
- 具体器件实现位于独立仓库 `oh-my-robot-devices`，只依赖 PAL 公开契约与 kernel/sync。

不再维护聚合伞头 `pal_dev.h`；按外设包含最小契约头即可。
