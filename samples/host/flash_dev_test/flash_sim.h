/**
 * @file   flash_sim.h
 * @brief  NOR flash 物理行为模拟器（FlashOps 后端，host 测试用）
 *
 * 模拟语义（对齐/越界已由框架校验，此处只管"物理行为"）：
 * - 写 = program：1→0 单向。strictProgram 开启时，目标位含 0→1 翻转
 *   （写未擦区）返回 OM_ERR_FLASH_IO——模拟真实 NOR 的行为下限；
 * - 擦 = 将目标范围置为 erasedValue（框架保证整扇区）；
 * - 错误注入：failWriteErase 置位时 write/erase 直接返回 OM_ERR_FLASH_IO 且不落位
 *   （模拟后端拒绝——框架错误传播语义测试用；半擦/半写等更细粒度场景后续
 *    需要时在 op 内按条件截断即可，opCount 可观测）。
 *
 * 并发探测：opCount 统计后端调用次数。框架互斥生效时（读写擦全排他），
 * 多线程累加无损；并发测试断言 opCount == 期望值作为锁有效性证据之一。
 */

#ifndef FLASH_SIM_H
#define FLASH_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/peripheral/flash/pal_flash_dev.h"

typedef struct FlashSim {
    uint8_t *mem; /* 宿主堆内存，容量 = capacity */
    uint32_t capacity;
    uint8_t erasedValue;
    uint32_t writeUnit;  /* 与 geometry.writeUnit 一致（校验步长） */
    bool strictProgram;  /* true：0→1 翻转写拒绝（OM_ERR_FLASH_IO） */
    bool failWriteErase; /* true：write/erase 直接返回 OM_ERR_FLASH_IO 且不落位 */
    uint32_t opCount;    /* 后端调用计数（读/写/擦各 +1，含失败尝试） */
    uint32_t opDelayMs;  /* 每操作模拟耗时（测队列/BUSY/并发时序；0 = 瞬时） */
} FlashSim;

/** @brief 初始化模拟器（mem 由 sim 内部分配并置为 erasedValue） */
void flash_sim_init(FlashSim *sim, uint32_t capacity, uint8_t erased_value, uint32_t write_unit);

/** @brief 释放模拟器内存 */
void flash_sim_deinit(FlashSim *sim);

/** @brief 设置每操作模拟耗时（ms）；0 = 瞬时 */
void flash_sim_set_delay(FlashSim *sim, uint32_t delay_ms);

/** @brief 后端错误注入：置 true 后 write/erase 直接返回 OM_ERR_FLASH_IO（不落位） */
void flash_sim_set_fail(FlashSim *sim, bool fail);

/** @brief 后端 ops（注册时 hw 传 FlashSim*） */
OmRet flash_sim_read(FlashDev *dev, uint32_t addr, void *buf, size_t len);
OmRet flash_sim_write(FlashDev *dev, uint32_t addr, const void *data, size_t len);
OmRet flash_sim_erase(FlashDev *dev, uint32_t addr, size_t len);

#endif /* FLASH_SIM_H */
