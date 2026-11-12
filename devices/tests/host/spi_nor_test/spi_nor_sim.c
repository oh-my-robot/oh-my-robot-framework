/**
 * @file   spi_nor_sim.c
 * @brief  W25Q256JV SPI 从设备仿真实现
 */

#include "spi_nor_sim.h"

#include <stdlib.h>
#include <string.h>

#define SIM_CMD_NONE 0x00u
#define SIM_CMD_WRITE_ENABLE 0x06u
#define SIM_CMD_READ_STATUS1 0x05u
#define SIM_CMD_READ_JEDEC_ID 0x9Fu
#define SIM_CMD_FAST_READ_4B 0x0Cu
#define SIM_CMD_PAGE_PROGRAM_4B 0x12u
#define SIM_CMD_SECTOR_ERASE_4B 0x21u

#define SIM_SR1_WIP 0x01u
#define SIM_SR1_WEL 0x02u

#define SIM_JEDEC_ID_0 0xEFu /* 厂商 */
#define SIM_JEDEC_ID_1 0x40u /* 类型 */
#define SIM_JEDEC_ID_2 0x19u /* 容量：256Mbit */

/*===========================================================================
 * 器件内部行为
 *===========================================================================*/

static void sim_start_busy(SpiNorSim *s)
{
    s->sr1 |= SIM_SR1_WIP;
    s->busyReads = SPI_NOR_SIM_BUSY_READS;
}

/** 状态读取即"时间推进"：读满预算次数后器件转为就绪，写使能随之自清 */
static void sim_tick_busy(SpiNorSim *s)
{
    if ((s->sr1 & SIM_SR1_WIP) == 0u || s->busyReads == 0u)
    {
        return;
    }
    s->busyReads--;
    if (s->busyReads == 0u)
    {
        s->sr1 &= (uint8_t)~SIM_SR1_WIP;
        s->sr1 &= (uint8_t)~SIM_SR1_WEL;
    }
}

static void sim_program_byte(SpiNorSim *s, uint8_t value)
{
    if ((s->sr1 & SIM_SR1_WIP) != 0u || (s->sr1 & SIM_SR1_WEL) == 0u)
    {
        return; /* 忙中或未写使能：器件忽略 */
    }
    if (s->addr >= s->capacity)
    {
        return;
    }

    if (s->progLen == 0u)
    {
        s->progPageBase = s->addr - (s->addr % SPI_NOR_SIM_PAGE_SIZE);
    }
    else if (s->addr >= s->progPageBase + SPI_NOR_SIM_PAGE_SIZE)
    {
        s->pageCrossings++; /* 驱动必须自己拆页，跨页是驱动缺陷 */
    }

    /* 真器件在页内回卷：越出页尾的字节落回页首 */
    uint32_t offset = s->addr - s->progPageBase;
    uint32_t slot = s->progPageBase + offset % SPI_NOR_SIM_PAGE_SIZE;
    if (slot < s->capacity)
    {
        s->mem[slot] &= value; /* 编程只能把 1 写成 0 */
    }
    s->addr++;
    s->progLen++;
}

/** 片选释放 = 一条命令结束：此处才真正执行擦除/编程的收尾 */
static void sim_end_command(SpiNorSim *s)
{
    if (s->argLeft != 0u)
    {
        s->cmd = SIM_CMD_NONE;
        return; /* 地址没收全：命令作废 */
    }

    if (s->cmd == SIM_CMD_PAGE_PROGRAM_4B && s->progLen > 0u)
    {
        s->progOps++;
        sim_start_busy(s);
    }
    else if (s->cmd == SIM_CMD_SECTOR_ERASE_4B)
    {
        if ((s->sr1 & SIM_SR1_WIP) == 0u && (s->sr1 & SIM_SR1_WEL) != 0u &&
            s->addr < s->capacity)
        {
            uint32_t base = s->addr - (s->addr % SPI_NOR_SIM_SECTOR_SIZE);
            memset(&s->mem[base], 0xFF, SPI_NOR_SIM_SECTOR_SIZE);
            s->eraseOps++;
            sim_start_busy(s);
        }
    }

    s->cmd = SIM_CMD_NONE;
    s->argLeft = 0u;
    s->dummyLeft = 0u;
    s->progLen = 0u;
}

