#!/usr/bin/env python3
"""Check that a device library only depends on framework public contracts.

Scans the device library's include/ and src/ trees for:
- includes of platform/third_party/drivers/src private paths;
- vendor SDK headers (STM32, TI, DeviceFamily, FreeRTOS, CMSIS);
- board-specific macros (OM_BOARD_*, BSP_*).

The check is intentionally source-level: the build system already limits the
device target's include paths, while this gate catches conditional or
commented-out dependencies that the compiler never sees.
"""

from __future__ import annotations

import argparse
import os
import re
import sys


SCAN_DIRS = ("include", "src")
SOURCE_SUFFIXES = (".c", ".h", ".cc", ".cpp", ".hpp", ".inc")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
FORBIDDEN_INCLUDE_PREFIXES = (
    "platform/",
    "third_party/",
    "drivers/src/",
    "bsp_",
    "stm32",
    "ti/",
    "DeviceFamily",
)
FORBIDDEN_INCLUDE_EXACT = (
    "FreeRTOS.h",
    "task.h",
    "cmsis_os.h",
    "cmsis_os2.h",
)
FORBIDDEN_TOKENS = (
    re.compile(r"\bOM_BOARD_[A-Za-z0-9_]+"),
    re.compile(r"\bBSP_[A-Za-z0-9_]+"),
)


def strip_comments(text: str) -> str:
    """Replace C comments with spaces while preserving line structure."""
    result: list[str] = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        next_char = text[index + 1] if index + 1 < length else ""
        if char == "/" and next_char == "/":
            end = text.find("\n", index)
            if end == -1:
                end = length
            result.append(" " * (end - index))
            index = end
        elif char == "/" and next_char == "*":
            end = text.find("*/", index + 2)
            if end == -1:
                end = length
            else:
                end += 2
            result.append("".join("\n" if c == "\n" else " " for c in text[index:end]))
            index = end
        else:
            result.append(char)
            index += 1
    return "".join(result)


def include_violation(header: str) -> str | None:
    normalized = header.replace("\\", "/")
    for prefix in FORBIDDEN_INCLUDE_PREFIXES:
        if normalized.startswith(prefix):
            return f"forbidden include: {header}"
    if normalized in FORBIDDEN_INCLUDE_EXACT:
        return f"forbidden include: {header}"
    return None


def scan_file(path: str) -> list[str]:
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = strip_comments(handle.read())
    violations: list[str] = []
    for match in INCLUDE_RE.finditer(text):
        violation = include_violation(match.group(1))
        if violation:
            line = text.count("\n", 0, match.start()) + 1
            violations.append(f"{line}: {violation}")
    for pattern in FORBIDDEN_TOKENS:
        for match in pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            violations.append(f"{line}: forbidden token: {match.group(0)}")
    return violations


def iter_sources(root: str) -> list[str]:
    sources: list[str] = []
    for directory in SCAN_DIRS:
        base = os.path.join(root, directory)
        if not os.path.isdir(base):
            continue
        for current, _, files in os.walk(base):
            for name in files:
                if name.endswith(SOURCE_SUFFIXES):
                    sources.append(os.path.join(current, name))
    return sorted(sources)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--devices-root",
        required=True,
        help="device library root (contains include/ and src/)",
    )
    args = parser.parse_args()

    root = os.path.abspath(args.devices_root)
    if not os.path.isdir(root):
        print(f"[oh-my-robot] devices root not found: {root}", file=sys.stderr)
        return 2

    sources = iter_sources(root)
    if not sources:
        print(
            f"[oh-my-robot] no device sources found under {root}/include or {root}/src",
            file=sys.stderr,
        )
        return 2

    violations: list[tuple[str, str]] = []
    for path in sources:
        relative = os.path.relpath(path, root)
        for violation in scan_file(path):
            violations.append((relative, violation))

    if not violations:
        return 0

    print("[oh-my-robot] device dependency boundary check failed:")
    for relative, violation in violations:
        print(f"[oh-my-robot]   {relative}:{violation}")
    print(
        "[oh-my-robot]   action: depend on framework public headers only; "
        "move board/vendor integration into the application or platform layer"
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
