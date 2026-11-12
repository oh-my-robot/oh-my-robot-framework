/**
 * @file backend.c
 * @brief log 输出后端注册表（定长数组，临界区保护；多生产者广播的扇出端）
 * @details 注册/注销/级别调节为管理类 API（线程上下文）；accept_mask/push_mask 为
 *          消费编排（core.c/ring.c 调用）——每条消息按 (module, level) 在交付前求值
 *          一次得接受位图（0 = 被全部后端拒绝 → 零格式化），逐段按位图位移扇出
 *          （零查表）。注册携带默认级（过滤阈值），运行时经 om_log_backend_set_level
 *          调默认级、set/clear/get_module_level 调按模块覆盖表（覆盖命中优先默认级）。
 */

#include "core/om_config.h"

#if OM_USE_LOG

#include "core/om_def.h"
#include "core/om_interrupt.h"

#include "log_internal.h"

#include <string.h>

/** @brief 覆盖表哨兵：0xFF = 未覆盖（读作"用默认级"；memset 全置便捷——值 255 非合法级别） */
#define LOG_BACKEND_MOD_UNSET 0xFF

/** @brief 后端表项：level = 默认级（过滤阈值——msg.level >= 生效级才投递）；
 *  modLevel = 按模块覆盖表（moduleId → 级别；0xFF = 未覆盖 → 用默认级） */
typedef struct
{
    OmLogBackend *backend;
    OmLogLevel level;                     /* 默认级（注册时设置——语义不变） */
    uint8_t modLevel[OM_LOG_MAX_MODULES]; /* 覆盖：0xFF = 未覆盖 → 用默认级 */
    uint8_t used;
} LogBackendEntry;

_Static_assert(OM_LOG_MAX_BACKENDS <= 8, "OM_LOG_MAX_BACKENDS 超接受位图承载上限（uint8_t）");

/** @brief 后端注册表（定长数组，静态零初始化；所有读写均在临界区内，无初始化生命周期） */
static LogBackendEntry g_backends[OM_LOG_MAX_BACKENDS];

/** @brief 生效级：覆盖命中（moduleId 合法且覆盖表有值）→ 覆盖值；否则 → 默认级
 *  @param e 后端表项
 *  @param m 消息模块实例（moduleId < 0 = 未登记/表满——覆盖表不可查，回退默认级兜底）
 *  @return 该后端对该模块消息的级别门槛 */
static OmLogLevel backend_eff_level(const LogBackendEntry *e, const OmLogModule *m)
{
    if (m->moduleId >= 0 && m->moduleId < OM_LOG_MAX_MODULES &&
        e->modLevel[(size_t)m->moduleId] != LOG_BACKEND_MOD_UNSET)
    {
        return (OmLogLevel)e->modLevel[(size_t)m->moduleId];
    }
    return e->level;
}

/** @brief 接受位图：bit i = 后端 i 接受该消息（used && level >= 生效级）
 *  @param module 消息模块实例（moduleId < 0 → 全部后端按默认级裁判——覆盖不可查兜底）
 *  @param level 消息级别
 *  @return 位图（0 = 被全部后端拒绝——调用方零格式化快路径；一条消息求值一次，
 *          段级推送仅按位图位移——见 push_mask） */
uint8_t log_backend_accept_mask(const OmLogModule *module, OmLogLevel level)
{
    uint8_t mask = 0;
    size_t i;
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++)
    {
        if (g_backends[i].used && level >= backend_eff_level(&g_backends[i], module))
        {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}

/** @brief 按位图扇出：对 mask 置位的后端逐段 push（表序——与 accept_mask 求值同序）
 *  @param mask 接受位图（accept_mask 产物——本函数不做二次过滤判定）
 *  @param seg 段数据
 *  @param len 段字节数 */
void log_backend_push_mask(uint8_t mask, const char *seg, size_t len)
{
    size_t i;
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++)
    {
        if ((mask & (uint8_t)(1u << i)) != 0)
        {
            g_backends[i].backend->push(g_backends[i].backend, seg, len);
        }
    }
}

/** @brief panic 投递：无 per-backend 级别过滤（提满全出——崩溃证据保全）；
 *  panic 钩子优先（最可靠通道）；NULL 钩子 → 退回 push（尽力而为）
 *  @param seg 段数据
 *  @param len 段字节数
 *  @note 与正常路径的差异 = 无过滤判定（无 accept_mask——提满全出）+ 提交通道选择
 *        （panic 优先/push 兜底）；调用者须在禁中断/故障上下文（om_log_panic 内已禁
 *        中断）——无表锁（故障上下文） */
void log_backend_panic_push_all(const char *seg, size_t len)
{
    size_t i;
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++)
    {
        if (g_backends[i].used)
        {
            if (g_backends[i].backend->panic != NULL)
            {
                g_backends[i].backend->panic(g_backends[i].backend, seg, len);
            }
            else
            {
                g_backends[i].backend->push(g_backends[i].backend, seg, len);
            }
        }
    }
}

