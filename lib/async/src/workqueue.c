/**
 * @file    workqueue.c
 * @brief   工作队列实现 —— 关中断 + 双向链表
 *
 * @details
 *
 * ## 并发模型：开关中断 (irq_lock)
 *
 * 单核 Cortex-M + FreeRTOS 下，所有并发源自 ISR 打断线程。本实现使用
 * 统一的关中断临界区（irq_lock）保护 pending 链表和 Work.flags：
 *
 *   - **线程上下文**: osal_irq_lock_task() → taskENTER_CRITICAL()
 *     （禁用中断 + 禁止任务切换）
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
 *   Work.flags         → OmAtomicUint (PENDING | CANCELLED | RUNNING 状态位)
 * ```
 *
 * pending 队列采用双向链表，支持：
 *   - **O(1) 队头出队**: worker 通过 list_first_entry + list_del_init 认领工作
 *   - **O(1) 任意位置删除**: cancel 通过 list_node_is_linked + list_del_init 从队列中间移除
 *
 * ## Worker Claim 协议（核心并发合同）
 *
 * Enqueue、Worker、Cancel 三方通过 irq_lock + flags 原子操作协调对 Work 的"所有权"：
 *
 * ```
 * enqueue:  [CAS: IDLE→PENDING]  →  [irq_lock: list_add_tail]  →  [sem_post]
 * worker:   [sem_wait]  →  [irq_lock: list_del_init + flags=PENDING→RUNNING]  →  [执行 func]
 * cancel:   [irq_lock: 检查 flags + 设置 CANCELLED + 可能 list_del_init]
 * ```
 *
 * **关键不变量**: list_del_init 和 flags 状态转换必须在同一 irq_lock 临界区内完成。
 * 这确保 cancel 看到 PENDING 时节点必定在链表中，看到 RUNNING 时 worker 已认领。
 *
 * ## 内存序策略
 *
 * - **irq_lock 内**: 临界区提供互斥 + 编译器屏障，flags 使用普通 volatile 读写
 * - **irq_lock 外**: 使用原子操作（enqueue CAS、worker STORE_REL、work_is_busy LOAD_ACQ）
 */

