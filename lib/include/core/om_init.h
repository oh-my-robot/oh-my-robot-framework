/**
 * @file  om_init.h
 * @brief 分散加载自动注册初始化系统
 * @details 参考 Linux initcall、Zephyr SYS_INIT、RT-Thread auto-init。模块在自己的
 *          .c 里写一行 OM_INIT_<LEVEL>(func) 即可把回调注册进 .om_init_<N> 段；
 *          om_do_initcalls(level_lo, level_hi) 在启动期按级别区间依次调用注册的回调，
 *          main() 无需显式调用各模块的 init。
 *
 *          ── 链接器段布局：每级一段，级别顺序在链接期解析（Linux/RT-Thread 同款）──
 *
 *            每个级别一个独立段 .om_init_<N>（N = 级别数值），链接脚本按级别顺序
 *            排列这些段 → 内存里表项天然按级别聚集，级别顺序由链接器保证。
 *            同级内的执行顺序由 prio 决定（启动期一次小排序，见下）。
 *
 *            每级段的边界符号 __om_init_<N>_start / __om_init_<N>_end：
 *              GCC ld  : 每级 PROVIDE(__om_init_<N>_start = .); KEEP(*(.om_init_<N>));
 *                        PROVIDE(__om_init_<N>_end = .)
 *              armlink : 每级一个执行域 ER_OM_INIT_<N>（含 *(.om_init_<N>)），
 *                        om_init.c 内 extern Image$$ER_OM_INIT_<N>$$Base/Limit 并宏重命名
 *
 *          ── 排序策略：级别顺序链接期，同级 prio 运行期 ───────────────────
 *
 *            成熟框架的级别顺序都在链接期解析（Linux/RT-Thread 每级一段按级排列；
 *            Zephyr 另用链接器 SORT_BY_NAME 排同级 prio——但 armlink 无段内排序等价物，
 *            故 OMR 不做链接期 prio 排序）。OMR 采用：级别顺序链接期（每级一段）；
 *            同级 prio 在启动期做一次小排序（表项数少，成本可忽略），双工具链可移植。
 *
 *          ── 级别划分：依赖轴镜像分层（每级 = 架构的一层）────────────────────
 *
 *            EARLIEST  —— 硬件/时钟就绪前，仅寄存器操作
 *            BOARD     —— 板级自举(om_board_init@prio0) + bsp 设备注册（设备提供者）
 *            DRIVER    —— PAL/适配器/电机驱动（设备消费者，device_find 绑定）
 *            SERVICE   —— comm/log/config/diagnostics
 *            SYSTEM    —— chassis/gimbal/supercap 等业务系统
 *            APPLICATION —— app 自身启动设置（建业务线程等），依赖业务系统就绪
 *            LATE      —— 全部就绪后的自检/诊断收尾
 *
 *            级序即架构层序（lower layer 先注册，上层可 device_find 到下层），
 *            对应 Zephyr PRE_KERNEL_1(提供者)/PRE_KERNEL_2(消费者) 与 Linux
 *            subsys/device 的分层思想，也对应 RT-Thread BOARD/DEVICE/COMPONENT/APP。
 *
 *          ── 调度器上下文（能力轴，属性而非一级）──────────────────────────
 *
 *            EARLIEST / BOARD / DRIVER —— 调度器未启，跑在 main 调用帧（main 由框架
 *                                         默认提供弱符号，用户不写，见 ADR-0013），
 *                                         回调不得阻塞、不得使用需调度器的 OSAL 服务
 *            SERVICE / SYSTEM / APPLICATION / LATE —— 调度器已启，跑在 init 线程，
 *                                         可阻塞/IPC/建线程
 *
 * @note  1. 编译器差异由 om_def.h 的 OM_SECTION / OM_USED 吸收
 *        2. 链接器差异收敛于板级 linker 目录（.ld / .sct），每级段与边界符号见上
 *        3. 回调返回 OmRet；失败默认记录并继续（启动期 abort=变砖），
 *           定义 OM_INIT_ABORT_ON_FAIL 可在首个失败时中止
 *        4. 同级同 prio 的相对顺序不保证；需要强序请用不同 prio
 *        5. 自注册 entry 的存活保障：框架源经 oh_my_robot.selfreg 规则直连 binary，
 *           板级外设经 selfreg_sources 直连（见 CLAUDE.md / ADR-0010）——仅靠
 *           OM_USED+KEEP 救不回被静态库按需抽取丢弃的 .o
 *        6. 级"只增不删"：新增级不影响老模块，删除级要改所有注册点
 */

