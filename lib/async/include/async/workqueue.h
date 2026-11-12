/**
 * @file    workqueue.h
 * @brief   工作队列：关中断保护的双向链表工作队列（参考 Linux cmwq / Zephyr work_q）
 * @details
 *
 *
 * ## 并发模型：开关中断 (irq_lock)
 *
 * 单核 MCU 上，所有并发竞争源自 ISR/线程 打断线程。本模块统一使用关中断
 * 临界区保护 pending 链表和 Work.flags 的读写：
 *
 * | 调用上下文   | 关中断方式                  | 效果                          |
 * |-------------|---------------------------|------------------------------|
 * | 线程上下文   | osal_irq_lock_task()      | 禁中断 + 禁抢占                   |
 * | ISR 上下文  | osal_irq_lock_from_isr()  | 保存并屏蔽中断掩码               |
 *
 * irq_lock 临界区内代码极为简短（几个指针赋值），关中断时间可忽略。
 * 因为关中断自动禁止了抢占，临界区内无需 CAS 自旋、互斥量或额外内存屏障。
 *
 * ## 数据结构：侵入式双向链表
 *
 * pending 队列基于 `ListHead` 双向循环链表，Work.node 嵌入在 Work 结构体中：
 *
 * ```
 *   Workqueue.pending (哨兵)
 *        ↓
 *   [哨兵] ⇄ [Work A] ⇄ [Work B] ⇄ [Work C] ⇄ [哨兵]
 * ```
 *
 * 双向链表支持两项关键操作：
 * - **O(1) 队头出队**: worker 通过 list_first_entry 获取下一个工作项
 * - **O(1) 任意位置删除**: cancel 通过 list_del 从队列中间移除
 *
 * ## Worker Ownership 协议
 *
 * Enqueue、Worker、Cancel 三方通过 irq_lock + flags 协调对 Work 的"所有权"：
 *
 * ```
 * enqueue:  [irq_lock: flags 检查 + list_add_tail]  →  [sem_post]
 * worker:   [sem_wait]  →  [irq_lock: list_del + flags=RUNNING]  →  [执行 func]  →  [irq_lock: flags=IDLE]
 * cancel:   [irq_lock: 检查 flags + list_del + flags=IDLE]
 * ```
 *
 * **关键不变量**: flags 检查、链表操作和 flags 状态转换均在同一 irq_lock 临界区内完成。
 * 这确保 cancel 看到 PENDING 时节点必定在链表中，看到 RUNNING 时 worker 已认领。
 *
 * ## 调用者责任
 *
 * - **Workqueue 实例**：调用 `workqueue_init` 前必须清零（`Workqueue wq = {0};` 或显式
 *   `memset`）。`init` 通过 `state==UNINIT` 检测重复初始化，未清零实例的状态字段是
 *   垃圾值，无法可靠识别。
 * - **Work 实例**：首次使用前必须调用 `work_init`。
 * - **Work 内存生命周期**：worker 在 `func` 返回后才会将 flags 从 RUNNING 改回 IDLE，
 *   此写入发生在 func 内部任何通知（如 sem_post）**之后**。因此调用者通过自己的同步
 *   原语（如 sem_wait）感知到"func 已执行"时，**不能**立即释放或复用 work 内存；
 *   必须先调用 `work_wait_idle(work)` 或确认 `work_is_busy(work) == false`。
 *
 * ## 生命周期状态机
 *
 * ```
 *   UNINIT ── init ──→ IDLE ──start──→ RUNNING ──stop──→ STOPPING ── drain ──→ IDLE
 *                     ↑                                                    │
 *                     └────────────────────────────────────────────────────┘
 *                                        (可循环)
 *                                     deinit → UNINIT
 * ```
 */
#ifndef ASYNC_WORKQUEUE_H
#define ASYNC_WORKQUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "data_struct/corelist.h"
#include "core/om_def.h"
#include "osal/osal_core.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"
#include "sync/completion.h"

