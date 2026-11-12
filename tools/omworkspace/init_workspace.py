#!/usr/bin/env python3
"""Generate a lightweight project shell for an oh-my-robot checkout.

The shell contains a directory junction/symlink to the framework checkout,
a project-level om_preset.lua, and a minimal xmake.lua with one binary target.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys


FRAMEWORK_ALIAS = "oh-my-robot"


def normalize(path: str) -> str:
    return os.path.normcase(os.path.abspath(path))


def remove_alias(alias: str) -> None:
    if not os.path.lexists(alias):
        return
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "rmdir", alias], check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if os.path.lexists(alias):
            subprocess.run(["cmd", "/c", "del", "/f", "/q", alias], check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        os.unlink(alias)


def create_alias(output_dir: str, framework_root: str) -> str:
    alias = os.path.join(output_dir, FRAMEWORK_ALIAS)
    if os.path.lexists(alias):
        remove_alias(alias)
    if os.name == "nt":
        subprocess.run(
            ["cmd", "/c", "mklink", "/J", alias, framework_root],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
    else:
        os.symlink(framework_root, alias, target_is_directory=True)
    if not os.path.isdir(alias):
        raise RuntimeError(f"framework alias creation failed: {alias}")
    return alias


def build_xmake_lua(
    project_name: str,
    target_name: str,
    framework_alias: str,
    entry: str,
    rules: list[str],
    has_devices: bool,
) -> str:
    rule_call = ", ".join(f'"oh_my_robot.{name}"' for name in rules)
    deps = '"tar_oh_my_robot"' + (', "tar_awdevices"' if has_devices else "")
    return "\n".join(
        [
            f'set_project("{project_name}")',
            'set_xmakever("3.0.7")',
            'add_rules("mode.debug", "mode.release")',
            'set_policy("build.optimization.lto", false)',
            'add_rules("plugin.compile_commands.autoupdate", {outputdir = os.projectdir()})',
            "",
            f'local framework_root = path.join(os.projectdir(), [[{framework_alias}]])',
            f'includes("{framework_alias}")',
            "",
            f'target("{target_name}")',
            '    set_kind("binary")',
            f'    set_filename("{target_name}.elf")',
            f"    add_deps({deps})",
            f"    add_rules({rule_call})",
            f'    add_files(path.join([[{framework_alias}]], [[{entry}]]))',
            "target_end()",
            "",
        ]
    )


def build_readme(framework_root: str, preset_source: str) -> str:
    return "\n".join(
        [
            "# OM Workspace Shell",
            "",
            "这个目录是为 `oh-my-robot` worktree 生成的轻量项目壳。",
            "它不会复制框架源码，而是通过本地 `oh-my-robot` 目录链接/junction 指向当前 checkout。",
            "",
            "## 当前框架引用",
            "",
            f"- alias: `{FRAMEWORK_ALIAS}`",
            f"- target: `{framework_root}`",
            "",
            "## 项目根预设",
            "",
            "- 当前目录会生成项目级 `om_preset.lua` 模板。",
            f"- 当前 preset 来源：`{preset_source}`",
            "- 这个文件只对当前项目壳生效，请直接按本机路径修改其中的 `sdk/bin` 配置。",
            "- 重新执行 `xmake init_workspace --force=true` 时，会覆盖 `om_preset.lua`；不加 `--force` 则保留已有文件。",
            "",
            "## 常用命令",
            "",
            "```powershell",
            "xmake f -c -m debug",
            "xmake",
            "```",
            "",
        ]
    )


def build_gitignore() -> str:
    return "\n".join(
        [
            "/.xmake/",
            "/build/",
            "/compile_commands.json",
            f"/{FRAMEWORK_ALIAS}/",
            "",
        ]
    )


def read_rules_file(path: str) -> list[str]:
    import re

    if not os.path.isfile(path):
        raise RuntimeError(f"binary rule set not found: {path}")
    pattern = re.compile(r'^\s*"([A-Za-z0-9_]+)",?\s*$')
    rules: list[str] = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            match = pattern.match(line)
            if match:
                rules.append(match.group(1))
    if not rules:
        raise RuntimeError(f"no binary rules found in: {path}")
    return rules


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, help="workspace output directory")
    parser.add_argument("--framework", required=True, help="framework root")
    parser.add_argument("--preset", help="preset source file")
    parser.add_argument("--project-name", help="project name (default: output basename)")
    parser.add_argument("--target", default="robot_project", help="binary target name")
    parser.add_argument(
        "--entry",
        default="samples/motor/p1010b/main.c",
        help="entry source path relative to framework root",
    )
    parser.add_argument(
        "--rules",
        default="context,board_assets,image_convert,project_cfg,selfreg",
        help="comma-separated binary rule names",
    )
    parser.add_argument(
        "--rules-file",
        help="binary_rules.lua path (single source of truth)",
    )
    parser.add_argument("--force", action="store_true", help="overwrite generated files")
    args = parser.parse_args()

    output_dir = os.path.abspath(args.output)
    framework_root = os.path.abspath(args.framework)
    if not os.path.isdir(framework_root):
        raise RuntimeError(f"framework root not found: {framework_root}")
    if normalize(output_dir) == normalize(framework_root):
        raise RuntimeError("workspace output cannot be the framework root itself")

    if os.path.isdir(output_dir) and os.listdir(output_dir) and not args.force:
        raise RuntimeError(
            f"workspace output already contains files: {output_dir}. "
            "Pass --force=true to overwrite generated files."
        )
    os.makedirs(output_dir, exist_ok=True)

    preset_source = args.preset or os.path.join(framework_root, "om_preset.example.lua")
    if not os.path.isfile(preset_source):
        raise RuntimeError(f"preset source not found: {preset_source}")

    try:
        create_alias(output_dir, framework_root)
        preset_path = os.path.join(output_dir, "om_preset.lua")
        existed_before = os.path.isfile(preset_path)
        if existed_before and not args.force:
            preset_status = "kept"
        else:
            shutil.copyfile(preset_source, preset_path)
            preset_status = "overwritten" if existed_before else "created"

        project_name = args.project_name or os.path.basename(output_dir.rstrip("\\/"))
        if args.rules_file:
            rules = read_rules_file(args.rules_file)
        else:
            rules = [item.strip() for item in args.rules.split(",") if item.strip()]
        if not rules:
            raise RuntimeError("at least one binary rule is required")

        has_devices = os.path.isdir(os.path.join(framework_root, "devices"))
        with open(os.path.join(output_dir, "xmake.lua"), "w", encoding="utf-8") as handle:
            handle.write(
                build_xmake_lua(
                    project_name, args.target, FRAMEWORK_ALIAS, args.entry, rules,
                    has_devices,
                )
            )
        with open(os.path.join(output_dir, "README.md"), "w", encoding="utf-8") as handle:
            handle.write(build_readme(framework_root, preset_source))
        with open(os.path.join(output_dir, ".gitignore"), "w", encoding="utf-8") as handle:
            handle.write(build_gitignore())
    except Exception as exc:
        print(f"[oh-my-robot] workspace init failed: {exc}", file=sys.stderr)
        return 1

    print(f"[oh-my-robot] workspace initialized: {output_dir}")
    print(f"[oh-my-robot] framework root: {framework_root}")
    print(f"[oh-my-robot] preset source: {preset_source}")
    print(f"[oh-my-robot] project preset: {preset_path} ({preset_status})")
    print(f"[oh-my-robot] target: {args.target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
