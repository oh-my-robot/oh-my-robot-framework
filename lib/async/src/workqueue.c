/**
 * @file    workqueue.c
 * @brief   工作队列实现 —— 关中断 + 双向链表
 *
 * @details
 *
 * ## 并发模型：开关中断 (irq_lock)
 *
 * 单核 MCU + RTOS 下，所有并发源自 ISR 打断线程。本实现使用
 * 统一的关中断临界区（irq_lock）保护 pending 链表和 Work.flags：
 *
 *   - **线程上下文**: osal_irq_lock_task()（禁用中断 + 禁止任务切换）
 *   - **ISR 上下文**: osal_irq_lock_from_isr() → 保存并屏蔽中断掩码
 *
 * irq_lock 临界区内代码极为简短（几个指针赋值 + 位操作），关中断时间可忽略。
 * 因为关中断自动禁止了抢占，临界区内**不需要** CAS 自旋、互斥量或额外内存屏障。
 *
 * ## 数据结构
 *
 * ```
 *   Workqueue.pending → ListHead (双向循环链表哨兵)
 *   Work.node          → ListHead (嵌入 Work 的节点)
 *   Work.flags         → volatile uint32_t (PENDING | RUNNING 状态位)
 * ```
 *
 * pending 队列采用双向链表，支持：
 *   - **O(1) 队头出队**: worker 通过 list_first_entry + list_del 认领工作
 *   - **O(1) 任意位置删除**: cancel 通过 list_del 从队列中间移除
 *
 * ## Worker Claim 协议（核心并发合同）
 *
 * Enqueue、Worker、Cancel 三方通过 irq_lock + volatile flags 协调对 Work 的"所有权"：
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
 * 注意：worker 在 func 返回后才写 flags=IDLE，该写入发生在 func 内部任何通知
 * （如 sem_post、completion_done）**之后**。调用者通过自己的同步原语感知 func
 * 完成时，**不能**立即释放或复用 work 内存；必须额外调用 work_wait_idle 或
 * 检查 work_is_busy==false，以确认 worker 已写完 flags。
 *
 * ## 内存序策略
 *
 * - **irq_lock 内**: 临界区提供互斥 + 编译器屏障，flags 使用普通 volatile 读写
 * - **irq_lock 外**: 单核上 volatile 读写天然可见（无 store buffer），无需额外内存屏障
 */

#include "async/workqueue.h"
#include "osal/osal_time.h"
#include "sync/completion.h"

#include <stddef.h>

/** 信号量最大计数：1（二值信号量，用于唤醒通知） */
#define WQ_SEM_MAX_COUNT ((uint32_t)1U)

/* ===================================================================
 * Worker 线程
 *
 * worker 线程是工作队列的核心消费者，运行以下循环：
 *   1. sem_wait 等待工作到达信号
 *   2. 在 irq_lock 内从 pending 链表头部取出一个 Work
 *   3. 在 irq_lock 内检查 flags 并认领 Work 的所有权
 *   4. 在 irq_lock 外执行 Work.func 回调
 *   5. 重复直到链表为空，检查 STOPPING 退出条件
 *
 * 对应 Linux kernel process_one_work() 的简化版。
 * =================================================================== */

/**
 * @brief Worker 线程入口函数
 *
 * @param arg  指向 Workqueue 实例的指针。
 */
