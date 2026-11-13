#!/usr/bin/env python3
"""Render compatibility views from tools/omcheck/compat.yaml.

Formats:
- confluence: a release-view page for the Confluence compatibility tree;
- release-notes: the "compatibility" section of a GitHub Release draft.

The renderer is read-only and deterministic for a given compat.yaml plus
explicit --tag/--commit/--date inputs.
"""

from __future__ import annotations

import argparse
import datetime
import subprocess
import sys
from pathlib import Path
from typing import Any

from compat_lib import CompatError, load_compat, validate_compat


MATRIX_URL = (
    "https://bamboolab.atlassian.net/wiki/spaces/OMR/pages/1146881/"
    "OMR+Compatibility+Matrix"
)
ROADMAP_URL = (
    "https://bamboolab.atlassian.net/wiki/spaces/OMR/pages/1114113/"
    "OMR+Roadmap"
)
RELEASE_SPEC_URL = (
    "https://github.com/oh-my-robot/oh-my-robot-framework/blob/integration/"
    "docs/process/git_version_release_spec.md"
)

STATUS_LABELS = {
    "verified": "已验证",
    "experimental": "实验性",
    "excluded": "排除",
}


def cell(value: Any) -> str:
    """Escape a value for a Markdown table cell."""
    text = str(value).replace("|", r"\|")
    return " ".join(text.split())


def devices_text(devices: dict[str, Any] | None) -> str:
    if not devices:
        return "—"
    version = devices.get("version")
    ref = devices.get("ref")
    if version and ref:
        return f"`{version}`（ref `{ref}`）"
    if version:
        return f"`{version}`"
    return f"ref `{ref}`"


def app_text(app: dict[str, Any] | None) -> str:
    if not app:
        return "应用自持，未声明"
    text = f"`{app.get('version')}`"
    if app.get("framework"):
        text += f"（framework `{app['framework']}`）"
    return text


def contracts_text(contracts: dict[str, Any] | None) -> str:
    if not contracts:
        return "无"
    return "、".join(f"`{name}` {version}" for name, version in contracts.items())


def toolchain_text(build: dict[str, Any]) -> str:
    text = str(build["toolchain"])
    if build.get("toolchain_version"):
        text += f" {build['toolchain_version']}"
    return text


def board_text(build: dict[str, Any]) -> str:
    text = str(build["board"])
    if build.get("chip"):
        text += f"（{build['chip']}）"
    return text


