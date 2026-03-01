# OM 架构参考手册

## 0. 说明
- 本文基于当前仓库 `oh-my-robot/` 目录源码与公开头文件生成。
- 内外部划分规则（方案A）：
  - 外部宏/API：公开头文件中的宏/类型/函数（排除头文件保护宏）。
  - 内部宏：头文件保护宏，以及以 `_` 或 `__` 开头的宏。
  - 内部类型/函数：方案A不做推断，统一标记为“无”。
- 依赖/调用摘要：
  - 依赖基于 `#include`（头文件 + 源文件）。
  - 调用基于 `.c` 源文件中对公开 API 名称的正则匹配统计，非完整静态调用图。

## 0.1 命名约定（统一）
- 结构体实体类型统一使用 `_s` 后缀。
- `_t` 后缀仅用于句柄/指针/抽象别名。
- 枚举与联合后缀分别统一使用 `_e`、`_u`。
- 类型名（结构体/枚举/联合）使用 `CamelCase`。
- 成员变量使用 `camelBack`。
- 函数名、普通变量、参数名使用 `snake_case`。
- 宏与枚举常量使用 `UPPER_CASE`。

## 1. third_party（外部依赖）

### 主要模块
- `第三方库与外部实现（FreeRTOS/CMSIS/HAL 等）`

### 入口文件
- `oh-my-robot/platform/osal/freertos/FreeRTOS`
- `oh-my-robot/platform/bsp/vendor`

### 对外 API 列表
- 无公开 C 头文件 API。

### 依赖/调用关系摘要
- 依赖（基于 `#include`）
  - 无
- 调用（基于 `.c` 对公开 API 的显式调用）
  - 无或不适用

**备注**：当前仓库未出现独立的 oh-my-robot/third_party 目录；外部代码主要随平台与板级目录进入。

## 2. bsp（板级支持层）

### 主要模块
- `板级源码与启动文件`
- `板级数据装配（board/chip/vendor）`
- `BSP 构建入口与装配脚本`

### 入口文件
- `oh-my-robot/platform/bsp/bsp.lua`
- `oh-my-robot/platform/bsp/arch.lua`
- `oh-my-robot/platform/bsp/components.lua`
- `oh-my-robot/platform/bsp/data_loader.lua`
- `oh-my-robot/platform/bsp/assets.lua`
- `oh-my-robot/platform/bsp/inputs.lua`
- `oh-my-robot/platform/bsp/boards`
- `oh-my-robot/platform/bsp/data`

### 对外 API 列表
- 无公开 C 头文件 API。

### 依赖/调用关系摘要
- 依赖（基于 `#include`）
  - 无
- 调用（基于 `.c` 对公开 API 的显式调用）
  - 无或不适用

**备注**：该层以板级源码/数据/构建脚本为主，无公开 C 头文件 API。

## 3. platform（端口与平台适配层）

### 主要模块
- `OSAL 端口实现`
- `sync 加速后端`

### 入口文件
- `oh-my-robot/platform/osal/xmake.lua`
- `oh-my-robot/platform/osal/freertos`
- `oh-my-robot/platform/sync/xmake.lua`
- `oh-my-robot/platform/sync/freertos`

### 对外 API 列表
- 无公开 C 头文件 API。

### 依赖/调用关系摘要
- 依赖（基于 `#include`）
  - 无
- 调用（基于 `.c` 对公开 API 的显式调用）
  - 无或不适用

**备注**：该层提供实现文件与构建脚本，不提供公开 C 头文件 API（端口实现细节见 OSAL/sync 层摘要）。

## 4. core（基础能力层）

### 主要模块
- `基础定义/配置/编译器适配`
- `CPU 与中断定义`
- `算法（控制/协议）`
- `数据结构`
- `原子操作`
- `端口适配头`

### 入口文件
- `oh-my-robot/lib/include/awlib.h`
- `oh-my-robot/lib/include/core/om_compiler.h`
- `oh-my-robot/lib/include/core/om_config.h`
- `oh-my-robot/lib/include/core/om_def.h`
- `oh-my-robot/lib/include/core/om_interrupt.h`
- `oh-my-robot/lib/include/core/om_cpu.h`
- `oh-my-robot/lib/include/core/algorithm/om_algorithm.h`
- `oh-my-robot/lib/include/core/algorithm/controller/pid.h`
- `oh-my-robot/lib/include/core/algorithm/protocol/crc.h`
- `oh-my-robot/lib/include/core/data_struct/om_data_struct.h`
- `oh-my-robot/lib/include/core/data_struct/avltree.h`
- `oh-my-robot/lib/include/core/data_struct/corelist.h`
- `oh-my-robot/lib/include/core/data_struct/ringbuffer.h`
- `oh-my-robot/lib/include/atomic/atomic.h`
- `oh-my-robot/lib/include/atomic/atomic_simple.h`
- `oh-my-robot/lib/include/atomic/atomic_base.h`
- `oh-my-robot/lib/include/atomic/om_atomic_gcc.h`
- `oh-my-robot/lib/include/atomic/om_atomic_msvc.h`
- `oh-my-robot/lib/include/atomic/om_atomic_ac5.h`
- `oh-my-robot/lib/include/port/port_compiler.h`
- `oh-my-robot/lib/include/port/om_port_hw.h`

