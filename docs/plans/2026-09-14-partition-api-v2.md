# 分区表抽象 v2 实现计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 把分区表抽象从 v1（模块私有全局表 + name-only 操作）改造为 v2（调用方持有的注册表 + `open` 产出的句柄，模块零状态），为 bootloader 的多表/免运行期注册/可 const 需求铺路。

**Architecture:** 表所有权交还调用方——`OmPartitionRegistry{table, count}` 可由 `OM_PARTITION_REGISTRY` 宏在编译期构造为 const 常量；`om_partition_open` 做一次名字解析 + 器件解析 + 几何校验，产出 `OmPartitionHandle{reg, index}`；数据通路全部走句柄，操作期只做 `index < count` 域校验与分区内边界断言。模块不再持有任何静态变量，退化为纯函数集合。

**Tech Stack:** C11（`gnu11` / `-std:c11`）、xmake 3.0.7、host 侧复用 `samples/host/flash_dev_test` 的 `flash_sim.c` + `host_osal.c` 基础设施、实机侧 rm-a/F427（armclang）。

**设计事实源：** `lib/drivers/docs/partition_table_design.md`（v2 接口定稿，commit `c87acf8`）。任何实现分歧以该文档为准；文档与代码不一致时改代码。

**关键既有事实（写码前必读）：**
- CI 的 host-test 作业**已包含** `partition_test`（commit `0716de1`），实现期间必须保持其绿；本地复现命令见每个 Task 的"验证"段。
- 格式门禁用 **clang-format 21.1.8**：`/d/ProgramFiles/LLVM/bin/clang-format.exe`。PATH 里的 17.0.6 **会误报**（17↔21 判定有差异），不要用它。
- 本地 host 构建（GCC 路径）：`--mingw="D:/ProgramFiles/ProgramTools/WinGW-64/mingw64"`。
- 本地 host 构建（MSVC 路径）：须加 `--cflags="-utf-8"`。**Git Bash 下务必写 `-utf-8` 而非 `/utf-8`**——后者会被 MSYS 当路径转换成 `D:/ProgramFiles/GitBash/Git/utf-8`。
- `partition.c` 零 osal 依赖（可裁剪姿态），重写时**不要引入** osal/async 头。

---

## Task 0: 开 Issue 并回填编号

**为什么先做：** 仓库 SOP `docs/process/git_collaboration_spec.md` §8.1「严禁无 Issue 开发」；§10.7 要求提交带 Issue 锚点。

**Step 1: 在 upstream 开 Issue**

```bash
gh issue create --repo oh-my-robot/oh-my-robot-framework \
  --title "feat(drivers): 分区表抽象 v2——注册表+句柄形态（模块零状态，面向 bootloader 多表/免运行期注册）" \
  --body-file <描述文件>
```

描述须含：v1 的两个消费面硬伤（热路径两次名字查找 / 模块单例）、四项驱动需求（免运行期注册 / 表可来自介质 / 一 binary 多表并存 / 框架无隐藏全局）、设计文档链接（`lib/drivers/docs/partition_table_design.md`）、以及 §9 差异记账中的"向业界收敛"说明（Zephyr/MCUboot 均无来源校验）。

**Step 2: 回填编号**

把拿到的编号（设为 `#NN`）写入设计文档 `lib/drivers/docs/partition_table_design.md` 的 §6 关联节，并用于后续所有提交的锚点。

**Step 3: 提交**

```bash
git add lib/drivers/docs/partition_table_design.md
git commit -m "docs(storage): 分区表 v2 设计回填 Issue 锚点 (#NN)"
```

---

## Task 1: 头文件 v2（数据结构 + 声明）

**Files:**
- Modify: `lib/drivers/include/drivers/storage/partition.h`（全文重写）

**Step 1: 重写头文件**

保留 v1 文件头注释中仍然成立的部分（族边界 / 裁剪姿态 / 偏移语义），替换"安全与信任模型"段为 v2 的信任模型，并新增数据结构与 API 声明。