static void workqueue_worker_entry(void *arg)
{
    Workqueue *wq = (Workqueue *)arg;

    for (;;) {
        /** 1. 等待工作到达或停止信号。sem_wait 用 FOREVER 正常不应返回错误；
         *  若 sem 失效等异常情况发生，防御性地切换到 STOPPING 并退出 worker。 */
        OsalStatus ws = osal_sem_wait(wq->sem, OSAL_WAIT_FOREVER);
        if (ws != OSAL_OK) {
            OsalIrqIsrState k;
            osal_irq_lock(&k);
            if (wq->state == WORKQUEUE_STATE_RUNNING)
                wq->state = WORKQUEUE_STATE_STOPPING;
            osal_irq_unlock(k);
            completion_done(&wq->done);
            osal_thread_exit();
        }

        /** 2. 循环排空 pending 队列 */
        for (;;) {
            Work *w = NULL;

            /** 2a. 关中断：从链表取出下一个工作项并认领 */
            {
                OsalIrqIsrState k;
                osal_irq_lock(&k);

                if (!list_empty(&wq->pending)) {
                    w = list_first_entry(&wq->pending, Work, node);
                    list_del(&w->node);
                    w->flags = WORK_FLAG_RUNNING;
                }

                osal_irq_unlock(k);
            }

            /** 2b. 无更多工作：检查退出条件 */
            if (!w) {
                if (wq->state == WORKQUEUE_STATE_STOPPING) {
                    completion_done(&wq->done);
                    osal_thread_exit();
                }
                break;
            }

            /** 2c. 执行工作函数 */
            w->func(w);

            /** 2d. 在 irq_lock 内将 flags 复位为 IDLE。
             *  与 enqueue/cancel 的 flags 读写满足同一不变量；调用者通过
             *  work_wait_idle 或 work_is_busy 观察 IDLE 后方可释放 work 内存。 */
            {
                OsalIrqIsrState k;
                osal_irq_lock(&k);
                w->flags = WORK_FLAG_IDLE;
                osal_irq_unlock(k);
            }
        }
    }
}

/* ===================================================================
 * 生命周期管理：init / deinit
 * =================================================================== */

/**
 * @brief 初始化工作队列实例
 *
 * 初始化 pending 链表哨兵，创建信号量和 completion。
 * 初始化后状态为 IDLE，需调用 workqueue_start() 启动 worker 线程。
 *
 * @param wq   Workqueue 实例指针。
 * @param cfg  配置参数（名称、栈大小、优先级）；不可为 NULL。
 * @return     OM_OK 成功，OM_ERROR 资源分配失败，OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_init(Workqueue *wq, const WorkqueueConfig *cfg)
{
    if (!wq || !cfg)
        return OM_ERROR_PARAM;
    if (!cfg->stack_depth)
        return OM_ERROR_PARAM;

    /** 防止重复初始化 */
    if (wq->state != WORKQUEUE_STATE_UNINIT)
        return OM_ERROR;

    /** 初始化 pending 链表为空的哨兵态（自循环） */
    INIT_LIST_HEAD(&wq->pending);
    wq->thread = NULL;

    /** 创建计数型信号量（最大计数 1，初始计数 0） */
    OsalStatus st = osal_sem_create(&wq->sem, WQ_SEM_MAX_COUNT, 0U);
    if (st != OSAL_OK)
        return OM_ERROR;

    /** 初始化 worker 退出同步原语 */
    OmRet rc = completion_init(&wq->done);
    if (rc != OM_OK) {
        osal_sem_delete(wq->sem);
        wq->sem = NULL;
        return rc;
    }

    /** 设置状态为 IDLE */
    wq->state       = WORKQUEUE_STATE_IDLE;
    wq->name        = cfg->name ? cfg->name : "wq";
    wq->stack_depth = cfg->stack_depth;
    wq->priority    = cfg->priority;

    return OM_OK;
}

/**
 * @brief 销毁工作队列，释放所有资源
 *
 * @param wq  工作队列实例。
 * @return    OM_OK 成功，OM_ERROR 状态非法（未先 stop），OM_ERROR_PARAM 参数为 NULL。
 *
 * @pre  workqueue_stop() 必须已先调用。
 * @post 信号量已删除，completion 已销毁，状态为 UNINIT。
 */
OmRet workqueue_deinit(Workqueue *wq)
{
    if (!wq)
        return OM_ERROR_PARAM;

    /** 必须已 stop */
    if (wq->state != WORKQUEUE_STATE_IDLE)
        return OM_ERROR;

    if (wq->sem) {
        osal_sem_delete(wq->sem);
        wq->sem = NULL;
    }

    completion_deinit(&wq->done);

    wq->state = WORKQUEUE_STATE_UNINIT;
    wq->name  = NULL;
    return OM_OK;
}

/* ===================================================================
 * 生命周期管理：start / stop
 * =================================================================== */

