--- @file oh_my_robot/lib/xmake.lua
--- @brief OM lib 子库构建脚本
--- @details 按单一责任原则拆分 core/algorithm/drivers/ipc/async/systems 等独立静态库。

--- @target tar_awcore
--- @brief AW 核心静态库
--- @details 提供核心基础能力（类型、错误码、atomic）。
target("tar_awcore")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_rules("oh_my_robot.context", {public = true})
    add_includedirs("include", {public = true})
    add_files("source/core/**.c")
    -- om_system_startup.c 移除：它调用 osal，由 tar_awkernel 编译（保持本目标 OS 无关）
    remove_files("source/core/om_system_startup.c")
    -- om_main.c 移除：框架默认 main（弱符号）是 binary 级入口，经 oh_my_robot.selfreg
    -- 规则直编进 binary，不入静态归档（归档消费者/宿主工程自带 main，见 ADR-0011）
    remove_files("source/core/om_main.c")
target_end()

--- @target tar_awkernel
--- @brief 内核层目标：启动编排（init 子系统需要 osal 的部分）
--- @details 编译 source/core/om_system_startup.c（提供 om_system_startup()），
---          文件与 om_init.c 同目录（同属 init 子系统）但在此编译：
---          它调用 osal，而 tar_os→tar_awapi_osal→tar_awcore 已成链，
---          并入 tar_awcore 会造成构建环；同时保持 tar_awcore OS 无关。
target("tar_awkernel")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_rules("oh_my_robot.context")
    add_deps("tar_awcore", {public = true})
    add_deps("tar_awapi_osal", {public = true})
    add_deps("tar_os", {public = true})
    add_includedirs("include", {public = true})
    add_files("source/core/om_system_startup.c")
target_end()

--- @target tar_awapi_osal
--- @brief OSAL API 头文件接口
--- @details 为 OSAL 目标提供公共头文件。
target("tar_awapi_osal")
    add_rules("oh_my_robot.project_cfg")
    set_kind("headeronly")
    add_deps("tar_awcore", {public = true})
    add_includedirs("osal/include", {public = true})
target_end()

--- @target tar_awosal_probe
--- @brief OSAL 头文件探针目标
--- @details 聚合包含 OSAL 公共头，确保头文件参与默认 clang-tidy 分析。
target("tar_awosal_probe")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_deps("tar_awapi_osal")
    add_rules("oh_my_robot.context")
    add_files("osal/check/osal_headers_probe.c")
target_end()

--- @target tar_awdatastruct
--- @brief 数据结构静态库
--- @details 独立数据结构组件，可依赖 osal 临界区等 OS 原语。
target("tar_awdatastruct")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_rules("oh_my_robot.context")
    add_deps("tar_awcore", {public = true})
    add_deps("tar_awapi_osal", {public = true})
    add_includedirs("data_struct/include", {public = true})
    add_files("data_struct/src/**.c")
target_end()

--- @target tar_awapi_sync
--- @brief Sync API 头文件接口
--- @details 为同步原语目标提供公共头文件。
target("tar_awapi_sync")
    add_rules("oh_my_robot.project_cfg")
    set_kind("headeronly")
    add_includedirs("sync/include", {public = true})
target_end()

--- @target tar_awapi_ipc
--- @brief IPC API 头文件接口
--- @details 为跨上下文数据传输层提供公共头文件。
target("tar_awapi_ipc")
    add_rules("oh_my_robot.project_cfg")
    set_kind("headeronly")
    add_deps("tar_awdatastruct", {public = true})
    add_deps("tar_awapi_osal", {public = true})
    add_includedirs("ipc/include", {public = true})
target_end()

--- @target tar_awapi_async
--- @brief Async API 头文件接口
--- @details 为异步执行基座提供公共头文件与基础依赖。
target("tar_awapi_async")
    add_rules("oh_my_robot.project_cfg")
    set_kind("headeronly")
    add_deps("tar_awcore", {public = true})
    add_deps("tar_awapi_osal", {public = true})
    add_deps("tar_awapi_sync", {public = true})
    add_deps("tar_awapi_ipc", {public = true})
    add_includedirs("async/include", {public = true})
target_end()

--- @target tar_awapi_driver
--- @brief PAL API 头文件接口
--- @details 为驱动层提供公共头文件与依赖。
target("tar_awapi_driver")
    add_rules("oh_my_robot.project_cfg")
    set_kind("headeronly")
    add_deps("tar_awdatastruct", {public = true})
    add_includedirs("drivers/include", {public = true})
    add_includedirs("services/include", {public = true}) -- drivers→services 单向依赖（ADR-0016，当前仅 log）
    add_deps("tar_awapi_osal", {public = true})
    add_deps("tar_awapi_sync", {public = true})
    add_deps("tar_awapi_ipc", {public = true})
    add_deps("tar_awapi_async", {public = true})
target_end()

--- @target tar_awalgo
--- @brief 算法静态库
--- @details 提供算法相关实现。
target("tar_awalgo")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_rules("oh_my_robot.context")
    add_includedirs("algorithm/include", {public = true})
    add_deps("tar_awcore", {public = true})
    add_files("algorithm/src/**.c")
target_end()

--- @target tar_awdrivers
--- @brief 驱动静态库
--- @details 聚合驱动层实现与依赖。
target("tar_awdrivers")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_deps("tar_awapi_driver", {public = true})
    add_deps("tar_awasync", {public = true})
    add_rules("oh_my_robot.context")
    add_files("drivers/src/**.c")
target_end()

--- @target tar_awasync
--- @brief 异步执行基座静态库
--- @details 提供执行器、工作队列、延时队列与生命周期最小实现。
target("tar_awasync")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_rules("oh_my_robot.context")
    add_deps("tar_awapi_async", {public = true})
    local async_sources = os.files("async/src/**.c")
    if #async_sources > 0 then
        add_files(table.unpack(async_sources))
    end
target_end()

--- @target tar_awsystems
--- @brief 系统静态库
--- @details 提供系统级实现与公共头文件。
target("tar_awsystems")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_deps("tar_awdatastruct", {public = true})
    add_includedirs("include", {public = true})
    add_includedirs("systems/include", {public = true})
    add_files("systems/src/**.c")
target_end()

-- 配置注入 + 变更感知：全部库目标挂接 project_cfg 规则（appcfg/boardcfg 注入
-- 与二进制目标一致——配置分层一致性）与配置状态标记 depfile（同规则文件状态机）
-- 根治"旗标/配置变更不重编、静态库成员陈旧、库源配置不一致"（2026-09-05 审计实证）
local _cfg_rule_targets = {
    "tar_awalgo", "tar_awapi_async", "tar_awapi_driver", "tar_awapi_ipc", "tar_awapi_osal",
    "tar_awapi_sync", "tar_awasync", "tar_awcore", "tar_awdatastruct", "tar_awdrivers",
    "tar_awkernel", "tar_awosal_probe", "tar_awsystems",
}
-- 规则与 depfile 独立挂接（rule 文件的全局函数不跨文件可见——depfile 直接本地实现）
local _cfg_state_file = path.join(os.projectdir(), ".xmake", "om_cfg_state")
for _, _name in ipairs(_cfg_rule_targets) do
    local _t = target(_name)
    if _t then

        _t:add("depfiles", _cfg_state_file)
    end
end
