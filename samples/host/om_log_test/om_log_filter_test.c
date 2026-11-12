/**
 * @file om_log_filter_test.c
 * @brief log 管线测试：om_log_log 全链（编译期门控/后端扇出/per-backend 过滤/
 *        OFF/注册注销错误码/段切分拼接/头部格式）+ om_log_stats 丢计数（T6）
 *        + panic 投递（无 per-backend 过滤/panic 钩子优先/NULL→push 尽力）
 *        + om_log_panic 直出（提满全出/级别标注保留/桩时间戳恒定）
 */

#include "services/log/log.h"

#include "om_log_test_common.h"

#include "log_internal.h" /* OmLogMsg/log_msg_build（超限丢弃探针——T6） */

#include <stdarg.h>
#include <string.h>

/* ---- capture 后端 ---- */
/** @brief 捕获后端状态：拼接接收字节 + 段计数（断言比对与段切分验证用） */
typedef struct
{
    char buf[1024];
    size_t len;
    unsigned seg_count;
    unsigned panic_called;
} CaptureBackend;

static CaptureBackend g_cap_a;
static CaptureBackend g_cap_b;

/* 前向声明（capture_push_a/b 断言 backend==&g_backend_*，需先见实例声明） */
static OmLogBackend g_backend_a;
static OmLogBackend g_backend_b;

/** @brief capA 捕获 push（一后端一 push + 静态状态）
 *  @param backend 后端实例（must be &g_backend_a——校验回调收到的是注册的实例指针而非
 *         表项地址；2026-08-22 实测的 push(&g_backends[i].backend) 双重指针 bug 即由此
 *         断言拦截（真机 dev==NULL，host 因忽略参数漏检））
 *  @param seg 段数据
 *  @param len 段字节数 */
static void capture_push_a(OmLogBackend *backend, const char *seg, size_t len)
{
    EXPECT(backend == &g_backend_a);
    (void)memcpy(g_cap_a.buf + g_cap_a.len, seg, len);
    g_cap_a.len += len;
    g_cap_a.seg_count++;
}

/** @brief capB 捕获 push（同 capture_push_a，写 g_cap_b；校验 backend == &g_backend_b）
 *  @param backend 后端实例（must be &g_backend_b）
 *  @param seg 段数据
 *  @param len 段字节数 */
static void capture_push_b(OmLogBackend *backend, const char *seg, size_t len)
{
    EXPECT(backend == &g_backend_b);
    (void)memcpy(g_cap_b.buf + g_cap_b.len, seg, len);
    g_cap_b.len += len;
    g_cap_b.seg_count++;
}

/** @brief 捕获 flush（无操作占位，接口完整性；v1 无调用点）
 *  @param backend 后端实例（本测试未用） */
static void capture_flush(OmLogBackend *backend)
{
    (void)backend;
}

/** @brief capA panic 提交（记录调用标记——验证"panic 钩子优先"）
 *  @param backend 后端实例（本钩子未用）
 *  @param seg 段数据（本钩子未用）
 *  @param len 段字节数（本钩子未用） */
static void capture_panic_a(OmLogBackend *backend, const char *seg, size_t len)
{
    (void)backend;
    (void)seg;
    (void)len;
    g_cap_a.panic_called++;
}

/* 后端实例：文件级静态（unregister 按指针，main 需访问）；
 * capB 不加 panic（NULL）——验证 panic 投递时 NULL 钩子退回 push 尽力而为 */
static OmLogBackend g_backend_a = {"capA", capture_push_a, capture_flush, capture_panic_a};
static OmLogBackend g_backend_b = {"capB", capture_push_b, capture_flush, NULL};

/** @brief 注册 capA/capB 并验证管理 API 错误码
 *  @note capA 注册级别 DEBUG（全收）；capB 注册级别 ERROR（收紧）；本函数即
 *        管理 API 的负向用例（重复注册/NULL/级别越界/按名查找未找到） */
static void setup_backends(void)
{
    memset(&g_cap_a, 0, sizeof(g_cap_a));
    memset(&g_cap_b, 0, sizeof(g_cap_b));
    EXPECT(om_log_backend_register(&g_backend_a, OM_LOG_LEVEL_DEBUG) == OM_OK);
    EXPECT(om_log_backend_register(&g_backend_b, OM_LOG_LEVEL_ERROR) == OM_OK);
    EXPECT(om_log_backend_register(&g_backend_a, OM_LOG_LEVEL_INFO) == OM_ERR_ALREADY); /* 重复注册 */
    EXPECT(om_log_backend_register(NULL, OM_LOG_LEVEL_INFO) == OM_ERR_INVALID_ARG);
    EXPECT(om_log_backend_register(&g_backend_b, OM_LOG_LEVEL_MAX) == OM_ERR_INVALID_ARG); /* 级别越界 */
    EXPECT(om_log_backend_set_level("no_such", OM_LOG_LEVEL_ERROR) == OM_ERR_NOT_FOUND);
    EXPECT(om_log_backend_set_level("capA", OM_LOG_LEVEL_MAX) == OM_ERR_INVALID_ARG);
}

