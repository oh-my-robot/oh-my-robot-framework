--- @task init_workspace
--- @brief 初始化轻量项目壳
--- @details 生成项目壳的细节由 tools/omworkspace/init_workspace.py 承担；
---          本任务只解析参数并传入 binary 规则集。
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
        local om_root = path.directory(path.directory(os.scriptdir()))
        local modules_root = path.join(om_root, "xmake", "modules")
        local script = path.join(om_root, "tools", "omworkspace", "init_workspace.py")

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

        if not os.isfile(script) then
            raise("init_workspace script not found: " .. script)
        end

        local output_option = option.get("output")
        if not output_option or output_option == "" then
            raise("output path required: please pass --output=<dir>")
        end

        local framework_root = path.absolute(
            option.get("framework") or om_root,
            os.projectdir()
        )
        if not os.isdir(framework_root) then
            raise("framework root not found: " .. framework_root)
        end

        local rules_file = path.join(modules_root, "binary_rules.lua")
        if not os.isfile(rules_file) then
            raise("binary rule set not found: " .. rules_file)
        end

        local find_tool = import("lib.detect.find_tool")
        local python = find_tool("python3") or find_tool("python")
        if not python then
            raise("python3/python not found (required by tools/omworkspace)")
        end

        local argv = {
            script,
            "--output", path.absolute(output_option, os.projectdir()),
            "--framework", framework_root,
            "--target", option.get("target") or "robot_project",
            "--entry", option.get("entry") or "samples/motor/p1010b/main.c",
            "--rules-file", rules_file,
        }

        local preset = option.get("preset")
        if preset and preset ~= "" then
            argv[#argv + 1] = "--preset"
            argv[#argv + 1] = path.absolute(preset, os.projectdir())
        end

        local project_name = option.get("project_name")
        if project_name and project_name ~= "" then
            argv[#argv + 1] = "--project-name"
            argv[#argv + 1] = project_name
        end

        if resolve_bool(option.get("force"), false) then
            argv[#argv + 1] = "--force"
        end

        local code = os.execv(python.program or python, argv, {
            stdout = io.stdout,
            stderr = io.stderr,
        })
        if code ~= 0 then
            raise("workspace init failed")
        end
    end)
