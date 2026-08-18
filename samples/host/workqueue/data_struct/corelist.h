/**
 * @file    corelist.h
 * @brief   MSVC 兼容的 corelist.h 本地覆盖
 *
 * 引入原始 corelist.h 后覆盖 container_of 宏，移除 GCC 语句表达式。
 * 需 MSVC 2022 17.9+（C11 typeof 支持）。
 */
#ifndef __HOST_CORE_LIST_OVERRIDE__
#define __HOST_CORE_LIST_OVERRIDE__

#include "../../../../lib/data_struct/include/data_struct/corelist.h"

#ifdef _MSC_VER
/* MSVC: 移除 GCC 语句表达式，使用简化版 container_of */
#undef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr)-offsetof(type, member)))

#undef CONTAINER_OF
#define CONTAINER_OF(ptr, type, member) container_of(ptr, type, member)
#endif

#endif /* __HOST_CORE_LIST_OVERRIDE__ */
