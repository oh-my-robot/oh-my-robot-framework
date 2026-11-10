/**
 * @file om_log_module_filter_test.c
 * @brief log 后端按模块过滤测试：per-backend 默认级 + 按模块覆盖（覆盖放宽/收紧/
 *        OFF 显式拒/clear 回退默认/get 生效值——覆盖命中或回落默认级的判据；
 *        用例 B：白名单/多后端独立/OFF×FATAL 探针/错误码路径（含 NULL 名参数））
 */

#include "services/log/log.h"

#include "om_log_test_common.h"

#include <string.h>

/* ---- capture 后端 ---- */
/** @brief 捕获后端状态：拼接接收字节 + 段计数（断言比对与零扇出验证用） */
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

/** @brief capA 捕获 push
 *  @param backend 后端实例（must be &g_backend_a——校验回调收到注册的实例指针，防双重指针回归）
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

/** @brief 捕获 flush（无操作占位，接口完整性；本测试无调用点） */
static void capture_flush(OmLogBackend *backend)
{
    (void)backend;
}

/* 后端实例：文件级静态（unregister 按指针，main 需访问）；name = 覆盖 API 按名解析键 */
static OmLogBackend g_backend_a = {"backend_a", capture_push_a, capture_flush, NULL};
static OmLogBackend g_backend_b = {"backend_b", capture_push_b, capture_flush, NULL};

/* ---- 被测模块（手写结构体经公开 om_log_log 调用——moduleId=-1 惰性登记，
 *      首次日志入模块表，与 OM_LOG_MODULE 宏路径一致；覆盖 API 依赖可解析 id） ---- */
static OmLogModule g_mod_a = {"mod_a", OM_LOG_LEVEL_DEBUG, -1};
static OmLogModule g_mod_b = {"mod_b", OM_LOG_LEVEL_DEBUG, -1};
static OmLogModule g_mod_c = {"mod_c", OM_LOG_LEVEL_DEBUG, -1};

/** @brief 用例 A：默认级拒绝/接受 + 覆盖放宽生效 + get 生效值（覆盖与默认回落）+
 *        OFF 显式拒 + clear 回退默认 */
static void test_override_basic(void)
{
    /* 后端 a 以默认级 WARN 注册（覆盖 API 按名解析后端） */
    EXPECT(om_log_backend_register(&g_backend_a, OM_LOG_LEVEL_WARN) == OM_OK);

    /* 模块惰性登记促发：mod_a/mod_b 各打一条（INFO 无后端接受 → 滞留环中，随后续
     * drain 以默认 WARN 逐条拒掉——零段零字节，不污染断言） */
    om_log_log(&g_mod_a, OM_LOG_LEVEL_INFO, "reg mod_a");
    om_log_log(&g_mod_b, OM_LOG_LEVEL_INFO, "reg mod_b");

    /* 默认级 WARN：INFO 拒（零格式化）；WARN 收 */
    memset(&g_cap_a, 0, sizeof(g_cap_a));
    om_log_log(&g_mod_a, OM_LOG_LEVEL_INFO, "info dropped by default");
    EXPECT(g_cap_a.seg_count == 0);
    EXPECT(g_cap_a.len == 0);
    om_log_log(&g_mod_a, OM_LOG_LEVEL_WARN, "warn accepted by default");
    EXPECT(g_cap_a.len > 0);

    /* 覆盖放宽 mod_a → INFO：get 生效值 = 覆盖级；未覆盖 mod_b 回落默认级 WARN */
    memset(&g_cap_a, 0, sizeof(g_cap_a));
    EXPECT(om_log_backend_set_module_level("backend_a", "mod_a", OM_LOG_LEVEL_INFO) == OM_OK);
    OmLogLevel eff = OM_LOG_LEVEL_OFF;
    EXPECT(om_log_backend_get_module_level("backend_a", "mod_a", &eff) == OM_OK &&
           eff == OM_LOG_LEVEL_INFO);
    EXPECT(om_log_backend_get_module_level("backend_a", "mod_b", &eff) == OM_OK &&
           eff == OM_LOG_LEVEL_WARN);
    om_log_log(&g_mod_a, OM_LOG_LEVEL_INFO, "info accepted after override");
    EXPECT(g_cap_a.len > 0);
    EXPECT(g_cap_a.seg_count > 0);
    EXPECT(strcmp(g_cap_a.buf, "[INF][00:00:00.000][mod_a] info accepted after override\n") == 0);

    /* 覆盖收紧 mod_b → OFF：WARN 也拒（显式拒优先于默认级——消息滞留环中零扇出） */
    memset(&g_cap_a, 0, sizeof(g_cap_a));
    EXPECT(om_log_backend_set_module_level("backend_a", "mod_b", OM_LOG_LEVEL_OFF) == OM_OK);
    om_log_log(&g_mod_b, OM_LOG_LEVEL_WARN, "warn rejected by off");
    EXPECT(g_cap_a.seg_count == 0);
    EXPECT(g_cap_a.len == 0);

    /* clear mod_b → 回退默认级：WARN 恢复接收（本次 drain 连同上一条滞留 WARN 一并
     * 发射——消费时刻裁判，故此处仅断言增量接收） */
    EXPECT(om_log_backend_clear_module_level("backend_a", "mod_b") == OM_OK);
    memset(&g_cap_a, 0, sizeof(g_cap_a));
    om_log_log(&g_mod_b, OM_LOG_LEVEL_WARN, "warn accepted after clear");
    EXPECT(g_cap_a.len > 0);

    EXPECT(om_log_backend_unregister(&g_backend_a) == OM_OK);
}

