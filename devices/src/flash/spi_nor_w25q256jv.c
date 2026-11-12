/**
 * @file   spi_nor_w25q256jv.c
 * @brief  SPI NOR 芯片驱动：W25Q256JV —— FlashOps 后端实现
 */

#include "devices/flash/spi_nor_w25q256jv.h"

#include "osal/osal_time.h"

/*===========================================================================
 * 命令码（器件指令集）
 *===========================================================================*/

#define W25Q_CMD_WRITE_ENABLE 0x06u    /* 置写使能锁存 */
#define W25Q_CMD_READ_STATUS1 0x05u    /* 读状态寄存器-1 */
#define W25Q_CMD_READ_JEDEC_ID 0x9Fu   /* 读厂商/类型/容量编码 */
#define W25Q_CMD_FAST_READ_4B 0x0Cu    /* 快速读，4 字节地址 */
#define W25Q_CMD_PAGE_PROGRAM_4B 0x12u /* 页编程，4 字节地址 */
#define W25Q_CMD_SECTOR_ERASE_4B 0x21u /* 4KB 扇区擦除，4 字节地址 */

/* 状态寄存器-1 位 */
#define W25Q_SR1_WIP 0x01u /* 写/擦进行中 */
#define W25Q_SR1_WEL 0x02u /* 写使能已锁存 */

/*===========================================================================
 * 几何（芯片常量）
 *===========================================================================*/

#define W25Q256JV_CAPACITY (32u * 1024u * 1024u)
#define W25Q256JV_PAGE_SIZE 256u
#define W25Q256JV_SECTOR_SIZE 4096u

/* 就绪/写使能的等待上限（毫秒）：超过即判失败，不无限等。
 * 上限远大于手册给出的最坏值，只用于把"器件不再应答"与"器件慢"分开。 */
#ifndef OM_W25Q_PROGRAM_TIMEOUT_MS
#define OM_W25Q_PROGRAM_TIMEOUT_MS 100u
#endif
#ifndef OM_W25Q_ERASE_TIMEOUT_MS
#define OM_W25Q_ERASE_TIMEOUT_MS 2000u
#endif
#ifndef OM_W25Q_WEL_TIMEOUT_MS
#define OM_W25Q_WEL_TIMEOUT_MS 10u
#endif

static const FlashGeometry gW25q256jvGeom = {
    .capacity = W25Q256JV_CAPACITY,
    .erasedValue = 0xFFu,
    .writeUnit = 1u, /* 页内任意字节起写，最小单位 1 字节 */
    .pageSize = W25Q256JV_PAGE_SIZE,
    .sectorSize = W25Q256JV_SECTOR_SIZE,
    .sectorCount = W25Q256JV_CAPACITY / W25Q256JV_SECTOR_SIZE,
    .sectorRegions = NULL,
    .caps = 0u,
};

/*===========================================================================
 * 内部：状态与等待
 *===========================================================================*/

static OmRet w25q_read_status1(W25q256jvDev *nor, uint8_t *sr)
{
    uint8_t cmd = W25Q_CMD_READ_STATUS1;
    return spi_write_then_read(nor->spi, &cmd, 1u, sr, 1u);
}

/** 轮询等待写/擦完成。
 *  每次轮询之间让出 CPU——器件层的让出契约由本循环承担：一片在擦除时，
 *  其它任务照常运行，不被本循环占住。迭代数即毫秒预算（每次一轮询加一次
 *  让出），故超时以毫秒计。 */
static OmRet w25q_wait_ready(W25q256jvDev *nor, uint32_t timeout_ms)
{
    for (uint32_t elapsed = 0u; elapsed < timeout_ms; elapsed++)
    {
        uint8_t sr = 0u;
        OmRet ret = w25q_read_status1(nor, &sr);
        if (ret != OM_OK)
        {
            return ret;
        }
        if ((sr & W25Q_SR1_WIP) == 0u)
        {
            return OM_OK;
        }
        osal_sleep_ms(1u);
    }
    return OM_ERR_FLASH_TIMEOUT;
}

/** 置写使能并确认已锁存。
 *  未锁存时器件会静默忽略随后的编程/擦除——在此处报出，胜过让上层
 *  收到一个"成功"却什么都没发生。 */
static OmRet w25q_write_enable(W25q256jvDev *nor)
{
    uint8_t cmd = W25Q_CMD_WRITE_ENABLE;
    OmRet ret = spi_write(nor->spi, &cmd, 1u);
    if (ret != OM_OK)
    {
        return ret;
    }
    for (uint32_t elapsed = 0u; elapsed < OM_W25Q_WEL_TIMEOUT_MS; elapsed++)
    {
        uint8_t sr = 0u;
        ret = w25q_read_status1(nor, &sr);
        if (ret != OM_OK)
        {
            return ret;
        }
        if ((sr & W25Q_SR1_WEL) != 0u)
        {
            return OM_OK;
        }
        osal_sleep_ms(1u);
    }
    return OM_ERR_FLASH_IO;
}

