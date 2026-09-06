/**
 * @file    bsp_flash_f4.c
 * @brief   STM32F4 家族片内 Flash BSP 适配层（FlashOps 后端 + 注册入口）
 * @details 实现 FlashDev 的后端 ops（read/write/erase）与静态几何，并以
 *          OM_INIT_BOARD 自注册 "flash0" 设备（flash_register，独立执行域）。
 *
 *          芯片覆盖：STM32F407xx（1MB，双物理 bank，扇区连续编号 0..11）与
 *          STM32F427xx（2MB dual-bank，24 扇区连续编号 0..23，真机实测固化）。
 *          地址语义为设备内偏移（0 起），XIP 基址换算在适配器内部（FLASH_BASE + off）。
 *
 *          完成源策略（D-07/K-16：事件主路径、轮询退化）：
 *          erase = EOP/ERR 中断主路径（FLASH_IRQn → ISR post sem → worker 阻塞等，
 *          零轮询零唤醒）；无中断芯片用 BSP_FLASH4_IRQ_DISABLED 宏切退化
 *          睡眠轮询。write 逐字 BSY = ~30us 级微等待（低于调度粒度，非轮询主路径），
 *          每 64 字让出一次（长写批量防饿死）。
 *          ops 在线程上下文、持设备互斥（域 worker）下被调用。
 */

#include <string.h>

#include "core/om_init.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "stm32f4xx_hal.h"

/*===========================================================================
 * 静态几何（区域表：连续扇区段序列，适配器持 const 实例）
 *===========================================================================*/

#if defined(STM32F427xx)
/* 2MB dual-bank（真机实测）：每 bank 1MB = {16K×4, 64K, 128K×7}，
 * 扇区 24 个连续编号 0..23（HAL Sector 参数直接线性号） */
static const FlashSectorRegion gFlash4Regions[] = {
    {0u, 16u * 1024u, 4u},
    {64u * 1024u, 64u * 1024u, 1u},
    {128u * 1024u, 128u * 1024u, 7u},
    {1024u * 1024u, 16u * 1024u, 4u},
    {1088u * 1024u, 64u * 1024u, 1u},
    {1152u * 1024u, 128u * 1024u, 7u},
};
#elif defined(STM32F407xx)
/* 1MB：双 bank 各 {16K×4, 64K, 128K×3}，扇区地址连续（F407 固定布局） */
static const FlashSectorRegion gFlash4Regions[] = {
    {0u, 16u * 1024u, 4u},
    {64u * 1024u, 64u * 1024u, 1u},
    {128u * 1024u, 128u * 1024u, 3u},
    {512u * 1024u, 16u * 1024u, 4u},
    {576u * 1024u, 64u * 1024u, 1u},
    {640u * 1024u, 128u * 1024u, 3u},
};
#else
#error "bsp_flash_f4: unsupported STM32F4 chip"
#endif

static const FlashGeometry gFlash4Geom = {
    .capacity = FLASH_END - FLASH_BASE + 1u, /* 器件头给出映射末端 */
    .erasedValue = 0xFFu,
    .writeUnit = 4u, /* F4 最小编程单位：字（32 位），地址须字对齐 */
    .pageSize = 0u,
    .sectorSize = 0u,
    .sectorCount = 0u,
    .sectorRegions = gFlash4Regions,
};

static FlashDev gFlash4Dev;

/* 批量写让出周期（字）：64 字 ≈ 2ms 连续 BSY 等待后让出一次 */
#define FLASH4_YIELD_WORD_INTERVAL 64u

/*===========================================================================
 * 内部辅助
 *===========================================================================*/

/** @brief 由设备内偏移求物理扇区号（addr 须为扇区边界，几何与区域表一致） */
static uint32_t bsp_flash4_sector_of(uint32_t addr)
{
    uint32_t idx = 0u;
    for (uint32_t i = 0u; i < sizeof(gFlash4Regions) / sizeof(gFlash4Regions[0]); i++)
    {
        const FlashSectorRegion *r = &gFlash4Regions[i];
        uint32_t size = r->size * r->count;
        if (addr < r->offset + size)
        {
            return idx + (addr - r->offset) / r->size;
        }
        idx += r->count;
    }
    return 0u; /* 不可达：框架已校验 addr 在范围内且为扇区边界 */
}

/** @brief 当前扇区尺寸（区域表查找；几何表小，线性扫描） */
static uint32_t bsp_flash4_sector_size_at(uint32_t addr)
{
    for (uint32_t i = 0u; i < sizeof(gFlash4Regions) / sizeof(gFlash4Regions[0]); i++)
    {
        const FlashSectorRegion *r = &gFlash4Regions[i];
        if (addr < r->offset + r->size * r->count)
        {
            return r->size;
        }
    }
    return 0u;
}

/* 操作完成/错误位全集（F42x 专属 PGPERR/PGSERR 按型号条件并入） */
#define FLASH4_ERR_FLAGS_BASE (FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR)
#ifdef FLASH_FLAG_PGPERR
#define FLASH4_ERR_FLAGS (FLASH4_ERR_FLAGS_BASE | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR)
#else
#define FLASH4_ERR_FLAGS FLASH4_ERR_FLAGS_BASE
#endif
#define FLASH4_OP_FLAGS (FLASH_FLAG_EOP | FLASH4_ERR_FLAGS)