/**
 * @brief 启动 worker 线程
 *
 * 通过 irq_lock 将状态从 IDLE 切换到 RUNNING，然后创建任务
 * 作为 worker 线程。如果线程创建失败，状态回退到 IDLE。
 *
 * @param wq  已初始化的工作队列。
 * @return    OM_OK 成功，OM_ERROR 状态非法或线程创建失败，OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_start(Workqueue *wq)
{
    if (!wq)
        return OM_ERROR_PARAM;

    /** irq_lock 内检查并切换状态：IDLE → RUNNING */
    {
        OsalIrqIsrState key;
        osal_irq_lock(&key);
        if (wq->state != WORKQUEUE_STATE_IDLE) {
            osal_irq_unlock(key);
            return OM_ERROR;
        }
        wq->state = WORKQUEUE_STATE_RUNNING;
        osal_irq_unlock(key);
    }

    /** 排空上一次循环可能残留的信号量计数 */
    while (osal_sem_wait(wq->sem, 0U) == OSAL_OK) {
    }

    /** 重置 completion，准备新 worker 周期 */
    OmRet crc = completion_init(&wq->done);
    if (crc != OM_OK) {
        OsalIrqIsrState key;
        osal_irq_lock(&key);
        wq->state = WORKQUEUE_STATE_IDLE;
        osal_irq_unlock(key);
        return crc;
    }

    /** 配置并创建 worker 线程 */
    OsalThreadAttr attr = {0};
    attr.name           = wq->name;
    attr.stackSize      = wq->stack_depth;
    attr.priority       = wq->priority;

    OsalStatus st = osal_thread_create(&wq->thread, &attr,
                                       workqueue_worker_entry, wq);
    if (st != OSAL_OK) {
        /** 线程创建失败，状态回退 */
        OsalIrqIsrState key;
        osal_irq_lock(&key);
        wq->state = WORKQUEUE_STATE_IDLE;
        osal_irq_unlock(key);
        return OM_ERROR;
    }

    return OM_OK;
}

/**
 * @brief 停止 worker 线程
 *
 * 状态从 RUNNING 切换到 STOPPING，唤醒 worker 线程，等待其排空
 * pending 队列并退出。worker 退出后断言 pending 已为空；若断言在 release
 * 构建中被禁用，仍兜底清理残留节点。
 *
 * @param wq  正在运行的工作队列。
 * @return    OM_OK 成功，OM_ERROR 状态非法，OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_stop(Workqueue *wq)
{
    if (!wq)
        return OM_ERROR_PARAM;

    /** irq_lock 内检查并切换状态：RUNNING → STOPPING */
    {
        OsalIrqIsrState key;
        osal_irq_lock(&key);
        if (wq->state != WORKQUEUE_STATE_RUNNING) {
            osal_irq_unlock(key);
            return OM_ERROR;
        }
        wq->state = WORKQUEUE_STATE_STOPPING;
        osal_irq_unlock(key);
    }

    /** 唤醒 worker 线程，使其开始排空 */
    osal_sem_post(wq->sem);

    /** 等待 worker 线程退出信号 */
    completion_wait(&wq->done, OSAL_WAIT_FOREVER);
    wq->thread = NULL;

    /** 正常路径下 worker 退出前已排空 pending；本断言验证这一不变量。
     *  - Debug 构建：触发死循环断言，便于调试定位 worker 异常退出。
     *  - Release 构建（OSAL_ASSERT 为空操作）：仍清理残留并继续，避免下次
     *    start 看到悬挂节点。这种清理意味着 work 的 func 不会被调用，
     *    调用者需另行处理其资源；这是 best-effort 的兜底，不应依赖。 */
    {
        OsalIrqIsrState k;
        osal_irq_lock(&k);
        if (!list_empty(&wq->pending)) {
            Work *w, *tmp;
            list_for_each_entry_safe(w, tmp, &wq->pending, node)
            {
                list_del(&w->node);
                w->flags = WORK_FLAG_IDLE;
            }
            OSAL_ASSERT(0);
        }
        osal_irq_unlock(k);
    }

    /** 状态切换：STOPPING → IDLE（irq_lock 内） */
    {
        OsalIrqIsrState key;
        osal_irq_lock(&key);
        wq->state = WORKQUEUE_STATE_IDLE;
        osal_irq_unlock(key);
    }

    return OM_OK;
}

