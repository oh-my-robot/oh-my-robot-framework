#!/usr/bin/env python3
"""Compatibility data model for tools/omcheck/compat.yaml.

The file is the machine-readable source for release compatibility views:
- version axes owned by framework / devices / app / contract schemas;
- verified board-os-toolchain combinations and their evidence.

This module only loads and validates data. Page and release-note rendering
live in compat_render.py.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

try:
    import yaml
except ImportError:  # pragma: no cover - exercised only without PyYAML
    yaml = None


SCHEMA_VERSION = 1

TOP_LEVEL_KEYS = {
    "schema",
    "framework",
    "devices",
    "app",
    "contracts",
    "builds",
}
DEVICE_KEYS = {"version", "ref", "framework"}
APP_KEYS = {"version", "framework"}
BUILD_KEYS = {
    "board",
    "chip",
    "os",
    "toolchain",
    "toolchain_version",
    "status",
    "evidence",
}
BUILD_STATUSES = ("verified", "experimental", "excluded")

SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$"
)
TAG_RE = re.compile(r"^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
                    r"(?:-[0-9A-Za-z.-]+)?$")
RANGE_TERM_RE = re.compile(
    r"^(>=|<=|>|<|==|=)?\s*"
    r"((?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:\.(?:0|[1-9]\d*))?)$"
)
REF_RE = re.compile(r"^[0-9a-f]{7,40}$")
CONTRACT_VERSION_RE = re.compile(
    r"^(?:[1-9]\d*|(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))$"
)


class CompatError(Exception):
    """Raised when the file cannot be read or parsed as YAML."""


def load_compat(path: str | Path) -> dict[str, Any]:
    if yaml is None:
        raise CompatError("PyYAML is required: pip install pyyaml")
    try:
        with open(path, encoding="utf-8") as handle:
            data = yaml.safe_load(handle)
    except OSError as exc:
        raise CompatError(f"cannot read {path}: {exc}") from exc
    except yaml.YAMLError as exc:
        raise CompatError(f"invalid YAML in {path}: {exc}") from exc
    if data is None:
        raise CompatError(f"{path} is empty")
    if not isinstance(data, dict):
        raise CompatError(f"{path} must contain a mapping at the top level")
    return data


def parse_version(text: str) -> tuple[int, int, int]:
    """Parse a SemVer identity version; build/prerelease tags are ignored for ordering."""
    match = SEMVER_RE.match(text)
    if not match:
        raise ValueError(f"invalid SemVer: {text!r}")
    return int(match.group(1)), int(match.group(2)), int(match.group(3))


def parse_range(text: str) -> list[tuple[str, tuple[int, int, int]]]:
    """Parse a conjunction like ``>=0.2 <0.3`` into comparison terms."""
    terms = [term for term in re.split(r"[\s,]+", text.strip()) if term]
    if not terms:
        raise ValueError("empty version range")
    parsed: list[tuple[str, tuple[int, int, int]]] = []
    for term in terms:
        match = RANGE_TERM_RE.match(term)
        if not match:
            raise ValueError(f"invalid range term: {term!r}")
        operator = match.group(1) or "=="
        parts = [int(part) for part in match.group(2).split(".")]
        if len(parts) == 2:
            parts.append(0)
        parsed.append((operator, (parts[0], parts[1], parts[2])))
    return parsed


def version_in_range(version: str, version_range: str) -> bool:
    value = parse_version(version)
    for operator, bound in parse_range(version_range):
        if operator == ">=" and not value >= bound:
            return False
        if operator == ">" and not value > bound:
            return False
        if operator == "<=" and not value <= bound:
            return False
        if operator == "<" and not value < bound:
            return False
        if operator in ("==", "=") and not value == bound:
            return False
    return True


def _check_keys(
    value: dict[str, Any],
    allowed: set[str],
    path: str,
    errors: list[str],
) -> None:
    unknown = sorted(set(value) - allowed)
    if unknown:
        errors.append(f"{path}: unknown key(s): {', '.join(unknown)}")


def _check_version(
    value: Any,
    path: str,
    errors: list[str],
) -> str | None:
    if not isinstance(value, str):
        errors.append(f"{path}: must be a SemVer string")
        return None
    if value.startswith("v"):
        errors.append(f"{path}: use SemVer without the leading 'v'")
        return None
    if not SEMVER_RE.match(value):
        errors.append(f"{path}: invalid SemVer: {value!r}")
        return None
    return value


def _check_range(value: Any, framework: str, path: str, errors: list[str]) -> None:
    if not isinstance(value, str):
        errors.append(f"{path}: must be a range string, e.g. '>=0.2 <0.3'")
        return
    try:
        if not version_in_range(framework, value):
            errors.append(
                f"{path}: range {value!r} does not include framework {framework}"
            )
    except ValueError as exc:
        errors.append(f"{path}: {exc}")


def _check_devices_pin(
    devices: dict[str, Any],
    pin: str,
    errors: list[str],
) -> None:
    pin = pin.strip()
    if TAG_RE.match(pin):
        if devices.get("version") != pin[1:]:
            errors.append(
                f"devices.version: {devices.get('version')!r} does not match "
                f"pinned tag {pin!r}"
            )
        return
    ref = devices.get("ref")
    if not isinstance(ref, str) or not REF_RE.match(ref):
        errors.append("devices.ref: required when CI pins a devices commit")
    elif not (pin.startswith(ref) or ref.startswith(pin)):
        errors.append(f"devices.ref: {ref!r} does not match pinned {pin!r}")


def validate_compat(
    data: dict[str, Any],
    tag: str | None = None,
    devices_ref: str | None = None,
) -> list[str]:
    """Return validation errors; an empty list means the file is valid."""
    errors: list[str] = []
    _check_keys(data, TOP_LEVEL_KEYS, "compat", errors)

    if data.get("schema") != SCHEMA_VERSION:
        errors.append(
            f"schema: expected {SCHEMA_VERSION}, got {data.get('schema')!r}"
        )

    framework = _check_version(data.get("framework"), "framework", errors)

    if tag is not None:
        if not isinstance(tag, str) or not TAG_RE.match(tag):
            errors.append(f"tag: invalid release tag: {tag!r}")
        elif framework is not None and tag[1:] != framework:
            errors.append(
                f"tag: {tag!r} does not match framework {framework!r}"
            )

    devices = data.get("devices")
    if devices is None and devices_ref is not None:
        errors.append("devices: required when CI pins a devices commit")
    if devices is not None:
        if not isinstance(devices, dict):
            errors.append("devices: must be a mapping")
        else:
            _check_keys(devices, DEVICE_KEYS, "devices", errors)
            has_version = "version" in devices
            has_ref = "ref" in devices
            if not has_version and not has_ref:
                errors.append("devices: require 'version' or 'ref'")
            if has_version:
                _check_version(devices.get("version"), "devices.version", errors)
            if has_ref:
                ref = devices.get("ref")
                if not isinstance(ref, str) or not REF_RE.match(ref):
                    errors.append(
                        "devices.ref: must be a 7-40 character lowercase hex commit"
                    )
            if "framework" not in devices:
                errors.append("devices.framework: required range is missing")
            elif framework is not None:
                _check_range(
                    devices.get("framework"),
                    framework,
                    "devices.framework",
                    errors,
                )
            if devices_ref is not None:
                _check_devices_pin(devices, devices_ref, errors)

    app = data.get("app")
    if app is not None:
        if not isinstance(app, dict):
            errors.append("app: must be a mapping")
        else:
            _check_keys(app, APP_KEYS, "app", errors)
            if "version" not in app:
                errors.append("app.version: required")
            else:
                _check_version(app.get("version"), "app.version", errors)
            if "framework" in app and framework is not None:
                _check_range(app.get("framework"), framework, "app.framework", errors)

    contracts = data.get("contracts")
    if contracts is not None:
        if not isinstance(contracts, dict):
            errors.append("contracts: must be a mapping")
        else:
            for name, version in contracts.items():
                path = f"contracts.{name}"
                if not isinstance(name, str) or not name.strip():
                    errors.append("contracts: axis names must be non-empty strings")
                    continue
                if isinstance(version, bool) or not isinstance(
                    version, (int, str)
                ):
                    errors.append(f"{path}: must be an integer or major.minor string")
                elif isinstance(version, str) and not CONTRACT_VERSION_RE.match(version):
                    errors.append(f"{path}: invalid contract version: {version!r}")
                elif isinstance(version, int) and version <= 0:
                    errors.append(f"{path}: contract version must be positive")

    builds = data.get("builds")
    if not isinstance(builds, list) or not builds:
        errors.append("builds: must be a non-empty list")
    else:
        seen: set[tuple[str, str, str]] = set()
        for index, build in enumerate(builds):
            path = f"builds[{index}]"
            if not isinstance(build, dict):
                errors.append(f"{path}: must be a mapping")
                continue
            _check_keys(build, BUILD_KEYS, path, errors)
            for key in ("board", "os", "toolchain"):
                value = build.get(key)
                if not isinstance(value, str) or not value.strip():
                    errors.append(f"{path}.{key}: required non-empty string")
            status = build.get("status")
            if status not in BUILD_STATUSES:
                errors.append(
                    f"{path}.status: expected one of {', '.join(BUILD_STATUSES)}"
                )
            evidence = build.get("evidence")
            if not isinstance(evidence, str) or not evidence.strip():
                errors.append(f"{path}.evidence: required non-empty string")
            key = (
                str(build.get("board")),
                str(build.get("os")),
                str(build.get("toolchain")),
            )
            if key in seen:
                errors.append(
                    f"{path}: duplicate build combination "
                    f"{key[0]}/{key[1]}/{key[2]}"
                )
            seen.add(key)

    return errors
