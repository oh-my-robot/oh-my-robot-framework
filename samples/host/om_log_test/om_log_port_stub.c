/**
 * @file om_log_port_stub.c
 * @brief host 端口桩：临界区 no-op（host 单线程，无需真实关中断语义）
 * @note 仅为 host 测试满足链接；目标侧由 platform 层提供真实实现
 */

#include "core/port/om_port_hw.h"

/* 契约见 core/port/om_port_hw.h（host no-op；目标侧 = PRIMASK 保存/恢复） */
port_critical_key_t port_critical_enter(void)
{
    return 0;
}

void port_critical_exit(port_critical_key_t key)
{
    (void)key;
}