```c
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
 * 形态（v2）：注册表由调用方持有（可整体 const、可驻 ROM 或调用方 RAM），
 * 句柄由 om_partition_open 产出。**本模块零状态**——无私有全局、无 init
 * 顺序依赖、无并发保护需求（全部 API 天然可重入）。
 *
 * 信任模型（v2，与 v1 的差异见设计文档 §9）：句柄内的 index 在操作期受
 * `index < reg->count` 域校验约束，offset/size 恒从表取（调用方改不动）；
 * 几何正确性由 open 期按分区校验 + 可选的全表 registry_validate 保证。
 * 本模型与 Zephyr flash_area / MCUboot flash_area 同形（两者均无来源校验），
 * 且额外保留 open 期几何校验。
 *
 * 偏移语义：便捷层 off 一律为分区内偏移；越界返回 OM_ERR_INVALID_ARG。
 * 对齐语义：erase/erase_range 的扇区对齐由底层器件访问层强制（配置错误在
 * 调用期显式报错，不静默波及邻区）。
 */

#ifndef OM_PARTITION_H
#define OM_PARTITION_H

#include <stddef.h>
#include <stdint.h>

#include "core/om_def.h"

typedef struct OmPartitionEntry {
    const char *name;    /* 逻辑名（表内唯一）——字符串本体在表内，只读 */
    const char *devName; /* 器件名（flash0…） */
    uint32_t    offset;  /* 器件内偏移 */
    uint32_t    size;    /* 分区大小 */
} OmPartitionEntry;

/** 注册表：表 + 条目数。可整体 const（ROM 常量）；表可指向 ROM 或调用方 RAM */
typedef struct OmPartitionRegistry {
    const OmPartitionEntry *table; /* 模块只读该表 */
    uint32_t                count;
} OmPartitionRegistry;

/**
 * 编译期常量注册表：免运行期注册。
 * @warning 实参**必须是数组**（勿传指针）：传指针时 sizeof 比值退化为 0，
 *          得到空表语义（首次 open 即 NOT_FOUND）——响亮失败，不会越界。
 */
#define OM_PARTITION_REGISTRY(table_)                        \
    { (table_), (uint32_t)(sizeof(table_) / sizeof((table_)[0])) }

/** 句柄：open 产出，数据通路的唯一入口。调用方持有，不得手改字段 */
typedef struct OmPartitionHandle {
    const OmPartitionRegistry *reg;   /* 所属注册表 */
    uint32_t                   index; /* 操作期校验 index < reg->count */
} OmPartitionHandle;

/**
 * @brief 全表校验（可选，fail-fast）：结构 + 几何
 * @param reg 注册表
 * @return OM_OK / OM_ERR_INVALID_ARG（reg 空、count==0、条目字段非法、重名、
 *         或**器件已注册时**越容量/非扇区友好）
 * @note 器件未注册的条目跳过几何校验（顺序解耦）——与 v1 注册期语义一致；
 *       这些条目的几何正确性由 open 期兜底。应用在上电初始化调用；
 *       引导程序亦建议调用（配置错就停住的价值最高）。
 */
OmRet om_partition_registry_validate(const OmPartitionRegistry *reg);

/** @brief 条目数（reg 为 NULL 时返回 0） */
uint32_t om_partition_registry_count(const OmPartitionRegistry *reg);

/**
 * @brief 按索引取条目（枚举用，by-value 拷贝）
 * @return OM_OK / OM_ERR_INVALID_ARG / OM_ERR_NOT_FOUND（index 越界）
 */
OmRet om_partition_registry_at(const OmPartitionRegistry *reg, uint32_t index,
                               OmPartitionEntry *out);

/**
 * @brief 按名查询（by-value 纯信息，**不碰器件**，沿用 v1 语义）
 * @return OM_OK / OM_ERR_NOT_FOUND（含空表语义）/ OM_ERR_INVALID_ARG
 */
OmRet om_partition_query(const OmPartitionRegistry *reg, const char *name,
                         OmPartitionEntry *out);

/**
 * @brief 解析分区为句柄（名字 + 器件解析 + 几何校验）
 * @return OM_OK / OM_ERR_NOT_FOUND（名字未命中 / 器件不存在 / 空表）
 *         / OM_ERR_INVALID_ARG（越器件容量 / 非扇区友好）
 */
OmRet om_partition_open(const OmPartitionRegistry *reg, const char *name,
                        OmPartitionHandle *h);

/** @brief 分区内偏移读（句柄域校验 + 双端越界校验） */
OmRet om_partition_read(const OmPartitionHandle *h, uint32_t off, void *buf, size_t len);

/** @brief 分区内偏移写（同 read 契约） */
OmRet om_partition_write(const OmPartitionHandle *h, uint32_t off, const void *data, size_t len);

/** @brief 整分区擦除（扇区对齐由器件层强制，配置错误显式报错） */
OmRet om_partition_erase(const OmPartitionHandle *h);

/**
 * @brief 分区内按范围擦除（扇区对齐由器件层强制，不静默扩擦）
 * @note len == 0 为无操作（返回 OM_OK）
 */
OmRet om_partition_erase_range(const OmPartitionHandle *h, uint32_t off, size_t len);

#endif /* OM_PARTITION_H */
```

**Step 2: 暂不改实现，确认编译失败面**

Run: `xmake f -P samples/host/partition_test -p mingw -m debug --mingw="D:/ProgramFiles/ProgramTools/WinGW-64/mingw64" --cflags="-utf-8" -y && xmake build -P samples/host/partition_test 2>&1 | head -20`

Expected: 大量编译错误（`partition.c` 与 `partition_test.c` 仍在用 v1 API）。这是**预期的中间态**——Task 2 会补齐实现，Task 6 重写测试。

**Step 3: 提交（与 Task 2 合并提交，见 Task 2 Step 4）**

头文件单独提交会留下不可编译的历史（违反 SOP §9.2「随时可编译」），故与实现同批。

---

## Task 2: 实现——registry_validate / count / at / query

**Files:**
- Modify: `lib/drivers/src/storage/partition.c`（全文重写主体）

**Step 1: 写实现**

