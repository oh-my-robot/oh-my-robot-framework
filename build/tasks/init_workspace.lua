--- @task init_workspace
--- @brief 初始化轻量项目壳
--- @details
--- 为当前 `oh-my-robot` checkout 生成一个最小可用的顶层工程目录，
--- 解决 worktree 只有框架源码、缺少项目入口与项目级 `om_preset.lua` 的问题。
task("init_workspace")
    set_menu {
        usage = "xmake init_workspace [options]",
        description = "Generate a lightweight project shell for the current oh-my-robot checkout",
        options = {
            {"o", "output", "kv", nil, "Workspace output directory"},
            {"f", "framework", "kv", nil, "Framework root path (default: current checkout)"},
            {"p", "preset", "kv", nil, "Preset source file to copy as om_preset.lua"},
            {"n", "project_name", "kv", nil, "Project name (default: output directory name)"},
            {"t", "target", "kv", "robot_project", "Top-level target name"},
            {"e", "entry", "kv", "samples/motor/p1010b/main.c", "Entry source path relative to framework root"},
            {"F", "force", "kv", "false", "Overwrite existing generated files (true/false)"},
        }
    }
    on_run(function()
        local option = import("core.base.option")
        local FRAMEWORK_ALIAS = "oh-my-robot"

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

        --- 获取路径末段名称
        ---@param input_path string 路径
        ---@return string name 名称
        local function basename(input_path)
            local normalized = (input_path or ""):gsub("[/\\]+$", "")
            local name = normalized:match("([^/\\]+)$")
            return name or normalized
        end

        --- 将路径规范化为绝对路径
        ---@param input_path string 输入路径
        ---@param base_dir string 基准目录
        ---@return string absolute_path 绝对路径
        local function resolve_absolute_path(input_path, base_dir)
            if path.is_absolute(input_path) then
                return path.absolute(input_path)
            end
            return path.absolute(input_path, base_dir)
        end

        --- 规范化路径字符串，便于比较是否指向同一目录
        ---@param input_path string 输入路径
        ---@return string normalized_path 规范化后的绝对路径
        local function canonicalize_path(input_path)
            local normalized_path = path.translate(path.absolute(input_path)):gsub("[/\\]+$", "")
            if is_host("windows") then
                normalized_path = normalized_path:lower()
            end
            return normalized_path
        end

        --- 断言目录是否允许生成项目壳
        ---@param output_dir string 输出目录
        ---@param framework_root string 框架根目录
        ---@param force boolean 是否允许覆盖
        ---@return nil
        local function assert_output_dir_writable(output_dir, framework_root, force)
            if canonicalize_path(output_dir) == canonicalize_path(framework_root) then
                raise("workspace output cannot be the framework root itself: " .. output_dir)
            end
            if not os.isdir(output_dir) then
                return
            end
            local entries = os.files(path.join(output_dir, "**"))
            if #entries == 0 or force then
                return
            end
            raise("workspace output already contains files: " .. output_dir .. ". Pass --force=true to overwrite generated files.")
        end

        --- 将路径字符串转换为 Lua 长字符串字面量可接受的样式
        ---@param raw_path string 原始路径
        ---@return string normalized_path 规范化路径
        local function normalize_lua_path(raw_path)
            return (raw_path or ""):gsub("\\", "/")
        end

        --- 删除项目壳中的框架别名链接
        ---@param alias_path string 别名路径
        ---@return nil
        local function remove_framework_alias(alias_path)
            if not os.isfile(alias_path) and not os.isdir(alias_path) then
                return
            end
            if is_host("windows") then
                if os.isdir(alias_path) then
                    os.runv("cmd", {"/c", "rmdir", path.translate(alias_path)})
                else
                    os.runv("cmd", {"/c", "del", "/f", "/q", path.translate(alias_path)})
                end
                return
            end
            os.tryrm(alias_path)
        end

        --- 在项目壳中创建稳定的框架别名链接
        ---@param output_dir string 输出目录
        ---@param framework_root string 框架根目录
        ---@param force boolean 是否允许覆盖
        ---@return string alias_path 别名路径
        local function create_framework_alias(output_dir, framework_root, force)
            local alias_path = path.join(output_dir, FRAMEWORK_ALIAS)
            if os.isfile(alias_path) or os.isdir(alias_path) then
                if not force then
                    raise("framework alias already exists: " .. alias_path .. ". Pass --force=true to recreate it.")
                end
                remove_framework_alias(alias_path)
            end

            if is_host("windows") then
                os.runv("cmd", {"/c", "mklink", "/J", path.translate(alias_path), path.translate(framework_root)})
            else
                os.runv("ln", {"-s", framework_root, alias_path})
            end

            if not os.isdir(alias_path) then
                raise("framework alias creation failed: " .. alias_path)
            end
            return alias_path
        end

        --- 解析 preset 源文件路径
        ---@param preset_option string|nil 命令行 preset 参数
        ---@param framework_root string 框架根目录
        ---@return string preset_source_path preset 源文件路径
        local function resolve_preset_source_path(preset_option, framework_root)
            if preset_option and preset_option ~= "" then
                local preset_source_path = resolve_absolute_path(preset_option, os.projectdir())
                if not os.isfile(preset_source_path) then
                    raise("preset source not found: " .. preset_source_path)
                end
                return preset_source_path
            end

            local example_path = path.join(framework_root, "om_preset.example.lua")
            if not os.isfile(example_path) then
                raise("om_preset example not found: " .. example_path)
            end
            return example_path
        end

        --- 在项目根生成 om_preset.lua（首次创建或按 force 覆盖）
        ---@param output_dir string 输出目录
        ---@param preset_source_path string preset 源文件路径
        ---@param force boolean 是否允许覆盖
        ---@return string preset_path 项目根 preset 路径
        ---@return string status 处理结果 created|overwritten|kept
        local function ensure_project_preset(output_dir, preset_source_path, force)
            local preset_path = path.join(output_dir, "om_preset.lua")
            local existed_before = os.isfile(preset_path)
            if existed_before then
                if not force then
                    return preset_path, "kept"
                end
                os.tryrm(preset_path)
            end
            os.cp(preset_source_path, preset_path)
            if existed_before then
                return preset_path, "overwritten"
            end
            return preset_path, "created"
        end

        --- 生成 workspace xmake.lua
        ---@param project_name string 项目名
        ---@param target_name string 目标名
        ---@param framework_alias string framework 别名目录
        ---@param entry_relative_path string 相对 framework 的入口路径
        ---@return string content 文件内容
        local function build_xmake_lua(project_name, target_name, framework_alias, entry_relative_path)
            return table.concat({
                string.format('set_project("%s")', project_name),
                'set_xmakever("3.0.7")',
                'add_rules("mode.debug", "mode.release")',
                'set_policy("build.optimization.lto", false)',
                'add_rules("plugin.compile_commands.autoupdate", {outputdir = os.projectdir()})',
                "",
                string.format('local framework_root = path.join(os.projectdir(), [[%s]])', framework_alias),
                string.format('includes("%s")', framework_alias),
                "",
                string.format('target("%s")', target_name),
                '    set_kind("binary")',
                string.format('    set_filename("%s.elf")', target_name),
                '    add_deps("tar_oh_my_robot")',
                '    add_rules("oh_my_robot.context", "oh_my_robot.board_assets", "oh_my_robot.image_convert")',
                string.format('    add_files(path.join([[%s]], [[%s]]))', framework_alias, entry_relative_path),
                'target_end()',
                "",
            }, "\n")
        end

        --- 生成 README
        ---@param framework_root string 框架实际路径
        ---@param preset_source_path string preset 源文件路径
        ---@return string content 文件内容
        local function build_readme(framework_root, preset_source_path)
            return table.concat({
                "# OM Workspace Shell",
                "",
                "这个目录是为 `oh-my-robot` worktree 生成的轻量项目壳。",
                "它不会复制框架源码，而是通过本地 `oh-my-robot` 目录链接/junction 指向当前 checkout。",
                "",
                "## 当前框架引用",
                "",
                string.format("- alias: `%s`", FRAMEWORK_ALIAS),
                string.format("- target: `%s`", normalize_lua_path(framework_root)),
                "",
                "## 项目根预设",
                "",
                "- 当前目录会生成项目级 `om_preset.lua` 模板。",
                string.format("- 当前 preset 来源：`%s`", normalize_lua_path(preset_source_path)),
                "- 这个文件只对当前项目壳生效，请直接按本机路径修改其中的 `sdk/bin` 与烧录参数。",
                "- 重新执行 `xmake init_workspace --force=true` 时，会覆盖 `om_preset.lua`；不加 `--force` 则保留已有文件。",
                "",
                "## 常用命令",
                "",
                "```powershell",
                "xmake f -c -m debug",
                "xmake",
                "```",
                "",
            }, "\n")
        end

        --- 生成 .gitignore
        ---@return string content 文件内容
        local function build_gitignore()
            return table.concat({
                "/.xmake/",
                "/build/",
                "/compile_commands.json",
                "/oh-my-robot/",
                "",
            }, "\n")
        end

        local output_option = option.get("output")
        if not output_option or output_option == "" then
            raise("output path required: please pass --output=<dir>")
        end

        local framework_default = path.directory(path.directory(os.scriptdir()))
        local output_dir = resolve_absolute_path(output_option, os.projectdir())
        local framework_root = resolve_absolute_path(option.get("framework") or framework_default, os.projectdir())
        local preset_source_path = resolve_preset_source_path(option.get("preset"), framework_root)
        local project_name = option.get("project_name")
        local target_name = option.get("target") or "robot_project"
        local entry_relative_path = normalize_lua_path(option.get("entry") or "samples/motor/p1010b/main.c")
        local force = resolve_bool(option.get("force"), false)

        if not os.isdir(framework_root) then
            raise("framework root not found: " .. framework_root)
        end
        if not os.isfile(path.join(framework_root, "xmake.lua")) then
            raise("framework xmake.lua not found: " .. framework_root)
        end
        if not os.isfile(path.join(framework_root, entry_relative_path)) then
            raise("entry file not found under framework root: " .. path.join(framework_root, entry_relative_path))
        end

        project_name = project_name and project_name ~= "" and project_name or basename(output_dir)
        assert_output_dir_writable(output_dir, framework_root, force)

        os.mkdir(output_dir)
        create_framework_alias(output_dir, framework_root, force)
        local preset_path, preset_status = ensure_project_preset(output_dir, preset_source_path, force)
        io.writefile(path.join(output_dir, "xmake.lua"), build_xmake_lua(project_name, target_name, FRAMEWORK_ALIAS, entry_relative_path))
        io.writefile(path.join(output_dir, "README.md"), build_readme(framework_root, preset_source_path))
        io.writefile(path.join(output_dir, ".gitignore"), build_gitignore())

        print("[oh-my-robot] workspace initialized: " .. output_dir)
        print("[oh-my-robot] framework root: " .. framework_root)
        print("[oh-my-robot] preset source: " .. preset_source_path)
        print("[oh-my-robot] project preset: " .. preset_path .. " (" .. preset_status .. ")")
        print("[oh-my-robot] target: " .. target_name)
    end)