/* 调试：最近一次操作错误的 SR 原值（真机排查用；符号可 halt 读取） */
volatile uint32_t gBspFlash4DbgSr;

/** @brief 由线性扇区号求 SNB 编码（F42x：SNB 12-15 保留，bank2 扇区须 +4）
 *  @param linear 线性扇区号（0..23，几何区域表顺序） */
static uint32_t bsp_flash4_snb_of(uint32_t linear)
{
#if defined(STM32F427xx)
    return (linear >= 12u) ? linear + 4u : linear;
#else
    return linear; /* F407：12 扇区线性编号即 SNB */
#endif
}

/** @brief 等待 BSY 清（让出式：睡眠轮询；超时防御返回 IO 错） */
static OmRet bsp_flash4_wait_busy(uint32_t timeout_ms)
{
    uint32_t waited = 0u;
    while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY))
    {
        osal_sleep_ms(1);
        if (++waited >= timeout_ms)
        {
            return OM_ERR_FLASH_IO;
        }
    }
    /* 操作错误标志——BSY 清后检查并清（原值留调试符号） */
    if (__HAL_FLASH_GET_FLAG(FLASH4_ERR_FLAGS))
    {
        gBspFlash4DbgSr = FLASH->SR;
        __HAL_FLASH_CLEAR_FLAG(FLASH4_ERR_FLAGS);
        return OM_ERR_FLASH_IO;
    }
    return OM_OK;
}

/*===========================================================================
 * FlashOps 实现
 *===========================================================================*/

/**
 * @brief 片内读 = 直接内存读（XIP）
 */
static OmRet bsp_flash4_read(FlashDev *dev, uint32_t addr, void *buf, size_t len)
{
    (void)dev;
    memcpy(buf, (const void *)(FLASH_BASE + addr), len);
    return OM_OK;
}

/**
 * @brief 写 = 逐字 program（框架已保证字对齐与目标已擦）
 *        寄存器级：解锁 → 置 PG → 逐字写 + BSY 紧凑等待（~30us/字，
 *        远小于调度 tick）→ 每 64 字让出一次 → 回读校验 → 清 PG → 上锁
 */
static OmRet bsp_flash4_write(FlashDev *dev, uint32_t addr, const void *data, size_t len)
{
    (void)dev;
    const uint8_t *src = (const uint8_t *)data;
    uint32_t cur = addr;

    HAL_FLASH_Unlock();
    /* 显式置字宽编程（PSIZE=word 2'b10）——PSIZE 复位值不保证为字宽；
     * 操作前清状态标志（残留 EOP/错误干扰后续判定） */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR);
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_1 | FLASH_CR_PG;

    uint32_t word_cnt = 0u;
    while (cur < addr + (uint32_t)len)
    {
        uint32_t word;
        memcpy(&word, src + (cur - addr), sizeof(word)); /* data 未必字对齐 */

        /* 回读校验以 AND 期望为据（program 单向 1→0；0→1 位保持）：
         * 对合法用法（已擦区写入）等价严格等于；对义务违反（0→1）仍可校验。
         * old 必须在编程前采样（写后读回已是 AND 结果） */
        uint32_t old = *(volatile uint32_t *)(FLASH_BASE + cur);

        *(volatile uint32_t *)(FLASH_BASE + cur) = word; /* 触发编程 */
        uint32_t spin = 0u;
        while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY))
        {
            if (++spin > 100000u)
            {
                FLASH->CR &= ~FLASH_CR_PG;
                HAL_FLASH_Lock();
                return OM_ERR_FLASH_IO; /* 字编程超时防御（~ms 级） */
            }
        }
        if (*(volatile uint32_t *)(FLASH_BASE + cur) != (old & word))
        {
            FLASH->CR &= ~FLASH_CR_PG;
            HAL_FLASH_Lock();
            return OM_ERR_FLASH_IO;
        }
        cur += sizeof(word);

        if (++word_cnt >= FLASH4_YIELD_WORD_INTERVAL)
        {
            word_cnt = 0u;
            osal_sleep_ms(1); /* 批量写让出：防饿死低优先级线程 */
        }
    }

    FLASH->CR &= ~FLASH_CR_PG;
    HAL_FLASH_Lock();
    return OM_OK;
}

/**
 * @brief 擦 = 逐扇区擦除（框架已保证 addr/len 为整扇区边界序列）
 *        寄存器级：解锁 → 置 SER+SNB → STRT → BSY 让出轮询 → 清位 → 上锁
 */
#ifndef BSP_FLASH4_IRQ_DISABLED
/* 擦除完成事件（EOP/ERR ISR post_from_isr；worker sem_wait——事件主路径，D-07/K-16） */
static OsalSem *gFlash4EopSem;
static volatile uint32_t gFlash4IrqEvt; /* ISR 记录的事件 SR（错误位现场），worker 消费后清 0 */