### 对外 API 列表
#### `oh-my-robot/lib/include/atomic/atomic.h`
- 外部宏
  - `om_load`
  - `om_store`
  - `om_exchange`
  - `om_cas`
  - `om_fetch_add`
  - `om_fetch_sub`
  - `om_fetch_and`
  - `om_fetch_or`
  - `om_fetch_xor`
  - `om_thread_fence`
  - `om_signal_fence`
- 内部宏
  - `AW_ATOMICS_H`
  - `_AW_CAST_8`
  - `_AW_CAST_16`
  - `_AW_CAST_32`
  - `_AW_CAST_64`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/atomic/om_atomic_ac5.h`
- 外部宏
  - 无
- 内部宏
  - `AW_BACKEND_AC5_H`
  - `_om_impl_load`
  - `_om_impl_store`
  - `_om_impl_exchange`
  - `_om_impl_cas`
  - `_om_impl_fetch_add`
  - `_om_impl_fetch_sub`
  - `_om_impl_fetch_and`
  - `_om_impl_fetch_or`
  - `_om_impl_fetch_xor`
  - `_om_impl_thread_fence`
  - `_om_impl_signal_fence`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - `_om_ac5_barrier`
- 内部函数
  - 无

#### `oh-my-robot/lib/include/atomic/atomic_base.h`
- 外部宏
  - `AW_USE_STDATOMIC`
  - `AW_MO_RELAXED`
  - `AW_MO_CONSUME`
  - `AW_MO_ACQUIRE`
  - `AW_MO_RELEASE`
  - `AW_MO_ACQ_REL`
  - `AW_MO_SEQ_CST`
  - `om_atomic_t`
  - `AW_ATOMIC_VAR_INIT`
  - `AW_INLINE`
- 内部宏
  - `AW_ATOMIC_BASE_H`
- 外部类型
  - `om_memory_order`
  - `om_atomic_int_t`
  - `om_atomic_uint_t`
  - `om_atomic_long_t`
  - `om_atomic_ulong_t`
  - `om_atomic_llong_t`
  - `om_atomic_ullong_t`
  - `om_atomic_ptr_t`
  - `om_atomic_size_t`
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/atomic/om_atomic_gcc.h`
- 外部宏
  - 无