/* ---- 被测模块（注册宏 + 调用宏冒烟） ---- */
OM_LOG_MODULE(testmod, OM_LOG_LEVEL_INFO);

/* ---- om_log_stats 超限探针（T6） ---- */

/** @brief 打包探针用模块实例（独立于 testmod——探针直调 log_msg_build，
 *         不走调用宏；本 TU 静态，与 formatter_test 的 fake_mod 无冲突） */
static const OmLogModule g_fake_module = {"stats_probe", OM_LOG_LEVEL_INFO};

/** @brief 超限打包探针：9 参（> OM_LOG_MAX_ARGS 8）→ log_msg_build false（内部超限计数递增）
 *  @param fmt 格式串
 *  @return 打包结果（false = 超限丢弃）
 *  @note log_msg_build 形参为 va_list（x86_64 下为数组类型），必须经 varargs 包装入口调用
 *        （T3 build_check 同款模式——main 内不能直接 va_start 取参） */
static bool build_over_limit_probe(const char *fmt, ...)
{
    OmLogMsg m;
    va_list ap;
    va_start(ap, fmt);
    bool ok = log_msg_build(&m, &g_fake_module, OM_LOG_LEVEL_INFO, 0U, fmt, ap);
    va_end(ap);
    return ok;
}

int main(void)
{
    setup_backends();

    /* INFO：capA（DEBUG 全收）收，capB（ERROR）不收
     * （时间戳 = osal 单调 ms host 桩恒 0 → "00:00:00.000"——头部格式自检） */
    OM_LOG_INFO("hello %d", 42);
    EXPECT(strcmp(g_cap_a.buf, "[INF][00:00:00.000][testmod] hello 42\n") == 0);
    EXPECT(g_cap_b.len == 0);

    /* DEBUG < 编译期 INFO → 全静默（编译期门控） */
    OM_LOG_DEBUG("below compile level");
    EXPECT(g_cap_a.len == strlen("[INF][00:00:00.000][testmod] hello 42\n"));

    /* ERROR：capA + capB 都收 */
    OM_LOG_ERROR("oops %s", "x");
    EXPECT(strcmp(g_cap_b.buf, "[ERR][00:00:00.000][testmod] oops x\n") == 0);
    EXPECT(strcmp(g_cap_a.buf + strlen("[INF][00:00:00.000][testmod] hello 42\n"),
                  "[ERR][00:00:00.000][testmod] oops x\n") == 0);

    /* 段切分：>32B 消息多段回调，拼接一致 */
    {
        char long_msg[200];
        size_t i;
        for (i = 0; i < sizeof(long_msg) - 1; i++)
        {
            long_msg[i] = (char)('a' + (i % 26));
        }
        long_msg[sizeof(long_msg) - 1] = '\0';
        g_cap_a.seg_count = 0;
        OM_LOG_INFO("long:%s", long_msg);
        char expect[240];
        (void)snprintf(expect, sizeof(expect), "[INF][00:00:00.000][testmod] long:%s\n", long_msg);
        EXPECT(strcmp(g_cap_a.buf + g_cap_a.len - strlen(expect), expect) == 0);
        EXPECT(g_cap_a.seg_count > 1);
    }

    /* 级别调节：capA 调 OFF → ERROR 只进 capB */
    EXPECT(om_log_backend_set_level("capA", OM_LOG_LEVEL_OFF) == OM_OK);
    size_t len_a_off = g_cap_a.len;
    size_t len_b_before = g_cap_b.len;
    OM_LOG_ERROR("after off");
    EXPECT(g_cap_a.len == len_a_off);
    EXPECT(g_cap_b.len > len_b_before);

    /* capB 也 OFF → 全静默（无后端接受 → 消息滞留环中，零格式化） */
    EXPECT(om_log_backend_set_level("capB", OM_LOG_LEVEL_OFF) == OM_OK);
    size_t len_b_off = g_cap_b.len;
    OM_LOG_ERROR("all off");
    EXPECT(g_cap_b.len == len_b_off);
    /* 服务就绪点回放（服务侧触发，模拟 OM_INIT_SERVICE）：滞留消息 emit——级别仍全 OFF
     * → 无输出（回放时刻裁判） */
    log_service_start();
    EXPECT(g_cap_b.len == len_b_off);

    /* om_log_stats：初始 0 → 超限丢弃后 dropped 递增（T6） */
    {
        OmLogStats st;
        EXPECT(om_log_stats(&st) == OM_OK);
        EXPECT(st.dropped == 0);
        EXPECT(om_log_stats(NULL) == OM_ERR_INVALID_ARG);
    }
    /* 超限丢弃计数：9 参 > OM_LOG_MAX_ARGS 8 → false + g_dropped_overflow 递增 */
    EXPECT(build_over_limit_probe("%d %d %d %d %d %d %d %d %d", 1, 2, 3, 4, 5, 6, 7, 8, 9) == false);
    {
        OmLogStats st;
        EXPECT(om_log_stats(&st) == OM_OK);
        EXPECT(st.dropped >= 1);
    }

    /* panic 投递：无 per-backend 过滤（提满全出）+ panic 钩子优先（NULL→push 尽力） */
    {
        EXPECT(om_log_backend_set_level("capB", OM_LOG_LEVEL_OFF) == OM_OK);
        g_cap_a.panic_called = 0;
        size_t len_a = g_cap_a.len;
        size_t len_b = g_cap_b.len;
        log_backend_panic_push_all("pseg", 4);
        EXPECT(g_cap_a.panic_called == 1); /* capA：panic 钩子被优先调用（单次调用一次回调） */
        EXPECT(g_cap_a.len == len_a);      /* capA：push 未调用（钩子优先） */
        EXPECT(g_cap_b.len > len_b);       /* capB：NULL 钩子 → push 尽力 */
        /* 恢复级别（后续用例依赖） */
        EXPECT(om_log_backend_set_level("capB", OM_LOG_LEVEL_ERROR) == OM_OK);
        EXPECT(om_log_backend_set_level("capA", OM_LOG_LEVEL_DEBUG) == OM_OK);
    }

    /* om_log_panic 直出：无后端接受级别也全出（提满）；级别标注保留；头部时间戳恒定（桩） */
    {
        /* capA/capB 均 OFF——正常路径全静默（上方"all off"已证），panic 提满全出 */
        EXPECT(om_log_backend_set_level("capA", OM_LOG_LEVEL_OFF) == OM_OK);
        EXPECT(om_log_backend_set_level("capB", OM_LOG_LEVEL_OFF) == OM_OK);
        g_cap_a.len = 0;
        g_cap_b.len = 0;
        g_cap_a.panic_called = 0;
        g_cap_b.seg_count = 0;
        om_log_panic(&_om_log_module, OM_LOG_LEVEL_ERROR, "panic %d", 7);
        g_cap_a.buf[g_cap_a.len] = '\0'; /* 复位 len 后必须补 NUL（截断比较） */
        g_cap_b.buf[g_cap_b.len] = '\0';
        EXPECT(g_cap_a.panic_called > 0);                  /* capA：panic 钩子接件（钩子优先——证据保全路径，每段一次回调） */
        EXPECT(g_cap_a.panic_called == g_cap_b.seg_count); /* 钩子/推送同构——每段各一次 */
        EXPECT(g_cap_b.len > 0);                           /* capB：NULL 钩子 → push 收全文（OFF 也全出——提满） */
        EXPECT(strcmp(g_cap_b.buf, "[ERR][00:00:00.000][testmod] panic 7\n") == 0);
        /* 级别标注保留 + 桩时间戳恒定（全程逐字节比对） */
        EXPECT(g_cap_a.len == 0); /* capA：钩子优先——panic 后 push 未调用 */
        /* 恢复级别（后续用例依赖） */
        EXPECT(om_log_backend_set_level("capA", OM_LOG_LEVEL_DEBUG) == OM_OK);
        EXPECT(om_log_backend_set_level("capB", OM_LOG_LEVEL_ERROR) == OM_OK);
    }

    /* 模块级调节：set/get 行为 + 判定链生效（testmod 已由首日志惰性登记；
     * 仅操作 _om_log_module（宏生成 static 可写）——不触碰 g_fake_module 隐患 fixture） */
    {
        OmLogLevel lv;
        EXPECT(om_log_module_get_level("testmod", &lv) == OM_OK && lv == OM_LOG_LEVEL_INFO);
        EXPECT(om_log_module_get_level("no_such_mod", &lv) == OM_ERR_NOT_FOUND);
        EXPECT(om_log_module_set_level("testmod", OM_LOG_LEVEL_WARN) == OM_OK);
        size_t len_before = g_cap_a.len;
        OM_LOG_INFO("filtered by runtime level"); /* INFO < WARN → 判定链拦截 */
        EXPECT(g_cap_a.len == len_before);
        OM_LOG_ERROR("still visible"); /* ERROR >= WARN → 出 */
        EXPECT(g_cap_a.len > len_before);
        /* 恢复 + 边界 */
        EXPECT(om_log_module_set_level("testmod", OM_LOG_LEVEL_INFO) == OM_OK);
        EXPECT(om_log_module_set_level("no_such_mod", OM_LOG_LEVEL_ERROR) == OM_ERR_NOT_FOUND);
        EXPECT(om_log_module_set_level("testmod", OM_LOG_LEVEL_MAX) == OM_ERR_INVALID_ARG);
        EXPECT(om_log_module_get_level(NULL, &lv) == OM_ERR_INVALID_ARG);
    }

    /* 注销：OK → 重复 NOT_FOUND → NULL INVALID_ARG */
    EXPECT(om_log_backend_unregister(&g_backend_a) == OM_OK);
    EXPECT(om_log_backend_unregister(&g_backend_a) == OM_ERR_NOT_FOUND);
    EXPECT(om_log_backend_unregister(NULL) == OM_ERR_INVALID_ARG);
    EXPECT(om_log_backend_unregister(&g_backend_b) == OM_OK);

    if (g_log_test_failed)
    {
        printf("om_log_filter_test: FAIL\n");
        return 1;
    }
    printf("om_log_filter_test: ALL PASS\n");
    return 0;
}