```c
/**
 * @file   partition.c
 * @brief  分区表抽象实现 v2（注册表 + 句柄，模块零状态）
 *
 * 实现要点：
 * - 零状态：无私有全局、无 init 依赖；全部 API 天然可重入；
 * - 注册表由调用方持有，模块只读其 table/count；
 * - 句柄域校验：index < reg->count，offset/size 恒从表取；
 * - 便捷层一律句柄入口，内部对权威条目重解析，杜绝外部描述符伪造；
 * - 器件访问经底层器件 API（flash_* 同步面）——对齐/容量由该层强制。
 */

#include <string.h>

#include "core/om_def.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "drivers/storage/partition.h"

/* ===================================================================
 * 内部：注册表遍历
 * =================================================================== */

/** @brief 注册表可用性（表非空 + count 非零） */
static bool partition_reg_usable(const OmPartitionRegistry *reg)
{
    return reg != NULL && reg->table != NULL && reg->count > 0u;
}

/** @brief 按索引取条目指针（调用方保证 index < count） */
static const OmPartitionEntry *partition_entry_at(const OmPartitionRegistry *reg, uint32_t index)
{
    return &reg->table[index];
}

/** @brief 权威表线性查找（表小；name 主键唯一） */
static const OmPartitionEntry *partition_lookup(const OmPartitionRegistry *reg, const char *name)
{
    if (!partition_reg_usable(reg) || !name)
    {
        return NULL;
    }
    for (uint32_t i = 0; i < reg->count; i++)
    {
        if (strcmp(reg->table[i].name, name) == 0)
        {
            return &reg->table[i];
        }
    }
    return NULL;
}

/* ===================================================================
 * 内部：几何校验（v1 逻辑原样保留）
 * =================================================================== */

/** @brief 扇区友好判定：分区擦除闭包恰好等于自身——start 为扇区起点，
 *  size 为自 start 起整扇区数（跨 region 时逐段验证，均匀几何为退化情形） */
static bool is_partition_sector_aligned(const FlashGeometry *g, uint32_t off, uint32_t size)
{
    if (g->sectorSize > 0u)
    {
        return off % g->sectorSize == 0u && size % g->sectorSize == 0u;
    }
    uint32_t remaining = size;
    uint32_t cur = off;
    const FlashSectorRegion *r = g->sectorRegions;
    for (; r && remaining > 0u; r++)
    {
        uint32_t rStart = r->offset;
        uint32_t rLen = r->size * r->count;
        if (cur >= rStart + rLen)
        {
            continue;
        }
        if (cur < rStart || (cur - rStart) % r->size != 0u)
        {
            return false;
        }
        uint32_t avail = rStart + rLen - cur;
        uint32_t take = (remaining < avail) ? remaining : avail;
        if (take % r->size != 0u)
        {
            return false;
        }
        remaining -= take;
        cur += take;
    }
    return remaining == 0u;
}

/** @brief 单条目几何校验（器件未注册 → 跳过，返回 OM_OK） */
static OmRet partition_entry_validate(const OmPartitionEntry *e)
{
    FlashDev *dev = flash_find(e->devName);
    if (!dev)
    {
        return OM_OK; /* 顺序解耦：几何正确性由 open 期兜底 */
    }
    const FlashGeometry *g = flash_geometry(dev);
    if (!g)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (e->offset >= g->capacity || e->size > g->capacity - e->offset)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (!is_partition_sector_aligned(g, e->offset, e->size))
    {
        return OM_ERR_INVALID_ARG;
    }
    return OM_OK;
}

/* ===================================================================
 * 注册表面
 * =================================================================== */

OmRet om_partition_registry_validate(const OmPartitionRegistry *reg)
{
    if (!partition_reg_usable(reg))
    {
        return OM_ERR_INVALID_ARG;
    }
    /* 结构校验：字段非空、size 非零、offset+size 无溢出、重名 */
    for (uint32_t i = 0; i < reg->count; i++)
    {
        const OmPartitionEntry *e = partition_entry_at(reg, i);
        if (!e->name || !e->devName || e->size == 0u || e->offset > UINT32_MAX - e->size)
        {
            return OM_ERR_INVALID_ARG;
        }
        for (uint32_t j = 0; j < i; j++)
        {
            if (strcmp(reg->table[j].name, e->name) == 0)
            {
                return OM_ERR_INVALID_ARG;
            }
        }
    }
    /* 几何校验（器件未注册的条目跳过） */
    for (uint32_t i = 0; i < reg->count; i++)
    {
        OmRet ret = partition_entry_validate(partition_entry_at(reg, i));
        if (ret != OM_OK)
        {
            return ret;
        }
    }
    return OM_OK;
}

uint32_t om_partition_registry_count(const OmPartitionRegistry *reg)
{
    return partition_reg_usable(reg) ? reg->count : 0u;
}

OmRet om_partition_registry_at(const OmPartitionRegistry *reg, uint32_t index,
                               OmPartitionEntry *out)
{
    if (!partition_reg_usable(reg) || !out)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (index >= reg->count)
    {
        return OM_ERR_NOT_FOUND;
    }
    *out = *partition_entry_at(reg, index);
    return OM_OK;
}

OmRet om_partition_query(const OmPartitionRegistry *reg, const char *name, OmPartitionEntry *out)
{
    if (!name || !out)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e = partition_lookup(reg, name);
    if (!e)
    {
        return OM_ERR_NOT_FOUND; /* 含空表语义；*out 不动 */
    }
    *out = *e;
    return OM_OK;
}
```

**Step 2: 构建（此时仍会因 open/read/... 未实现而失败）**

Run: `xmake build -P samples/host/partition_test 2>&1 | grep -c error`
Expected: 仍有错（`om_partition_open` 等未定义），但 `partition.c` 本身语法应通过。

**Step 3: 提交（与 Task 3/4 合并，见 Task 4 Step 4）**

---

## Task 3: 实现——open（句柄解析）

**Files:**
- Modify: `lib/drivers/src/storage/partition.c`

**Step 1: 追加实现**

```c
/* ===================================================================
 * 解析面
 * =================================================================== */

OmRet om_partition_open(const OmPartitionRegistry *reg, const char *name,
                        OmPartitionHandle *h)
{
    if (!name || !h)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (!partition_reg_usable(reg))
    {
        return OM_ERR_NOT_FOUND; /* 空表语义 */
    }
    for (uint32_t i = 0; i < reg->count; i++)
    {
        const OmPartitionEntry *e = partition_entry_at(reg, i);
        if (strcmp(e->name, name) != 0)
        {
            continue;
        }
        /* 器件解析：不存在 → NOT_FOUND（幽灵器件条目不阻断其它条目） */
        FlashDev *dev = flash_find(e->devName);
        if (!dev)
        {
            return OM_ERR_NOT_FOUND;
        }
        const FlashGeometry *g = flash_geometry(dev);
        if (!g || e->offset >= g->capacity || e->size > g->capacity - e->offset)
        {
            return OM_ERR_INVALID_ARG;
        }
        if (!is_partition_sector_aligned(g, e->offset, e->size))
        {
            return OM_ERR_INVALID_ARG; /* 非扇区友好：erase 闭包 != 自身 */
        }
        h->reg = reg;
        h->index = i;
        return OM_OK;
    }
    return OM_ERR_NOT_FOUND;
}
```

**Step 2: 提交（与 Task 4 合并）**

---

## Task 4: 实现——read / write / erase / erase_range

**Files:**
- Modify: `lib/drivers/src/storage/partition.c`

**Step 1: 追加实现**

