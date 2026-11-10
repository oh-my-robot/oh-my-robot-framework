/**
 * @file module.c
 * @brief log 模块登记与模块级管理：log_module_check_in 惰性登记（首次日志调用入库——幂等）
 *        + om_log_module_set_level/get_level（按名运行时调节/查询模块级别）
 * @details 表 = 定长数组（实例指针条目，键 = 实例指针——TU 静态唯一）；惰性：OM_LOG_MODULE
 *          生成实例 moduleId 初值 -1（未登记）→ 首次日志调用入表；抑制：表满写实例
 *          moduleId = -2（幂等短路，不重复扫描）。实例可写前提：OM_LOG_MODULE 生成非
 *          const 实例（本文件经指针写回 moduleId——契约见 log.h 宏注释）。
 */

#include "core/om_config.h"

#if OM_USE_LOG

#include "core/om_def.h"
#include "services/log/log.h" /* OmLogModule（登记条目类型） */

#include "log_internal.h"

#include <string.h>

/* 模块登记表上限（配置键落地前本文件默认值；om_appcfg.h 可覆写——同款 #ifndef 模式） */
#ifndef OM_LOG_MAX_MODULES
#define OM_LOG_MAX_MODULES 16
#endif

/* 返回码哨兵（契约见 log_internal.h：>=0 = moduleId；-2 = 表满；-3 = 参数非法） */
#define LOG_MODULE_ERR_FULL (-2)
#define LOG_MODULE_ERR_INVALID (-3)

/** @brief 模块登记表：条目 = 实例指针（幂等键；模块管理按 moduleId 索引得实例）
 *  @note 模块实例须由 OM_LOG_MODULE 生成（非 const static——登记写回 moduleId） */
static const OmLogModule *g_module_table[OM_LOG_MAX_MODULES];

int log_module_check_in(const OmLogModule *module)
{
    int i;
    if (module == NULL || module->name == NULL)
    {
        return LOG_MODULE_ERR_INVALID; /* 参数非法（无副作用） */
    }
    if (module->moduleId >= 0)
    {
        return module->moduleId; /* 已登记——幂等 */
    }
    if (module->moduleId == LOG_MODULE_ERR_FULL)
    {
        return LOG_MODULE_ERR_FULL; /* 表满已抑制——幂等（不重复查表） */
    }
    for (i = 0; i < OM_LOG_MAX_MODULES; i++)
    {
        if (g_module_table[i] == module)
        {
            ((OmLogModule *)module)->moduleId = i; /* 指针已在表（异常路径）——补登回写 */
            return i;
        }
    }
    for (i = 0; i < OM_LOG_MAX_MODULES; i++)
    {
        if (g_module_table[i] == NULL)
        {
            g_module_table[i] = module;
            ((OmLogModule *)module)->moduleId = i;
            return i;
        }
    }
    ((OmLogModule *)module)->moduleId = LOG_MODULE_ERR_FULL; /* 表满置 -2——抑制重复尝试 */
    return LOG_MODULE_ERR_FULL;
}

/** @brief 按名查找模块（线性——set/get 低频；表内 NULL 槽=未登记，跳过）
 *  @param name 模块名
 *  @return 匹配条目指针；未找到或参数非法 → NULL */
static const OmLogModule *module_find(const char *name)
{
    uint32_t i;
    if (name == NULL)
    {
        return NULL;
    }
    for (i = 0; i < OM_LOG_MAX_MODULES; i++)
    {
        if (g_module_table[i] != NULL && strcmp(g_module_table[i]->name, name) == 0)
        {
            return g_module_table[i];
        }
    }
    return NULL;
}

/** @brief 按名解析模块 id（只读——后端按模块覆盖 API 用：覆盖表以 id 索引）
 *  @param name 模块名
 *  @return >=0 = moduleId；-1 = 未登记/不存在（首次打日志后登记——惰性语义） */
int log_module_resolve_id(const char *name)
{
    const OmLogModule *m = module_find(name);
    return (m != NULL) ? m->moduleId : -1;
}

/** @brief 运行时调节模块级别（契约见 services/log/log.h）
 *  @param module_name 模块名
 *  @param level 目标级别（>= OM_LOG_LEVEL_MAX = INVALID_ARG）
 *  @return OM_OK；OM_ERR_NOT_FOUND（未登记/不存在——首次日志后登记）；OM_ERR_INVALID_ARG */
OmRet om_log_module_set_level(const char *module_name, OmLogLevel level)
{
    const OmLogModule *m;
    if (module_name == NULL || level >= OM_LOG_LEVEL_MAX)
    {
        return OM_ERR_INVALID_ARG;
    }
    m = module_find(module_name);
    if (m == NULL)
    {
        return OM_ERR_NOT_FOUND;
    }
    ((OmLogModule *)m)->level = level; /* 实例可写（OM_LOG_MODULE 宏 static 非 const——同 check_in 回写） */
    return OM_OK;
}

/** @brief 查询模块级别 */
OmRet om_log_module_get_level(const char *module_name, OmLogLevel *level)
{
    const OmLogModule *m;
    if (module_name == NULL || level == NULL)
    {
        return OM_ERR_INVALID_ARG;
    }
    m = module_find(module_name);
    if (m == NULL)
    {
        return OM_ERR_NOT_FOUND;
    }
    *level = m->level;
    return OM_OK;
}

#endif /* OM_USE_LOG */
