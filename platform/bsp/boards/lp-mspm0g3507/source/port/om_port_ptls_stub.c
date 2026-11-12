/**
 * @file om_port_ptls_stub.c
 * @brief TI POSIX TLS 兼容桩 — 框架未集成 TI POSIX 层，提供空实现
 */

void PTLS_taskDeleteHook(void *tcb)
{
    (void)tcb;
}