- 内部宏
  - `AW_BACKEND_GCC_H`
  - `_om_impl_load`
  - `_om_impl_store`
  - `_om_impl_exchange`
  - `_om_impl_cas`
  - `_om_impl_fetch_add`
  - `_om_impl_fetch_sub`
  - `_om_impl_fetch_and`
  - `_om_impl_fetch_or`
  - `_om_impl_fetch_xor`
  - `_om_impl_thread_fence`
  - `_om_impl_signal_fence`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/atomic/om_atomic_msvc.h`
- 外部宏
  - 无
- 内部宏
  - `AW_AWTOMIC_MSVC_H`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - `_om_msvc_barrier_pre`
  - `_om_msvc_barrier_post`
  - `_om_msvc_load_8`
  - `_om_msvc_store_8`
  - `_om_msvc_exchange_8`
  - `_om_msvc_cas_8`
  - `_om_msvc_fetch_add_8`
  - `_om_msvc_fetch_or_8`
  - `_om_msvc_fetch_and_8`
  - `_om_msvc_fetch_xor_8`
  - `_om_msvc_load_16`
  - `_om_msvc_store_16`
  - `_om_msvc_exchange_16`
  - `_om_msvc_cas_16`
  - `_om_msvc_fetch_add_16`
  - `_om_msvc_fetch_or_16`
  - `_om_msvc_fetch_and_16`
  - `_om_msvc_fetch_xor_16`
  - `_om_msvc_load_32`
  - `_om_msvc_store_32`
  - `_om_msvc_exchange_32`
  - `_om_msvc_cas_32`
  - `_om_msvc_fetch_add_32`
  - `_om_msvc_fetch_or_32`
  - `_om_msvc_fetch_and_32`
  - `_om_msvc_fetch_xor_32`
  - `_om_msvc_load_64`
  - `_om_msvc_store_64`
  - `_om_msvc_exchange_64`
  - `_om_msvc_cas_64`
  - `_om_msvc_fetch_add_64`
  - `_om_msvc_fetch_or_64`
  - `_om_msvc_fetch_and_64`
  - `_om_msvc_fetch_xor_64`
- 内部函数
  - 无

#### `oh-my-robot/lib/include/atomic/atomic_simple.h`
- 外部宏
  - `om_load_acq`
  - `om_load_rlx`
  - `om_load_consume`
  - `om_store_rel`
  - `om_store_rlx`
  - `om_swap_ar`
  - `om_swap_acq`
  - `om_swap_rel`
  - `om_swap_rlx`
  - `om_cas_ar`
  - `om_cas_acq`
  - `om_cas_rel`
  - `om_cas_rlx`
  - `om_faa_rlx`
  - `om_faa_acq`
  - `om_faa_rel`
  - `om_faa_ar`
  - `om_fas_rlx`
  - `om_fas_acq`
  - `om_fas_rel`
  - `om_fas_ar`
  - `om_inc_rlx`
  - `om_inc_ar`
  - `om_dec_rlx`
  - `om_dec_ar`
  - `om_fand_rlx`
  - `om_fand_acq`
  - `om_fand_rel`
  - `om_fand_ar`
  - `om_for_rlx`
  - `om_for_acq`
  - `om_for_rel`
  - `om_for_ar`
  - `om_fxor_rlx`
  - `om_fxor_acq`
  - `om_fxor_rel`
  - `om_fxor_ar`
  - `om_fence_acq`
  - `om_fence_rel`
  - `om_fence_ar`
  - `om_fence_seq`
  - `om_compiler_barrier`
- 内部宏
  - `AW_ATOMIC_SIMPLE_H`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/awlib.h`
- 外部宏
  - 无
- 内部宏
  - `__AWLIB_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/algorithm/om_algorithm.h`
- 外部宏
  - 无
- 内部宏
  - `__AW_ALGORITHM_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/algorithm/controller/pid.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_PID_H__`
- 外部类型
  - `improvementParams`
  - `PidMode_e`
  - `settings`
  - `PidController_t`
- 内部类型
  - 无
- 外部函数
  - `pid_init`
  - `pid_set_params`
  - `pid_set_output_limit`
  - `pid_set_integral_limit`
  - `pid_set_dead_band`
  - `pid_set_derivative_filter_coeff`
  - `pid_set_derivative_first_enable`
  - `pid_set_variable_integral_thresholds`
  - `pid_reset`
  - `pid_compute`
  - `pid_get_component_outputs`
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/algorithm/protocol/crc.h`
- 外部宏
  - `CRC8_SZ`
  - `CRC16_SZ`
- 内部宏
  - `__CRC_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - `verify_crc8_check_sum`
  - `verify_crc16_check_sum`
  - `append_crc8_check_sum`
  - `append_crc16_check_sum`
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/om_compiler.h`
- 外部宏
  - 无
- 内部宏
  - 无
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/om_config.h`
- 外部宏
  - 无