```c
/* ===================================================================
 * 数据通路（句柄入口）
 * =================================================================== */

/** @brief 句柄域校验 + 权威条目/器件解析 + 分区内范围断言 */
static OmRet partition_range(const OmPartitionHandle *h, uint32_t off, size_t len,
                             const OmPartitionEntry **outE, FlashDev **outDev)
{
    if (!h || !h->reg || !partition_reg_usable(h->reg) || h->index >= h->reg->count)
    {
        return OM_ERR_INVALID_ARG; /* 伪造/损坏句柄 */
    }
    const OmPartitionEntry *e = partition_entry_at(h->reg, h->index);
    if (off >= e->size || len > e->size - off)
    {
        return OM_ERR_INVALID_ARG; /* 双端越界（off < size 先行，杜绝 off+len 溢出） */
    }
    FlashDev *dev = flash_find(e->devName);
    if (!dev)
    {
        return OM_ERR_NOT_FOUND;
    }
    const FlashGeometry *g = flash_geometry(dev);
    if (!g || e->offset >= g->capacity || e->size > g->capacity - e->offset)
    {
        return OM_ERR_INVALID_ARG;
    }
    *outE = e;
    *outDev = dev;
    return OM_OK;
}

OmRet om_partition_read(const OmPartitionHandle *h, uint32_t off, void *buf, size_t len)
{
    if (!buf)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e;
    FlashDev *dev;
    OmRet ret = partition_range(h, off, len, &e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    return flash_read(dev, e->offset + off, buf, len);
}

OmRet om_partition_write(const OmPartitionHandle *h, uint32_t off, const void *data, size_t len)
{
    if (!data)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e;
    FlashDev *dev;
    OmRet ret = partition_range(h, off, len, &e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    return flash_write(dev, e->offset + off, data, len);
}

OmRet om_partition_erase(const OmPartitionHandle *h)
{
    if (!h || !h->reg || !partition_reg_usable(h->reg) || h->index >= h->reg->count)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e = partition_entry_at(h->reg, h->index);
    FlashDev *dev = flash_find(e->devName);
    if (!dev)
    {
        return OM_ERR_NOT_FOUND;
    }
    /* 整分区擦：扇区对齐由器件层强制（配置错误显式报 INVALID_ARG，不静默扩擦） */
    return flash_erase(dev, e->offset, e->size);
}

OmRet om_partition_erase_range(const OmPartitionHandle *h, uint32_t off, size_t len)
{
    if (len == 0u)
    {
        return OM_OK; /* 无操作 */
    }
    const OmPartitionEntry *e;
    FlashDev *dev;
    OmRet ret = partition_range(h, off, len, &e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    /* 扇区对齐由器件层强制（flash_erase_validate）——非对齐显式报错，不扩擦 */
    return flash_erase(dev, e->offset + off, len);
}
```

**Step 2: 确认无残留 v1 符号**

Run: `grep -n "s_table\|s_count\|om_partition_register" lib/drivers/src/storage/partition.c lib/drivers/include/drivers/storage/partition.h`
Expected: 无输出（v1 的模块私有全局与注册函数已彻底移除）。

**Step 3: 格式门禁**

Run: `find lib platform samples -name '*.[ch]' -print0 | xargs -0 /d/ProgramFiles/LLVM/bin/clang-format.exe --dry-run --Werror`
Expected: 通过（无输出）。若不通过：用同一二进制 `-i` 就地格式化，重跑确认。

**Step 4: 提交（Task 1–4 合并为一个提交）**

```bash
git add lib/drivers/include/drivers/storage/partition.h lib/drivers/src/storage/partition.c
git commit -m "feat(drivers): 分区表抽象 v2——注册表+句柄形态，模块零状态 (#NN)"
```

> 说明：头文件 + 实现是一个原子单元（头改而实现不改 = 不可编译），按 SOP §9.2「随时可编译」必须同批提交。

---

## Task 5: host 测试——迁移 v1 用例至 v2 API

**Files:**
- Modify: `samples/host/partition_test/partition_test.c`（重写）

**Step 1: 重写夹具与 T0/T1（结构校验 → registry_validate；查询改为带 reg）**

关键改动点（其余照搬 v1 语义）：

```c
/* 表实例：改为 const 注册表（编译期常量，免运行期注册） */
static const OmPartitionEntry table_good[] = {
    {P_BOOT, "flash0", 0x00000u, 0x1000u},
    {P_APP, "flash0", 0x01000u, 0x20000u},
    {P_META, "flash0", 0x21000u, 0x1000u},
    {P_GHOST, "no_such_flash", 0x00000u, 0x1000u},
};
static const OmPartitionRegistry reg_good = OM_PARTITION_REGISTRY(table_good);

static const OmPartitionEntry table_geom_bad[] = {
    {P_APP, "flash0", 0x01000u, 0x20000u},
    {P_BAD_ALIGN, "flash0", 0x22000u, 0x100u},
};
static const OmPartitionRegistry reg_geom_bad = OM_PARTITION_REGISTRY(table_geom_bad);

static const OmPartitionEntry table_cap_bad[] = {
    {P_APP, "flash0", CAP - 0x1000u, 0x2000u},
};
static const OmPartitionRegistry reg_cap_bad = OM_PARTITION_REGISTRY(table_cap_bad);

static const OmPartitionEntry dup_table[] = {
    {P_APP, "flash0", 0x1000u, 0x2000u},
    {P_APP, "flash0", 0x3000u, 0x2000u},
};
static const OmPartitionRegistry reg_dup = OM_PARTITION_REGISTRY(dup_table);

static const OmPartitionEntry bad_field[] = {{NULL, "flash0", 0u, 0x1000u}};
static const OmPartitionRegistry reg_bad_field = OM_PARTITION_REGISTRY(bad_field);

static const OmPartitionEntry zero_size[] = {{P_APP, "flash0", 0u, 0u}};
static const OmPartitionRegistry reg_zero_size = OM_PARTITION_REGISTRY(zero_size);
```

T0 的用例映射（v1 → v2）：

| v1 用例 | v2 表达 |
|---|---|
| `query before register → NOT_FOUND` | `om_partition_query(&reg_good, …)` 在**器件注册前**调用——注意 query 不碰器件，故仍应命中；"空表语义"改由 `reg_none = {NULL, 0}` 表达 |
| `register NULL table / zero count / 重名 / NULL name / zero size` | `om_partition_registry_validate(&…)` 对应各坏表 → `OM_ERR_INVALID_ARG` |
| 顺序解耦：器件未注册时几何坏表 → 跳过 | `om_partition_registry_validate(&reg_geom_bad) == OM_OK`（器件未注册） |
| 几何 fail-fast：器件在 → 整表拒绝 | 注册 flash0 后 `validate(&reg_geom_bad) == OM_ERR_INVALID_ARG`、`validate(&reg_cap_bad) == OM_ERR_INVALID_ARG` |