/* ===================================================================
 * 入队操作（Enqueue）
 *
 * enqueue 的 flags 检查和链表插入在同一 irq_lock 内完成，
 * 消除 CAS 与 list_add_tail 之间的 TOCTOU 窗口。
 * irq_lock 保证 flags 和链表操作的原子性。
 * =================================================================== */

/**
 * @brief 将工作项入队
 *
 * 线程安全和 ISR 安全。
 *
 * @param wq    目标工作队列。
 * @param work  要入队的工作项。
 * @return      OM_OK 成功入队；
 *              OM_ERROR 工作队列未在运行；
 *              OM_ERROR_BUSY 工作项已 PENDING 或 RUNNING；
 *              OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_enqueue(Workqueue *wq, Work *work)
{
    if (!wq || !work)
        return OM_ERROR_PARAM;

    /** 拒绝：工作队列未在运行 */
    if (wq->state != WORKQUEUE_STATE_RUNNING) {
        return OM_ERROR;
    }

    /** 关中断：flags 去重检查 + 链表插入在同一临界区内 */
    {
        OsalIrqIsrState key;
        osal_irq_lock(&key);

        if (work->flags != WORK_FLAG_IDLE) {
            osal_irq_unlock(key);
            return OM_ERROR_BUSY;
        }

        work->flags = WORK_FLAG_PENDING;
        list_add_tail(&work->node, &wq->pending);

        osal_irq_unlock(key);
    }

    /** 唤醒 worker 线程 */
    osal_sem_post_auto(wq->sem);

    return OM_OK;
}

/* ===================================================================
 * 取消操作
 *
 * cancel 所有操作（flags 检查、链表删除、flags 恢复）均在 irq_lock
 * 临界区内完成。irq_lock 禁用中断和抢占，保证 flags 和链表操作的原子性。
 * =================================================================== */

/**
 * @brief 取消一个 pending 工作项
 *
 * 线程安全和 ISR 安全。调用者无需知道 work 在哪个 workqueue 上。
 *
 * @param work  要取消的工作项。
 * @return      OM_OK 成功取消（work 恢复 IDLE，可重新入队）；
 *              OM_ERROR_BUSY work 正在执行，无法取消；
 *              OM_ERROR work 不在 pending 状态；
 *              OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_cancel(Work *work)
{
    if (!work)
        return OM_ERROR_PARAM;

    OsalIrqIsrState key;
    osal_irq_lock(&key);

    uint32_t f = work->flags;

    /** 拒绝：work 正在执行（worker 已认领） */
    if (f & WORK_FLAG_RUNNING) {
        osal_irq_unlock(key);
        return OM_ERROR_BUSY;
    }

    /** 拒绝：work 不在 pending 状态 */
    if (!(f & WORK_FLAG_PENDING)) {
        osal_irq_unlock(key);
        return OM_ERROR;
    }

    /** 从 pending 链表移除并恢复 IDLE。
     *  irq_lock 保证 worker 不可能并发取走该节点。 */
    list_del(&work->node);
    work->flags = WORK_FLAG_IDLE;

    osal_irq_unlock(key);
    return OM_OK;
}

/* ===================================================================
 * 排空操作（Flush）
 *
 * 参考 Linux 内核 flush_workqueue 的 barrier work 方案：向 pending 队列
 * 尾部插入一个空操作 barrier work，等待它被 worker 处理完毕（即 flags 从
 * RUNNING 回到 IDLE）。FIFO 语义保证 barrier 之前的所有 work 已执行完毕。
 *
 * 这里不使用 Completion 信号：worker 在 func 返回后才会写 flags=IDLE，
 * 该写入发生在 func 内部的任何通知（如 completion_done）之后，调用者收到
 * 通知时 worker 可能尚未写完 flags。改用 work_wait_idle 直接等 bw 进入
 * IDLE，保证 flush 返回时 worker 已不再持有 bw，bw 的栈帧可安全销毁。
 * =================================================================== */

/** barrier work 的回调：空操作，仅作 FIFO 标记 */
static void flush_barrier_fn(Work *w)
{
    (void)w;
}

