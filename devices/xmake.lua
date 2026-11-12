--- @target tar_awapi_devices
--- @brief 器件库公共头目标
target("tar_awapi_devices")
    add_rules("oh_my_robot.project_cfg")
    set_kind("headeronly")
    add_deps("tar_awapi_driver", {public = true})
    add_includedirs("include", {public = true})
target_end()

--- @target tar_awdevices
--- @brief 器件库静态库（OMR-15 staging；OMR-16 抽取为独立仓库）
target("tar_awdevices")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_rules("oh_my_robot.context")
    add_deps("tar_awapi_devices", {public = true})
    add_files("src/**.c")
target_end()