- 内部宏
  - `OM_SYNC_ACCEL`
  - `__OM_CONFIG_H__`
  - `__OM_USE_ASSERT`
  - `__OM_USE_HAL_SERIALS`
  - `__OM_USE_HAL_CAN`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/om_cpu.h`
- 外部宏
  - `OM_CPU_ERRHANDLER`
- 内部宏
  - `__OM_BOARD_H__`
  - `__OM_CPU_NAME`
- 外部类型
  - `OmBoardInterface_s`
  - `OmCpu_s`
  - `cputime_cnt`
  - `cputime_s`
  - `cputime_ms`
  - `cputime_us`
  - `OmCpu_t`
  - `OmBoardInterface_t`
- 内部类型
  - 无
- 外部函数
  - `om_get_cpu_name`
  - `om_get_firmware_version`
  - `om_cpu_get_time_s`
  - `om_cpu_get_time_ms`
  - `om_cpu_get_time_us`
  - `om_cpu_get_delta_time_s`
  - `om_cpu_errhandler`
  - `om_cpu_reset`
  - `om_cpu_delay_ms`
  - `om_cpu_register`
  - `om_core_init`
  - `om_board_init`
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/om_def.h`
- 外部宏
  - `OM_NULL`
  - `OM_WAIT_FOREVER`
  - `DEV_STATUS_CLOSED`
  - `DEV_STATUS_OK`
  - `DEV_STATUS_BUSY_RX`
  - `DEV_STATUS_BUSY_TX`
  - `DEV_STATUS_BUSY`
  - `DEV_STATUS_TIMEOUT`
  - `DEV_STATUS_ERR`
  - `DEV_STATUS_INITED`
  - `DEV_STATUS_OPENED`
  - `DEV_STATUS_SUSPEND`
  - `DEVICE_CMD_CFG`
  - `DEVICE_CMD_SUSPEND`
  - `DEVICE_CMD_RESUME`
  - `DEVICE_CMD_SET_IOTYPE`
  - `DEVICE_REG_STANDALONG`
  - `DEVICE_REG_RDONLY`
  - `DEVICE_REG_WRONLY`
  - `DEVICE_REG_RDWR`
  - `DEVICE_REG_RDWR_MASK`
  - `DEVICE_REG_POLL_TX`
  - `DEVICE_REG_INT_TX`
  - `DEVICE_REG_DMA_TX`
  - `DEVICE_REG_TXTYPE_MASK`
  - `DEVICE_REG_POLL_RX`
  - `DEVICE_REG_INT_RX`
  - `DEVICE_REG_DMA_RX`
  - `DEVICE_REG_RXTYPE_MASK`
  - `DEVICE_REG_IOTYPE_MASK`
  - `DEVICE_REG_MASK`
  - `DEVICE_O_RDONLY`
  - `DEVICE_O_WRONLY`
  - `DEVICE_O_RDWR`
  - `DEVICE_O_RDWR_MASK`
  - `DEVICE_O_POLL_TX`
  - `DEVICE_O_INT_TX`
  - `DEVICE_O_DMA_TX`
  - `DEVICE_O_TXTYPE_MASK`
  - `DEVICE_O_POLL_RX`
  - `DEVICE_O_INT_RX`
  - `DEVICE_O_DMA_RX`
  - `DEVICE_O_RXTYPE_MASK`
  - `DEVICE_O_IOTYPE_MASK`
  - `DEVICE_O_MASK`
- 内部宏
  - `__OM_DEF_H__`
  - `__OM_VERSION`
  - `__om_attribute`
  - `__om_used`
  - `__om_weak`
  - `__om_section`
  - `__om_align`
  - `__om_packed`
  - `__dbg_param_def`
- 外部类型
  - `om_bool_e`
  - `om_action_e`
  - `OmRet_e`
  - `OmLogLevel_e`
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/om_interrupt.h`
- 外部宏
  - `om_hw_enable_interrupt_force`
  - `om_hw_disable_interrupt_force`
  - `om_hw_get_primask`
  - `om_hw_set_primask`
- 内部宏
  - `__OM_API_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - `om_hw_disable_interrupt`
  - `om_hw_restore_interrupt`
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/data_struct/avltree.h`
- 外部宏
  - 无
- 内部宏
  - `__MY_AVL_TREE__H`
- 外部类型
  - `avl_tree_t`
- 内部类型
  - 无
- 外部函数
  - `avl_tree_create`
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/data_struct/om_data_struct.h`
- 外部宏
  - 无
- 内部宏
  - `__AW_DATA_STRUCT_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/data_struct/corelist.h`
- 外部宏
  - `NULL`
  - `LIST_POISON1`
  - `LIST_POISON2`
  - `offsetof`
  - `container_of`
  - `LIST_HEAD_INIT`
  - `LIST_HEAD`
  - `list_entry`
  - `list_first_entry`
  - `list_for_each`
  - `list_for_each_safe`
  - `list_for_each_prev`
  - `list_for_each_entry`
  - `list_for_each_entry_safe`
- 内部宏
  - `__CORE_LIST_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/core/data_struct/ringbuffer.h`
- 外部宏
  - `smp_rmb`
  - `smp_wmb`
- 内部宏
  - `RINGBUF_H`
- 外部类型
  - `Ringbuf_s`
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/port/port_compiler.h`
- 外部宏
  - `AW_COMPILER_MSVC`
  - `AW_COMPILER_AC6`
  - `AW_COMPILER_GCC_LIKE`
  - `AW_COMPILER`
  - `AW_COMPILER_AC5`
  - `AW_STR_HELPER`
  - `AW_STR`
  - `AW_NOTE`
- 内部宏
  - `__OM_PORT_COMPILER_H__`
  - `__port_attribute`
  - `__port_used`
  - `__port_weak`
  - `__port_section`
  - `__port_align`
  - `__port_noreturn`
  - `__port_packed`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/include/port/om_port_hw.h`