OmRet om_log_backend_register(OmLogBackend *backend, OmLogLevel level)
{
    size_t i;
    port_critical_key_t key;
    if (backend == NULL || backend->name == NULL || backend->push == NULL)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (level >= OM_LOG_LEVEL_MAX)
    {
        return OM_ERR_INVALID_ARG;
    }
    key = om_hw_disable_interrupt();
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++)
    {
        if (g_backends[i].used && g_backends[i].backend == backend)
        {
            om_hw_restore_interrupt(key);
            return OM_ERR_ALREADY;
        }
    }
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++)
    {
        if (!g_backends[i].used)
        {
            g_backends[i].backend = backend;
            g_backends[i].level = level;
            memset(g_backends[i].modLevel, LOG_BACKEND_MOD_UNSET,
                   sizeof(g_backends[i].modLevel)); /* 覆盖表初始：全未覆盖 → 默认级 */
            g_backends[i].used = 1;
            om_hw_restore_interrupt(key);
            return OM_OK;
        }
    }
    om_hw_restore_interrupt(key);
    return OM_ERR_FULL;
}

OmRet om_log_backend_unregister(OmLogBackend *backend)
{
    size_t i;
    port_critical_key_t key;
    if (backend == NULL)
    {
        return OM_ERR_INVALID_ARG;
    }
    key = om_hw_disable_interrupt();
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++)
    {
        if (g_backends[i].used && g_backends[i].backend == backend)
        {
            memset(g_backends[i].modLevel, LOG_BACKEND_MOD_UNSET,
                   sizeof(g_backends[i].modLevel)); /* 清覆盖——防陈旧（槽复用前状态归一） */
            g_backends[i].used = 0;
            om_hw_restore_interrupt(key);
            return OM_OK;
        }
    }
    om_hw_restore_interrupt(key);
    return OM_ERR_NOT_FOUND;
}

OmRet om_log_backend_set_level(const char *backend_name, OmLogLevel level)
{
    size_t i;
    port_critical_key_t key;
    if (backend_name == NULL || level >= OM_LOG_LEVEL_MAX)
    {
        return OM_ERR_INVALID_ARG;
    }
    key = om_hw_disable_interrupt();
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++)
    {
        if (g_backends[i].used && strcmp(g_backends[i].backend->name, backend_name) == 0)
        {
            g_backends[i].level = level;
            om_hw_restore_interrupt(key);
            return OM_OK;
        }
    }
    om_hw_restore_interrupt(key);
    return OM_ERR_NOT_FOUND;
}

/** @brief 设置后端对某模块的覆盖级别（契约见 services/log/log.h）
 *  @note 模块 id 在临界区外解析（模块表无锁读写——module_find 既有语义）；
 *        后端表查找/覆盖写入在临界区内 */
OmRet om_log_backend_set_module_level(const char *backend_name, const char *module_name,
                                      OmLogLevel level)
{
    int id;
    size_t i;
    port_critical_key_t key;
    if (backend_name == NULL || module_name == NULL || level >= OM_LOG_LEVEL_MAX)
    {
        return OM_ERR_INVALID_ARG;
    }
    id = log_module_resolve_id(module_name);
    if (id < 0)
    {
        return OM_ERR_NOT_FOUND; /* 未登记（模块首次打日志后登记）——同 om_log_module_set_level 惰性语义 */
    }
    key = om_hw_disable_interrupt();
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++)
    {
        if (g_backends[i].used && strcmp(g_backends[i].backend->name, backend_name) == 0)
        {
            g_backends[i].modLevel[(size_t)id] = (uint8_t)level;
            om_hw_restore_interrupt(key);
            return OM_OK;
        }
    }
    om_hw_restore_interrupt(key);
    return OM_ERR_NOT_FOUND;
}

/** @brief 清除后端对某模块的覆盖（生效级回退默认级——契约见 services/log/log.h） */
OmRet om_log_backend_clear_module_level(const char *backend_name, const char *module_name)
{
    int id;
    size_t i;
    port_critical_key_t key;
    if (backend_name == NULL || module_name == NULL)
    {
        return OM_ERR_INVALID_ARG;
    }
    id = log_module_resolve_id(module_name);
    if (id < 0)
    {
        return OM_ERR_NOT_FOUND; /* 未登记——同 set_module_level 惰性语义 */
    }
    key = om_hw_disable_interrupt();
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++)
    {
        if (g_backends[i].used && strcmp(g_backends[i].backend->name, backend_name) == 0)
        {
            g_backends[i].modLevel[(size_t)id] = LOG_BACKEND_MOD_UNSET;
            om_hw_restore_interrupt(key);
            return OM_OK;
        }
    }
    om_hw_restore_interrupt(key);
    return OM_ERR_NOT_FOUND;
}

/** @brief 查询后端对某模块的生效级（覆盖命中 → 覆盖值；未覆盖 → 默认级） */
OmRet om_log_backend_get_module_level(const char *backend_name, const char *module_name,
                                      OmLogLevel *level)
{
    int id;
    size_t i;
    port_critical_key_t key;
    if (backend_name == NULL || module_name == NULL || level == NULL)
    {
        return OM_ERR_INVALID_ARG;
    }
    id = log_module_resolve_id(module_name);
    if (id < 0)
    {
        return OM_ERR_NOT_FOUND; /* 未登记——同 set_module_level 惰性语义 */
    }
    key = om_hw_disable_interrupt();
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++)
    {
        if (g_backends[i].used && strcmp(g_backends[i].backend->name, backend_name) == 0)
        {
            *level = (g_backends[i].modLevel[(size_t)id] != LOG_BACKEND_MOD_UNSET)
                         ? (OmLogLevel)g_backends[i].modLevel[(size_t)id]
                         : g_backends[i].level; /* 生效值：覆盖命中 ? 覆盖值 : 默认级 */
            om_hw_restore_interrupt(key);
            return OM_OK;
        }
    }
    om_hw_restore_interrupt(key);
    return OM_ERR_NOT_FOUND;
}

#endif /* OM_USE_LOG */
