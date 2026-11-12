/**
 * @file ring.c
 * @brief log 统一消息环通道能力：LogRing 实例 API（生产入环/消费抽环/滞留回放）
 * @details 能力-实例分离（框架惯例——Pipe/mpsc 同构）：本文件仅提供通道能力（含门铃
 *          post 组合——pipe 同构），实例（LogRing）由服务主体（core.c）静态持有——
 *          零初始化（惰性 init），早期/最小配置可用（Ringbuf 纯原子无 OSAL）。
 *          生产（log_ring_produce）：临界区串行（多生产者收敛单写者——成本 = 消息拷贝
 *          级）ringbuf_in；满 = 丢新 + 计数。
 *          消费触发（OM_LOG_ASYNC 选择）：1 = 日志线程（**调度器在 log_async.c**——
 *          门铃创建/线程本体/等待循环；本文件仅生产侧 空→非空 才 post（pipe 模式））；
 *          0 = 现场触发——生产后按 accept_mask(本消息) != 0（被任一后端按
 *          (module, level) 生效级接受）→ drain 全量（保生产序，含本消息与滞留段）
 *          → 现场 emit；全拒 → 滞留环中（"deferred"回归形态）。
 *          消费（log_ring_drain）：循环 ringbuf_out → log_emit_args（单消费者 SPSC 读侧）；
 *          尾部丢弃后验告警（log_drop_warn——节流 + 增量；直接 emit 不走环防递归）。
 */

#include "core/om_config.h"

#if OM_USE_LOG

#include "core/om_def.h"
#include "core/om_interrupt.h"
#include "log_internal.h"
#include "osal/osal_time.h" /* 时间戳（host 测试经本地桩头 shadow） */
#if OM_LOG_ASYNC
#include "osal/osal_sem.h" /* 门铃（二值信号量；post_auto 自动分流——生产侧 post） */
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief 惰性初始化（首次生产/消费触发；幂等——并发初化写入同参，无害） */
static void ring_ensure(LogRing *ring)
{
    if (!ring->inited)
    {
        (void)ringbuf_init(&ring->rb, ring->buf, sizeof(OmLogMsg), OM_LOG_RING_LEN);
        ring->inited = true;
    }
}

void log_ring_produce(LogRing *ring, const OmLogMsg *msg)
{
    port_critical_key_t key = om_hw_disable_interrupt(); /* 生产串行化（多生产者→单写者角色） */
    ring_ensure(ring);
    bool was_empty = ringbuf_is_empty(&ring->rb);
    if (ringbuf_in(&ring->rb, msg, 1) == 0)
    {
        ring->dropped++; /* 满 = 丢新 + 计数（后验告警由消费侧补发——不阻塞调用方） */
        om_hw_restore_interrupt(key);
        return;
    }
#if OM_LOG_ASYNC
    if (was_empty && ring->doorbell != NULL) /* 空→非空才 post（pipe 门铃模式——连续写不重复唤醒） */
    {
        (void)osal_sem_post_auto(ring->doorbell); /* 线程/ISR 自动分流（post_from_isr 语义） */
    }
#else
    /* 现场判定：本消息被任一后端接受才 drain（临界区内读表——含按模块覆盖的生效级裁判） */
    bool fire = log_backend_accept_mask(msg->module, msg->level) != 0;
#endif
    om_hw_restore_interrupt(key);
#if !OM_LOG_ASYNC
    if (fire)
    {
        log_ring_drain(ring); /* 现场发射：drain 全量（保生产序——含本消息与滞留段） */
    }
#endif
}

void log_ring_drain(LogRing *ring)
{
    OmLogMsg msg;
    /* 环初始化仅生产侧执行（首次生产必然先于任何消费；若系统零日志，未初始化环
     * mask=0 → cap=1 → 空语义——消费侧安全空转） */
    while (ringbuf_out(&ring->rb, &msg, 1))
    {
        log_emit_args(&msg); /* 消费时刻格式化+扇出（单消费者——SPSC 读侧原子） */
    }
    if (ring->dropped > 0)
    {
        (void)log_drop_warn(&ring->warn, log_service_module(), "ring-full", ring->dropped,
                            osal_time_now_monotonic());
    }
}

void log_ring_flush(LogRing *ring)
{
#if !OM_LOG_ASYNC
    log_ring_drain(ring); /* 服务就绪点：回放滞留段（当下后端表过滤——printk 语义） */
#endif
}

uint32_t log_dropped_ring(const LogRing *ring)
{
    return ring->dropped;
}

/** @brief 丢弃后验告警（丢弃点共同）：节流 + 增量报告——消息经 log_emit_args 直接 emit
 *  （不走环——告警自身引发的环满不再递归；WRN 级，per-backend 过滤正常生效）
 *  @param st 状态（每丢弃点一实例：warned_upto=已上报累计，last_warn_ms=上次上报时刻）
 *  @param module 告警模块实例（log_service_module——"log"）
 *  @param site 丢弃点描述（静态字符串；消息 %s 参数——生命周期框架内恒定）
 *  @param dropped 丢弃点累计丢弃数
 *  @param now_ms 单调毫秒（调用方取 osal_time_now_monotonic——host 测试可控）
 *  @return true = 已补发 WRN */
bool log_drop_warn(LogDropWarnState *st, const OmLogModule *module, const char *site,
                   uint32_t dropped, uint32_t now_ms)
{
    if (dropped <= st->warned_upto)
    {
        return false; /* 无新丢弃 */
    }
    if (st->ever && now_ms - st->last_warn_ms < OM_LOG_DROP_WARN_INTERVAL_MS)
    {
        return false; /* 节流（回绕安全——无符号差；ever=false=从未上报→首次放行） */
    }
    OmLogMsg msg;
    msg.fmt = "log drop: %s dropped %u (total %u)";
    msg.level = OM_LOG_LEVEL_WARN;
    msg.module = module;
    msg.ts = now_ms;
    msg.argBuf[0] = (uintptr_t)site;
    msg.argBuf[1] = dropped - st->warned_upto; /* 增量 */
    msg.argBuf[2] = dropped;                   /* 累计 */
    msg.argCount = 3;
    log_emit_args(&msg);
    st->warned_upto = dropped;
    st->last_warn_ms = now_ms;
    st->ever = 1U;
    return true;
}

#endif /* OM_USE_LOG */