/** 推进一个字节：入参为 MOSI，返回 MISO */
static uint8_t sim_byte(SpiNorSim *s, uint8_t tx)
{
    if (s->cmd == SIM_CMD_NONE)
    {
        s->cmd = tx;
        s->argLeft = 0u;
        s->addr = 0u;
        s->dummyLeft = 0u;
        s->idPos = 0u;
        s->progLen = 0u;

        switch (tx)
        {
        case SIM_CMD_WRITE_ENABLE:
            if ((s->sr1 & SIM_SR1_WIP) == 0u)
            {
                s->sr1 |= SIM_SR1_WEL;
            }
            break;
        case SIM_CMD_FAST_READ_4B:
            s->argLeft = 4u;
            s->dummyLeft = 1u;
            break;
        case SIM_CMD_PAGE_PROGRAM_4B:
        case SIM_CMD_SECTOR_ERASE_4B:
            s->argLeft = 4u;
            break;
        default:
            break;
        }
        return 0xFFu;
    }

    if (s->argLeft > 0u)
    {
        s->addr = (s->addr << 8) | (uint32_t)tx;
        s->argLeft--;
        return 0xFFu;
    }

    switch (s->cmd)
    {
    case SIM_CMD_READ_STATUS1: {
        uint8_t out = s->sr1;
        s->statReads++;
        if ((out & SIM_SR1_WIP) != 0u)
        {
            s->wipSeen++;
        }
        sim_tick_busy(s);
        return out;
    }
    case SIM_CMD_READ_JEDEC_ID:
        if (s->idPos == 0u)
        {
            s->idPos++;
            return SIM_JEDEC_ID_0;
        }
        if (s->idPos == 1u)
        {
            s->idPos++;
            return SIM_JEDEC_ID_1;
        }
        return SIM_JEDEC_ID_2;

    case SIM_CMD_FAST_READ_4B:
        if (s->dummyLeft > 0u)
        {
            s->dummyLeft--;
            return 0xFFu;
        }
        {
            uint8_t out = 0xFFu;
            if (s->addr < s->capacity)
            {
                out = s->mem[s->addr];
            }
            s->addr++;
            return out;
        }

    case SIM_CMD_PAGE_PROGRAM_4B:
        sim_program_byte(s, tx);
        return 0xFFu;

    default:
        return 0xFFu;
    }
}

/*===========================================================================
 * SpiControllerOps
 *===========================================================================*/

static OmRet sim_configure(SpiBus *bus, const SpiDeviceCfg *cfg)
{
    if (!bus || !cfg)
    {
        return OM_ERR_INVALID_ARG;
    }
    /* 框架的动态超时按实际频率计算，此值必须非零 */
    bus->actualHz = cfg->maxHz;
    return OM_OK;
}

static OmRet sim_transfer_one(SpiBus *bus, const uint8_t *tx, uint8_t *rx, size_t len)
{
    SpiNorSim *s = bus ? bus->hwPrivate : NULL;
    if (!s)
    {
        return OM_ERR_INVALID_ARG;
    }

    for (size_t i = 0u; i < len; i++)
    {
        uint8_t in = tx ? tx[i] : 0xFFu;
        uint8_t out = sim_byte(s, in);
        if (rx)
        {
            rx[i] = out;
        }
    }

    /* 同步完成：框架在调用本函数前置位 busy，提前到达的完成不会被丢弃 */
    hal_spi_isr(bus, OM_OK, len);
    return OM_OK;
}

static OmRet sim_control(SpiBus *bus, uint32_t cmd, void *arg)
{
    (void)bus;
    (void)cmd;
    (void)arg;
    return OM_OK;
}

static void sim_set_cs(SpiBus *bus, uint8_t cs_id, bool assert)
{
    (void)cs_id;
    spi_nor_sim_cs_edge(bus ? bus->hwPrivate : NULL, assert);
}

/*===========================================================================
 * 生命周期
 *===========================================================================*/

void spi_nor_sim_init(SpiNorSim *sim)
{
    memset(sim, 0, sizeof(*sim));
    sim->capacity = SPI_NOR_SIM_CAPACITY;
    sim->mem = (uint8_t *)malloc(SPI_NOR_SIM_CAPACITY);
    if (sim->mem)
    {
        memset(sim->mem, 0xFF, SPI_NOR_SIM_CAPACITY);
    }

    sim->ops.configure = sim_configure;
    sim->ops.transferOne = sim_transfer_one;
    sim->ops.control = sim_control;
    sim->ops.setCs = sim_set_cs;
}

void spi_nor_sim_deinit(SpiNorSim *sim)
{
    free(sim->mem);
    sim->mem = NULL;
}

void spi_nor_sim_fill(SpiNorSim *sim, uint8_t value)
{
    if (sim->mem)
    {
        memset(sim->mem, value, sim->capacity);
    }
}

void spi_nor_sim_cs_edge(SpiNorSim *sim, bool assert)
{
    if (!sim)
    {
        return;
    }
    if (assert)
    {
        sim->csAsserts++;
    }
    else
    {
        sim_end_command(sim);
    }
}
