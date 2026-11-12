/**
 * @file   spi_nor_w25q256jv.h
 * @brief  SPI NOR 芯片驱动：W25Q256JV（32MB，4KB 扇区 / 256B 页）
 *
 * 本文件是 FlashOps 的一个后端实现，把器件层的读/写/擦落到 SPI 总线上；
 * 上层（分区表、存储原语）只经 FlashDev 访问，不感知 SPI。
 *
 * 地址宽度：本器件容量超出 3 字节地址可表达的范围，故读/写/擦一律使用
 * 4 字节地址命令。这些命令的地址字段长度是命令自身的属性，与器件当前
 * 处于哪种地址模式无关；因此本驱动从不触碰器件的地址模式切换命令——
 * 切模式会让器件停留在被切走的状态上，板级复位后按 3 字节地址访问的
 * 引导程序将读到错误位置。地址宽度在本驱动内是编译期常量。
 *
 * 让出契约：写/擦内部等待器件就绪（轮询器件状态寄存器的 WIP 位）时按
 * 器件层契约让出 CPU，不忙等。本驱动不含延时循环以外的任何等待。
 */

#ifndef OM_SPI_NOR_W25Q256JV_H
#define OM_SPI_NOR_W25Q256JV_H

#include "core/om_def.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "drivers/peripheral/spi/pal_spi_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 芯片驱动对象：FlashDev 外壳 + 器件私有态。
 *  调用方静态分配，注册后由框架持有（与 FlashDev 同生命周期）。 */
typedef struct W25q256jvDev {
    FlashDev dev;      /* 注册进器件层的设备外壳 */
    HalSpiDevice *spi; /* 该片所在的 SPI 从设备（板级挂载，驱动不拥有） */
} W25q256jvDev;

/**
 * @brief 注册为 flash 器件（内部调用 flash_register）
 * @param nor  芯片驱动对象（静态或 BSS 分配）
 * @param name 器件名（如 "nor0"），全局唯一
 * @param spi  已 attach 到 SPI 总线的从设备（片选/时钟由板级配置决定）
 * @return 同 flash_register
 * @note 线程上下文；设备为永驻对象，无注销路径
 */
OmRet w25q256jv_register(W25q256jvDev *nor, const char *name, HalSpiDevice *spi);

/**
 * @brief 读器件识别码（厂商 + 类型 + 容量）
 * @param spi 已挂载的 SPI 从设备（不必先注册为 flash 器件）
 * @param out 输出 24 位识别码
 * @return OM_OK / OM_ERR_INVALID_ARG / 总线错误
 * @note 上电自检用：不写不擦，一步证明"总线 → 片选 → 器件"整条链路可用
 */
OmRet w25q256jv_read_jedec_id(HalSpiDevice *spi, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OM_SPI_NOR_W25Q256JV_H */
