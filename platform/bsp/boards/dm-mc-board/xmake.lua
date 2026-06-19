--- @file oh_my_robot/platform/bsp/boards/dm-mc-board/xmake.lua
--- @brief dm-mc-board BSP 构建脚本

target("tar_board")
    set_kind("static")
    add_rules("oh_my_robot.context")
    add_deps("tar_awapi_driver", {public = true})
    add_deps("tar_awdrivers", {public = true})
    on_load(function(target)
        local board_root = os.scriptdir()
        local bsp_root = path.join(board_root, "..", "..")
        local om_root = path.join(bsp_root, "..", "..")
        local modules_root = path.join(om_root, "build", "modules")

        local bsp = import("bsp", {rootdir = bsp_root})
        local om_robot = import("oh-my-robot", {rootdir = modules_root})

        local context = om_robot.get_context()
        local inputs = bsp.get_board_build_inputs(context.board_name)
        if inputs.includedirs and #inputs.includedirs > 0 then
            target:add("includedirs", inputs.includedirs, {public = false})
        end
        if inputs.defines and #inputs.defines > 0 then
            target:add("defines", inputs.defines)
        end
        if inputs.sources and #inputs.sources > 0 then
            target:add("files", inputs.sources)
        end
        if inputs.headerfiles and #inputs.headerfiles > 0 then
            target:add("headerfiles", inputs.headerfiles)
        end
        if inputs.extrafiles and #inputs.extrafiles > 0 then
            target:add("extrafiles", inputs.extrafiles)
        end
    end)
target_end()