- 外部宏
  - `PORT_PRIMASK_ENABLED`
  - `PORT_PRIMASK_DISABLED`
- 内部宏
  - `__OM_PORT__HW_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

### 依赖/调用关系摘要
- 依赖（基于 `#include`）
  - 未归类外部头文件
    - `intrin.h`
    - `windows.h`
- 调用（基于 `.c` 对公开 API 的显式调用）
  - 无或不适用

## 5. OSAL（操作系统抽象层）

### 主要模块
- `核心与配置`
- `线程`
- `时间`
- `事件`
- `互斥锁`
- `信号量`
- `队列`
- `定时器`
- `端口选择`

### 入口文件
- `oh-my-robot/lib/osal/include/osal/osal.h`
- `oh-my-robot/lib/osal/include/osal/osal_core.h`
- `oh-my-robot/lib/osal/include/osal/osal_thread.h`
- `oh-my-robot/lib/osal/include/osal/osal_time.h`
- `oh-my-robot/lib/osal/include/osal/osal_event.h`
- `oh-my-robot/lib/osal/include/osal/osal_timer.h`
- `oh-my-robot/lib/osal/include/osal/osal_mutex.h`
- `oh-my-robot/lib/osal/include/osal/osal_sem.h`
- `oh-my-robot/lib/osal/include/osal/osal_queue.h`
- `oh-my-robot/lib/osal/include/osal/osal_config.h`
- `oh-my-robot/lib/osal/include/osal/osal_port.h`

### 对外 API 列表
#### `oh-my-robot/lib/osal/include/osal/osal.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_OSAL_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/osal/include/osal/osal_config.h`
- 外部宏
  - `OSAL_WAIT_FOREVER`
  - `OSAL_STACK_WORD_BYTES`
  - `OSAL_PRIORITY_MAX`
  - `OSAL_TASK_NAME_MAX`
  - `OSAL_QUEUE_REGISTRY_MAX`
- 内部宏
  - `__OM_OSAL_CONFIG_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/osal/include/osal/osal_core.h`
- 外部宏
  - `OSAL_OK`
  - `OSAL_ERR`
  - `OSAL_ERR_TIMEOUT`
  - `OSAL_ERR_PARAM`
  - `OSAL_ERR_CTX`
  - `OSAL_ERR_NOMEM`
  - `OSAL_ERR_UNSUPPORTED`
  - `OSAL_ASSERT`
  - `OSAL_ASSERT_IN_TASK`
- 内部宏
  - `__OM_OSAL_CORE_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - `osal_in_isr`
  - `osal_critical_enter`
  - `osal_critical_exit`
  - `osal_irq_save`
  - `osal_irq_restore`
  - `osal_malloc`
  - `osal_free`
- 内部函数
  - 无

#### `oh-my-robot/lib/osal/include/osal/osal_event.h`
- 外部宏
  - `OSAL_EVENT_OPT_WAIT_ALL`
  - `OSAL_EVENT_OPT_NO_CLEAR`
- 内部宏
  - `__OM_OSAL_EVENT_H__`
- 外部类型
  - `osal_event_t`
- 内部类型
  - 无
- 外部函数
  - `osal_event_create`
  - `osal_event_delete`
  - `osal_event_set`
  - `osal_event_set_isr`
  - `osal_event_clear`
  - `osal_event_wait`
- 内部函数
  - 无

#### `oh-my-robot/lib/osal/include/osal/osal_mutex.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_OSAL_MUTEX_H__`
- 外部类型
  - `osal_mutex_t`
- 内部类型
  - 无
- 外部函数
  - `osal_mutex_create`
  - `osal_mutex_delete`
  - `osal_mutex_lock`
  - `osal_mutex_unlock`
- 内部函数
  - 无

#### `oh-my-robot/lib/osal/include/osal/osal_port.h`
- 外部宏
  - `OSAL_PORT_FREERTOS`
  - `OSAL_PORT_POSIX`
  - `OM_OSAL_PORT`
- 内部宏
  - `__OM_OSAL_PORT_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/osal/include/osal/osal_queue.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_OSAL_QUEUE_H__`
- 外部类型
  - `osal_queue_t`
- 内部类型
  - 无
- 外部函数
  - `osal_queue_create`
  - `osal_queue_delete`
  - `osal_queue_send`
  - `osal_queue_send_isr`
  - `osal_queue_recv`
  - `osal_queue_recv_isr`
  - `osal_queue_peek`
  - `osal_queue_peek_isr`
  - `osal_queue_reset`
  - `osal_queue_messages_waiting`
  - `osal_queue_spaces_available`
- 内部函数
  - 无

#### `oh-my-robot/lib/osal/include/osal/osal_sem.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_OSAL_SEM_H__`
- 外部类型
  - `osal_sem_t`
- 内部类型
  - 无
- 外部函数
  - `osal_sem_create`
  - `osal_sem_delete`
  - `osal_sem_wait`
  - `osal_sem_post`
  - `osal_sem_post_isr`
- 内部函数
  - 无

#### `oh-my-robot/lib/osal/include/osal/osal_thread.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_OSAL_THREAD_H__`
- 外部类型
  - `osal_thread_attr_t`
  - `osal_thread_t`
- 内部类型
  - 无
- 外部函数
  - `osal_thread_create`
  - `osal_thread_self`
  - `osal_thread_sleep_ms`
  - `osal_thread_yield`
  - `osal_thread_exit`
  - `osal_thread_terminate`
  - `osal_kernel_start`
- 内部函数
  - 无

#### `oh-my-robot/lib/osal/include/osal/osal_time.h`
- 外部宏
  - `OSAL_MS_PER_SEC`
  - `OSAL_US_PER_MS`
  - `OSAL_NS_PER_MS`
  - `OSAL_NS_PER_SEC`
- 内部宏
  - `__OM_OSAL_TIME_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - `osal_time_us_to_ms_ceil`
  - `osal_time_ns_to_ms_ceil`
  - `osal_time_ms`
  - `osal_time_ms64`
  - `osal_time_us`
  - `osal_time_us64`
  - `osal_time_ns64`
  - `osal_delay_until_ms`
