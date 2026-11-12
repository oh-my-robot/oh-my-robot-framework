# OSAL 总览

本目录提供操作系统抽象层（OSAL）。上层模块仅依赖 OSAL 接口，具体 RTOS/系统由端口层适配实现。

## 目录结构
- `include/osal`：OSAL 核心接口（线程、时间、事件、互斥锁、信号量、队列、定时器）。
- `platform/osal/freertos`：FreeRTOS 端口实现（MCU 优先）。
- `platform/osal/linux`：Linux 端口骨架（Phase 0 占位）。

## OSAL 接口
`include/osal` 下的头文件是对外唯一接口：
- `osal_core.h`：中断判断、临界区、内存申请释放、通用返回码。
- `osal_thread.h`：线程创建/join/退出/终止/让出。
- `osal_time.h`：单调时钟（ms）、休眠与周期延时。
- `osal_event.h`：事件标志对象（event flags，支持 ISR set）。
- `osal_timer.h`：软件定时器。
- `osal_mutex.h`：互斥锁（线程上下文）。
- `osal_sem.h`：信号量（ISR 支持 post，支持计数查询）。
- `osal_queue.h`：队列（ISR 支持收发）。

## 端口层说明
- `platform/osal/freertos`：基于 FreeRTOS 的实现。
- `platform/osal/linux`：Linux 端口骨架（待后续阶段启用）。

## 时间单位约定
- OSAL 公共接口默认使用毫秒。
- FreeRTOS 端口将毫秒转换为 tick（向上取整），再依据 `configTICK_RATE_HZ` 返回毫秒语义。
- Linux 端口在后续阶段接入。

## 时间语义
- `osal_time_now_monotonic` 提供单调时间，不受系统时间回拨影响。
- `osal_sleep_ms(OSAL_WAIT_FOREVER)` 非法，返回 `OSAL_INVALID`。
- `osal_delay_until(deadline_cursor_ms, period_ms, missed_periods)` 使用“过期追赶”策略：每次只推进一个周期，不跳过周期；`missed_periods` 反馈本次调用前已过期的周期数量。
- `deadline_cursor_ms` 采用 in/out 游标语义：首次传 `0`，后续原样回传，不需要调用者手动计算下一次唤醒时刻。
- `osal_sem_wait(OSAL_WAIT_FOREVER)` 保留“无限等待”语义。
- `osal_sem_post/osal_sem_post_from_isr` 在计数已满时返回 `OSAL_NO_RESOURCE`（非阻塞失败）。
- `osal_mutex` 为非递归语义；同线程重复加锁按超时规则处理。
- `osal_mutex_unlock` 在非 owner 调用时返回 `OSAL_INVALID`。

## ISR 使用规则
- ISR 中禁止调用阻塞接口。
- 仅使用 ISR 版本 API：
  - `osal_event_flags_set_from_isr`
  - `osal_sem_post_from_isr`
  - `osal_sem_get_count_from_isr`
  - `osal_queue_send_from_isr`
  - `osal_queue_recv_from_isr`
- 线程接口约束：
  - `OsalThreadAttr_s.stackSize` 统一使用“字节”语义。
  - `osal_thread_create/join/terminate/yield/exit/kernel_start` 仅允许在线程上下文调用。
  - 当前 FreeRTOS 端口 `osal_thread_join` 返回 `OSAL_NOT_SUPPORTED`。

## 配置
主要配置位于 `lib/include/core/om_config.h` 与 `lib/osal/include/osal/osal_config.h`：
- `OSAL_WAIT_FOREVER`：无限等待超时值。
- `OSAL_STACK_WORD_BYTES`：栈单位大小（字节）。
- `OSAL_PRIORITY_MAX`：最大优先级数量。
- `OSAL_TASK_NAME_MAX`：任务名称最大长度。
- `OSAL_QUEUE_REGISTRY_MAX`：队列注册表最大数量。

## 构建开关
- `OM_OSAL_PORT`：选择 OSAL 后端（`freertos` / `posix`）。

## 测试策略
当前阶段（架构早期、跨平台构建刚搭起）：优先做接口一致性验证。行为测试可在 BSP 与样例稳定后，再在板级或 HIL 环境引入。

## 典型使用流程
1. 初始化系统。
2. 创建 OSAL 资源（线程、队列、互斥锁、信号量、定时器等）。
3. 启动调度器（`osal_kernel_start()`）。
4. ISR 中仅调用 ISR 版本接口，避免阻塞。

示例：
```c
#include "osal/osal.h"

static void app_thread(void* arg)
{
    (void)arg;
    while (1)
    {
        (void)osal_sleep_ms(10);
    }
}

void app_start(void)
{
    OsalThread_t thread;
    OsalThreadAttr_s attr = {"app", 2048, 2};
    (void)osal_thread_create(&thread, &attr, app_thread, NULL);
    (void)osal_kernel_start();
}
```