/* --------------------------------------------------------------------------
 * Work 标志位
 *
 * 被 work_is_busy() 等内联函数使用，必须暴露在公共头文件中。
 * 所有 flags 修改均在 irq_lock 临界区内完成，调用者不应直接写 flags。
 *
 * 状态迁移规则：
 *   IDLE ──[enqueue irq_lock]──→ PENDING ──[worker irq_lock]──→ RUNNING ──[worker irq_lock, func 返回后]──→ IDLE
 *   PENDING ──[cancel irq_lock]──→ IDLE
 * -------------------------------------------------------------------------- */

#define WORK_FLAG_IDLE    ((uint32_t)0x00U) /**< 空闲，可被入队 */
#define WORK_FLAG_PENDING ((uint32_t)0x01U) /**< 在 pending 队列中等待 */
#define WORK_FLAG_RUNNING ((uint32_t)0x02U) /**< worker 正在执行 */

/* --------------------------------------------------------------------------
 * Workqueue 生命周期状态
 * -------------------------------------------------------------------------- */

#define WORKQUEUE_STATE_UNINIT   0 /**< 从未初始化或已销毁 */
#define WORKQUEUE_STATE_IDLE     1 /**< 已初始化但 worker 未运行 */
#define WORKQUEUE_STATE_RUNNING  2 /**< 接受并执行工作 */
#define WORKQUEUE_STATE_STOPPING 3 /**< 正在排空，拒绝新工作 */

/* --------------------------------------------------------------------------
 * 类型定义
 * -------------------------------------------------------------------------- */

/** Work 前置声明，用于 WorkFunc 签名 */
typedef struct Work Work;

/**
 * @brief 工作函数签名
 *
 * 参考内核工作队列的 work item 回调约定。
 * 当 work 被调度执行时，worker 线程调用此函数。
 *
 * @param work  指向正在执行的 Work。使用 container_of() 获取嵌入结构体。
 *
 * 示例：
 *   struct my_device {
 *       Work ws;
 *       int state;
 *   };
 *
 *   void my_handler(Work *w) {
 *       struct my_device *dev = container_of(w, struct my_device, ws);
 *       dev->state = 1;
 *   }
 */
typedef void (*WorkFunc)(Work *work);

/**
 * @brief 工作项（嵌入调用者结构体使用）
 *
 *
 * 字段说明：
 * - node:  侵入式双向链表节点，挂在 Workqueue.pending 队列中
 * - func:  工作处理函数，worker 线程执行时调用
 * - data:  用户上下文指针，可在 handler 中通过 work->data 访问
 * - flags: 状态标志位（WORK_FLAG_*），在 irq_lock 内管理
 */
struct Work {
    ListHead node;           /**< 侵入式链表节点，挂在 pending 队列 */
    WorkFunc func;           /**< 工作处理函数 */
    void *data;              /**< 用户上下文 */
    volatile uint32_t flags; /**< 状态标志位 (WORK_FLAG_*) */
};

/**
 * @brief 工作队列配置参数
 *
 * 传递给 workqueue_init()，设置 worker 线程属性。
 */
typedef struct WorkqueueConfig {
    const char *name;     /**< 调试名称（用作线程名） */
    uint32_t stack_depth; /**< worker 线程栈大小（字节）；为 0 时 workqueue_init 返回 OM_ERROR_PARAM */
    uint32_t priority;    /**< worker 线程优先级（RTOS 原生优先级值，0 = idle priority）。
                           *  本模块不做范围检查，调用者需保证小于 RTOS 的最大优先级数；
                           *  实践中建议 ≥ 1，避免 worker 与 idle 任务同优先级导致调度延迟。 */
} WorkqueueConfig;

/**
 * @brief 工作队列实例
 *
 * 用法：
 *   Workqueue wq;
 *   workqueue_init(&wq, &cfg);
 */