- 内部函数
  - 无

#### `oh-my-robot/lib/osal/include/osal/osal_timer.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_OSAL_TIMER_H__`
- 外部类型
  - `osal_timer_mode_t`
  - `osal_timer_t`
- 内部类型
  - 无
- 外部函数
  - `osal_timer_create`
  - `osal_timer_start`
  - `osal_timer_stop`
  - `osal_timer_reset`
  - `osal_timer_delete`
  - `osal_timer_get_id`
  - `osal_timer_set_id`
- 内部函数
  - 无

### 依赖/调用关系摘要
- 依赖（基于 `#include`）
  - `core`
  - `third_party`
- 调用（基于 `.c` 对公开 API 的显式调用）
  - 无或不适用

## 6. sync（同步语义层）

### 主要模块
- `completion（one-shot 同步语义）`

### 入口文件
- `oh-my-robot/lib/sync/include/sync/completion.h`

### 对外 API 列表
#### `oh-my-robot/lib/sync/include/sync/completion.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_SYNC_COMPLETION_H__`
- 外部类型
  - `Completion_s`
  - `comp_status_e`
  - `Completion_t`
- 内部类型
  - 无
- 外部函数
  - `completion_init`
  - `completion_deinit`
  - `completion_wait`
  - `completion_done`
- 内部函数
  - 无

### 依赖/调用关系摘要
- 依赖（基于 `#include`）
  - `core`
  - `osal`
  - `third_party`
- 调用（基于 `.c` 对公开 API 的显式调用）
  - `osal`：osal_irq_restore(17), osal_irq_save(6), osal_in_isr(5), osal_sem_post(2), osal_sem_post_isr(2), osal_sem_wait(2), osal_thread_self(2), osal_sem_create(1), osal_sem_delete(1)

## 7. drivers（驱动层与设备模型）

### 主要模块
- `设备模型`
- `外设抽象（PAL）`
- `CAN 外设`
- `串口外设`
- `电机框架`
- `厂商驱动（DJI）`

### 入口文件
- `oh-my-robot/lib/drivers/include/drivers/model/device.h`
- `oh-my-robot/lib/drivers/include/drivers/peripheral/pal_dev.h`
- `oh-my-robot/lib/drivers/include/drivers/peripheral/can/pal_can_dev.h`
- `oh-my-robot/lib/drivers/include/drivers/peripheral/serial/pal_serial_dev.h`
- `oh-my-robot/lib/drivers/include/drivers/motor/motor.h`
- `oh-my-robot/lib/drivers/include/drivers/motor/motor_drv.h`
- `oh-my-robot/lib/drivers/include/drivers/motor/vendors/dji/dji_motor_conf.h`
- `oh-my-robot/lib/drivers/include/drivers/motor/vendors/dji/dji_motor_drv.h`

### 对外 API 列表
#### `oh-my-robot/lib/drivers/include/drivers/model/device.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_DEVICE_H__`
- 外部类型
  - `DevInterface_s`
  - `DevAttr_s`
  - `Device_s`
  - `DevInterface_t`
  - `Device_t`
  - `DevAttr_t`
