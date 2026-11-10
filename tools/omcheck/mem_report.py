#!/usr/bin/env python3
"""Build-time memory usage report.

Runs the toolchain's size utility against the linked ELF and prints a concise
FLASH/RAM usage summary. Linker-script totals/percentages are deliberately
not parsed here: those are board/chip configuration facts, not build-system
facts.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys


def find_tool(name: str, bin_dir: str | None) -> str | None:
    candidates = [name]
    if os.name == "nt" and not name.lower().endswith(".exe"):
        candidates.insert(0, name + ".exe")
    for candidate in candidates:
        if bin_dir:
            path = os.path.join(bin_dir, candidate)
            if os.path.isfile(path):
                return path
    for candidate in candidates:
        path = shutil.which(candidate)
        if path:
            return path
    if bin_dir:
        for candidate in candidates:
            path = shutil.which(candidate, path=bin_dir)
            if path:
                return path
    return None


def run(program: str, args: list[str]) -> str:
    proc = subprocess.run(
        [program, *args],
        capture_output=True,
        text=True,
        errors="replace",
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"{os.path.basename(program)} failed ({proc.returncode}): "
            f"{proc.stderr.strip() or proc.stdout.strip()}"
        )
    return proc.stdout


def report_gnu(elf: str, bin_dir: str | None) -> str:
    program = find_tool("arm-none-eabi-size", bin_dir) or find_tool("size", bin_dir)
    if not program:
        raise RuntimeError("arm-none-eabi-size not found")
    output = run(program, [elf])
    pattern = re.compile(
        r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+[0-9a-fA-F]+\s+\S+\s*$",
        re.MULTILINE,
    )
    match = pattern.search(output)
    if not match:
        raise RuntimeError("unable to parse arm-none-eabi-size output")
    text, data, bss = (int(match.group(i)) for i in (1, 2, 3))
    return f"FLASH={text + data} RAM={data + bss}"


def report_armclang(elf: str, bin_dir: str | None) -> str:
    program = find_tool("fromelf", bin_dir)
    if not program:
        raise RuntimeError("fromelf not found")
    output = run(program, ["--text", "-z", elf])
    pattern = re.compile(
        r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+\d+\s+ROM Totals",
        re.MULTILINE,
    )
    match = pattern.search(output)
    if not match:
        raise RuntimeError("unable to parse fromelf ROM Totals")
    code, ro, rw, zi = (int(match.group(i)) for i in (1, 2, 3, 4))
    return f"FLASH={code + ro + rw} RAM={rw + zi}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, help="linked ELF path")
    parser.add_argument(
        "--toolchain",
        default="gnu-rm",
        choices=("gnu-rm", "armclang", "tiarmclang"),
        help="toolchain family",
    )
    parser.add_argument("--bin-dir", help="toolchain bin directory")
    args = parser.parse_args()

    try:
        if args.toolchain == "armclang":
            summary = report_armclang(args.elf, args.bin_dir)
        else:
            summary = report_gnu(args.elf, args.bin_dir)
    except Exception as exc:  # report is informational; never break the build
        print(f"[oh-my-robot] memory report unavailable: {exc}")
        return 0

    print(f"[oh-my-robot] memory (used): {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