**Step 2: 跑测试，确认失败**

Run:
```bash
xmake f -P samples/host/partition_test -p mingw -m debug --mingw="D:/ProgramFiles/ProgramTools/WinGW-64/mingw64" --cflags="-utf-8" -y
xmake build -P samples/host/partition_test && xmake run -P samples/host/partition_test host_partition_test
```
Expected: 编译通过（新 API 已实现），用例有 FAIL（T2/T3 仍用 v1 调用形态尚未改）。

**Step 3: 重写 T2/T3（数据通路改句柄入口）**

```c
/* T2：句柄数据通路边界 */
static void test_io_boundary(void)
{
    printf("[T2] handle I/O bounds\n");
    static uint8_t buf[512];
    OmPartitionHandle h;

    CHECK(om_partition_open(&reg_good, P_BOOT, &h) == OM_OK, "open boot partition");

    memset(buf, 0x5A, sizeof(buf));
    CHECK(om_partition_write(&h, 0u, buf, sizeof(buf)) == OM_OK, "write within partition");
    memset(buf, 0, sizeof(buf));
    CHECK(om_partition_read(&h, 0u, buf, sizeof(buf)) == OM_OK, "read within partition");
    CHECK(buf[0] == 0x5A && buf[511] == 0x5A, "roundtrip content matches");

    CHECK(om_partition_read(&h, 0x1000u, buf, 1u) == OM_ERR_INVALID_ARG,
          "read at partition end rejected");
    CHECK(om_partition_read(&h, 0x0FF0u, buf, 0x20u) == OM_ERR_INVALID_ARG,
          "read crossing end rejected");
    CHECK(om_partition_write(&h, 0x0FFCu, buf, 8u) == OM_ERR_INVALID_ARG,
          "write crossing end rejected");
    CHECK(om_partition_read(&h, 0u, NULL, 1u) == OM_ERR_INVALID_ARG, "read NULL buf rejected");
    CHECK(om_partition_write(&h, 0u, NULL, 1u) == OM_ERR_INVALID_ARG, "write NULL data rejected");

    CHECK(om_partition_open(&reg_good, "no_such", &h) == OM_ERR_NOT_FOUND,
          "open unknown name -> NOT_FOUND");
    CHECK(om_partition_open(&reg_good, P_GHOST, &h) == OM_ERR_NOT_FOUND,
          "open ghost-device partition -> NOT_FOUND");
}
```

**Step 4: 跑测试，确认全绿**

Run: `xmake run -P samples/host/partition_test host_partition_test`
Expected: `=== 27 passed, 0 failed ===`（具体数随用例数而定；关键是 **0 failed**）。

**Step 5: 提交**

```bash
git add samples/host/partition_test/partition_test.c
git commit -m "test(storage): partition_test 迁移至 v2 句柄 API (#NN)"
```

---

## Task 6: host 测试——v2 新增用例

**Files:**
- Modify: `samples/host/partition_test/partition_test.c`

**Step 1: 写新用例（先写测试）**

```c
/* ===================================================================
 * T4: v2 新增——多注册表隔离 / 句柄域校验 / RAM 来源表
 * =================================================================== */

/* 第二张表：与 reg_good 同名不同址，验证多表并存不串扰 */
static const OmPartitionEntry table_b[] = {
    {P_BOOT, "flash0", 0x10000u, 0x1000u}, /* 同名 boot，偏移不同 */
    {P_APP, "flash0", 0x11000u, 0x10000u},
};
static const OmPartitionRegistry reg_b = OM_PARTITION_REGISTRY(table_b);

static void test_multi_registry_and_handle(void)
{
    printf("[T4] multi-registry isolation + handle domain check\n");

    OmPartitionHandle ha;
    OmPartitionHandle hb;
    OmPartitionEntry ea;
    OmPartitionEntry eb;

    CHECK(om_partition_open(&reg_good, P_BOOT, &ha) == OM_OK, "open boot in reg_good");
    CHECK(om_partition_open(&reg_b, P_BOOT, &hb) == OM_OK, "open boot in reg_b");
    om_partition_query(&reg_good, P_BOOT, &ea);
    om_partition_query(&reg_b, P_BOOT, &eb);
    CHECK(ea.offset == 0x00000u && eb.offset == 0x10000u,
          "same name resolves per-registry (no cross-talk)");

    /* 跨注册表操作互不影响：写 reg_good 的 boot 不触 reg_b 的 boot */
    static uint8_t w[16];
    memset(w, 0x33, sizeof(w));
    CHECK(om_partition_write(&ha, 0u, w, sizeof(w)) == OM_OK, "write via handle A");
    static uint8_t r[16];
    CHECK(om_partition_read(&hb, 0u, r, sizeof(r)) == OM_OK, "read via handle B");
    CHECK(r[0] == 0xFF, "reg_b region untouched by reg_good write");

    /* 句柄域校验：伪造 index 越界 → INVALID_ARG */
    OmPartitionHandle forged = {&reg_good, 99u};
    CHECK(om_partition_read(&forged, 0u, r, 1u) == OM_ERR_INVALID_ARG,
          "forged index rejected");
    OmPartitionHandle null_reg = {NULL, 0u};
    CHECK(om_partition_read(&null_reg, 0u, r, 1u) == OM_ERR_INVALID_ARG,
          "NULL reg in handle rejected");
    CHECK(om_partition_read(NULL, 0u, r, 1u) == OM_ERR_INVALID_ARG, "NULL handle rejected");
    CHECK(om_partition_erase(&forged) == OM_ERR_INVALID_ARG, "forged handle erase rejected");

    /* 枚举面 */
    CHECK(om_partition_registry_count(&reg_good) == G_COUNT, "registry_count");
    CHECK(om_partition_registry_count(NULL) == 0u, "registry_count(NULL) == 0");
    OmPartitionEntry e0;
    CHECK(om_partition_registry_at(&reg_good, 0u, &e0) == OM_OK, "registry_at(0)");
    CHECK(strcmp(e0.name, P_BOOT) == 0, "registry_at(0) is boot");
    CHECK(om_partition_registry_at(&reg_good, G_COUNT, &e0) == OM_ERR_NOT_FOUND,
          "registry_at out of range -> NOT_FOUND");
}

/* RAM 来源表：运行期填字段，行为与 const 注册表一致 */
static OmPartitionEntry g_ram_table[2];
static OmPartitionRegistry g_ram_reg;

static void test_ram_sourced_registry(void)
{
    printf("[T5] RAM-sourced registry\n");

    g_ram_table[0] = (OmPartitionEntry){P_META, "flash0", 0x21000u, 0x1000u};
    g_ram_table[1] = (OmPartitionEntry){P_APP, "flash0", 0x01000u, 0x20000u};
    g_ram_reg.table = g_ram_table;
    g_ram_reg.count = 2u;

    CHECK(om_partition_registry_validate(&g_ram_reg) == OM_OK, "validate RAM registry");
    OmPartitionHandle h;
    CHECK(om_partition_open(&g_ram_reg, P_META, &h) == OM_OK, "open in RAM registry");
    static uint8_t b[8];
    CHECK(om_partition_read(&h, 0u, b, sizeof(b)) == OM_OK, "read via RAM registry handle");
}
```

