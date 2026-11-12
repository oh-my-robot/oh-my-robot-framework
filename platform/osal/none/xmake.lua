--- @file oh_my_robot/platform/osal/none/xmake.lua
--- @brief osal-none（裸机单执行流）OSAL 构建脚本
--- @details 定义 tar_awapi_osal 的端口注入与 tar_os 静态库。
--- 无 RTOS 内核/portable：tar_os 编端口族文件（osal_*_none.c）+ 按构建上下文
--- arch 解析的移植实现（portable/<arch>/osal_none_arch.c，缺失即报错）；
--- host 测试的 arch 实现属测试基础设施（各 host 工程自带），不入本目标。

local none_root = os.scriptdir()

--- 解析移植实现路径（portable/<arch>/osal_none_arch.c）
---@param arch string 架构名称
---@return string|nil arch_file 移植实现路径
---@return string|nil err_msg 错误信息
local function resolve_arch_file(arch)
    if not arch or arch == "" then
        return nil, "osal-none: build context arch is empty"
    end
    local arch_file = path.join(none_root, "portable", arch, "osal_none_arch.c")
    if not os.isfile(arch_file) then
        return nil, "osal-none arch port not found: " .. arch_file
    end
    return arch_file, nil
end

--- @target tar_awapi_osal
--- @brief os=none 端口注入：OM_OSAL_PORT 选择 + portdef 头路径
--- @details 公共宏经依赖链传播到全部 osal 消费单元（lib/async workqueue
--- 坍缩分支、lib/drivers flash os 轴分支等据此走无 OS 语义）。
target("tar_awapi_osal")
    add_rules("oh_my_robot.project_cfg")
    add_cxflags("-DOM_OSAL_PORT=3", {public = true}) -- OSAL_PORT_NONE
    add_includedirs(none_root, {public = true}) -- om_osal_portdef.h
target_end()

--- @target tar_os
--- @brief osal-none 端口静态库
--- @details 提供 osal_malloc/free 等 OSAL 面实现（链接契约校验对象）。
target("tar_os")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_rules("oh_my_robot.context")
    add_deps("tar_awapi_osal", {public = false})
    add_includedirs(none_root, {public = true}) -- om_osal_portdef.h
    add_files("osal_*_none.c")
    --- 配置阶段注入移植实现（按构建上下文 arch）
    ---@param target target 目标对象
    on_load(function(target)
        local oh_my_robot = import("oh-my-robot")
        local context = oh_my_robot.get_context()
        local arch_file, err = resolve_arch_file(context.arch)
        if not arch_file then
            raise(err)
        end
        target:add("files", arch_file)
    end)
target_end()
