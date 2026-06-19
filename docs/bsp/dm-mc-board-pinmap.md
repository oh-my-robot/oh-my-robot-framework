# DM-MC02 / DM-MC-Board02 Pinmap

当前文档只保留本阶段已经确认、且当前代码真的在用的资源。

| 资源 | MCU 引脚 | 外设 | AF | 当前用途 | 备注 |
| --- | --- | --- | --- | --- | --- |
| USART1_TX | PA9 | USART1 | AF7 | 基础串口 | 已实现 |
| USART1_RX | PA10 | USART1 | AF7 | 基础串口 | 已实现 |
| UART7_TX | PE8 | UART7 | AF7 | 基础串口 | 已实现 |
| UART7_RX | PE7 | UART7 | AF7 | 基础串口 | 已实现 |
| UART8_TX | PE1 | UART8 | AF8 | 基础串口 | 已实现 |
| UART8_RX | PE0 | UART8 | AF8 | 基础串口 | 已实现 |
| UART9_TX | PD15 | UART9 | AF11 | 基础串口 | 已实现 |
| UART9_RX | PD14 | UART9 | AF11 | 基础串口 | 已实现 |
| USART10_TX | PE3 | USART10 | AF11 | 监视串口 | 已实现 |
| USART10_RX | PE2 | USART10 | AF4 | 监视串口 | 已实现 |
| POWER_5V_EN | PC15 | GPIO | - | 可控 5V | 当前实现按低电平使能 |
| POWER_OUT1_EN | PC14 | GPIO | - | 可控 OUT1 | 当前实现按低电平使能 |
| POWER_OUT2_EN | PC13 | GPIO | - | 可控 OUT2 | 当前实现按低电平使能 |
| VBAT_ADC | PC4 | ADC1_INP4 | - | 输入电压采样 | `Vin = Vadc * 11` |
| BMI088_ACC_CS | PC0 | GPIO | - | IMU 片选 | 当前禁止再当 LED 使用 |
| WS2812_DATA | PA7 | SPI6_MOSI | 待后续 | 板载彩灯 | 本轮未实现 |
