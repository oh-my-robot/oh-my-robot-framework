--- @file oh_my_robot/platform/ipc/xmake.lua
--- @brief IPC 构建脚本
--- @details 负责跨上下文数据传输通道的编译注入。

local ipc_root = os.scriptdir()
local lib_root = path.join(ipc_root, "..", "..", "lib")
local ipc_source_glob = path.join(lib_root, "ipc", "src", "*.c")

--- @target tar_ipc
--- @brief IPC 静态库
--- @details 注入跨上下文数据通道实现。
target("tar_ipc")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_deps("tar_awapi_ipc", {public = true})
    add_deps("tar_awcore", {public = true})
    add_deps("tar_osal", {public = true})
    add_rules("oh_my_robot.context")
    add_files(ipc_source_glob)
target_end()