#ifndef __OM_INIT_H__
#define __OM_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "core/om_def.h"

/*===========================================================================
 * 类型
 *===========================================================================*/

/** @brief 初始化回调函数类型 —— 返回 OmRet，OM_OK 表示成功 */
typedef OmRet (*OmInitFn)(void);

/** @brief init 注册表项 —— 由 OM_INIT 宏放入 .om_init 段 */
typedef struct
{
    OmInitFn fn;      /**< 回调函数（NULL 表示空槽，跳过） */
    const char *name; /**< 诊断用名（编译期 #func） */
    uint8_t level;    /**< 级别 OmInitLevel */
    uint8_t prio;     /**< 同级优先级 0-99，小者先执行 */
} OmInitEntry;

/*===========================================================================
 * 级别（依赖轴：每级 = 架构的一层）
 *===========================================================================*/

/** @brief 初始化级别类型（数值即链接器段 .om_init_<N> 的段号） */
typedef uint8_t OmInitLevel;

#define OM_INIT_LEVEL_EARLIEST    0 /**< 硬件/时钟前，仅寄存器；调度器未启，不可阻塞 */
#define OM_INIT_LEVEL_BOARD       1 /**< 板级自举 + bsp 设备注册（设备提供者）；不可阻塞 */
#define OM_INIT_LEVEL_DRIVER      2 /**< PAL/适配器/电机驱动（设备消费者）；不可阻塞 */
#define OM_INIT_LEVEL_SERVICE     3 /**< comm/log/config/diagnostics；可阻塞/IPC */
#define OM_INIT_LEVEL_SYSTEM      4 /**< 业务系统 chassis/gimbal/supercap；可阻塞/IPC */
#define OM_INIT_LEVEL_APPLICATION 5 /**< app 自身启动设置（建业务线程等），依赖业务系统就绪；可阻塞/IPC */
#define OM_INIT_LEVEL_LATE        6 /**< 全就绪后自检/诊断；可阻塞/IPC */
#define OM_INIT_LEVEL_COUNT       7 /**< 级别总数（哨兵，用作 om_do_initcalls 的上界） */

/*===========================================================================
 * 注册宏
 *===========================================================================*/

#define OM_INIT_PASTE2(a, b) a##b
#define OM_INIT_PASTE(a, b)  OM_INIT_PASTE2(a, b)

/** @brief 字符串化助手：先展开参数再转字符串（供段名拼接） */
#define OM_INIT_STR_HELPER(x) #x
#define OM_INIT_STR(x)        OM_INIT_STR_HELPER(x)

/** @brief 分级别名的默认优先级（0-99，小者先） */
#ifndef OM_INIT_PRIO_DEFAULT
#define OM_INIT_PRIO_DEFAULT 50
#endif

/**
 * @brief 注册初始化回调到 .om_init_<N> 段（泛型，自选级别与优先级）
 *
 * @param func  回调函数名（OmInitFn：OmRet func(void)）
 * @param level 级别（OM_INIT_LEVEL_*，数值即段号）
 * @param prio  同级优先级 0-99，小者先执行
 *
 * 用 __COUNTER__ 生成唯一符号名，允许同一 func 在多个级别或多个 prio 注册
 * （对应 Linux __define_initcall 的 id 参数解决的"重复符号"问题）。
 * 段名 .om_init_<N> 由级别数值拼出，级别顺序由链接脚本按段排列保证。
 */