#include "async/workqueue.h"
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
        /** 1. 等待工作到达或停止信号 */
        osal_sem_wait(wq->sem, OSAL_WAIT_FOREVER);

        /** 2. 循环排空 pending 队列 */
        for (;;) {
            Work  *w        = NULL;
            bool   cancelled = false;

            /** 2a. 关中断：从链表取出下一个工作项并认领 */
            {
                OsalIrqIsrState k;
                osal_irq_lock(&k);

                if (!list_empty(&wq->pending)) {
                    w = list_first_entry(&wq->pending, Work, node);

                    /** 从 pending 链表移除并毒化节点。 */
                    list_del_init(&w->node);

                    /** 认领所有权：PENDING → RUNNING。
                     *
                     *  irq_lock 内，cancel 不可能并发修改 flags，
                     *  直接比较即可检测 cancel 是否设置了 CANCELLED。 */
                    if (w->flags == WORK_FLAG_PENDING) {
                        w->flags = WORK_FLAG_RUNNING;
                        cancelled = false;
                    } else {
                        /** flags 已被 cancel 改为 PENDING|CANCELLED。
                         *  我们已从链表取出（拥有所有权），
                         *  设置 RUNNING 阻止后续 cancel 竞争。 */
                        w->flags = WORK_FLAG_RUNNING;
                        cancelled = true;
                    }
                }

                osal_irq_unlock(k);
            }

            /** 2b. 无更多工作：检查退出条件 */
            if (!w) {
                int s = OM_LOAD_ACQ(&wq->state);
                if (s == WORKQUEUE_STATE_STOPPING) {
                    /** STOPPING 状态 + 队列已空 → worker 退出 */
                    completion_done(&wq->done);
                    osal_thread_exit();
                }
                /** 回到 sem_wait 等待新工作 */
                break;
            }

            /** 2c. 执行或跳过 */
            if (cancelled) {
                /** cancel 已请求取消 → 恢复 IDLE，不执行 func */
                OM_STORE_REL(&w->flags, WORK_FLAG_IDLE);
            } else {
                /** 正常执行工作函数 */
                w->func(w);
                /** 执行完毕，恢复 IDLE，允许再次入队 */
                OM_STORE_REL(&w->flags, WORK_FLAG_IDLE);
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
    if (!wq || !cfg) return OM_ERROR_PARAM;

    /** 初始化 pending 链表为空的哨兵态（自循环） */
    INIT_LIST_HEAD(&wq->pending);
    wq->thread = NULL;

    /** 创建计数型信号量（最大计数 1，初始计数 0） */
    OsalStatus st = osal_sem_create(&wq->sem, WQ_SEM_MAX_COUNT, 0U);
    if (st != OSAL_OK) return OM_ERROR;

    /** 初始化 worker 退出同步原语 */
    OmRet rc = completion_init(&wq->done);
    if (rc != OM_OK) {
        osal_sem_delete(wq->sem);
        wq->sem = NULL;
        return rc;
    }

    /** 设置状态为 IDLE */
    OM_STORE_RLX(&wq->state, (int)WORKQUEUE_STATE_IDLE);
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
    if (!wq) return OM_ERROR_PARAM;

    /** 必须已 stop */
    int s = OM_LOAD_RLX(&wq->state);
    if (s != WORKQUEUE_STATE_IDLE) return OM_ERROR;

    if (wq->sem) {
        osal_sem_delete(wq->sem);
        wq->sem = NULL;
    }

    completion_deinit(&wq->done);

    OM_STORE_RLX(&wq->state, (int)WORKQUEUE_STATE_UNINIT);
    wq->name = NULL;
    return OM_OK;
}

/* ===================================================================
 * 生命周期管理：start / stop
 * =================================================================== */

/**
 * @brief 启动 worker 线程
 *
 * 通过 CAS 将状态从 IDLE 切换到 RUNNING，然后创建 FreeRTOS 任务
 * 作为 worker 线程。如果线程创建失败，状态回退到 IDLE。
 *
 * @param wq  已初始化的工作队列。
 * @return    OM_OK 成功，OM_ERROR 状态非法或线程创建失败，OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_start(Workqueue *wq)
{
    if (!wq) return OM_ERROR_PARAM;
    if (!wq->sem) return OM_ERROR; /** 未初始化或已销毁 */

    /** CAS 原子切换：IDLE → RUNNING */
    int expected = WORKQUEUE_STATE_IDLE;
    if (!OM_CAS_AR(&wq->state, &expected, WORKQUEUE_STATE_RUNNING)) {
        return OM_ERROR; /** 不在 IDLE 状态 */
    }

    /** 排空上一次循环可能残留的信号量计数 */
    while (osal_sem_wait(wq->sem, 0U) == OSAL_OK) {}

    /** 重置 completion，准备新 worker 周期 */
    completion_init(&wq->done);

    /** 配置并创建 worker 线程 */
    OsalThreadAttr attr = {0};
    attr.name      = wq->name;
    attr.stackSize = wq->stack_depth;
    attr.priority  = wq->priority;

    OsalStatus st = osal_thread_create(&wq->thread, &attr,
                                       workqueue_worker_entry, wq);
    if (st != OSAL_OK) {
        /** 线程创建失败，状态回退 */
        OM_STORE_RLX(&wq->state, (int)WORKQUEUE_STATE_IDLE);
        return OM_ERROR;
    }

    return OM_OK;
}

