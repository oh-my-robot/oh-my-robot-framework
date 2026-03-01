--- @file oh_my_robot/build/xmake.lua
--- @brief OM 构建入口
--- @details 负责加载构建配置、规则、工具链与模块入口。
set_xmakever("3.0.7")

add_moduledirs("modules")
includes("toolchains")
add_rules("oh_my_robot.toolchain_bootstrap")
includes("config")
includes("rules")
includes("tasks")

includes("../lib")
includes("../platform/osal")
includes("../platform/sync")
includes("../platform/bsp")

--- @target tar_oh_my_robot
--- @brief OM 聚合目标
--- @details 仅做依赖聚合与传播，不直接产出静态库。
target("tar_oh_my_robot")
    set_kind("phony")
    add_deps("tar_awcore", {public = true})
    add_deps("tar_awosal_probe", {public = true})
    add_deps("tar_awalgo", {public = true})
    add_deps("tar_awdrivers", {public = true})
    add_deps("tar_awasync", {public = true})
    add_deps("tar_awsystems", {public = true})
    add_deps("tar_bsp", {public = true})
    add_deps("tar_osal", {public = true})
    add_deps("tar_sync", {public = true})
target_end()
