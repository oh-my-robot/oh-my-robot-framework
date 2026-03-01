FreeRTOS 内核目标说明

- 通过 om_freertos_kernel 统一编译 FreeRTOS 内核与 portable。
- OSAL 端口仅依赖该内核目标，不与 BSP 直接耦合。
- FreeRTOSConfig.h 由板级配置负责提供/调整。