- 内部类型
  - 无
- 外部函数
  - `device_find`
  - `device_init`
  - `device_open`
  - `device_close`
  - `device_read`
  - `device_write`
  - `device_ctrl`
  - `device_register`
  - `device_set_read_cb`
  - `device_set_write_cb`
  - `device_set_err_cb`
  - `device_set_param`
  - `device_read_cb`
  - `device_write_cb`
  - `device_err_cb`
  - `device_get_oparams`
  - `device_get_cflags`
  - `device_set_cflags`
  - `device_set_status`
  - `device_clr_status`
  - `device_check_status`
  - `device_get_regparams`
  - `device_get_name`
- 内部函数
  - 无

#### `oh-my-robot/lib/drivers/include/drivers/motor/motor.h`
- 外部宏
  - 无
- 内部宏
  - 无
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/drivers/include/drivers/motor/motor_drv.h`
- 外部宏
  - 无
- 内部宏
  - 无
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/drivers/include/drivers/motor/vendors/dji/dji_motor_conf.h`
- 外部宏
  - `DJI_MOTOR_RX_ID_MIN`
  - `DJI_MOTOR_RX_ID_MAX`
  - `DJI_MOTOR_MAX_TX_UNITS`
  - `DJI_ASSERT`
- 内部宏
  - `__DJI_MOTOR_CONFIG_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/drivers/include/drivers/motor/vendors/dji/dji_motor_drv.h`
- 外部宏
  - `DJI_RX_ID_START`
  - `DJI_RX_ID_END`
  - `DJI_MOTOR_RX_MAP_SIZE`
  - `DJI_TX_ID_C6X0_1_4`
  - `DJI_TX_ID_MIX_1_FF`
  - `DJI_TX_ID_GM6020_V_5_7`
  - `DJI_TX_ID_GM6020_C_1_4`
  - `DJI_TX_ID_GM6020_C_5_7`
  - `DJI_ERR_NONE`
  - `DJI_ERR_MEM`
  - `DJI_ERR_VOLT`
  - `DJI_ERR_PHASE`
  - `DJI_ERR_SENSOR`
  - `DJI_ERR_TEMP_HIGH`
  - `DJI_ERR_STALL`
  - `DJI_ERR_CALIB`
  - `DJI_ERR_OVERHEAT`
- 内部宏
  - `__DJI_MOTOR_DRIVER_H__`
- 外部类型
  - `DJIMotorBus_s`
  - `DJIMotorType_e`
  - `DJIMotorCtrlMode_e`
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/drivers/include/drivers/peripheral/can/pal_can_dev.h`
- 外部宏
  - `CAN_TX_MODE_UNOVERWRTING`
  - `CAN_TX_MODE_OVERWRITING`
  - `CAN_FILTER_CFG_INIT`
  - `CAN_REG_INT_TX`
  - `CAN_REG_INT_RX`
  - `CAN_REG_IS_STANDALONG`
  - `CAN_O_INT_TX`
  - `CAN_O_INT_RX`
  - `CAN_CMD_CFG`
  - `CAN_CMD_SUSPEND`
  - `CAN_CMD_RESUME`
  - `CAN_CMD_SET_IOTYPE`
  - `CAN_CMD_CLR_IOTYPE`
  - `CAN_CMD_CLOSE`
  - `CAN_CMD_FLUSH`
  - `CAN_CMD_SET_FILTER`
  - `CAN_CMD_SET_FILTER_AUTO_BANK`
  - `CAN_CMD_START`
  - `CAN_CMD_GET_STATUS`
  - `CAN_DATA_MSG_DSC_INIT`
  - `CAN_REMOTE_MSG_DSC_INIT`
  - `CAN_DEFUALT_CFG`
  - `IS_CAN_FILTER_INVALID`
  - `IS_CAN_MAILBOX_INVALID`
- 内部宏
  - `__HAL_CAN_CORE_H`
- 外部类型
  - `CanErrCounter_s`
  - `CanBitTime_s`
  - `CanTimeCfg_s`
  - `CanFilterCfg_s`
  - `CanFilter_s`
  - `CanMsgDsc_s`
  - `CanUserMsg_s`
  - `CanMsgList_s`
  - `CanMsgFifo_s`
  - `CanMailbox_s`
  - `CanFunctionalCfg_s`
  - `CanCfg_s`
  - `CanAdapterInterface_s`
  - `CanHwInterface_s`
  - `CanStatusManager_s`
  - `CanRxHandler_s`
  - `CanTxHandler_s`
  - `HalCanHandler_s`
  - `CanNodeErrStatus_e`
  - `CanDlc_e`
  - `CanSjw_e`
  - `CanBS1_e`
  - `CanBS2_e`
  - `CanBaudRate_e`
  - `CanWorkMode_e`
  - `CanFilterMode_e`
  - `CanIdType_e`
  - `CanFilterIdType_e`
  - `CanMsgType_e`
  - `CanFdMsgBrs_e`
  - `CanFdMsgFormat_e`
  - `CanFdESI_e`
  - `CanErrorCode_e`
  - `CanIsrEvent_e`
  - `CanErrEvent_e`
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/drivers/include/drivers/peripheral/pal_dev.h`
- 外部宏
  - 无
