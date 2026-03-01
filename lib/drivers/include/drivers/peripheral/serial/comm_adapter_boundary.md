# Serial Comm Adapter 边界约束

本文用于固定 Serial/UART 实现端接入 `services/comm` 的边界，不定义业务语义。

## 目录落位

- 推荐实现文件：`oh-my-robot/lib/drivers/src/peripheral/serial/comm_adapter_serial*.c`
- 推荐头文件：`oh-my-robot/lib/drivers/src/peripheral/serial/comm_adapter_serial*.h`

## 依赖方向

- 允许：`comm_adapter_serial*` 依赖 `drivers/peripheral/serial` 抽象 + `services/comm` 抽象。
- 禁止：`services/comm(core)` 直接依赖 `comm_adapter_serial*` 实现文件。
- 禁止：`drivers/peripheral/serial` 核心路径反向依赖 `services/comm` 业务语义。

## 责任边界

- 只负责总线消息与 `CommMsg` 抽象之间的映射与注册接入。
- 不负责业务协议解析。
- 不在中断路径执行阻塞逻辑。

## 规范引用

- 总规范：`oh-my-robot/document/architecture/分层与依赖规范.md`
- 通信总纲：`oh-my-robot/lib/services/doc/公共通信重构总纲.md`
