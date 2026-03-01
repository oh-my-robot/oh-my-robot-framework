--- @file oh_my_robot/build/modules/toolchain_lib.lua
--- @brief OM 工具链工具库
--- @details 提供工具链默认配置、参数注入与镜像转换逻辑。

--- 工具链工具库
---@class ToolchainLib

local build_root = path.join(os.scriptdir(), "..")
local config_module = import("toolchains.toolchain_config", {rootdir = build_root})
local flags_module = import("toolchains.toolchain_flags", {rootdir = build_root})
local arch_module = import("toolchains.toolchain_arch", {rootdir = build_root})
local image_module = import("toolchains.toolchain_image", {rootdir = build_root})
local validate_module = import("toolchains.toolchain_validate", {rootdir = build_root})
local runtime_module = import("toolchains.toolchain_runtime", {rootdir = build_root})
local lto_module = import("toolchains.toolchain_lto", {rootdir = build_root})
local memreport_module = import("toolchains.toolchain_memreport", {rootdir = build_root})
local linkguard_module = import("toolchains.toolchain_linkguard", {rootdir = build_root})

--- 获取工具链配置数据
---@return ToolchainsConfig
function get_toolchain_config()
    return config_module.get_toolchain_config()
end

--- 解析工具链上下文
---@param toolchain_config ToolchainsConfig|nil 工具链配置
---@return table context
function resolve_toolchain_context(toolchain_config)
    return config_module.resolve_toolchain_context(toolchain_config)
end

--- 解析当前工具链名称，并确保已设置
---@return string toolchain_name 工具链名称
function resolve_toolchain_name()
    return config_module.resolve_toolchain_name()
end

--- 追加构建参数到目标
---@param target target 目标对象
---@param kind string 参数类型（cflags/cxflags/asflags/ldflags）
---@param flags table|nil 参数列表
---@param force boolean 是否强制设置
---@return nil
function add_target_flags(target, kind, flags, force)
    return flags_module.add_target_flags(target, kind, flags, force)
end

--- 批量追加构建参数到目标
---@param target target 目标对象
---@param flags table|nil 参数集合（cflags/cxflags/asflags/ldflags）
---@param options table|nil 选项（force/include_ldflags）
---@return nil
function add_target_flag_set(target, flags, options)
    return flags_module.add_target_flag_set(target, flags, options)
end

--- 应用工具链默认值，并校验工具链路径
---@param toolchain_context table|nil 工具链上下文
---@return table context
function apply_defaults(toolchain_context)
    return config_module.apply_defaults(toolchain_context)
end

--- 解析平台 arch（工具链可覆盖，默认来自板级 CPU）
---@param toolchain_config ToolchainsConfig 工具链配置
---@param toolchain_name string 工具链名称
---@param board_arch string|nil 板级 arch
---@return string platform_arch
function resolve_platform_arch(toolchain_config, toolchain_name, board_arch)
    return config_module.resolve_platform_arch(toolchain_config, toolchain_name, board_arch)
end

--- 写入平台 arch
---@param platform_arch string 平台 arch
---@return nil
function apply_platform_arch(platform_arch)
    return config_module.apply_platform_arch(platform_arch)
end

--- 校验架构 traits
---@param traits table|nil 架构 traits
---@return nil
function validate_arch_traits(traits)
    return arch_module.validate_arch_traits(traits)
end

--- 将架构 traits 映射为工具链 flags
---@param toolchain_config ToolchainsConfig 工具链配置
---@param toolchain_name string 工具链名称
---@param traits table 架构 traits
---@return table flags
function map_arch_flags(toolchain_config, toolchain_name, traits)
    return arch_module.map_arch_flags(toolchain_config, toolchain_name, traits)
end

--- 校验工具链 flags 的结构与内容
---@param flags table|nil 工具链 flags 结构
---@return nil
function validate_flags(flags)
    return flags_module.validate_flags(flags)
end

--- 获取工具链 flags（包含 common 合并）
---@param toolchain_config ToolchainsConfig 工具链配置表
---@param toolchain_name string 工具链名称
---@return table flags 参数表（cflags/cxflags/asflags/ldflags）
function get_flags(toolchain_config, toolchain_name)
    return flags_module.get_flags(toolchain_config, toolchain_name)
end

