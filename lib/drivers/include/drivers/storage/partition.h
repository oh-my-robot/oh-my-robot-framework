/**
 * @file   partition.h
 * @brief  分区表抽象——可擦存储器件族的上层语义（boot/OTA/存储上层消费面）
 *
 * 族边界：本模块服务可擦存储族（erase 语义 = 区域生命周期操作）。随机器件族
 * （EEPROM/FRAM）与块设备族（SD/eMMC）的分区形态另属（命名窗口 / GPT 式），
 * 不并入本抽象；免擦可擦器件（MRAM 类）经 erase 缺省语义并入。
 *
 * 裁剪姿态：可裁剪组件（非必备）——boot/OTA/存储上层才消费；静态库
 * "无引用不抽取"天然裁剪（无独立开关）。依赖面 = flash 设备族同步面，
 * 与 OM_FLASH_SYNC_ONLY / osal-none 裁剪组合兼容（本模块零 osal 依赖）。
 *
 * 安全与信任模型：
 * - 注册形态：公共头零数据符号——表经 om_partition_register 交入模块内部
 *   （私有指针持有），外部无符号可达、无绕过面（不能绕过 name-only API
 *   自行取表拼地址）；注册的表须 const 静态存储期数据（落只读存储）。
 * - 硬契约：本模块所有操作 API 只接受分区名（name）为唯一可信输入，
 *   内部对权威表重新解析并校验——不接受外部传入的分区描述作为访问依据。
 *
 * 偏移语义：便捷层 off 一律为分区内偏移；越界返回 OM_ERR_INVALID_ARG。
 * 对齐语义：erase 的扇区对齐由底层器件访问层强制（整分区擦须扇区对齐，
 * 配置错误在调用期显式报错，不静默波及邻区）。
 */

#ifndef OM_PARTITION_H
#define OM_PARTITION_H

#include <stddef.h>
#include <stdint.h>

#include "core/om_def.h"

typedef struct OmPartitionEntry {
    const char *name;    /* 逻辑名（表内唯一）——字符串本体在表内，只读 */
    const char *devName; /* 器件名（flash0…） */
    uint32_t offset;     /* 器件内偏移 */
    uint32_t size;       /* 分区大小 */
} OmPartitionEntry;

/**
 * @brief 注册分区表（模块内私有持有；先于一切操作调用）
 * @param table 表数组——须 const 静态存储期数据（落只读存储；模块不拷贝）
 * @param count 条目数
 * @return OM_OK / OM_ERR_INVALID_ARG（table/条目字段非法）
 *
 * 注册期校验：条目 name/devName 非空、size > 0、offset+size 无溢出。
 * 器件存在性与容量校验在操作期（器件可能晚于表注册）。
 */
OmRet om_partition_register(const OmPartitionEntry *table, uint32_t count);

/**
 * @brief 按名查询分区（返回值拷贝——纯信息，不参与访问决策）
 * @param name 分区名
 * @param out  输出条目；未找到时不动
 * @return OM_OK / OM_ERR_NOT_FOUND（含未注册=空表语义）/ OM_ERR_INVALID_ARG
 */
OmRet om_partition_query(const char *name, OmPartitionEntry *out);

/** @brief 分区内偏移读（name-only：内部重解析权威表 + 双端越界校验） */
OmRet om_partition_read(const char *name, uint32_t off, void *buf, size_t len);

/** @brief 分区内偏移写（同 read 契约） */
OmRet om_partition_write(const char *name, uint32_t off, const void *data, size_t len);

/** @brief 整分区擦除（扇区对齐由器件层强制，配置错误显式报错） */
OmRet om_partition_erase(const char *name);

#endif /* OM_PARTITION_H */
