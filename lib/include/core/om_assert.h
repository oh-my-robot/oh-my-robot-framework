/**
 * @file  om_assert.h
 * @brief 框架断言（kernel-core，OS 无关）——触发源收口之一（ADR-0014）
 * @details 断言失败即不可恢复（状态不变量被违反，继续运行产生未定义行为）→ 统一经
 *          om_fatal_error(OM_FATAL_ASSERT) 进入致命错误设施，由 handler 决定恢复策略。
 *          与 om_fatal.h 同属 fatal 设施家族：断言是"触发源"，设施是"收敛入口"。
 *
 *          开关：om_config.h 的 OM_USE_ASSERT（默认启用；应用可在 om_appcfg.h 中
 *          #undef 关闭）。关闭后断言展开为空操作——契约检查的取舍由工程决定。
 *
 *          说明：cause 暂用 OM_ERR_CONFLICT（状态冲突）；断言位置（__FILE__:__LINE__）
 *          随 fatal context 扩展（ADR-0014 后续演进）提供。
 */
#ifndef __OM_ASSERT_H__
#define __OM_ASSERT_H__

#include "core/om_def.h"
#include "core/om_fatal.h"

#if defined(OM_USE_ASSERT)
/** @brief 断言：条件不成立 → om_fatal_error(OM_FATAL_ASSERT)（携带 __FILE__:__LINE__），永不返回 */
#define OM_ASSERT(cond)                                                                      \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            static const OmFatalContext s_assert_ctx = {.file = __FILE__, .line = __LINE__}; \
            om_fatal_error(OM_FATAL_ASSERT, OM_ERR_CONFLICT, &s_assert_ctx);                 \
        }                                                                                    \
    } while (0)
#else
#define OM_ASSERT(cond) ((void)0)
#endif

#endif /* __OM_ASSERT_H__ */
