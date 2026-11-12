/**
 * @file   osal_none_cfg.h
 * @brief  osal-none 端口配置（默认值 + #ifndef 守卫，工程可覆写）
 *
 * - OM_OSAL_NONE_HEAP_SIZE：osal_malloc 静态池字节数（heap_1 形态：
 *   只分配不释放）。默认 4096；0 = 禁用堆（osal_malloc 恒 NULL——
 *   纯静态对象形态，面积极端预算用）。bootloader 工程按模块清单预算
 *   覆写；host 测试调大。
 * - 块对齐 = 8 字节（OSAL_NONE_ALIGN，见 osal_none_internal.h）；
 *   本形态不分配任务栈，不承担 ABI 栈对齐。
 */

#ifndef OM_OSAL_NONE_CFG_H
#define OM_OSAL_NONE_CFG_H

#ifndef OM_OSAL_NONE_HEAP_SIZE
#define OM_OSAL_NONE_HEAP_SIZE 4096u
#endif

#endif /* OM_OSAL_NONE_CFG_H */