- 内部宏
  - `__PAL_DEV_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/drivers/include/drivers/peripheral/serial/pal_serial_dev.h`
- 外部宏
  - `SERIAL_O_BLCK_RX`
  - `SERIAL_O_BLCK_TX`
  - `SERIAL_O_NBLCK_RX`
  - `SERIAL_O_NBLCK_TX`
  - `SERIAL_REG_INT_RX`
  - `SERIAL_REG_INT_TX`
  - `SERIAL_REG_DMA_RX`
  - `SERIAL_REG_DMA_TX`
  - `SERIAL_REG_POLL_TX`
  - `SERIAL_REG_POLL_RX`
  - `SERIAL_CMD_SET_IOTPYE`
  - `SERIAL_CMD_CLR_IOTPYE`
  - `SERIAL_CMD_SET_CFG`
  - `SERIAL_CMD_CLOSE`
  - `SERIAL_CMD_FLUSH`
  - `SERIAL_CMD_SUSPEND`
  - `SERIAL_CMD_RESUME`
  - `SERIAL_MIN_TX_BUFSZ`
  - `SERIAL_MIN_RX_BUFSZ`
  - `SERIAL_RXFLUSH`
  - `SERIAL_TXFLUSH`
  - `SERIAL_TXRXFLUSH`
  - `SERIAL_DEFAULT_CFG`
- 内部宏
  - `__HAL_SERIAL_H__`
- 外部类型
  - `SerialCfg_s`
  - `SerialInterface_s`
  - `SerialFifo_s`
  - `SerialPriv_s`
  - `HalSerial_s`
  - `DataBits_e`
  - `StopBits_e`
  - `Parity_e`
  - `FlowCtrl_e`
  - `Oversampling_e`
  - `SerialEvent_e`
  - `SerialErrCode_e`
  - `SerialFifoStatus_e`
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

### 依赖/调用关系摘要
- 依赖（基于 `#include`）
  - `core`
  - `osal`
  - `sync`
- 调用（基于 `.c` 对公开 API 的显式调用）
  - `osal`：osal_irq_restore(26), osal_irq_save(19), osal_free(6), osal_malloc(6), osal_in_isr(3), osal_timer_create(1), osal_timer_delete(1), osal_timer_get_id(1), osal_timer_start(1)
  - `sync`：completion_done(2), completion_init(2), completion_wait(2)

## 8. services（通用服务层）

### 主要模块
- `config`
- `diagnostics`
- `event`
- `fs`
- `comm`
- `log`

### 入口文件
- `oh-my-robot/lib/services/include/services/services.h`

### 对外 API 列表
#### `oh-my-robot/lib/services/include/services/services.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_SERVICES_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

### 依赖/调用关系摘要
- 依赖（基于 `#include`）
  - 无
- 调用（基于 `.c` 对公开 API 的显式调用）
  - 无或不适用

**备注**：当前仅有占位头文件，未声明服务层对外 API。

## 9. systems（系统/业务子系统层）

### 主要模块
- `arm`
- `chassis`
- `gimbal`
- `robot`
- `supercap`

### 入口文件
- `oh-my-robot/lib/systems/include/systems/systems.h`
- `oh-my-robot/lib/systems/include/systems/supercap/supercap.h`

### 对外 API 列表
#### `oh-my-robot/lib/systems/include/systems/supercap/supercap.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_SUPERCAP_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

#### `oh-my-robot/lib/systems/include/systems/systems.h`
- 外部宏
  - 无
- 内部宏
  - `__OM_SYSTEMS_H__`
- 外部类型
  - 无
- 内部类型
  - 无
- 外部函数
  - 无
- 内部函数
  - 无

### 依赖/调用关系摘要
- 依赖（基于 `#include`）
  - 无
- 调用（基于 `.c` 对公开 API 的显式调用）
  - 无或不适用