**Step 2: 跑测试，确认新增用例通过**

Run: `xmake build -P samples/host/partition_test && xmake run -P samples/host/partition_test host_partition_test`
Expected: 0 failed。

**Step 3: 加 erase_range 用例（先写测试）**

```c
/* ===================================================================
 * T6: erase_range
 * =================================================================== */

static void test_erase_range(void)
{
    printf("[T6] erase_range\n");
    OmPartitionHandle h;

    /* 用 P_APP（0x01000 起 0x20000 = 32 个 4K 扇区）验证范围擦 */
    CHECK(om_partition_open(&reg_good, P_APP, &h) == OM_OK, "open app partition");

    static uint8_t pat[0x1000];
    memset(pat, 0x77, sizeof(pat));
    CHECK(om_partition_write(&h, 0u, pat, sizeof(pat)) == OM_OK, "write first sector of app");
    CHECK(om_partition_write(&h, 0x1000u, pat, sizeof(pat)) == OM_OK, "write second sector");

    /* 只擦第一个扇区：第二个扇区内容必须保留 */
    CHECK(om_partition_erase_range(&h, 0u, 0x1000u) == OM_OK, "erase_range first sector");
    static uint8_t probe[8];
    CHECK(om_partition_read(&h, 0u, probe, sizeof(probe)) == OM_OK, "read erased sector");
    CHECK(probe[0] == 0xFF, "first sector erased");
    CHECK(om_partition_read(&h, 0x1000u, probe, sizeof(probe)) == OM_OK, "read kept sector");
    CHECK(probe[0] == 0x77, "second sector untouched (no silent over-erase)");

    /* 非扇区对齐 → 器件层显式拒绝 */
    CHECK(om_partition_erase_range(&h, 0u, 0x800u) == OM_ERR_INVALID_ARG,
          "half-sector erase rejected");
    CHECK(om_partition_erase_range(&h, 2u, 0x1000u) == OM_ERR_INVALID_ARG,
          "misaligned erase offset rejected");

    /* 分区内越界 → 本层拒绝 */
    CHECK(om_partition_erase_range(&h, 0x1F000u, 0x2000u) == OM_ERR_INVALID_ARG,
          "erase_range crossing partition end rejected");

    /* len == 0 无操作 */
    CHECK(om_partition_erase_range(&h, 0u, 0u) == OM_OK, "erase_range len==0 no-op");
}
```

**Step 4: 在 main() 中接线并跑全量**

在 `main()` 的 `test_erase_and_misconfig()` 之后追加：

```c
    test_multi_registry_and_handle();
    test_ram_sourced_registry();
    test_erase_range();
```

Run: `xmake build -P samples/host/partition_test && xmake run -P samples/host/partition_test host_partition_test`
Expected: `0 failed`，退出码 0。

**Step 5: 格式门禁 + 提交**

```bash
find lib platform samples -name '*.[ch]' -print0 | xargs -0 /d/ProgramFiles/LLVM/bin/clang-format.exe --dry-run --Werror
git add samples/host/partition_test/partition_test.c
git commit -m "test(storage): partition_test 补 v2 用例——多注册表隔离/句柄域校验/RAM 来源表/erase_range (#NN)"
```

---

## Task 7: 回归确认——CI 覆盖的全部 host 测试

**Files:** 无改动（纯验证）

**Step 1: 逐个跑 CI 覆盖的四组**

```bash
# 存储（本次改动）
xmake f -P samples/host/partition_test -p mingw -m debug --mingw="D:/ProgramFiles/ProgramTools/WinGW-64/mingw64" --cflags="-utf-8" -y
xmake run -P samples/host/partition_test host_partition_test

xmake f -P samples/host/flash_dev_test -p mingw -m debug --mingw="D:/ProgramFiles/ProgramTools/WinGW-64/mingw64" --cflags="-utf-8" -y
xmake run -P samples/host/flash_dev_test host_flash_test
```
Expected: 两者均 `0 failed`（partition 改动不应影响 flash 侧，此为回归确认）。

**Step 2: ARM 交叉构建（CI build 作业等价物）**

Run: `xmake build`（在构建壳目录 `logger_store_build/`）
Expected: `build ok`，Flash/RAM 占用不显著变化（partition 模块被裁剪器"无引用不抽取"，未消费时应不增长）。

**Step 3: 无提交**（纯验证）

---

## Task 8: 实机验证 sample

**Files:**
- Create: `samples/pal/partition/main.c`
- Modify: 构建壳 `logger_store_build/xmake.lua`（**非仓库文件**，用户本地配置）