def render_confluence(
    data: dict[str, Any],
    tag: str | None,
    commit: str | None,
    date: str,
) -> str:
    framework = data["framework"]
    major, minor = framework.split(".")[:2]
    devices = data.get("devices") or None
    app = data.get("app") or None
    contracts = data.get("contracts") or None

    lines = [
        f"# OMR Compatibility v{major}.{minor}.x",
        "",
        "> 发布视图；机器可读事实源为 `tools/omcheck/compat.yaml`，"
        "本页由 `compat_render.py` 生成。",
        "",
        "## 发布信息",
        "",
    ]
    if tag:
        lines.append(
            f"- GitHub Release: [OMR {tag}]"
            f"(https://github.com/oh-my-robot/oh-my-robot-framework/releases/tag/{tag})"
        )
        if commit:
            lines.append(f"- Tag: `{tag}` → `{commit}`")
        else:
            lines.append(f"- Tag: `{tag}`")
    else:
        lines.append(f"- 目标版本: `v{framework}`（未打 tag）")
    lines.extend(
        [
            f"- 生成日期: {date}",
            "",
            "## 支持组合",
            "",
            "| 框架 | 器件库 | 板卡 | 运行环境 | 工具链 | 状态 | 测试证据 |",
            "| --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for build in data["builds"]:
        lines.append(
            "| "
            + " | ".join(
                [
                    cell(f"v{framework}"),
                    cell(devices_text(devices)),
                    cell(board_text(build)),
                    cell(build["os"]),
                    cell(toolchain_text(build)),
                    cell(STATUS_LABELS[build["status"]]),
                    cell(build["evidence"]),
                ]
            )
            + " |"
        )

    lines.extend(
        [
            "",
            "## 版本轴",
            "",
            "| 轴 | 版本 | 兼容范围 |",
            "| --- | --- | --- |",
            f"| 框架 | `v{framework}` | SemVer |",
            f"| 器件库 | {devices_text(devices)} | "
            f"`{(devices or {}).get('framework', '—')}` |",
            f"| App | {app_text(app)} | "
            f"`{(app or {}).get('framework', '—')}` |",
            f"| 契约 | {contracts_text(contracts)} | 按轴单独声明 |",
            "",
            "## 关联",
            "",
            f"- [OMR Compatibility Matrix]({MATRIX_URL})",
            f"- [OMR Roadmap]({ROADMAP_URL})",
            f"- [发布与版本规范]({RELEASE_SPEC_URL})",
            "",
        ]
    )
    return "\n".join(lines)


def render_release_notes(
    data: dict[str, Any],
    tag: str | None,
    commit: str | None,
    date: str,
) -> str:
    framework = data["framework"]
    devices = data.get("devices") or None
    app = data.get("app") or None
    contracts = data.get("contracts") or None
    version_text = f"`v{framework}`"
    if tag:
        version_text = f"`{tag}`"
        if commit:
            version_text += f"（`{commit}`）"

    lines = [
        "## ✅ 兼容性",
        "",
        "| 项目 | 内容 |",
        "| --- | --- |",
        f"| Framework | {version_text} |",
        f"| Devices | {devices_text(devices)}"
        + (
            f"（framework `{devices['framework']}`）"
            if devices and devices.get("framework")
            else ""
        )
        + " |",
        f"| App | {app_text(app)} |",
        f"| 契约 | {contracts_text(contracts)} |",
        "",
        "| 板卡 | 运行环境 | 工具链 | 状态 | 测试证据 |",
        "| --- | --- | --- | --- | --- |",
    ]
    for build in data["builds"]:
        lines.append(
            "| "
            + " | ".join(
                [
                    cell(board_text(build)),
                    cell(build["os"]),
                    cell(toolchain_text(build)),
                    cell(STATUS_LABELS[build["status"]]),
                    cell(build["evidence"]),
                ]
            )
            + " |"
        )
    ref = tag or "integration"
    lines.extend(
        [
            "",
            "> 兼容窗口与破坏性变更判定见 "
            "[ADR-0030]"
            f"(https://github.com/oh-my-robot/oh-my-robot-framework/blob/{ref}/"
            "docs/adr/0030-version_axes_and_compatibility_policy.md)。",
            "",
        ]
    )
    return "\n".join(lines)


def resolve_commit(compat_path: str, tag: str | None) -> str | None:
    if not tag:
        return None
    repo = Path(compat_path).resolve().parent
    try:
        proc = subprocess.run(
            ["git", "-C", str(repo), "rev-parse", "--short=8", f"{tag}^{{commit}}"],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return None
    if proc.returncode != 0:
        return None
    return proc.stdout.strip() or None


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--compat",
        default="tools/omcheck/compat.yaml",
        help="path to compat.yaml",
    )
    parser.add_argument(
        "--format",
        dest="output_format",
        choices=("confluence", "release-notes"),
        required=True,
        help="output view",
    )
    parser.add_argument(
        "--tag",
        help="release tag (e.g. v0.2.0); enables the GitHub Release link",
    )
    parser.add_argument(
        "--commit",
        help="short commit for the tag; resolved from git when omitted",
    )
    parser.add_argument(
        "--date",
        default=datetime.date.today().isoformat(),
        help="generation date (default: today)",
    )
    parser.add_argument(
        "--out",
        help="write to a file instead of stdout",
    )
    args = parser.parse_args()

    try:
        data = load_compat(args.compat)
    except CompatError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    errors = validate_compat(data, tag=args.tag)
    if errors:
        for error in errors:
            print(f"{args.compat}: {error}", file=sys.stderr)
        return 1

    commit = args.commit or resolve_commit(args.compat, args.tag)
    if args.output_format == "confluence":
        text = render_confluence(data, args.tag, commit, args.date)
    else:
        text = render_release_notes(data, args.tag, commit, args.date)

    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