/** @brief 用例 B：白名单（后端默认 OFF + 点名模块覆盖抬升）+ 多后端独立 + 错误码路径
 *  @note 惰性登记陷阱：mod_c 按名设覆盖前须先打日志促登记（未登记 = NOT_FOUND——同
 *        om_log_module_set_level 语义）；sync 模式 drain 在消费时刻裁判——滞留消息
 *        随其后任一接受消息的 drain 一并发射，断言前按序重置捕获缓冲 */
static void test_whitelist_and_errors(void)
{
    /* 后端 a 默认 WARN（多后端独立对照）；后端 b 默认 OFF + mod_c 覆盖抬升 INFO = 白名单 */
    EXPECT(om_log_backend_register(&g_backend_a, OM_LOG_LEVEL_WARN) == OM_OK);
    EXPECT(om_log_backend_register(&g_backend_b, OM_LOG_LEVEL_OFF) == OM_OK);

    /* mod_c 先打一条促登记（INFO 无人接受 → 滞留环中，随后续 drain 以消费时刻裁判发射） */
    om_log_log(&g_mod_c, OM_LOG_LEVEL_INFO, "reg mod_c");
    EXPECT(om_log_backend_set_module_level("backend_b", "mod_c", OM_LOG_LEVEL_INFO) == OM_OK);

    /* 白名单收：mod_c 的 INFO 仅 backend_b 收（backend_a 默认 WARN 拒 INFO——零段） */
    memset(&g_cap_a, 0, sizeof(g_cap_a));
    memset(&g_cap_b, 0, sizeof(g_cap_b));
    om_log_log(&g_mod_c, OM_LOG_LEVEL_INFO, "info accepted via whitelist");
    EXPECT(g_cap_a.seg_count == 0);
    EXPECT(g_cap_b.len > 0);

    /* 多后端独立：mod_a 全拒（滞留）；WARN 仅 backend_a 收——白名单外模块对 backend_b
     * 任何级别零段 */
    memset(&g_cap_a, 0, sizeof(g_cap_a));
    memset(&g_cap_b, 0, sizeof(g_cap_b));
    om_log_log(&g_mod_a, OM_LOG_LEVEL_INFO, "info to no backend");
    EXPECT(g_cap_a.seg_count == 0);
    EXPECT(g_cap_b.seg_count == 0);
    om_log_log(&g_mod_a, OM_LOG_LEVEL_WARN, "warn to backend_a only");
    EXPECT(g_cap_a.len > 0);
    EXPECT(g_cap_b.seg_count == 0);

    /* OFF×FATAL 探针：白名单外模块的 FATAL 对默认 OFF 后端也零段——OFF = 显式拒（全级，
     * 与"仅拒 < FATAL"的判据差异被钉死）；对照：默认 WARN 的 backend_a 应照收 */
    memset(&g_cap_a, 0, sizeof(g_cap_a));
    memset(&g_cap_b, 0, sizeof(g_cap_b));
    om_log_log(&g_mod_a, OM_LOG_LEVEL_FATAL, "fatal rejected by whitelist");
    EXPECT(g_cap_b.seg_count == 0);
    EXPECT(g_cap_a.len > 0);

    /* 白名单内模块（覆盖 INFO）的 FATAL 应收——覆盖只决定门槛：FATAL >= INFO 放行 */
    memset(&g_cap_b, 0, sizeof(g_cap_b));
    om_log_log(&g_mod_c, OM_LOG_LEVEL_FATAL, "fatal accepted by whitelist");
    EXPECT(g_cap_b.len > 0);

    /* NOT_FOUND：模块未登记（"ghost_mod"从未打日志——惰性语义）/ 后端名不存在 */
    EXPECT(om_log_backend_set_module_level("backend_b", "ghost_mod", OM_LOG_LEVEL_INFO) ==
           OM_ERR_NOT_FOUND);
    EXPECT(om_log_backend_set_module_level("no_such_backend", "mod_c", OM_LOG_LEVEL_INFO) ==
           OM_ERR_NOT_FOUND);

    /* clear/get_module_level 错误码补全：NOT_FOUND（未登记模块 / 不存在后端） */
    EXPECT(om_log_backend_clear_module_level("backend_b", "ghost_mod") == OM_ERR_NOT_FOUND);
    EXPECT(om_log_backend_clear_module_level("no_such_backend", "mod_c") == OM_ERR_NOT_FOUND);
    OmLogLevel eff;
    EXPECT(om_log_backend_get_module_level("no_such_backend", "mod_c", &eff) == OM_ERR_NOT_FOUND);
    EXPECT(om_log_backend_get_module_level("backend_b", "ghost_mod", &eff) == OM_ERR_NOT_FOUND);

    /* INVALID_ARG：级别越界（>= MAX）/ get 输出指针 NULL */
    EXPECT(om_log_backend_set_module_level("backend_b", "mod_c", OM_LOG_LEVEL_MAX) ==
           OM_ERR_INVALID_ARG);
    EXPECT(om_log_backend_get_module_level("backend_b", "mod_c", NULL) == OM_ERR_INVALID_ARG);

    /* 三 API 的 NULL 名参数 = INVALID_ARG：参数校验先于按名解析——NULL 模块名被拦在
     * "未登记 NOT_FOUND"判定之前（resolve 不接触 NULL） */
    EXPECT(om_log_backend_set_module_level(NULL, "mod_c", OM_LOG_LEVEL_INFO) ==
           OM_ERR_INVALID_ARG);
    EXPECT(om_log_backend_set_module_level("backend_b", NULL, OM_LOG_LEVEL_INFO) ==
           OM_ERR_INVALID_ARG);
    EXPECT(om_log_backend_clear_module_level(NULL, "mod_c") == OM_ERR_INVALID_ARG);
    EXPECT(om_log_backend_clear_module_level("backend_b", NULL) == OM_ERR_INVALID_ARG);
    EXPECT(om_log_backend_get_module_level(NULL, "mod_c", &eff) == OM_ERR_INVALID_ARG);
    EXPECT(om_log_backend_get_module_level("backend_b", NULL, &eff) == OM_ERR_INVALID_ARG);

    EXPECT(om_log_backend_unregister(&g_backend_b) == OM_OK);
    EXPECT(om_log_backend_unregister(&g_backend_a) == OM_OK);
}

int main(void)
{
    test_override_basic();
    test_whitelist_and_errors();

    if (g_log_test_failed)
    {
        printf("om_log_module_filter_test: FAIL\n");
        return 1;
    }
    printf("om_log_module_filter_test: ALL PASS\n");
    return 0;
}
