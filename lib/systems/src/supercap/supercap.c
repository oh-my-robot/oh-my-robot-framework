/**
 * @file  supercap.c
 * @brief 超级电容业务模块（骨架 + 自注册初始化示例）
 * @details 本模块尚未实现具体逻辑，仅以 OM_INIT_SYSTEM 自注册一个骨架初始化回调，
 *          用于演示/验证分散加载自动注册初始化系统（见 core/om_init.h）。
 */

#include "systems/supercap/supercap.h"
#include "core/om_def.h"
#include "core/om_init.h"
#include "osal/osal.h"

struct Supercap
{
    int placeholder; /**< 占位，模块尚未实现 */
};

static struct Supercap g_supercap;

Supercap *supercap_init(void *cfg)
{
    (void)cfg;
    return &g_supercap;
}

void supercap_update(Supercap *cap)
{
    (void)cap;
}

void supercap_shutdown(Supercap *cap)
{
    (void)cap;
}

/**
 * @brief 业务系统自注册初始化（SYSTEM 级，默认 prio）
 * @details 由 init 线程在调度器启动后调用 om_do_initcalls(SERVICE, COUNT) 触发
 *          （Phase 3：post-scheduler，可阻塞/IPC/建线程）。
 *
 *          下列 volatile 全局用于硬件调试观察（验证 init 线程确实跑了 SYSTEM 级、
 *          且能在初始化期阻塞——后者在调度器前会死锁，故能区分上下文）。
 */
volatile int g_supercap_init_entered; /* 进入回调置 1 */
volatile int g_supercap_init_blocked; /* 阻塞返回后置 1（证明 post-scheduler） */

static OmRet supercap_self_init(void)
{
    g_supercap_init_entered = 1;
    /* 阻塞 10ms：证明本回调运行在调度器已启的 init 线程里（Phase 3 能力）；
     * 若误在调度器前运行，阻塞原语会死锁/异常。 */
    (void)osal_sleep_ms(10);
    g_supercap_init_blocked = 1;
    /* TODO: 注册到设备模型 / 申请资源 / 建后台任务 */
    return OM_OK;
}

OM_INIT_SYSTEM(supercap_self_init);
