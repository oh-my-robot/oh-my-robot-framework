/**
 * @file   flash_sim.c
 * @brief  NOR flash 物理行为模拟器实现
 */

#include <stdlib.h>
#include <string.h>

#include "flash_sim.h"

void flash_sim_init(FlashSim *sim, uint32_t capacity, uint8_t erased_value, uint32_t write_unit)
{
    memset(sim, 0, sizeof(*sim));
    sim->capacity = capacity;
    sim->erasedValue = erased_value;
    sim->writeUnit = write_unit;
    sim->strictProgram = true;
    sim->mem = (uint8_t *)malloc(capacity);
    if (!sim->mem)
    {
        return; /* OOM：后续 ops 对 NULL 防御性返回 IO 错误 */
    }
    memset(sim->mem, erased_value, capacity);
}

void flash_sim_deinit(FlashSim *sim)
{
    if (!sim)
    {
        return;
    }
    free(sim->mem);
    sim->mem = NULL;
}

void flash_sim_set_delay(FlashSim *sim, uint32_t delay_ms)
{
    if (sim)
    {
        sim->opDelayMs = delay_ms;
    }
}

void flash_sim_set_fail(FlashSim *sim, bool fail)
{
    if (sim)
    {
        sim->failWriteErase = fail;
    }
}

/* 模拟真实器件耗时（让 host 上的队列/并发时序可观测） */
static void flash_sim_poke_delay(FlashSim *sim)
{
    if (sim->opDelayMs > 0)
    {
        osal_sleep_ms(sim->opDelayMs);
    }
}

static FlashSim *flash_sim_from_dev(FlashDev *dev)
{
    if (!dev)
    {
        return NULL;
    }
    return (FlashSim *)dev->hw;
}

OmRet flash_sim_read(FlashDev *dev, uint32_t addr, void *buf, size_t len)
{
    FlashSim *sim = flash_sim_from_dev(dev);
    if (!sim || !sim->mem)
    {
        return OM_ERR_FLASH_IO;
    }
    sim->opCount++;
    flash_sim_poke_delay(sim);
    memcpy(buf, sim->mem + addr, len);
    return OM_OK;
}

OmRet flash_sim_write(FlashDev *dev, uint32_t addr, const void *data, size_t len)
{
    FlashSim *sim = flash_sim_from_dev(dev);
    if (!sim || !sim->mem)
    {
        return OM_ERR_FLASH_IO;
    }
    sim->opCount++;
    flash_sim_poke_delay(sim);
    if (sim->failWriteErase)
    {
        return OM_ERR_FLASH_IO; /* 注入拒绝：未落位 */
    }

    const uint8_t *src = (const uint8_t *)data;
    uint8_t *dst = sim->mem + addr;

    if (sim->strictProgram)
    {
        /* program 物理语义：结果 = dst AND src（只允许 1→0 与同值）；
         * 违规 = dst 中已为 0 的位被 src 置 1（写未擦区） */
        for (size_t i = 0; i < len; i++)
        {
            if (((uint8_t)~dst[i] & src[i]) != 0)
            {
                return OM_ERR_FLASH_IO;
            }
        }
    }
    /* 落位：program 后目标 = 原值与新值逐位 AND */
    for (size_t i = 0; i < len; i++)
    {
        dst[i] &= src[i];
    }
    return OM_OK;
}

OmRet flash_sim_erase(FlashDev *dev, uint32_t addr, size_t len)
{
    FlashSim *sim = flash_sim_from_dev(dev);
    if (!sim || !sim->mem)
    {
        return OM_ERR_FLASH_IO;
    }
    sim->opCount++;
    flash_sim_poke_delay(sim);
    if (sim->failWriteErase)
    {
        return OM_ERR_FLASH_IO; /* 注入拒绝：未落位 */
    }
    memset(sim->mem + addr, sim->erasedValue, len);
    return OM_OK;
}
