/**
 * @file   host_mem.c
 * @brief  OSAL 堆接口的 host 实现（spi_nor_test 用）
 *
 * 唯一的消费者是 GPIO 控制器注册时分配中断回调表——本测试不用引脚中断，
 * 故只需满足分配/释放本身，不做池化或统计。
 */

#include <stdlib.h>

#include "osal/osal_core.h"

void *osal_malloc(size_t size)
{
    return malloc(size);
}

void osal_free(void *ptr)
{
    free(ptr);
}
