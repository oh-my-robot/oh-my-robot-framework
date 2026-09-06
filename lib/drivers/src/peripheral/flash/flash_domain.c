/**
 * @file   flash_domain.c
 * @brief  执行域生命周期（= Workqueue 包装：worker 线程 + 请求 FIFO）
 *
 * 域是执行者归属单元：域内设备的写/擦请求由其 worker 串行执行；
 * 不同域并行。每物理片默认独立域（NULL 注册时框架用设备内嵌 autoDomain），
 * 共享域（同总线多片省线程）由适配器显式创建并传入多个设备。
 */

#include <string.h>

#include "drivers/peripheral/flash/pal_flash_dev.h"

OmRet flash_domain_init(FlashDomain *dom, const char *name, uint32_t prio, uint32_t stack)
{
    if (!dom || !name || stack == 0)
    {
        return OM_ERR_INVALID_ARG;
    }

    /* Workqueue 实例要求清零后 init（state 检测） */
    memset(&dom->wq, 0, sizeof(dom->wq));

    WorkqueueConfig cfg = {
        .name = name,
        .stack_depth = stack,
        .priority = prio,
    };
    OmRet ret = workqueue_init(&dom->wq, &cfg);
    if (ret != OM_OK)
    {
        return ret;
    }
    return workqueue_start(&dom->wq);
}