static size_t w25q_fill_addr4(uint8_t *dst, uint32_t addr)
{
    dst[0] = (uint8_t)(addr >> 24);
    dst[1] = (uint8_t)(addr >> 16);
    dst[2] = (uint8_t)(addr >> 8);
    dst[3] = (uint8_t)addr;
    return 4u;
}

/*===========================================================================
 * FlashOps
 *===========================================================================*/

static OmRet w25q_read(FlashDev *dev, uint32_t addr, void *buf, size_t len)
{
    W25q256jvDev *nor = dev->hw;

    /* 命令 + 4 字节地址 + 1 字节空转周期（快速读固有），随后是数据段 */
    uint8_t cmd[6];
    cmd[0] = W25Q_CMD_FAST_READ_4B;
    w25q_fill_addr4(&cmd[1], addr);
    cmd[5] = 0u;

    return spi_write_then_read(nor->spi, cmd, sizeof(cmd), buf, len);
}

/** 单次页编程：长度不超过页内剩余空间，由调用方保证不跨页 */
static OmRet w25q_program_chunk(W25q256jvDev *nor, uint32_t addr, const uint8_t *data,
                                size_t len)
{
    uint8_t cmd[5];
    cmd[0] = W25Q_CMD_PAGE_PROGRAM_4B;
    w25q_fill_addr4(&cmd[1], addr);

    /* 命令与数据在同一个片选周期内，中间不得释放片选 */
    SpiTransfer xfers[2] = {
        {.txBuf = cmd, .rxBuf = NULL, .len = sizeof(cmd), .flags = SPI_XFER_FLAG_CS_HOLD},
        {.txBuf = data, .rxBuf = NULL, .len = len, .flags = 0u},
    };
    SpiMessage msg = {.transfers = xfers, .count = 2u};

    OmRet ret = w25q_write_enable(nor);
    if (ret != OM_OK)
    {
        return ret;
    }
    ret = spi_transfer(nor->spi, &msg);
    if (ret != OM_OK)
    {
        return ret;
    }
    return w25q_wait_ready(nor, OM_W25Q_PROGRAM_TIMEOUT_MS);
}

static OmRet w25q_write(FlashDev *dev, uint32_t addr, const void *data, size_t len)
{
    W25q256jvDev *nor = dev->hw;
    const uint8_t *p = data;

    /* 一次编程不得跨页：按页内剩余空间切段，逐段编程 */
    while (len > 0u)
    {
        uint32_t page_rest = W25Q256JV_PAGE_SIZE - (addr % W25Q256JV_PAGE_SIZE);
        size_t chunk = (len < page_rest) ? len : (size_t)page_rest;

        OmRet ret = w25q_program_chunk(nor, addr, p, chunk);
        if (ret != OM_OK)
        {
            return ret;
        }
        addr += (uint32_t)chunk;
        p += chunk;
        len -= chunk;
    }
    return OM_OK;
}

static OmRet w25q_erase(FlashDev *dev, uint32_t addr, size_t len)
{
    W25q256jvDev *nor = dev->hw;

    while (len > 0u)
    {
        uint8_t cmd[5];
        cmd[0] = W25Q_CMD_SECTOR_ERASE_4B;
        w25q_fill_addr4(&cmd[1], addr);

        OmRet ret = w25q_write_enable(nor);
        if (ret != OM_OK)
        {
            return ret;
        }
        ret = spi_write(nor->spi, cmd, sizeof(cmd));
        if (ret != OM_OK)
        {
            return ret;
        }
        ret = w25q_wait_ready(nor, OM_W25Q_ERASE_TIMEOUT_MS);
        if (ret != OM_OK)
        {
            return ret;
        }
        addr += W25Q256JV_SECTOR_SIZE;
        len -= W25Q256JV_SECTOR_SIZE;
    }
    return OM_OK;
}

static const FlashOps gW25q256jvOps = {
    .read = w25q_read,
    .write = w25q_write,
    .erase = w25q_erase,
};

/*===========================================================================
 * 对外
 *===========================================================================*/

OmRet w25q256jv_register(W25q256jvDev *nor, const char *name, HalSpiDevice *spi)
{
    if (!nor || !spi)
    {
        return OM_ERR_INVALID_ARG;
    }
    nor->spi = spi;
    return flash_register(&nor->dev, name, &gW25q256jvGeom, &gW25q256jvOps, nor);
}

OmRet w25q256jv_read_jedec_id(HalSpiDevice *spi, uint32_t *out)
{
    if (!spi || !out)
    {
        return OM_ERR_INVALID_ARG;
    }
    uint8_t cmd = W25Q_CMD_READ_JEDEC_ID;
    uint8_t id[3] = {0u, 0u, 0u};

    OmRet ret = spi_write_then_read(spi, &cmd, 1u, id, sizeof(id));
    if (ret != OM_OK)
    {
        return ret;
    }
    *out = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | (uint32_t)id[2];
    return OM_OK;
}
