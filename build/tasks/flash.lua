--- @task flash
--- @brief J-Link 烧录任务
--- @details 通过 J-Link Commander 对目标进行烧录。
task("flash")
    set_menu {
        usage = "xmake flash [options]",
        description = "Flash firmware via J-Link",
        options = {
            {"d", "device", "kv", "STM32F407IG", "J-Link device name"},
            {"i", "interface", "kv", "swd", "J-Link interface"},
            {"s", "speed", "kv", "4000", "J-Link speed (kHz)"},
            {"f", "firmware", "kv", nil, "Firmware file (.hex/.elf)"},
            {"H", "prefer_hex", "kv", nil, "Prefer HEX when auto resolving firmware (true/false)"},
            {"p", "program", "kv", nil, "J-Link executable path"},
            {"t", "target", "kv", "robot_project", "Target name"},
            {"r", "reset", "kv", "true", "Reset before/after flash"},
            {"g", "run", "kv", "true", "Run after flash"},
            {"n", "native_output", "kv", nil, "Show native flasher CLI output (true/false)"},
        }
    }
    on_run(function()
        -- 脚本域内定义流水线与工具函数，避免描述域使用脚本接口
        local build_root = path.join(os.scriptdir(), "..")
        local modules_root = path.join(build_root, "modules")

        --- 解析布尔值
        ---@param value string|boolean|nil 输入值
        ---@param default_value boolean 默认值
        ---@return boolean result
        local function resolve_bool(value, default_value)
            if value == nil then
                return default_value
            end
            if type(value) == "boolean" then
                return value
            end
            local normalized = tostring(value):lower()
            if normalized == "1" or normalized == "true" or normalized == "yes" then
                return true
            end
            if normalized == "0" or normalized == "false" or normalized == "no" then
                return false
            end
            return default_value
        end

        --- 获取绝对路径
        ---@param file_path string 文件路径
        ---@return string absolute_path
        local function resolve_path(file_path)
            if path.is_absolute(file_path) then
                return file_path
            end
            return path.join(os.projectdir(), file_path)
        end

        -- normalize extension (lowercase, with leading dot)
        local function normalize_extension(file_path)
            local ext = path.extension(file_path) or ""
            ext = ext:lower()
            if ext ~= "" and ext:sub(1, 1) ~= "." then
                ext = "." .. ext
            end
            return ext
        end

        -- ensure firmware path has explicit extension
        local function ensure_firmware_extension(file_path)
            local ext = normalize_extension(file_path)
            if ext == ".elf" or ext == ".hex" or ext == ".bin" then
                return
            end
            raise("firmware must include explicit extension: .elf/.hex/.bin")
        end

        -- read config scalar value
        local function read_config_scalar(value)
            if type(value) == "table" then
                return value[1]
            end
            return value
        end

        -- normalize toolchain name (strip bracket suffix)
        local function normalize_toolchain_name(name)
            if not name or name == "" then
                return name
            end
            local base = name:match("^(.-)%[")
            return base or name
        end

        --- 读取预设中的 J-Link 配置
        ---@return table|nil preset
        local function load_jlink_preset()
            local oh_my_robot = import("oh-my-robot", {rootdir = modules_root})
            local preset = oh_my_robot.get_preset()
            if not preset or type(preset) ~= "table" then
                return nil
            end
            local flash_preset = preset.flash
            if type(flash_preset) ~= "table" then
                return nil
            end
            if type(flash_preset.jlink) == "table" then
                return flash_preset.jlink
            end
            return flash_preset
        end

        -- load toolchain preset name
        local function load_toolchain_preset()
            local oh_my_robot = import("oh-my-robot", {rootdir = modules_root})
            local preset = oh_my_robot.get_preset()
            local toolchain_default = preset and preset.toolchain_default or nil
            return toolchain_default and toolchain_default.name or nil
        end

        --- 读取预设字段
        ---@param preset table|nil 预设表
        ---@param key string 字段名
        ---@return any value
        local function read_preset_value(preset, key)
            if not preset then
                return nil
            end
            local value = preset[key]
            if value == nil or value == "" then
                return nil
            end
            return value
        end

        --- 解析目标输出文件
        ---@param target_name string 目标名称
        ---@param prefer_hex boolean 是否优先使用 HEX
        ---@return string file_path
        local function resolve_target_output(target_name, prefer_hex)
            local project = import("core.project.project")
            project.load_targets()
            local target = project.target(target_name)
            if not target then
                raise("target not found: " .. target_name)
            end
            local target_dir = target:targetdir()
            local basename = target:basename()
            local elf_file = target:targetfile()
            local elf_ext = normalize_extension(elf_file)
            local hex_file = path.join(target_dir, basename .. ".hex")
            if prefer_hex and os.isfile(hex_file) then
                return hex_file
            end
            if os.isfile(elf_file) then
                if elf_ext == "" then
                    raise("target output missing extension: please set filename to .elf")
                end
                return elf_file
            end
            if os.isfile(hex_file) then
                return hex_file
            end
            raise("flash file not found: please build target and enable oh_my_robot.image_convert")
        end

        --- 解析 J-Link 可执行程序
        ---@param program string|nil 用户指定路径
        ---@return string program_path
        local function resolve_jlink_program(program)
            if program and program ~= "" then
                local abs = resolve_path(program)
                if os.isexec(abs) then
                    return abs
                end
                raise("jlink program not executable: " .. abs)
            end
            local find_tool = import("lib.detect.find_tool")
            local tool = find_tool("JLink", {paths = {}})
            if tool and tool.program and os.isexec(tool.program) then
                return tool.program
            end
            tool = find_tool("JLink.exe", {paths = {}})
            if tool and tool.program and os.isexec(tool.program) then
                return tool.program
            end
            raise("J-Link program not found: please pass --program=<path>")
        end

        --- 生成 J-Link 命令文件内容
        ---@param device string 设备名
        ---@param interface string 接口类型
        ---@param speed string|number 速度
        ---@param firmware string 烧录文件
        ---@param reset_after boolean 是否重置
        ---@param run_after boolean 是否运行
        ---@return string content
        local function build_jlink_command(device, interface, speed, firmware, reset_after, run_after)
            local lines = {}
            lines[#lines + 1] = "device " .. device
            lines[#lines + 1] = "if " .. interface
            lines[#lines + 1] = "speed " .. tostring(speed)
            if reset_after then
                lines[#lines + 1] = "r"
            end
            lines[#lines + 1] = "halt"
            lines[#lines + 1] = "loadfile " .. firmware
            if reset_after then
                lines[#lines + 1] = "r"
            end
            if run_after then
                lines[#lines + 1] = "g"
            end
            lines[#lines + 1] = "q"
            return table.concat(lines, "\n") .. "\n"
        end

        --- 写入 J-Link 命令文件
        ---@param build_dir string 构建目录
        ---@param content string 命令内容
        ---@return string command_path
        local function write_jlink_command_file(build_dir, content)
            local flash_dir = path.join(build_dir, "oh-my-robot", "flash")
            os.mkdir(flash_dir)
            local command_path = path.join(flash_dir, "jlink_flash.jlink")
            io.writefile(command_path, content)
            return command_path
        end

        --- 构建上下文
        ---@return table context
        local function build_context()
            local config = import("core.project.config")
            local option = import("core.base.option")
            config.load()
            local preset_toolchain = load_toolchain_preset()
            local config_toolchain = read_config_scalar(config.get("toolchain"))
            if preset_toolchain and config_toolchain then
                local preset_base = normalize_toolchain_name(preset_toolchain)
                local config_base = normalize_toolchain_name(config_toolchain)
                if preset_base ~= config_base then
                    print("[oh-my-robot] warning: preset toolchain (" .. preset_toolchain
                        .. ") differs from config toolchain (" .. config_toolchain
                        .. "). Using config.")
                end
            end
            return {
                config = config,
                option = option,
                preset = load_jlink_preset(),
            }
        end

        --- Step: 解析固件路径与选择策略
        ---@param ctx table 上下文
        ---@return table ctx
        local function step_resolve_firmware(ctx)
            local option = ctx.option
            local preset = ctx.preset
            ctx.target_name = option.get("target")
                or read_preset_value(preset, "target")
                or "robot_project"
            ctx.prefer_hex = resolve_bool(
                option.get("prefer_hex") or read_preset_value(preset, "prefer_hex"),
                true
            )
            local file_override = option.get("firmware")
                or read_preset_value(preset, "firmware")
                or read_preset_value(preset, "file")
            if file_override then
                ensure_firmware_extension(file_override)
            end
            ctx.firmware = file_override and resolve_path(file_override)
                or resolve_target_output(ctx.target_name, ctx.prefer_hex)
            if not os.isfile(ctx.firmware) then
                raise("flash file not found: " .. ctx.firmware)
            end
            return ctx
        end

        --- Step: 解析设备、接口与执行器
        ---@param ctx table 上下文
        ---@return table ctx
        local function step_resolve_driver(ctx)
            local option = ctx.option
            local preset = ctx.preset
            ctx.device = option.get("device")
                or read_preset_value(preset, "device")
                or "STM32F407IG"
            ctx.interface = option.get("interface")
                or read_preset_value(preset, "interface")
                or "swd"
            ctx.speed = option.get("speed")
                or read_preset_value(preset, "speed")
                or "4000"
            ctx.reset_after = resolve_bool(
                option.get("reset") or read_preset_value(preset, "reset"),
                true
            )
            ctx.run_after = resolve_bool(
                option.get("run") or read_preset_value(preset, "run"),
                true
            )
            ctx.native_output = resolve_bool(
                option.get("native_output") or read_preset_value(preset, "native_output"),
                false
            )
            ctx.program = resolve_jlink_program(
                option.get("program") or read_preset_value(preset, "program")
            )
            return ctx
        end

        --- Step: 生成命令并写入临时文件
        ---@param ctx table 上下文
        ---@return table ctx
        local function step_prepare_command(ctx)
            local command_content = build_jlink_command(
                ctx.device,
                ctx.interface,
                ctx.speed,
                ctx.firmware,
                ctx.reset_after,
                ctx.run_after
            )
            ctx.command_path = write_jlink_command_file(ctx.config.builddir(), command_content)
            return ctx
        end

        --- Step: 执行烧录
        ---@param ctx table 上下文
        ---@return table ctx
        local function step_execute(ctx)
            local command_arguments = {"-CommandFile", ctx.command_path}
            if ctx.native_output then
                os.execv(ctx.program, command_arguments)
            else
                os.runv(ctx.program, command_arguments)
            end
            -- 输出实际使用的可执行文件路径与工具链信息
            local toolchain_name = ctx.config and ctx.config.get("toolchain") or "unknown"
            print("[oh-my-robot] flash program: " .. tostring(ctx.program))
            print("[oh-my-robot] flash firmware: " .. tostring(ctx.firmware))
            print("[oh-my-robot] flash toolchain: " .. tostring(toolchain_name))
            print("[oh-my-robot] flash native output: " .. tostring(ctx.native_output))
            return ctx
        end

        local ctx = build_context()
        local steps = {
            step_resolve_firmware,
            step_resolve_driver,
            step_prepare_command,
            step_execute,
        }
        for _, step in ipairs(steps) do
            ctx = step(ctx)
        end
    end)
