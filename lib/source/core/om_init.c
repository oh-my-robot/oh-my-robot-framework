#include "core/om_init.h"

/*
 * 每级一个链接器段 .om_init_<N>（N = 级别数值，见 om_init.h 的 OM_INIT_LEVEL_*），
 * 链接脚本按级别顺序排列各段 → 表项在内存里按级别聚集，**级别顺序由链接器保证**
 * （Linux/RT-Thread 同款做法，见 om_init.h 顶部"排序策略"说明）。
 *
 * 每级段的边界符号 __om_init_<N>_start / __om_init_<N>_end 由链接器定义：
 *
 *   GCC ld :  每级一个 SECTIONS 条目：
 *             PROVIDE(__om_init_<N>_start = .); KEEP(*(.om_init_<N>));
 *             PROVIDE(__om_init_<N>_end = .)
 *
 *   armlink : scatter 每级一个执行域 ER_OM_INIT_<N>（含 *(.om_init_<N>)），
 *             armlink 自动生成 Image$$ER_OM_INIT_<N>$$Base / Limit；
 *             下方 #ifdef __ARMCC_VERSION 的宏把它们映射为统一名称
 *             __om_init_<N>_start / __om_init_<N>_end（armclang 允许 $ 出现在
 *             标识符中，直接 extern 引用，避免 .set 别名被最终符号表省略）。
 *
 * 框架代码统一使用 __om_init_<N>_start / __om_init_<N>_end，不区分链接器类型。
 */

#if defined(__ARMCC_VERSION)
#define __om_init_0_start Image$$ER_OM_INIT_0$$Base
#define __om_init_0_end   Image$$ER_OM_INIT_0$$Limit
#define __om_init_1_start Image$$ER_OM_INIT_1$$Base
#define __om_init_1_end   Image$$ER_OM_INIT_1$$Limit
#define __om_init_2_start Image$$ER_OM_INIT_2$$Base
#define __om_init_2_end   Image$$ER_OM_INIT_2$$Limit
#define __om_init_3_start Image$$ER_OM_INIT_3$$Base
#define __om_init_3_end   Image$$ER_OM_INIT_3$$Limit
#define __om_init_4_start Image$$ER_OM_INIT_4$$Base
#define __om_init_4_end   Image$$ER_OM_INIT_4$$Limit
#define __om_init_5_start Image$$ER_OM_INIT_5$$Base
#define __om_init_5_end   Image$$ER_OM_INIT_5$$Limit
#define __om_init_6_start Image$$ER_OM_INIT_6$$Base
#define __om_init_6_end   Image$$ER_OM_INIT_6$$Limit
#endif

extern const OmInitEntry __om_init_0_start[];
extern const OmInitEntry __om_init_0_end[];
extern const OmInitEntry __om_init_1_start[];
extern const OmInitEntry __om_init_1_end[];
extern const OmInitEntry __om_init_2_start[];
extern const OmInitEntry __om_init_2_end[];
extern const OmInitEntry __om_init_3_start[];
extern const OmInitEntry __om_init_3_end[];
extern const OmInitEntry __om_init_4_start[];
extern const OmInitEntry __om_init_4_end[];
extern const OmInitEntry __om_init_5_start[];
extern const OmInitEntry __om_init_5_end[];
extern const OmInitEntry __om_init_6_start[];
extern const OmInitEntry __om_init_6_end[];

/** @brief 每级段的边界区间（索引 = 级别数值） */
typedef struct
{
    const OmInitEntry *start;
    const OmInitEntry *end;
} OmInitRange;

static const OmInitRange s_level_ranges[OM_INIT_LEVEL_COUNT] = {
    {__om_init_0_start, __om_init_0_end},
    {__om_init_1_start, __om_init_1_end},
    {__om_init_2_start, __om_init_2_end},
    {__om_init_3_start, __om_init_3_end},
    {__om_init_4_start, __om_init_4_end},
    {__om_init_5_start, __om_init_5_end},
    {__om_init_6_start, __om_init_6_end},
};

