/**
 * @file   spi_nor_sim.h
 * @brief  W25Q256JV SPI 从设备仿真（host 测试用）
 *
 * 实现 SpiControllerOps，挂在真实 SPI 框架（hal_spi.c）之下，按字节解器件
 * 命令集：页编程、4KB 扇区擦除、4 字节地址快速读、状态寄存器读、JEDEC ID。
 *
 * 刻意保留的器件真实行为（测试的价值都在这些约束上）：
 *   - 编程只能把 1 写成 0（按位与），目标未擦时结果与真器件一致；
 *   - 擦除把整扇区置为 0xFF；
 *   - 写/擦期间 WIP 置位，且要经过若干次状态读取才清零——驱动必须真的走
 *     轮询等待路径；
 *   - WIP 期间新的编程/擦除被忽略，写使能也不锁存；
 *   - 单次编程跨页即记账（真器件只在本页内回卷，不报错——驱动必须自己拆页）。
 *
 * 未建模的：时钟频率、页编程时间、擦除时间、状态寄存器的保护位。
 */

#ifndef SPI_NOR_SIM_H
#define SPI_NOR_SIM_H

#include <stdint.h>

#include "drivers/peripheral/spi/pal_spi_dev.h"

#define SPI_NOR_SIM_CAPACITY    (32u * 1024u * 1024u)
#define SPI_NOR_SIM_PAGE_SIZE   256u
#define SPI_NOR_SIM_SECTOR_SIZE 4096u

/** 器件忙碌期间需要被读到的状态次数：>1 才能迫使驱动进入轮询等待 */
#define SPI_NOR_SIM_BUSY_READS 2u

typedef struct {
    SpiControllerOps ops; /* 交给 spi_bus_register */

    uint8_t *mem;
    uint32_t capacity;

    uint8_t sr1;        /* 状态寄存器-1：bit0 = WIP，bit1 = WEL */
    uint32_t busyReads; /* 还需被读几次才清 WIP */

    /* 命令解析 */
    uint32_t cmd;     /* 当前命令码，0 = 未收到 */
    uint32_t argLeft; /* 还需接收的地址字节数 */
    uint32_t addr;    /* 解析出的地址 */
    uint32_t dummyLeft;
    uint32_t idPos;
    uint32_t progPageBase; /* 本次编程的页基址（跨页检测与回卷落址） */
    uint32_t progLen;      /* 本次编程已接收的数据字节数 */

    /* 观测（测试断言用） */
    uint32_t statReads;     /* 状态寄存器被读次数 */
    uint32_t wipSeen;       /* 读到 WIP=1 的次数（证明等待路径真的走了） */
    uint32_t progOps;       /* 页编程次数 */
    uint32_t eraseOps;      /* 扇区擦除次数 */
    uint32_t csAsserts;     /* 片选拉低次数 */
    uint32_t pageCrossings; /* 单次编程跨页次数（必须为 0） */
} SpiNorSim;

/** @brief 初始化（分配容量内存，填充为擦后值） */
void spi_nor_sim_init(SpiNorSim *sim);

/** @brief 释放 */
void spi_nor_sim_deinit(SpiNorSim *sim);

/** @brief 全片填充指定值（构造"未擦"介质用） */
void spi_nor_sim_fill(SpiNorSim *sim, uint8_t value);

/** @brief 注入片选边沿
 *  GPIO 片选路径下，框架的片选经引脚写入落到硬件，不再经过控制器 ops->setCs，
 *  故由引脚侧把边沿转给器件，器件才能划分命令边界。 */
void spi_nor_sim_cs_edge(SpiNorSim *sim, bool assert);

#endif /* SPI_NOR_SIM_H */
