drivers/include/ 目录下的 drivers 是刻意保留的，这样外界在引用时是这样的形式：
#include "drivers/motor/motor.h"
这是刻意保留的“命名空间”，可以让头文件路径更稳定、语义更清晰，也避免和其它库的头文件名冲突。其他地方也有类似的设计。