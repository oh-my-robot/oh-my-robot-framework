drivers/include/ 目录下的 drivers 是刻意保留的，这样外界在引用时是这样的形式：
#include "drivers/peripheral/can/pal_can_dev.h"
这是刻意保留的“命名空间”，可以让头文件路径更稳定、语义更清晰，也避免和其它库的头文件名冲突。其他地方也有类似的设计。

该命名空间只属于驱动框架（设备模型、总线/设备类框架、PAL）。具体器件实现位于独立仓库 `oh-my-robot-devices`，使用 `devices/` 命名空间，不进入 `drivers/`。