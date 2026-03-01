--- @file oh_my_robot/build/rules/context.lua
--- @brief OM 上下文规则定义
--- @details 注入架构/工具链参数并执行构建前校验。
local om_root = path.join(os.scriptdir(), "..", "..")
local modules_root = path.join(om_root, "build", "modules")

--- @rule oh_my_robot.context
--- @brief 注入上下文编译/链接参数
--- @details 配置阶段读取上下文并向目标注入编译标志。
rule("oh_my_robot.context")
    --- 规则配置阶段读取上下文并注入编译标志
    ---@param target target 目标对象
    on_config(function(target)
        -- 读取上下文与工具链配置
        local toolchain_lib = import("toolchain_lib", {rootdir = modules_root})
        local oh_my_robot = import("oh-my-robot", {rootdir = modules_root})
        local om_version = import("om_version", {rootdir = modules_root})
        local config = import("core.project.config")
        config.load()
        oh_my_robot.sync_context_from_config()
        local context = oh_my_robot.get_context()
        local arch_flags = context.arch_flags
        local toolchain_flags = context.toolchain_flags
        local framework_version = om_version.resolve_framework_version()
        local toolchain_config = toolchain_lib.get_toolchain_config()
        local include_arch_ldflags = toolchain_lib.should_include_arch_ldflags(toolchain_config, context.toolchain_name)
        local semihosting_mode = toolchain_lib.resolve_semihosting_mode(config.get("semihosting"))
        local lto_mode = toolchain_lib.resolve_lto_mode(nil)
        local lto_plan = toolchain_lib.resolve_lto_plan(context.toolchain_name, lto_mode)
        target:add("defines", string.format("FRAMEWORK_VERSION=\"%s\"", framework_version), {public = true})
        -- 注入架构参数与工具链参数
        toolchain_lib.add_target_flag_set(target, arch_flags, {
            force = true,
            include_ldflags = include_arch_ldflags and (target:kind() == "binary"),
        })
        toolchain_lib.add_target_flag_set(target, toolchain_flags, {
            force = false,
            include_ldflags = (target:kind() == "binary"),
        })
        if lto_plan.enabled then
            -- LTO 参数采用工具链收敛计划统一注入，确保跨工具链行为可预测。
            toolchain_lib.add_target_flags(target, "cflags", lto_plan.cflags, false)
            toolchain_lib.add_target_flags(target, "cxflags", lto_plan.cxflags, false)
            if target:kind() == "binary" then
                toolchain_lib.add_target_flags(target, "ldflags", lto_plan.ldflags, false)
            end
        end
        if target:kind() == "binary" then
            local runtime_payload = toolchain_lib.resolve_runtime_payload(
                context.toolchain_name,
                semihosting_mode
            )
            toolchain_lib.add_target_flag_set(target, runtime_payload.flags, {
                force = false,
                include_ldflags = true,
            })
            if runtime_payload.sources and #runtime_payload.sources > 0 then
                target:add("files", runtime_payload.sources)
            end
        end
        -- 校验工具链与硬浮点支持
        toolchain_lib.ensure_toolchain_checked(context.toolchain_name)
        toolchain_lib.validate_hard_float_support(target, arch_flags.cflags, context.toolchain_name)
    end)
    --- 构建后输出构建模式与内存分布摘要
    ---@param target target 目标对象
    after_build(function(target)
        if target:kind() ~= "binary" then
            return
        end
        local oh_my_robot = import("oh-my-robot", {rootdir = modules_root})
        oh_my_robot.om_print_build_profile(target)
        local report = oh_my_robot.om_verify_link_contract(target)
        if not report or report.ok ~= true then
            raise((report and report.reason) or "[oh-my-robot] link contract check failed")
        end
    end)
rule_end()
