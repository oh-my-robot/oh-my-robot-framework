# Oh My Robot

面向机器人的跨平台嵌入式开发框架。

[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](LICENSE)
[![XMake](https://img.shields.io/badge/xmake-%E2%89%A53.0.7-orange)](https://xmake.io)

## 项目简介

Oh My Robot（OM）将机器人软件中常见的实时控制、多外设协作、跨任务通信等场景抽象为分层清晰、可移植的软件基础设施。框架的核心哲学是**分层原语化**——将软件栈划分为同步语义、异步调度、数据传输、OS 抽象、可选算法工具箱等独立原语，业务层可以直取任意一层进行组合，只引入你需要的抽象。平台适配统一收敛在 platform 层，由构建系统选择性编译，换芯片或换 RTOS 不触碰架构核心。

## 支持的平台（持续更新中）

| 类型 | 支持项 |
|------|--------|
| **MCU** | STM32F4 系列 |
| **RTOS** | FreeRTOS |
| **工具链** | arm-none-eabi-gcc (gnu-rm)、armclang (Arm Compiler 6) |
| **调试器** | J-Link、DAPLink (OpenOCD) |
| **构建系统** | XMake ≥ 3.0.7 |

## 项目结构

```
oh-my-robot/
├── lib/
│   ├── core/          # 核心：基础类型、错误码、通用宏、原子操作、平台无关的数据结构
│   ├── algorithm/     # 算法：控制、滤波等可选计算原语（平台无关，按需引入）
│   ├── osal/          # 操作系统抽象层：对 RTOS 的最小可移植抽象（线程、互斥、信号量、队列等）
│   ├── sync/          # 同步语义层：纯同步信号抽象（协调执行顺序），无数据载荷
│   ├── async/         # 异步执行基座：通用执行调度能力（工作队列、延时执行等）
│   ├── ipc/           # 跨上下文数据传输层：原始数据传输（无结构字节流、类型化离散消息）
│   ├── drivers/       # 驱动与 PAL：设备模型、外设驱动、可移植硬件抽象
│   ├── services/      # 通用服务层：可复用的通用服务组件（日志、配置、通信、诊断等）
│   └── systems/       # 业务子系统层：机器人系统级模块（chassis、gimbal、arm、robot 等）
├── platform/          # 平台适配：bsp · OSAL端口 · sync加速 · 工具链/ABI · RTOS绑定
├── tools/             # 工具与脚本
├── samples/           # 示例代码
├── docs/              # 项目文档
└── build/             # 构建系统（XMake）
```

## 文档

| 文档 | 说明 |
|------|------|
| [快速开始](docs/quick_start.md) | 环境搭建、工具链安装、构建与调试 |
| [架构参考手册](docs/architecture/architecture_reference_manual.md) | 分层结构、职责边界、依赖方向——**架构唯一总规范源** |
| [构建系统参考手册](docs/build/reference_manual.md) | OM 构建体系完整参考 |
| [构建任务手册](docs/build/build_tasks_manual.md) | 常用构建任务与工作流 |

模块设计文档随代码分布在各 `lib/*/docs/` 目录下，包括 async、sync、ipc、drivers、motor 等模块。

## 贡献

提交 PR 前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。工程流程见 [Git 协作规范](docs/process/git_collaboration_spec.md)（分支职责、提交纪律、PR 流程）和 [发布与版本规范](docs/process/git_version_release_spec.md)；项目管理规则见 Confluence [OM 项目管理原则](https://bamboolab.atlassian.net/wiki/spaces/OMR/pages/589827)。

## 反馈与讨论

- **Bug 与功能请求**：[GitHub Issues](https://github.com/oh-my-robot/oh-my-robot-framework/issues)
- **发布记录**：[GitHub Releases](https://github.com/oh-my-robot/oh-my-robot-framework/releases)

## 许可证

[Apache 2.0](LICENSE)
