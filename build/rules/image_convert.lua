--- @file oh_my_robot/build/rules/image_convert.lua
--- @brief OM 镜像转换规则定义
--- @details 构建后生成 hex/bin 镜像并在清理时移除。
local om_root = path.join(os.scriptdir(), "..", "..")
local modules_root = path.join(om_root, "build", "modules")

--- @rule oh_my_robot.image_convert
--- @brief 生成 hex/bin 镜像
--- @details 在构建完成后执行镜像转换。
rule("oh_my_robot.image_convert")
    --- 构建后生成 hex/bin 镜像
    ---@param target target 目标对象
    after_build(function(target)
        -- 解析工具链镜像转换器并执行转换
        local oh_my_robot = import("oh-my-robot", {rootdir = modules_root})
        local context = oh_my_robot.get_context()
        local toolchain_lib = import("toolchain_lib", {rootdir = modules_root})
        local toolchain_config = toolchain_lib.get_toolchain_config()
        local converters = toolchain_lib.resolve_image_converters(toolchain_config, context.toolchain_name)
        if not converters then
            return
        end
        local elf_file = target:targetfile()
        local output_dir = target:targetdir()
        local basename = target:basename()
        local hex_file = path.join(output_dir, basename .. ".hex")
        local bin_file = path.join(output_dir, basename .. ".bin")
        toolchain_lib.run_image_convert(converters.hex, elf_file, hex_file)
        toolchain_lib.run_image_convert(converters.bin, elf_file, bin_file)
    end)
    --- 清理时删除生成的 hex/bin 镜像
    ---@param target target 目标对象
    after_clean(function(target)
        local output_dir = target:targetdir()
        local basename = target:basename()
        local hex_file = path.join(output_dir, basename .. ".hex")
        local bin_file = path.join(output_dir, basename .. ".bin")
        if os.isfile(hex_file) then
            os.tryrm(hex_file)
        end
        if os.isfile(bin_file) then
            os.tryrm(bin_file)
        end
    end)
rule_end()