typedef struct Workqueue {
    ListHead pending;     /**< pending 工作项双向链表哨兵 */
    OsalSem *sem;         /**< 工作到达信号量（计数型，最大1） */
    OsalThread *thread;   /**< worker 线程句柄 */
    Completion done;      /**< worker 退出同步原语 */
    volatile int state;   /**< 生命周期状态 (WORKQUEUE_STATE_*) */
    uint32_t stack_depth; /**< worker 线程栈大小（字节） */
    uint32_t priority;    /**< worker 线程优先级 */
    const char *name;     /**< 调试名称 */
} Workqueue;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化工作队列实例
 *
 * 分配信号量和 completion 资源。初始化后状态为 IDLE，
 * 需要调用 workqueue_start() 启动 worker 线程。
 *
 * @param wq   Workqueue 实例指针。
 * @param cfg  配置参数（名称、栈大小、优先级）；不可为 NULL。
 * @return     OM_OK 成功，否则返回错误码。
 *
 * @pre  wq 指向有效的 Workqueue 实例。
 * @post wq 处于 WORKQUEUE_STATE_IDLE 状态。
 */
OmRet workqueue_init(Workqueue *wq, const WorkqueueConfig *cfg);

/**
 * @brief 销毁工作队列，释放所有资源
 *
 * @param wq  工作队列实例。
 * @return    OM_OK 成功，否则返回错误码。
 *
 * @pre  workqueue_stop() 必须已先调用，状态必须为 IDLE。
 * @post wq 处于 WORKQUEUE_STATE_UNINIT 状态。
 */
OmRet workqueue_deinit(Workqueue *wq);

/**
 * @brief 启动 worker 线程，开始接受并执行工作
 *
 * 创建任务作为 worker 线程。worker 线程从 pending 队列中
 * 取出 Work 并调用其 func 回调。
 *
 * @param wq  已初始化的工作队列。
 * @return    OM_OK 成功，否则返回错误码。
 *
 * @pre  wq 必须处于 IDLE 状态。
 * @post wq 处于 RUNNING 状态，worker 线程运行中。
 */
OmRet workqueue_start(Workqueue *wq);

/**
 * @brief 停止接收新工作，排空 pending 队列，等待 worker 线程退出
 *
 * 阻塞直到 worker 线程完全退出且所有 pending work 已被执行或清理。
 * 停止后可以再次 start 重新启动。
 *
 * @param wq  正在运行的工作队列。
 * @return    OM_OK 成功，否则返回错误码。
 *
 * @pre  wq 必须处于 RUNNING 状态。
 * @post wq 处于 IDLE 状态，worker 线程已退出。
 */
OmRet workqueue_stop(Workqueue *wq);