/**
 * @brief 排空所有 pending 工作项并同步等待完成
 *
 * 向 pending 队列尾部插入一个 barrier work 并阻塞等待其被 worker 处理完毕
 * （flags 回到 IDLE）。FIFO 语义保证 barrier 之前的所有 work 已执行完毕（或被取消）。
 *
 * 允许多个线程并发调用本接口：每个调用者在自己的栈上分配 barrier work，
 * 互不干扰；barrier work 之间也满足 FIFO 排队。
 *
 * @param wq  正在运行的工作队列。
 * @return    OM_OK 成功，OM_ERROR 工作队列未在运行或 enqueue 失败，
 *            OM_ERROR_TIMEOUT 等待 barrier 完成超时（理论上不会发生），
 *            OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_flush(Workqueue *wq)
{
    if (!wq)
        return OM_ERROR_PARAM;

    /** 工作队列必须处于 RUNNING 状态 */
    if (wq->state != WORKQUEUE_STATE_RUNNING) {
        return OM_ERROR;
    }

    /** 栈上分配 barrier work 并入队队尾 */
    Work bw;
    work_init(&bw, flush_barrier_fn, NULL);

    OmRet rc = workqueue_enqueue(wq, &bw);
    if (rc != OM_OK)
        return rc;

    /** 等 worker 完全释放 bw（flags 回到 IDLE）后再返回，bw 才能安全出栈 */
    return work_wait_idle(&bw, OSAL_WAIT_FOREVER);
}

/* ===================================================================
 * Work 状态等待
 *
 * 调用者析构或复用 work 内存前用于确认 worker 已不再持有 work。
 * 实现采用 yield + sleep 轮询：单核 MCU 上 worker 的 flags 写入对其他
 * 线程天然可见；调用者 yield 后 worker 必然能取得 CPU 时间推进至 IDLE。
 * =================================================================== */

/**
 * @brief 阻塞等待工作项回到 IDLE 状态
 *
 * 见 workqueue.h 中的接口文档。
 */
OmRet work_wait_idle(Work *work, uint32_t timeout_ms)
{
    if (!work)
        return OM_ERROR_PARAM;
    /** 仅允许线程上下文：内部使用 sleep 轮询 */
    if (osal_is_in_isr())
        return OM_ERROR_PARAM;

    uint32_t remaining = timeout_ms;
    for (;;) {
        /** 快速路径：已 IDLE 直接返回。
         *  flags 在 irq_lock 内被写入；单核上 sleep 之后的读必然看到最新值。 */
        OsalIrqIsrState k;
        osal_irq_lock(&k);
        uint32_t f = work->flags;
        osal_irq_unlock(k);
        if (f == WORK_FLAG_IDLE)
            return OM_OK;

        /** 超时检查（非 FOREVER 且配额耗尽） */
        if (remaining == 0U && timeout_ms != OSAL_WAIT_FOREVER) {
            return OM_ERROR_TIMEOUT;
        }

        /** sleep 让出 CPU 让 worker 推进；FOREVER 路径不消耗 remaining */
        if (timeout_ms != OSAL_WAIT_FOREVER) {
            osal_sleep_ms(1U);
            remaining = (remaining > 0U) ? (remaining - 1U) : 0U;
        } else {
            osal_sleep_ms(1U);
        }
    }
}

/* ===================================================================
 * 查询辅助函数
 * =================================================================== */

/**
 * @brief 查询工作队列当前生命周期状态
 *
 * @param wq  工作队列实例。
 * @return    WORKQUEUE_STATE_UNINIT / IDLE / RUNNING / STOPPING。
 */
int workqueue_get_state(const Workqueue *wq)
{
    if (!wq)
        return WORKQUEUE_STATE_UNINIT;
    return wq->state;
}

/**
 * @brief 查询 pending 队列是否为空
 *
 * 线程安全和 ISR 安全。在 irq_lock 内读取链表状态，确保获取一致快照。
 *
 * @param wq  工作队列实例。
 * @return    true 无 pending 工作项。
 */
bool workqueue_is_empty(const Workqueue *wq)
{
    if (!wq)
        return true;

    OsalIrqIsrState key;
    osal_irq_lock(&key);
    bool empty = list_empty(&wq->pending);
    osal_irq_unlock(key);
    return empty;
}
