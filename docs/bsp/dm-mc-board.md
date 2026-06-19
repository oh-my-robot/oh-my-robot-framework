# DM-MC02 / DM-MC-Board02 BSP

板卡内部名：`dm-mc-board`

## 当前范围

当前主线只推进：

- 五路基础串口
- `dm_mc02_bsp_smoke`
- `dm_mc02_loopback`
- `board_power` 第一阶段
- `WS2812`
- 蜂鸣器
- 用户按键

当前暂不推进上板联调：

- `CAN1 Classic CAN`
- `CAN2/CAN3`
- `CAN FD`
- BMI088
- LCD
- Camera

## 当前层级

- `platform/bsp/boards/dm-mc-board`
  - 底层平台外设层
  - 当前承接：`serial`、`can`
- `app/bsp/dm_mc_board`
  - 板级应用封装层
  - 当前承接：`board_power`、`WS2812`、蜂鸣器、按键

## 当前串口资源

- `USART1`：`PA9 / PA10`
- `UART7`：`PE8 / PE7`
- `UART8`：`PE1 / PE0`
- `UART9`：`PD15 / PD14`
- `USART10`：`PE3 / PE2`

默认监视串口：

- `USART10`
- `115200 8N1`

## 当前状态反馈资源

- `WS2812`：`PA7 / SPI6_MOSI`
- 蜂鸣器：`PB15 / TIM12_CH2`
- 用户按键：`PA15`

## 当前 CAN 资源

- `CAN1 / FDCAN1`：`PD0 / PD1`
- `CAN2 / FDCAN2`：`PB5 / PB6`
- `CAN3 / FDCAN3`：`PD12 / PD13`

当前阶段先不引入 `smoke` 测试路径。

## 当前电源资源

- 可控 `5V`：`PC15`
- 可控 `OUT1`：`PC14`
- 可控 `OUT2`：`PC13`
- `VBAT / VCC_IN` 采样：`ADC1_CH4 / PC4`

`VBAT` 当前换算关系已确认：

- `R86 = 100K`
- `R87 = 10K`
- `Vadc = Vin / 11`
- `Vin = Vadc * 11`

## 当前测试入口

- `dm_mc02_bsp_smoke`
  - 默认 `status profile`
  - 当前统一输出串口注册、电源状态、`VBAT`、`WS2812`、蜂鸣器、按键
- `dm_mc02_loopback`
  - 保持 `USART1/UART7/UART8/UART9` 物理回环

更详细的当前测试方法见：

- [docs/dm_mc02_status_feedback_test.md](/d:/RM/damiaoTest/docs/dm_mc02_status_feedback_test.md)