/** @brief 等待擦除完成事件（EOP/ERR ISR 唤醒）；错误判定经 ISR 记录的共享变量 */
static OmRet bsp_flash4_wait_eop(void)
{
    if (osal_sem_wait(gFlash4EopSem, 50000u) != OSAL_OK)
    {
        return OM_ERR_FLASH_IO; /* 超时防御 */
    }
    uint32_t evt = gFlash4IrqEvt;
    gFlash4IrqEvt = 0u;
    if (evt & FLASH4_ERR_FLAGS)
    {
        gBspFlash4DbgSr = evt;
        return OM_ERR_FLASH_IO;
    }
    return OM_OK;
}

/** @brief FLASH 全局中断：擦除完成/错误事件。
 *  记录事件 SR → 清 SR（写 1 清，阻止残留位重触发风暴）→ 唤醒等待者。
 *  错误判定经 gFlash4IrqEvt（worker 读共享变量，不用已清的 SR）；
 *  CR 使能位由 worker 统一关闭（ISR 不碰 CR，防读改写竞态）。 */
void FLASH_IRQHandler(void)
{
    uint32_t sr = FLASH->SR;
    if (sr & FLASH4_OP_FLAGS)
    {
        gFlash4IrqEvt = sr; /* 消费前保留错误位现场 */
        FLASH->SR = sr;     /* 写 1 清：清除触发源，阻止风暴 */
        osal_sem_post_from_isr(gFlash4EopSem);
    }
}
#endif /* BSP_FLASH4_IRQ_DISABLED */

static OmRet bsp_flash4_erase(FlashDev *dev, uint32_t addr, size_t len)
{
    (void)dev;
    uint32_t cur = addr;
    uint32_t end = addr + (uint32_t)len;

    HAL_FLASH_Unlock();
    /* 操作前清状态标志（EOP 与错误——残留标志会干扰后续操作判定） */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR);

    while (cur < end)
    {
        uint32_t size = bsp_flash4_sector_size_at(cur);
        uint32_t snb = bsp_flash4_snb_of(bsp_flash4_sector_of(cur));

        /* 与 HAL 序列对齐：先设 PSIZE（擦除同样需要，F42x），SER 与 SNB 一次写入 */
        FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SNB)) | FLASH_CR_PSIZE_1;
        FLASH->CR |= FLASH_CR_SER | (snb << 3u);

#ifndef BSP_FLASH4_IRQ_DISABLED
        /* 事件主路径：清残留事件（防上次异常遗留计数）→ 使能完成中断 → STRT → 等 EOP */
        osal_sem_wait(gFlash4EopSem, 0u);
        FLASH->CR |= FLASH_CR_EOPIE | FLASH_CR_ERRIE;
        FLASH->CR |= FLASH_CR_STRT;
        OmRet ret = bsp_flash4_wait_eop(); /* 阻塞让出，零轮询零唤醒 */
#else
        /* 退化路径：睡眠轮询 BSY（无中断芯片） */
        FLASH->CR |= FLASH_CR_STRT;
        OmRet ret = bsp_flash4_wait_busy(50000u);
#endif
        if (ret != OM_OK)
        {
            FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB | FLASH_CR_EOPIE | FLASH_CR_ERRIE);
            HAL_FLASH_Lock();
            return ret;
        }
        FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB | FLASH_CR_EOPIE | FLASH_CR_ERRIE);
        cur += size;
    }
    HAL_FLASH_Lock();
    return OM_OK;
}

static const FlashOps gFlash4Ops = {
    .read = bsp_flash4_read,
    .write = bsp_flash4_write,
    .erase = bsp_flash4_erase,
};

/*===========================================================================
 * 分散加载自注册（BOARD 级，经 om_do_initcalls 自动调用；独立执行域）
 *===========================================================================*/

static OmRet bsp_flash4_self_init(void)
{
#ifndef BSP_FLASH4_IRQ_DISABLED
    if (osal_sem_create(&gFlash4EopSem, 1u, 0u) != OSAL_OK)
    {
        return OM_ERR_NO_MEM;
    }
    /* ISR 内调 FreeRTOS API 的前提：优先级数值须 ≥ configMAX_SYSCALL 阈值(5)。
     * 寄存器直写（绕过 HAL/CMSIS 函数层，杜绝编码歧义）：
     * IP[FLASH_IRQn] 裸值 = 10 << (8 - PRIO_BITS)，F427 FLASH_IRQn = 4 */
    NVIC->IP[FLASH_IRQn] = (uint8_t)(10u << (8u - 4u));
    NVIC->ISER[FLASH_IRQn / 32u] = (1u << (FLASH_IRQn % 32u));
    gBspFlash4DbgSr = NVIC->IP[FLASH_IRQn]; /* 读回验证（调试符号） */
#endif
    return flash_register(&gFlash4Dev, "flash0", &gFlash4Geom, &gFlash4Ops, NULL, NULL);
}

OM_INIT_BOARD(bsp_flash4_self_init);
