/**
 * @file  om_hardfault.c
 * @brief HardFault 统一收口（ADR-0014）——Cortex-M 架构共享 strong 实现
 * @details
 * - 启动文件声明 weak HardFault_Handler；本文件提供 strong 实现，链接期覆盖，
 *   板级 bsp_cpu.c 不再自写（删除 while(1) 死循环实现）；
 * - 收口动作：naked 汇编捕获异常返回地址 PC（EXC_RETURN 判定 MSP/PSP，
 *   异常栈帧 PC 位于偏移 24 字节处）→ om_hardfault_entry 构造 OmFatalContext
 *   → om_fatal_error(OM_FATAL_HW_FAULT)——与断言/栈溢出同一收敛入口；
 * - naked 必须：在序言压栈前读取 SP（否则偏移错误）；ARMv7-M/M0+ 指令集兼容
 *   （ite/mrs msp,psp/ldr 偏移），gnu-rm 与 armclang 双工具链验证；
 * - 边界：fault 时栈可能已损坏（如栈溢出导致的 hardfault），PC 捕获尽力而为，
 *   ctx.pc 缺失时 handler 仍可依据 reason 处理。
 */
#include "core/om_fatal.h"

/** @brief C 帮手：构造 ctx 并进入 fatal 设施（naked 汇编跳转，永不返回）
 *  OM_USED：汇编跳转对编译器/LTO 不可见，不加会被 LTO codegen 移除（armclang L6137E） */
OM_USED void om_hardfault_entry(uintptr_t pc, OmFatalReason reason, OmRet cause)
{
    const OmFatalContext ctx = {.pc = pc};
    om_fatal_error(reason, cause, &ctx);
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        ".syntax unified\n"
        "mov r0, lr\n"            /* r0 = EXC_RETURN */
        "tst r0, #4\n"            /* bit2：0=MSP(handler)，1=PSP(线程) */
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"         /* r0 = 异常栈帧顶 */
        "ldr r1, [r0, #24]\n"     /* r1 = 帧内 PC（xPSR,PC,LR,R12,R3-R0，PC 偏移 24） */
        "mov r2, #3\n"            /* OM_FATAL_HW_FAULT */
        "mov r3, #11\n"           /* OM_ERR_IO */
        "b om_hardfault_entry\n"  /* 不返回 */
    );
}