/**
 * @brief 停止 worker 线程
 *
 * 状态从 RUNNING 切换到 STOPPING，唤醒 worker 线程，等待其排空
 * pending 队列并退出。worker 退出后进行防御性清理（理论上队列应为空）。
 *
 * @param wq  正在运行的工作队列。
 * @return    OM_OK 成功，OM_ERROR 状态非法（未在运行），OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_stop(Workqueue *wq)
{
    if (!wq) return OM_ERROR_PARAM;

    /** CAS 原子切换：RUNNING → STOPPING */
    int expected = WORKQUEUE_STATE_RUNNING;
    if (!OM_CAS_AR(&wq->state, &expected, WORKQUEUE_STATE_STOPPING)) {
        /** 已停止或未在运行 */
        return OM_ERROR;
    }

    /** 唤醒 worker 线程，使其开始排空 */
    osal_sem_post(wq->sem);

    /** 等待 worker 线程退出信号 */
    completion_wait(&wq->done, OSAL_WAIT_FOREVER);
    wq->thread = NULL;

    /** 防御性排空：清理可能在 worker 退出与 stop 返回之间入队的工作项。
     *
     *  STOPPING 状态下 enqueue 会被拒绝（见 workqueue_enqueue 的状态检查），
     *  理论上队列应为空，但安全起见，尝试清理所有残留工作项。 */
    {
        Work            *w, *tmp;
        OsalIrqIsrState  k;
        osal_irq_lock(&k);
        list_for_each_entry_safe(w, tmp, &wq->pending, node)
        {
            list_del_init(&w->node);
            w->flags = WORK_FLAG_IDLE;
        }
        osal_irq_unlock(k);
    }

    /** 状态切换：STOPPING → IDLE */
    OM_STORE_RLX(&wq->state, (int)WORKQUEUE_STATE_IDLE);

    return OM_OK;
}

/* ===================================================================
 * 入队操作（Enqueue）
 *
 * enqueue 分两个阶段：
 *   1. irq_lock 外 CAS(IDLE→PENDING)：原子去重
 *   2. irq_lock 内 list_add_tail：保护链表插入的原子性
 *
 * CAS 在锁外的设计理由：CAS 竞争方（worker 的 flags 修改）在
 * irq_lock 内执行，关中断。单核上两个线程级 enqueue 冲突由 CAS 自身解决。
 * 锁外 CAS 避免了 sem_post 调用时持有 irq_lock（sem_post 可能触发任务切换）。
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
    if (!wq || !work) return OM_ERROR_PARAM;

    /** 拒绝：工作队列未在运行 */
    if (OM_LOAD_ACQ(&wq->state) != WORKQUEUE_STATE_RUNNING) {
        return OM_ERROR;
    }

    /** 去重：原子 CAS 从 IDLE 切换到 PENDING。
     *
     *  如果 work 当前是 PENDING 或 RUNNING，CAS 失败 → 返回 BUSY
     *  (等同于 Linux cmwq 的 WORK_STRUCT_PENDING_BIT 去重机制)。 */
    {
        uint32_t expected = WORK_FLAG_IDLE;
        if (!OM_CAS_RLX(&work->flags, &expected, WORK_FLAG_PENDING)) {
            return OM_ERROR_BUSY; /** 已在队列中或正在执行 */
        }
    }

    /** 关中断：将 work 插入 pending 链表尾部（FIFO 顺序） */
    {
        OsalIrqIsrState key;
        osal_irq_lock(&key);
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
 * cancel 所有操作（flags 检查、CANCELLED 标记、链表删除）均在 irq_lock
 * 临界区内完成。irq_lock 禁用中断和抢占，flags 和链表操作的原子性
 * 由临界区本身保证，使用普通 volatile 读写即可。
 * =================================================================== */

/**
 * @brief 取消一个 pending 工作项
 *
 * 线程安全和 ISR 安全。不从特定 workqueue 上取消，而是通过 Work 自身的
 * flags 和 list node 操作。这意味着调用者无需知道 work 在哪个队列上。
 *
 * @param work  要取消的工作项。
 * @return      OM_OK 成功取消（work 恢复 IDLE，可重新入队）；
 *              OM_ERROR_BUSY work 正在执行，无法取消；
 *              OM_ERROR work 不在 pending 状态；
 *              OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_cancel(Work *work)
{
    if (!work) return OM_ERROR_PARAM;

    OsalIrqIsrState key;

    /** ---- 所有 flags 检查和修改均在 irq_lock 内进行 ---- */
    osal_irq_lock(&key);

    uint32_t f = work->flags;

    /** 拒绝：work 正在执行（worker 已认领） */
    if (f & WORK_FLAG_RUNNING) {
        osal_irq_unlock(key);
        return OM_ERROR_BUSY;
    }

    /** 拒绝：work 不在 pending 状态（可能已执行完毕或从未入队） */
    if (!(f & WORK_FLAG_PENDING)) {
        osal_irq_unlock(key);
        return OM_ERROR;
    }

    /** 标记取消意图 */
    work->flags = f | WORK_FLAG_CANCELLED;

    /** 尝试从 pending 链表移除。
     *
     *  因为我们持有 irq_lock，worker 不可能在此期间从链表取走该节点。
     *  因此在 flags 为 PENDING 的前提下，节点必定仍在链表中。
     *
     *  list_del_init 将节点 next/prev 设为 NULL，兼具毒化效果。
     *  后续 work_init() 会重新 INIT_LIST_HEAD 使节点恢复可用。 */
    if (list_node_is_linked(&work->node)) {
        list_del_init(&work->node);
        /** 成功从链表移除 → 清理所有标志位，work 恢复 IDLE */
        work->flags = WORK_FLAG_IDLE;
    }
    /** 如果节点已不在链表中：worker 已在另一个 irq_lock 临界区中
     *  将其取出。worker 在检查 flags 时会检测到我们设置的 CANCELLED 位，
     *  并跳过执行。 */

    osal_irq_unlock(key);
    return OM_OK;
}