**安全范式（照搬 `samples/pal/flash/main.c`）：** 验证专用区 = bank2 尾 128K 扇区（`0x1E0000`，rm-a/F427 2MB）；**操作前 blank 检查**，非空白（非本程序残留）跳过并报告，绝不真擦。

**Step 1: 写 sample（骨架，模型照 `samples/pal/flash/main.c`）**

```c
/**
 * @file main.c
 * @brief 分区表抽象 v2 真机验证（rm-a/F427）
 *
 * 覆盖 host 覆盖不了的四项：
 *   R1 真实非均匀扇区几何（每 bank 16K×4/64K/128K×7、24 扇区、bank2 SNB +4）
 *   R2 跨 16K→64K 尺寸边界的分区判定（合法跨段 vs 差一个扇区）
 *   R3 真实 FlashDev 通路（XIP 读 / 逐字 program + 回读校验 / EOP 中断驱动擦除）
 *   R4 真实擦除耗时（ms 级）下的 erase_range 语义
 *
 * 安全性：验证专用区 = bank2 尾 128K 扇区（app 镜像只占低地址）；操作前 blank
 * 检查，非空白（非本程序残留）跳过并报告。观测：串口。
 */

#include <string.h>

#include "core/om_init.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "drivers/peripheral/serial/log_serial_backend.h"
#include "drivers/storage/partition.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"
#include "services/log/log.h"

#include "bsp_serial.h"

OM_LOG_MODULE(log_part, OM_LOG_LEVEL_INFO);

/* 验证专用区：bank2 尾 128K 扇区 */
#define REG_TAIL   0x1E0000u
#define TAIL_SIZE  0x20000u
#define CAP_FULL   0x200000u

/* --- 分区表实例（编译期常量，免运行期注册 = v2 的核心诉求）---
 * 表覆盖验证专用区及其前置扇区，用于 R2 跨尺寸边界判定：
 *   tail128  : 0x1E0000 + 0x20000  —— 恰好一个 128K 扇区（合法）
 *   crossseg : 0x1C0000 + 0x40000  —— F427 bank2 的 s22+s23（两个 128K 扇区，
 *              0x1C0000 处无尺寸变化；跨尺寸判定需另设条目：真正的
 *              16K→64K→128K 跨段为 0x100000 + 0x40000）
 *   misalign : 0x1E0000 + 0x10000  —— 128K 扇区的一半（非法，open 应拒绝）
 */
static const OmPartitionEntry g_table[] = {
    {"tail128",  "flash0", REG_TAIL,         TAIL_SIZE},
    {"crossseg", "flash0", 0x1C0000u,        0x40000u},
    {"misalign", "flash0", REG_TAIL,         0x10000u},
};
static const OmPartitionRegistry g_reg = OM_PARTITION_REGISTRY(g_table);
static const OmPartitionRegistry g_reg_bad = OM_PARTITION_REGISTRY(g_table);

static int g_pass;
static int g_fail;

#define CHECK(cond, ...)                          \
    do                                            \
    {                                             \
        if (cond)                                 \
        {                                         \
            g_pass++;                             \
            OM_LOG_INFO("  PASS: " __VA_ARGS__);  \
        }                                         \
        else                                      \
        {                                         \
            g_fail++;                             \
            OM_LOG_ERROR("  FAIL: " __VA_ARGS__); \
        }                                         \
    } while (0)

static FlashDev *g_flash;

static bool part_is_blank(uint32_t addr)
{
    static uint8_t buf[256];
    const FlashGeometry *g = flash_geometry(g_flash);
    if (flash_read(g_flash, addr, buf, sizeof(buf)) != OM_OK)
    {
        return false;
    }
    for (uint32_t i = 0; i < sizeof(buf); i++)
    {
        if (buf[i] != g->erasedValue)
        {
            return false;
        }
    }
    return true;
}

/* --- R1/R2: 几何与边界判定 --- */
static void verify_geometry_and_align(void)
{
    OM_LOG_INFO("--- R1/R2 geometry + sector-friendly ---");

    /* 全表校验：器件已注册 → 几何逐条校验；misalign 非扇区友好 → 整表拒绝 */
    CHECK(om_partition_registry_validate(&g_reg_bad) == OM_ERR_INVALID_ARG,
          "misaligned entry rejects whole table (sector-friendly fail-fast)");

    /* crossseg：s22+s23 两个 128K 扇区，两端均为扇区边界 → 合法（非跨尺寸） */
    OmPartitionHandle h;
    CHECK(om_partition_open(&g_reg, "crossseg", &h) == OM_OK,
          "cross-segment aligned partition opens (s22+s23, no size change)");
    CHECK(om_partition_open(&g_reg, "misalign", &h) == OM_ERR_INVALID_ARG,
          "half-128K partition rejected at open");
    CHECK(om_partition_open(&g_reg, "tail128", &h) == OM_OK, "tail128 opens");
    CHECK(om_partition_open(&g_reg, "no_such", &h) == OM_ERR_NOT_FOUND, "unknown name rejected");
}

/* --- R3/R4: 真实通路 + 真实擦除耗时 --- */
static void verify_data_path(void)
{
    OM_LOG_INFO("--- R3/R4 data path + erase timing ---");

    if (!part_is_blank(REG_TAIL))
    {
        OM_LOG_ERROR("tail NOT blank (non-residue); data-path cases skipped");
        g_fail++;
        return;
    }

    OmPartitionHandle h;
    CHECK(om_partition_open(&g_reg, "tail128", &h) == OM_OK, "open tail128");

    uint32_t t0 = (uint32_t)osal_time_now_monotonic();
    CHECK(om_partition_erase_range(&h, 0u, TAIL_SIZE) == OM_OK, "erase_range whole 128K sector");
    uint32_t t1 = (uint32_t)osal_time_now_monotonic();
    OM_LOG_INFO("erase_range 128K took %u ms (R4: real erase latency)", (unsigned)(t1 - t0));

    static uint8_t w[1024];
    for (uint32_t i = 0; i < sizeof(w); i++)
    {
        w[i] = (uint8_t)(i & 0xFFu);
    }
    CHECK(om_partition_write(&h, 0u, w, sizeof(w)) == OM_OK, "write 1KB via handle");
    static uint8_t r[1024];
    memset(r, 0, sizeof(r));
    CHECK(om_partition_read(&h, 0u, r, sizeof(r)) == OM_OK, "read back 1KB");
    CHECK(memcmp(w, r, sizeof(w)) == 0, "content matches (real XIP read + program verify)");

    /* 范围擦非对齐 → 器件层显式拒绝（不扩擦） */
    CHECK(om_partition_erase_range(&h, 0u, 0x400u) == OM_ERR_INVALID_ARG,
          "misaligned erase_range rejected by device layer");

    /* 多注册表并存（同一 binary 内两张表实例引用同一常量表亦可） */
    CHECK(om_partition_registry_count(&g_reg) == 3u, "registry_count == 3");
}

static void part_verify_thread(void *arg)
{
    (void)arg;
    OM_LOG_INFO("=== partition v2 verify start ===");
    g_flash = flash_find("flash0");
    CHECK(g_flash != NULL, "flash_find(\"flash0\")");
    if (!g_flash)
    {
        OM_LOG_INFO("=== partition v2 verify: %d passed, %d failed ===", g_pass, g_fail);
        return;
    }
    verify_geometry_and_align();
    osal_sleep_ms(150);
    verify_data_path();
    OM_LOG_INFO("=== partition v2 verify: %d passed, %d failed ===", g_pass, g_fail);
    for (;;)
    {
        osal_sleep_ms(60000);
    }
}

static OmRet part_verify_main(void)
{
    OsalThread *vthread = NULL;
    OsalThreadAttr vattr = {"part_vfy", 2048u, OSAL_PRIO_NORMAL_BASE};
    (void)osal_thread_create(&vthread, &vattr, part_verify_thread, NULL);
    return OM_OK;
}
OM_INIT_APPLICATION(part_verify_main);
```

