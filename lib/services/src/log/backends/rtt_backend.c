/**
 * @file rtt_backend.c
 * @brief RTT 日志后端实现（push/panic = SEGGER_RTT_Write——内存环形缓冲提交）
 * @details SEGGER RTT 库（third_party/segger-rtt/——BSD 许可，保留 LICENSE.md）：
 *          写入 = 字节拷入 RAM 环形缓冲（写指针前进，纳秒级无硬件等待）；
 *          宿主经调试接口读取（J-Link RTT Viewer / RTTClient 等）。单生产者
 *          （log 服务保证：就绪=日志线程，兜底=调用侧，panic 时线程已死）——
 *          SPSC 环形无锁；通道 0 固定（v1）：库默认仅配置通道 0。
 */

#include "core/om_config.h"

#if OM_USE_LOG

#include "core/om_def.h"
#include "core/om_init.h" /* OM_INIT_DRIVER（默认后端隐藏注册） */

#include "SEGGER_RTT.h"
#include "services/log/log.h"
#include "services/log/rtt_backend.h"

#include <stddef.h> /* offsetof */

/* 首成员强转取实例的前提：backend 位于 offset 0（约束锁定在本文件——编译期保障，
 * 重构结构体顺序会立即编译报错）——与串口后端同款 */
_Static_assert(offsetof(OmRttBackend, backend) == 0,
               "OmRttBackend.backend 须为首成员（直接强转取实例的前提）");

/* 默认后端级别越界守卫：OM_LOG_LEVEL_* 为 enum 常量（预处理期对 #if 不可见——恒判 0），
 * 故用 _Static_assert 实现编译期防线（负数组编译错误同款）：越界值编译期报错。 */
_Static_assert(OM_LOG_RTT_LEVEL < OM_LOG_LEVEL_MAX,
               "OM_LOG_RTT_LEVEL 越界（须 < OM_LOG_LEVEL_MAX）");

/** @brief 段推送：字节拷入 RTT 环形缓冲（log 临界区/日志线程内被调——
 *          SPSC 单生产者由 log 服务编排保证；满丢弃 = RTT 库 skip 模式返回不足）
 *  @param backend 后端实例（首成员强转反查 OmRttBackend）
 *  @param seg 段数据
 *  @param len 段字节数 */
static void rtt_push(OmLogBackend *backend, const char *seg, size_t len)
{
    OmRttBackend *inst = (OmRttBackend *)backend;
    if (inst == NULL)
    {
        return;
    }
    if (len > 0xFFFFFFFFu) /* RTT 库 NumBytes 为 unsigned——超界防御（不可能实际触发） */
    {
        len = 0xFFFFFFFFu;
    }
    (void)SEGGER_RTT_Write(0u, seg, (unsigned)len); /* 通道 0 固定（v1） */
}

/** @brief panic 提交：同 push（内存直写——调试接口读，无硬件依赖——崩溃时仍可靠）
 *  @param backend 后端实例
 *  @param seg 段数据
 *  @param len 段字节数 */
static void rtt_panic(OmLogBackend *backend, const char *seg, size_t len)
{
    rtt_push(backend, seg, len);
}

/** @brief 注册 RTT 日志后端（契约见 services/log/rtt_backend.h）
 *  @param inst 实例（调用者静态分配，零初始化即可）
 *  @param name 后端名
 *  @param level 初始级别
 *  @return OM_OK 成功；OM_ERR_INVALID_ARG 参数非法；注册失败传播其错误码 */
OmRet om_rtt_backend_register(OmRttBackend *inst, const char *name, OmLogLevel level)
{
    if (inst == NULL || name == NULL)
    {
        return OM_ERR_INVALID_ARG;
    }
    inst->backend.name = name;
    inst->backend.push = rtt_push;
    inst->backend.flush = NULL;      /* RTT 无 flush 语义 */
    inst->backend.panic = rtt_panic; /* 内存通道——panic 同 push（天然可靠） */
    return om_log_backend_register(&inst->backend, level);
}

#if OM_LOG_RTT
/* 内置默认后端：宏开关（OM_LOG_RTT=1 零接线）+ 隐藏注册（OM_INIT_DRIVER——调度器前非阻塞，
 * 早于业务日志；存活经 selfreg 直注入同款机制）。API 版（om_rtt_backend_register）保留：
 * 多实例/自定义名使用；与默认后端同名需二选一（注册表查重返回 OM_ERR_ALREADY）。 */
static OmRttBackend g_rtt_default;

static OmRet rtt_backend_autoreg(void)
{
    return om_rtt_backend_register(&g_rtt_default, OM_LOG_RTT_NAME, OM_LOG_RTT_LEVEL);
}
OM_INIT_DRIVER(rtt_backend_autoreg);
#endif /* OM_LOG_RTT */

#endif /* OM_USE_LOG */
