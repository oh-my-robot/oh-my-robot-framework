#ifndef __OM_PORT__HW_H__
#define __OM_PORT__HW_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 平台无关的临界区 / 中断控制 API
 *
 * 每个平台（各 MCU 架构）需提供以下实现：
 *   port_critical_enter  — 保存当前中断屏蔽状态，关中断，返回恢复 key
 *   port_critical_exit   — 恢复到 key 对应的中断屏蔽状态
 *   port_int_disable     — 无条件关闭全局中断（错误处理等极端场景）
 *   port_int_enable      — 无条件开启全局中断
 *===========================================================================*/

/** @brief 临界区恢复密钥 — 平台定义，不透明 */
typedef uint32_t port_critical_key_t;

/** @brief 进入临界区：保存 + 关中断，返回恢复密钥 */
port_critical_key_t port_critical_enter(void);

/** @brief 退出临界区：恢复到 key 对应的中断状态 */
void port_critical_exit(port_critical_key_t key);

/** @brief 无条件关全局中断 */
void port_int_disable(void);

/** @brief 无条件开全局中断 */
void port_int_enable(void);

#ifdef __cplusplus
}
#endif

#endif /* __OM_PORT__HW_H__ */