/**
 * @brief 排序缓冲上限（栈上指针数组）。
 * @details 本级表项数 <= 该上限时按 prio 排序后执行；超出时退化为按链接顺序执行
 *          （全部仍会执行，但不保证顺序）。级"只增不删"，正常工程下同级表项数
 *          远小于该值；如需更大请外部定义覆盖。
 */
#ifndef OM_INIT_MAX_ENTRIES
#define OM_INIT_MAX_ENTRIES 128
#endif

/* 首个失败记录（无日志子系统时供诊断查询；后续对接 log/诊断服务） */
static const char *s_first_fail_name;
static OmRet s_first_fail_ret;

/** @brief 同级内排序键：prio 升序（级别顺序已由链接器保证） */
static inline int om_init_prio_key(const OmInitEntry *e)
{
    return (int)e->prio;
}

/** @brief 调用单个回调并登记首个失败；返回是否失败 */
static OmRet om_init_call_one(const OmInitEntry *e)
{
    OmRet ret = e->fn();
    if (ret == OM_OK) {
        return OM_OK;
    }
    if (s_first_fail_name == NULL) {
        s_first_fail_name = e->name;
        s_first_fail_ret  = ret;
    }
    return ret;
}

/** @brief 最近一次 om_do_initcalls 的首个失败回调名（无失败则 NULL）——供 fatal context 诊断 */
const char *om_init_last_fail_name(void)
{
    return s_first_fail_name;
}

OmRet om_do_initcalls(OmInitLevel level_lo, OmInitLevel level_hi)
{
    s_first_fail_name = NULL;
    s_first_fail_ret  = OM_OK;

    /* 逐级遍历 [level_lo, level_hi)（级别顺序由链接器按段排列保证） */
    for (uint8_t lvl = (uint8_t)level_lo; lvl < (uint8_t)level_hi; lvl++) {
        const OmInitEntry *sec_start = s_level_ranges[lvl].start;
        const OmInitEntry *sec_end   = s_level_ranges[lvl].end;

        /* 统计本级非空表项，决定是否走排序路径 */
        size_t total = 0;
        for (const OmInitEntry *p = sec_start; p < sec_end; p++) {
            if (p->fn != NULL) {
                total++;
            }
        }

        if (total <= OM_INIT_MAX_ENTRIES) {
            /* 收集到栈缓冲并按 prio 选择排序 */
            const OmInitEntry *order[OM_INIT_MAX_ENTRIES];
            size_t count = 0;
            for (const OmInitEntry *p = sec_start; p < sec_end; p++) {
                if (p->fn != NULL) {
                    order[count++] = p;
                }
            }

            for (size_t i = 0; i + 1 < count; i++) {
                size_t min = i;
                for (size_t j = i + 1; j < count; j++) {
                    if (om_init_prio_key(order[j]) < om_init_prio_key(order[min])) {
                        min = j;
                    }
                }
                if (min != i) {
                    const OmInitEntry *tmp = order[i];
                    order[i]               = order[min];
                    order[min]             = tmp;
                }
            }

            for (size_t i = 0; i < count; i++) {
#ifdef OM_INIT_ABORT_ON_FAIL
                if (om_init_call_one(order[i]) != OM_OK) {
                    return s_first_fail_ret;
                }
#else
                (void)om_init_call_one(order[i]);
#endif
            }
        } else {
            /* 超出缓冲：退化为链接顺序全执行（应调大 OM_INIT_MAX_ENTRIES） */
            for (const OmInitEntry *p = sec_start; p < sec_end; p++) {
                if (p->fn != NULL) {
#ifdef OM_INIT_ABORT_ON_FAIL
                    if (om_init_call_one(p) != OM_OK) {
                        return s_first_fail_ret;
                    }
#else
                    (void)om_init_call_one(p);
#endif
                }
            }
        }
    }

    return s_first_fail_ret;
}