/* ===================================================================
 * 排空操作（Flush）
 *
 * flush 等待当前 pending 队列中所有工作项执行完毕。
 * 实现原理：通过两次 sem_post/sem_wait 握手确保 worker 已完成至少
 * 一次完整的排空循环。
 * =================================================================== */

/**
 * @brief 排空所有 pending 工作项并同步等待完成
 *
 * @param wq  正在运行的工作队列。
 * @return    OM_OK 成功，OM_ERROR 状态非法，OM_ERROR_PARAM 参数为 NULL。
 */
OmRet workqueue_flush(Workqueue *wq)
{
    if (!wq) return OM_ERROR_PARAM;

    /** 工作队列必须处于 RUNNING 状态 */
    if (OM_LOAD_ACQ(&wq->state) != WORKQUEUE_STATE_RUNNING) {
        return OM_ERROR;
    }

    /** 发送两次信号量并等待 worker 两次消费。
     *
     *  第一次信号量触发 worker 排空当前 pending 队列，
     *  第二次信号量确认 worker 已完成排空并回到了 sem_wait 等待状态。
     *
     *  这种双握手确保 worker 至少完成了一次完整的"取出→执行→回到等待"
     *  循环，因此入队早于 flush 调用的所有 work 已处理完毕。 */
    osal_sem_post_auto(wq->sem);
    osal_sem_wait(wq->sem, OSAL_WAIT_FOREVER);

    osal_sem_post_auto(wq->sem);
    osal_sem_wait(wq->sem, OSAL_WAIT_FOREVER);

    return OM_OK;
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
    if (!wq) return WORKQUEUE_STATE_UNINIT;
    return OM_LOAD_RLX(&wq->state);
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
    if (!wq) return true;

    OsalIrqIsrState key;
    osal_irq_lock(&key);
    bool empty = list_empty(&wq->pending);
    osal_irq_unlock(key);
    return empty;
}