/**
 * @brief 将工作项入队，等待异步执行
 *
 * 线程安全和 ISR 安全。
 *
 * 去重语义: 如果 work 已在 pending 队列中或正在执行，返回 OM_ERROR_BUSY，
 * 调用为无操作。这与 Linux cmwq 的 `queue_work()` 行为一致
 * （WQ_UNBOUND 模式下通过 WORK_STRUCT_PENDING_BIT 实现）。
 *
 * **调度保证**: work 最终会在 worker 线程上下文中被恰好执行一次（除非被 cancel）。
 *
 * @param wq    目标工作队列。
 * @param work  要入队的工作项。必须已通过 work_init() 初始化。
 * @return      OM_OK 成功入队；
 *              OM_ERROR 工作队列未在运行；
 *              OM_ERROR_BUSY 工作项已在队列中或正在执行；
 *              OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_enqueue(Workqueue *wq, Work *work);

/**
 * @brief 取消一个尚未执行的工作项
 *
 * 线程安全和 ISR 安全。
 *
 * 如果 work 正处于 pending 状态（在队列中等待），将其从 pending 队列中移除
 * 并恢复为 IDLE 状态，可以重新入队。
 *
 * 如果 work 正在 worker 线程上执行，取消无法进行，返回 OM_ERROR_BUSY。
 *
 * **设计要点**: 调用者无需知道 work 在哪个 workqueue 上——Work 自身持有
 * 所有必要状态（flags + list node），cancel 通过 irq_lock 原子地检查
 * flags 并操作链表。
 *
 * @param work  要取消的工作项。
 * @return      OM_OK 成功取消；
 *              OM_ERROR_BUSY 工作项正在执行无法取消；
 *              OM_ERROR 工作项不在 pending 状态；
 *              OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_cancel(Work *work);

/**
 * @brief 排空所有 pending 工作项并等待完成
 *
 * 同步等待——返回时，所有当前 pending 的工作项已执行完毕（或被取消）。
 * 工作队列必须处于 RUNNING 状态。
 *
 * 实现采用 barrier work 方案：向队列尾部插入一个 barrier 工作项，
 * FIFO 语义保证 barrier 执行时之前的所有 work 已完成。
 *
 * @param wq  正在运行的工作队列。
 * @return    OM_OK 成功，OM_ERROR 工作队列未在运行，OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_flush(Workqueue *wq);

/**
 * @brief 查询工作队列当前生命周期状态
 *
 * @param wq  工作队列实例。
 * @return    WORKQUEUE_STATE_UNINIT / IDLE / RUNNING / STOPPING。
 */
int workqueue_get_state(const Workqueue *wq);

/**
 * @brief 查询 pending 队列是否为空
 *
 * 线程安全和 ISR 安全。
 *
 * @param wq  工作队列实例。
 * @return    true 无 pending 工作项。
 */
bool workqueue_is_empty(const Workqueue *wq);

/**
 * @brief 阻塞等待工作项回到 IDLE 状态
 *
 * 用于 work 内存生命周期管理：worker 在 `func` 返回后才将 flags 从 RUNNING 改回
 * IDLE，调用者通过自己的同步原语（如 sem_wait）感知到 func 已执行时，仍需调用
 * 本接口确认 worker 已完全释放 work，方可析构或复用 work 内存。
 *
 * 仅允许在线程上下文调用（内部使用 yield + sleep 轮询）。flags 读取为 volatile，
 * 单核天然可见；多核场景需配合 irq_lock 或内存屏障（本实现面向单核 MCU）。
 *
 * @param work        要等待的工作项。
 * @param timeout_ms  超时（毫秒），`OSAL_WAIT_FOREVER` 表示无限等待；0 表示非阻塞
 *                    探测（已 IDLE 返回 OM_OK，否则返回 OM_ERROR_TIMEOUT）。
 * @return            OM_OK 已进入 IDLE；
 *                    OM_ERROR_TIMEOUT 超时；
 *                    OM_ERROR_PARAM 参数为 NULL 或在 ISR 中调用。
 */
OmRet work_wait_idle(Work *work, uint32_t timeout_ms);

/* --------------------------------------------------------------------------
 * 内联辅助函数
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化 Work 结构体（首次使用前必须调用）
 *
 * 设置链表节点为哨兵态（自循环）、绑定处理函数和用户上下文、
 * 将 flags 重置为 IDLE。
 *
 * @param work  预分配的工作项。
 * @param func  工作执行时调用的处理函数。
 * @param data  用户上下文，可通过 work->data 访问。
 */
static inline void work_init(Work *work, WorkFunc func, void *data)
{
    INIT_LIST_HEAD(&work->node);
    work->func  = func;
    work->data  = data;
    work->flags = WORK_FLAG_IDLE;
}

/**
 * @brief 查询工作项是否处于忙碌状态（pending 或 running）
 *
 * @param work  工作项。
 * @return      true 工作项正在队列中或正在执行。
 */
static inline bool work_is_busy(const Work *work)
{
    return (work->flags & (WORK_FLAG_PENDING | WORK_FLAG_RUNNING)) != 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* ASYNC_WORKQUEUE_H */