> 注：日志后端接线（`om_log_serial_backend_register` + `OM_INIT_DRIVER`）照抄 `samples/pal/flash/main.c:40-48` 的既有写法，本处省略。

**Step 2: 在构建壳加 target（用户本地操作）**

在 `logger_store_build/xmake.lua` 追加：

```lua
target("partition_verify")
    set_kind("binary")
    set_filename("partition_verify.elf")
    add_deps("tar_oh_my_robot")
    add_rules("oh_my_robot.context", "oh_my_robot.board_assets", "oh_my_robot.image_convert", "oh_my_robot.project_cfg", "oh_my_robot.selfreg")
    add_files(path.join([[oh-my-robot]], [[samples/pal/partition/main.c]]))
target_end()
```

**Step 3: 构建并烧录**

```bash
cd logger_store_build
xmake f --board=rm-a-board --os=freertos --toolchain=armclang -m debug
xmake build partition_verify
xmake flash   # 或 .vscode 的调试配置
```

Expected: 构建 OK；烧录成功。

**Step 4: 串口观测，确认全 PASS**

Expected 输出（示意）：
```
=== partition v2 verify start ===
  PASS: flash_find("flash0")
--- R1/R2 geometry + sector-friendly ---
  PASS: misaligned entry rejects whole table (sector-friendly fail-fast)
  PASS: cross-segment aligned partition opens (s22+s23, no size change)
  PASS: half-128K partition rejected at open
  PASS: tail128 opens
  PASS: unknown name rejected
--- R3/R4 data path + erase timing ---
  PASS: open tail128
  PASS: erase_range whole 128K sector
erase_range 128K took <N> ms (R4: real erase latency)
  PASS: write 1KB via handle
  PASS: read back 1KB
  PASS: content matches (real XIP read + program verify)
  PASS: misaligned erase_range rejected by device layer
  PASS: registry_count == 3
=== partition v2 verify: 12 passed, 0 failed ===
```

**Step 5: 提交（仓库侧只有 sample；构建壳改动不入库）**

```bash
git add samples/pal/partition/main.c
git commit -m "test(storage): 分区表 v2 真机验证 sample——真实几何/跨尺寸边界/真实通路 (#NN)"
```

---

## Task 9: 设计收敛——提纯 ADR

**Files:**
- Create: `docs/adr/0024-partition_registry_handle.md`（编号取当下流水号，写前先 `ls docs/adr/` 确认）

**Step 1: 按 ADR 固定结构写**（`docs/process/document_governance_spec.md` §3.3：背景 / 考虑过的方案 / 最终决策 / 影响，四节且仅有这四节）

必写内容：
- 背景：v1 两个硬伤 + 四项驱动需求（含 bootloader 即将启动这一触发事实）；
- 方案：形态 A/B/C 三选一（A = 注册表+句柄，B = 条目指针+每操作带 reg，C = 只加 reg 参数不做句柄）；
- 决策：形态 A；
- 影响：**§9 差异记账全表**（尤其"防伪造由 name-only 硬契约改为句柄域校验 = 向业界收敛"这一条必须显式记账，附 Zephyr/McUBoot 源码对照）、以及"表可来自介质但解析器未做"这一挂起项。

**Step 2: 关联回填**

在 `lib/drivers/docs/partition_table_design.md` §6 关联节补 ADR 编号；ADR 内引用设计文档。

**Step 3: 提交**

```bash
git add docs/adr/0024-partition_registry_handle.md lib/drivers/docs/partition_table_design.md
git commit -m "docs(adr): 0024 分区表 v2 注册表+句柄形态（含防伪造契约向业界收敛的记账） (#NN)"
```

---

## 收尾检查清单

1. `git log --oneline upstream/integration..HEAD` —— 提交序列原子、每步可编译；
2. 每个提交带 `(#NN)` 锚点（SOP §10.7）；
3. 格式门禁用 **21.1.8** 全量通过；
4. CI 四组 host 测试全绿（partition / flash_dev / log 四件套 / core）；
5. ARM 交叉构建通过；
6. 实机 sample 串口 0 failed 的证据留档（贴进 PR 描述）；
7. PR 目标 `upstream/integration`，描述用 `Refs #NN`；含范围 / 风险 / 验证结果（SOP §11.1）。
