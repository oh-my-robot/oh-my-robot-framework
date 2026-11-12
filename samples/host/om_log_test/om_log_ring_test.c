/**
 * @file om_log_ring_test.c
 * @brief log 统一消息环测试（同步模式：现场触发 + 滞留回放 + 满丢告警）
 * @details 本目标以 OM_LOG_ASYNC=0 + OM_LOG_RING_LEN=4 编译（xmake.lua——零 OSAL：Ringbuf
 *          纯原子；门铃/线程为异步模式专属——目标板验证）。场景：
 *          ① 无后端滞留（不丢） ② 服务就绪点回放（生产序 + per-backend 过滤）
 *          ③ 环满 = 丢新 + 计数 + 'ring-full' WRN ④ 回放后现场直发（生产序保持）
 *          ⑤ om_log_stats 汇总（环满项） ⑥ 告警节流（显式时刻——时间桩恒 0）
 */

#include "services/log/log.h"

#include "om_log_test_common.h"

#include "log_internal.h" /* log_service_start / log_drop_warn / log_service_module */

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

/* ---- capture 后端（与 filter 测试同构——多目标独立编译） ---- */
typedef struct
{
    char buf[2048];
    size_t len;
} CaptureBackend;

static CaptureBackend g_cap_a;
static CaptureBackend g_cap_b;
static OmLogBackend g_backend_a;
static OmLogBackend g_backend_b;

static void capture_push_a(OmLogBackend *backend, const char *seg, size_t len)
{
    EXPECT(backend == &g_backend_a);
    (void)memcpy(g_cap_a.buf + g_cap_a.len, seg, len);
    g_cap_a.len += len;
}

static void capture_push_b(OmLogBackend *backend, const char *seg, size_t len)
{
    EXPECT(backend == &g_backend_b);
    (void)memcpy(g_cap_b.buf + g_cap_b.len, seg, len);
    g_cap_b.len += len;
}

static void capture_flush(OmLogBackend *backend)
{
    (void)backend;
}

static OmLogBackend g_backend_a = {"capA", capture_push_a, capture_flush, NULL};
static OmLogBackend g_backend_b = {"capB", capture_push_b, capture_flush, NULL};

/* ---- 被测模块 ---- */
OM_LOG_MODULE(ringtest, OM_LOG_LEVEL_INFO);

/** @brief 尾部比对：捕获缓冲复位 len 后须先补 NUL（截断比较——先例见 filter 测试）
 *  @param cap 捕获器
 *  @param expect 期望尾串（NUL 结尾） */
static void expect_tail(CaptureBackend *cap, const char *expect)
{
    size_t n = strlen(expect);
    cap->buf[cap->len] = '\0';
    EXPECT(cap->len >= n);
    if (cap->len >= n)
    {
        EXPECT(strcmp(cap->buf + cap->len - n, expect) == 0);
    }
}

int main(void)
{
    const char *const l_pre = "[INF][00:00:00.000][ringtest] pre 1\n";
    const char *const l_mid = "[INF][00:00:00.000][ringtest] mid 2\n";
    const char *const l_err = "[ERR][00:00:00.000][ringtest] err 3\n";
    const char *const l_tail = "[INF][00:00:00.000][ringtest] tail 4\n";
    const char *const warn =
        "[WRN][00:00:00.000][log] log drop: ring-full dropped 1 (total 1)\n";

    memset(&g_cap_a, 0, sizeof(g_cap_a));
    memset(&g_cap_b, 0, sizeof(g_cap_b));
    g_cap_a.buf[0] = '\0';
    g_cap_b.buf[0] = '\0';

    /* ① 无后端滞留：全部入环（容 4 槽）——第 5 条 ③ 满丢 + 计数 */
    OM_LOG_INFO("pre %d", 1);
    OM_LOG_INFO("mid %d", 2);
    OM_LOG_ERROR("err %d", 3);
    OM_LOG_INFO("tail %d", 4);
    OM_LOG_INFO("over %d", 5); /* 环满：丢新（滞留段保持最早） */
    EXPECT(g_cap_a.len == 0);
    EXPECT(g_cap_b.len == 0);

    /* 后端注册（capA 全收 / capB 只收 ERROR+）——回放仍按条过滤 */
    EXPECT(om_log_backend_register(&g_backend_a, OM_LOG_LEVEL_DEBUG) == OM_OK);
    EXPECT(om_log_backend_register(&g_backend_b, OM_LOG_LEVEL_ERROR) == OM_OK);

    /* ② 服务就绪点回放（服务侧触发——实例属主 core.c）：生产序保留 + per-backend 过滤
     * + ③ 丢弃告警（WRN 不进 capB） */
    log_service_start();
    {
        char expect_a[512];
        g_cap_a.buf[g_cap_a.len] = '\0';
        g_cap_b.buf[g_cap_b.len] = '\0';
        (void)snprintf(expect_a, sizeof(expect_a), "%s%s%s%s%s", l_pre, l_mid, l_err, l_tail,
                       warn);
        EXPECT(strcmp(g_cap_a.buf, expect_a) == 0); /* 保早（满丢新）+ 顺序 + 告警 */
        EXPECT(strcmp(g_cap_b.buf, l_err) == 0);    /* 级别过滤 + 告警未进 capB */
    }

    /* ⑤ 统计汇总：环满计数入 dropped */
    {
        OmLogStats st;
        EXPECT(om_log_stats(&st) == OM_OK);
        EXPECT(st.dropped == 1);
    }

    /* ④ 回放后现场直发：有后端接受 → drain（生产序：单条即发） */
    OM_LOG_INFO("post %d", 5);
    expect_tail(&g_cap_a, "[INF][00:00:00.000][ringtest] post 5\n");

    /* ⑥ 告警节流（显式时刻驱动——注入状态与时刻，纯状态机；
     * 告警消息时间戳 = 补发时刻 now（printk 语义——生产即打点）；
     * 初始态 = 零初始化（与真实实例字段相同——ever=0 首次放行） */
    {
        LogDropWarnState st = {0U, 0U, 0U};
        EXPECT(log_drop_warn(&st, log_service_module(), "ring-full", 1, 0));
        expect_tail(&g_cap_a,
                    "[WRN][00:00:00.000][log] log drop: ring-full dropped 1 (total 1)\n");
        EXPECT(!log_drop_warn(&st, log_service_module(), "ring-full", 5, 0));   /* 节流（间隔未到） */
        EXPECT(log_drop_warn(&st, log_service_module(), "ring-full", 5, 1000)); /* 增量 4 */
        expect_tail(&g_cap_a,
                    "[WRN][00:00:01.000][log] log drop: ring-full dropped 4 (total 5)\n");
        EXPECT(!log_drop_warn(&st, log_service_module(), "ring-full", 5, 2500)); /* 无新增 */
        EXPECT(log_drop_warn(&st, log_service_module(), "ring-full", 7, 2500));  /* 新增 + 间隔已到 */
        expect_tail(&g_cap_a,
                    "[WRN][00:00:02.500][log] log drop: ring-full dropped 2 (total 7)\n");
    }

    if (g_log_test_failed)
    {
        printf("om_log_ring_test: FAIL\n");
        return 1;
    }
    printf("om_log_ring_test: ALL PASS\n");
    return 0;
}