#define OM_INIT(func, level, prio)                                              \
    OM_USED static const OmInitEntry OM_INIT_PASTE(om_init_entry_, __COUNTER__) \
        OM_SECTION(".om_init_" OM_INIT_STR(level)) = {(func), #func, (uint8_t)(level), (uint8_t)(prio)}

/**
 * @brief 分级别名（语法糖，默认优先级 OM_INIT_PRIO_DEFAULT）
 *
 * 用法示例：
 *   OM_INIT_BOARD(bsp_can_register);                       // 设备提供者
 *   OM_INIT(om_board_init, OM_INIT_LEVEL_BOARD, 0);        // 需 prio 0 时用泛型
 *   OM_INIT_DRIVER(can_adapter_init);                      // 设备消费者
 *   OM_INIT_SYSTEM(supercap_self_init);
 */
#define OM_INIT_EARLIEST(fn)    OM_INIT(fn, OM_INIT_LEVEL_EARLIEST, OM_INIT_PRIO_DEFAULT)
#define OM_INIT_BOARD(fn)       OM_INIT(fn, OM_INIT_LEVEL_BOARD, OM_INIT_PRIO_DEFAULT)
#define OM_INIT_DRIVER(fn)      OM_INIT(fn, OM_INIT_LEVEL_DRIVER, OM_INIT_PRIO_DEFAULT)
#define OM_INIT_SERVICE(fn)     OM_INIT(fn, OM_INIT_LEVEL_SERVICE, OM_INIT_PRIO_DEFAULT)
#define OM_INIT_SYSTEM(fn)      OM_INIT(fn, OM_INIT_LEVEL_SYSTEM, OM_INIT_PRIO_DEFAULT)
#define OM_INIT_APPLICATION(fn) OM_INIT(fn, OM_INIT_LEVEL_APPLICATION, OM_INIT_PRIO_DEFAULT)
#define OM_INIT_LATE(fn)        OM_INIT(fn, OM_INIT_LEVEL_LATE, OM_INIT_PRIO_DEFAULT)

/*===========================================================================
 * 编排
 *===========================================================================*/

/**
 * @brief 执行 [level_lo, level_hi) 区间内所有已注册回调
 *
 * 按级别区间 [level_lo, level_hi) 逐级遍历对应的 .om_init_<N> 段（级别顺序由
 * 链接脚本按段排列保证，链接期解析）；同级内按 prio 升序排序后依次调用。
 *
 * 失败策略：默认记录首个失败的 {name, ret} 并继续执行其余回调，函数末尾返回该
 * 失败码（无失败返回 OM_OK）。定义 OM_INIT_ABORT_ON_FAIL 时，首个失败即立即返回。
 *
 * 典型用法（调度器上下文分裂）：
 *   - 调度器启动前（main）：
 *       om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_SERVICE)
 *     跑 EARLIEST + BOARD + DRIVER（不可阻塞）；
 *   - 调度器启动后（init 线程）：
 *       om_do_initcalls(OM_INIT_LEVEL_SERVICE, OM_INIT_LEVEL_COUNT)
 *     跑 SERVICE + SYSTEM + LATE（可阻塞/IPC）。
 *
 * @param level_lo 起始级别（含）
 * @param level_hi 结束级别（不含），用 OM_INIT_LEVEL_COUNT 表示到末尾
 * @return OmRet  OM_OK 或首个失败回调的错误码
 */
OmRet om_do_initcalls(OmInitLevel level_lo, OmInitLevel level_hi);

/** @brief 最近一次 om_do_initcalls 的首个失败回调名（无失败/未调用则 NULL）——fatal context 诊断用 */
const char *om_init_last_fail_name(void);

/**
 * @brief 启动前段：调度器前 om_do_initcalls(EARLIEST, SERVICE)——板级自举 + 外设注册 + 驱动
 *        （不可阻塞），结束后"硬件就绪、驱动已注册、调度器未启"。
 * @return OM_OK 或首个失败回调的错误码——调用方决定 om_fatal_error 或降级继续
 */
OmRet om_startup_pre_scheduler(void);

/**
 * @brief 启动后段：建 init 线程（CRITICAL 带，跑 SERVICE+SYSTEM+APPLICATION+LATE，可阻塞/
 *        IPC）→ osal_kernel_start() 启动调度器，**不返回**。
 * @details 依赖 om_startup_pre_scheduler() 已执行（顺序契约，文档约束，无运行时校验）；
 *          init 线程创建失败 / 调度器启动失败 → om_fatal_error(OM_FATAL_STARTUP)；
 *          线程内 SERVICE..LATE 任一 initcall 失败同样 fatal（启动期失败一律显式停机，
 *          由 handler 决定受控恢复——带病启动比停机更危险）。
 *          供自定义启动序列（强 main，ADR-0013 L1）在两段之间插入决策逻辑。
 */
void om_startup_post_scheduler(void);

/**
 * @brief 系统启动编排（kernel 层，正常不返回）——= om_startup_pre_scheduler() +
 *        om_startup_post_scheduler() 的默认组合接线；pre 失败 → om_fatal_error。
 * @details 用户默认不写 main：框架经 oh_my_robot.selfreg 注入弱 main（lib/source/core/
 *          om_main.c），启动文件调 main 即进入本编排。app 自身启动设置（建业务线程等）
 *          与其它模块一样经 OM_INIT_APPLICATION 分散加载，无需显式注册。需要自定义
 *          启动序列的用户可定义强 main 覆盖并自行组合 pre/post（逃生通道，见 ADR-0013、
 *          ADR-0014）。实现在 lib/source/core/om_system_startup.c（tar_awkernel）。
 */
void om_system_startup(void);

#ifdef __cplusplus
}
#endif

#endif /* __OM_INIT_H__ */