--- 获取镜像转换工具配置
---@param toolchain_config ToolchainsConfig 工具链配置表
---@param toolchain_name string 工具链名称
---@return table|nil converters 镜像转换器（hex/bin）
function resolve_image_converters(toolchain_config, toolchain_name)
    return image_module.resolve_image_converters(toolchain_config, toolchain_name)
end

--- 执行镜像转换
---@param converter table 镜像转换器配置
---@param input string 输入 ELF 文件路径
---@param output string 输出文件路径
---@return nil
function run_image_convert(converter, input, output)
    return image_module.run_image_convert(converter, input, output)
end

--- 校验硬浮点参数是否被工具链支持
---@param target target 目标对象
---@param flags table|nil 编译参数列表
---@param toolchain_name string 工具链名称
---@return nil
function validate_hard_float_support(target, flags, toolchain_name)
    return validate_module.validate_hard_float_support(target, flags, toolchain_name)
end

--- 确保工具链完成检测
---@param toolchain_name string 工具链名称
---@return nil
function ensure_toolchain_checked(toolchain_name)
    return validate_module.ensure_toolchain_checked(toolchain_name)
end

--- 判断是否应将 BSP arch ldflags 注入链接阶段
---@param toolchain_config ToolchainsConfig 工具链配置表
---@param toolchain_name string|nil 工具链名称
---@return boolean include_ldflags
function should_include_arch_ldflags(toolchain_config, toolchain_name)
    return config_module.should_include_arch_ldflags(toolchain_config, toolchain_name)
end

--- 解析 semihosting 模式
---@param mode string|nil 模式（off/on）
---@return string normalized_mode
function resolve_semihosting_mode(mode)
    return runtime_module.resolve_semihosting_mode(mode)
end

--- 解析运行时注入信息
---@param toolchain_name string|nil 工具链名称
---@param semihosting_mode string|nil semihosting 模式
---@return table payload
function resolve_runtime_payload(toolchain_name, semihosting_mode)
    return runtime_module.resolve_runtime_payload(toolchain_name, semihosting_mode)
end

--- 解析 LTO 模式
---@param mode string|nil 模式（auto/on/off）
---@return string normalized_mode
function resolve_lto_mode(mode)
    return lto_module.resolve_lto_mode(mode)
end

--- 解析 LTO 策略计划
---@param toolchain_name string|nil 工具链名称
---@param lto_mode string|nil LTO 模式（auto/on/off）
---@return table plan
function resolve_lto_plan(toolchain_name, lto_mode)
    return lto_module.resolve_lto_plan(toolchain_name, lto_mode)
end

--- 生成构建后内存分布报告
---@param target target 目标对象
---@param context table 上下文
---@return table report 报告数据
function build_memory_distribution_report(target, context)
    return memreport_module.build_memory_distribution_report(target, context)
end

--- 校验 OM 链接契约
---@param target target 目标对象
---@param context table 上下文
---@return table report 校验结果
function verify_om_link_contract(target, context)
    return linkguard_module.verify_om_link_contract(target, context)
end

--- 返回工具链工具库
---@return ToolchainLib
return {
    get_toolchain_config = get_toolchain_config,
    resolve_toolchain_context = resolve_toolchain_context,
    resolve_toolchain_name = resolve_toolchain_name,
    add_target_flags = add_target_flags,
    add_target_flag_set = add_target_flag_set,
    apply_defaults = apply_defaults,
    resolve_platform_arch = resolve_platform_arch,
    apply_platform_arch = apply_platform_arch,
    validate_arch_traits = validate_arch_traits,
    map_arch_flags = map_arch_flags,
    validate_flags = validate_flags,
    get_flags = get_flags,
    resolve_image_converters = resolve_image_converters,
    run_image_convert = run_image_convert,
    validate_hard_float_support = validate_hard_float_support,
    ensure_toolchain_checked = ensure_toolchain_checked,
    should_include_arch_ldflags = should_include_arch_ldflags,
    resolve_semihosting_mode = resolve_semihosting_mode,
    resolve_runtime_payload = resolve_runtime_payload,
    resolve_lto_mode = resolve_lto_mode,
    resolve_lto_plan = resolve_lto_plan,
    build_memory_distribution_report = build_memory_distribution_report,
    verify_om_link_contract = verify_om_link_contract,
}
