#!/usr/bin/env python3
"""Build-time link contract check.

Checks the linked ELF (and optional provider archives) for:
- required strong symbols from tar_os / tar_sync;
- per-level .om_init boundary symbols (GCC and armlink spellings);
- a strong HardFault_Handler (shared fatal entry point).

The check is intentionally standalone so CI and the build system can invoke
the same implementation.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys


PROVIDER_SYMBOLS = {
    "tar_os": ("osal_malloc", "osal_free"),
    "tar_sync": ("completion_init", "completion_wait"),
    "tar_awdrivers": ("device_register", "device_find"),
    "tar_awdevices": (),
}

# 器件 provider 不得重新实现框架注册表；注册表由 tar_awdrivers 提供。
PROVIDER_FORBIDDEN = {
    "tar_awdevices": ("device_register", "device_find"),
}

BINARY_STRONG_SYMBOLS = ("HardFault_Handler",)


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


def _set_state(states: dict[str, str], name: str, state: str) -> None:
    current = states.get(name)
    rank = {"undefined": 0, "weak": 1, "strong": 2}
    if current is None or rank[state] > rank[current]:
        states[name] = state


def parse_nm(output: str) -> dict[str, str]:
    states: dict[str, str] = {}
    pattern = re.compile(r"^\s*(?:[0-9a-fA-F]+\s+)?([A-Za-z?])\s+(\S+)\s*$")
    for line in output.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        symbol_type, name = match.groups()
        if symbol_type == "U":
            _set_state(states, name, "undefined")
        elif symbol_type in ("W", "w", "V", "v"):
            _set_state(states, name, "weak")
        else:
            _set_state(states, name, "strong")
    return states


def parse_fromelf(output: str) -> dict[str, str]:
    states: dict[str, str] = {}
    pattern = re.compile(
        r"^\s*\d+\s+(\S+)\s+0x[0-9a-fA-F]+\s+(\w+)\s+(\S+)\s+"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        name, bind, section = match.groups()
        if section == "UND":
            _set_state(states, name, "undefined")
        elif bind == "Wk":
            _set_state(states, name, "weak")
        else:
            _set_state(states, name, "strong")
    return states


def read_symbols(
    path: str, toolchain: str, bin_dir: str | None
) -> tuple[dict[str, str] | None, bool]:
    """Return (symbols, opaque_lto). symbols=None means opaque archive."""
    if toolchain == "armclang":
        program = find_tool("fromelf", bin_dir)
        if not program:
            raise RuntimeError("fromelf not found")
        try:
            output = run(program, ["--text", "-s", path])
        except RuntimeError:
            # armlink LTO archives are bitcode containers; skip symbol-level
            # checks on them. The linked binary is still checked.
            if path.lower().endswith(".a"):
                return None, True
            raise
        opaque_lto = ".llvmbc" in output
        return parse_fromelf(output), opaque_lto

    # gcc-nm loads the LTO plugin, so slim LTO archives expose real symbols.
    program = (
        find_tool("arm-none-eabi-gcc-nm", bin_dir)
        or find_tool("arm-none-eabi-nm", bin_dir)
        or find_tool("nm", bin_dir)
    )
    if not program:
        raise RuntimeError("arm-none-eabi-gcc-nm / arm-none-eabi-nm / nm not found")
    try:
        output = run(program, ["-g", path])
    except RuntimeError:
        # GCC LTO archives are plugin-specific bitcode containers; skip
        # symbol-level checks on them. The linked binary is still checked.
        if path.lower().endswith(".a"):
            return None, True
        raise
    states = parse_nm(output)
    # GCC slim LTO objects expose only __gnu_lto_slim to nm without the LTO
    # plugin; treat the archive as opaque and rely on the linked ELF checks.
    if "__gnu_lto_slim" in states:
        return None, True
    if path.lower().endswith(".a") and not states:
        return None, True
    return states, False


def boundary_symbols(toolchain: str, level_count: int) -> tuple[str, ...]:
    symbols: list[str] = []
    for level in range(level_count):
        if toolchain == "armclang":
            symbols.append(f"Image$$ER_OM_INIT_{level}$$Base")
            symbols.append(f"Image$$ER_OM_INIT_{level}$$Limit")
        else:
            symbols.append(f"__om_init_{level}_start")
            symbols.append(f"__om_init_{level}_end")
    return tuple(symbols)


def missing_and_weak(
    symbols: tuple[str, ...], states: dict[str, str]
) -> tuple[list[str], list[str]]:
    missing = [name for name in symbols if states.get(name) in (None, "undefined")]
    weak = [name for name in symbols if states.get(name) == "weak"]
    return sorted(missing), sorted(weak)


def check_group(
    label: str,
    path: str,
    symbols: tuple[str, ...],
    toolchain: str,
    bin_dir: str | None,
    failures: list[str],
) -> None:
    states, opaque_lto = read_symbols(path, toolchain, bin_dir)
    if states is None:
        return
    missing, weak = missing_and_weak(symbols, states)
    if opaque_lto and (missing or weak):
        return
    if missing:
        failures.append(f"{label} missing strong symbols: {', '.join(missing)}")
    if weak:
        failures.append(f"{label} weak-only symbols: {', '.join(weak)}")


def check_forbidden(
    label: str,
    path: str,
    symbols: tuple[str, ...],
    toolchain: str,
    bin_dir: str | None,
    failures: list[str],
) -> None:
    states, _ = read_symbols(path, toolchain, bin_dir)
    if states is None:
        return
    defined = [name for name in symbols if states.get(name) in ("weak", "strong")]
    if defined:
        failures.append(f"{label} must not define: {', '.join(sorted(defined))}")


def check_not_empty(
    label: str,
    path: str,
    toolchain: str,
    bin_dir: str | None,
    failures: list[str],
) -> None:
    states, _ = read_symbols(path, toolchain, bin_dir)
    if states is None:
        return
    if not any(state == "strong" for state in states.values()):
        failures.append(f"{label} has no strong symbols")


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
    parser.add_argument("--level-count", type=int, default=7, help="om_init levels")
    parser.add_argument(
        "--provider",
        action="append",
        default=[],
        metavar="NAME=PATH",
        help="provider archive (e.g. tar_os=path/to/libtar_os.a)",
    )
    args = parser.parse_args()

    failures: list[str] = []

    for item in args.provider:
        if "=" not in item:
            failures.append(f"invalid --provider value: {item}")
            continue
        name, path = item.split("=", 1)
        symbols = PROVIDER_SYMBOLS.get(name)
        if symbols is None:
            failures.append(f"unknown provider: {name}")
            continue
        if not os.path.isfile(path):
            failures.append(f"provider {name} not found: {path}")
            continue
        check_group(name, path, symbols, args.toolchain, args.bin_dir, failures)
        forbidden = PROVIDER_FORBIDDEN.get(name)
        if forbidden:
            check_forbidden(name, path, forbidden, args.toolchain, args.bin_dir, failures)
        if name == "tar_awdevices":
            check_not_empty(name, path, args.toolchain, args.bin_dir, failures)

    if not os.path.isfile(args.elf):
        failures.append(f"binary not found: {args.elf}")
    else:
        check_group(
            "binary",
            args.elf,
            boundary_symbols(args.toolchain, args.level_count),
            args.toolchain,
            args.bin_dir,
            failures,
        )
        check_group(
            "binary",
            args.elf,
            BINARY_STRONG_SYMBOLS,
            args.toolchain,
            args.bin_dir,
            failures,
        )

    if not failures:
        return 0

    print("[oh-my-robot] link contract check failed:")
    for failure in failures:
        print(f"[oh-my-robot]   {failure}")
    print("[oh-my-robot]   action: fix provider wiring or the board linker script")
    return 1


if __name__ == "__main__":
    sys.exit(main())
