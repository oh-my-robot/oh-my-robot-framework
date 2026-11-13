#!/usr/bin/env python3
"""Tests for the compat.yaml checker and renderers."""

from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

from compat_lib import validate_compat, version_in_range  # noqa: E402
from compat_render import render_confluence, render_release_notes  # noqa: E402


def valid_data() -> dict:
    return {
        "schema": 1,
        "framework": "0.2.0",
        "devices": {
            "version": "0.1.0",
            "framework": ">=0.2 <0.3",
        },
        "contracts": {},
        "builds": [
            {
                "board": "rm-a-board",
                "chip": "STM32F427II",
                "os": "freertos",
                "toolchain": "gnu-rm",
                "toolchain_version": "14.2.1",
                "status": "verified",
                "evidence": "CI build + link guard PASS",
            },
            {
                "board": "rm-c-board",
                "os": "freertos",
                "toolchain": "armclang",
                "status": "experimental",
                "evidence": "compile only",
            },
        ],
    }


class ValidateTests(unittest.TestCase):
    def assert_invalid(self, mutate, expected: str) -> None:
        data = valid_data()
        mutate(data)
        errors = validate_compat(data)
        self.assertTrue(
            any(expected in error for error in errors),
            f"expected {expected!r} in {errors}",
        )

    def test_valid(self) -> None:
        self.assertEqual(validate_compat(valid_data()), [])

    def test_valid_with_tag(self) -> None:
        self.assertEqual(validate_compat(valid_data(), tag="v0.2.0"), [])

    def test_tag_mismatch(self) -> None:
        errors = validate_compat(valid_data(), tag="v0.3.0")
        self.assertTrue(any("does not match framework" in error for error in errors))

    def test_schema_required(self) -> None:
        self.assert_invalid(lambda data: data.pop("schema"), "schema")

    def test_schema_version(self) -> None:
        self.assert_invalid(
            lambda data: data.update(schema=2),
            "schema",
        )

    def test_framework_requires_semver(self) -> None:
        self.assert_invalid(
            lambda data: data.update(framework="v0.2"),
            "framework",
        )

    def test_unknown_top_level_key(self) -> None:
        self.assert_invalid(
            lambda data: data.update(extra=True),
            "unknown key",
        )

    def test_devices_require_identity(self) -> None:
        def mutate(data):
            data["devices"] = {"framework": ">=0.2 <0.3"}

        self.assert_invalid(mutate, "version")

    def test_devices_range_must_include_framework(self) -> None:
        def mutate(data):
            data["devices"]["framework"] = ">=0.3 <0.4"

        self.assert_invalid(mutate, "does not include framework")

    def test_invalid_range(self) -> None:
        def mutate(data):
            data["devices"]["framework"] = ">=0.2 || <0.3"

        self.assert_invalid(mutate, "invalid range")

    def test_app_requires_version(self) -> None:
        self.assert_invalid(
            lambda data: data.update(app={"framework": ">=0.2 <0.3"}),
            "app.version",
        )

    def test_contract_version(self) -> None:
        data = valid_data()
        data["contracts"] = {"boot_ota": 1, "partition": "1.0"}
        self.assertEqual(validate_compat(data), [])
        self.assert_invalid(
            lambda value: value.update(contracts={"boot_ota": "v1"}),
            "invalid contract version",
        )
        self.assert_invalid(
            lambda value: value.update(contracts={"boot_ota": 0}),
            "positive",
        )

    def test_builds_required(self) -> None:
        self.assert_invalid(lambda data: data.update(builds=[]), "non-empty")

    def test_evidence_required(self) -> None:
        self.assert_invalid(
            lambda data: data["builds"][0].pop("evidence"),
            "evidence",
        )

    def test_invalid_status(self) -> None:
        self.assert_invalid(
            lambda data: data["builds"][0].update(status="ok"),
            "status",
        )

    def test_duplicate_build(self) -> None:
        def mutate(data):
            duplicate = copy.deepcopy(data["builds"][0])
            data["builds"].append(duplicate)

        self.assert_invalid(mutate, "duplicate build")

    def test_unknown_build_key(self) -> None:
        self.assert_invalid(
            lambda data: data["builds"][0].update(extra=True),
            "unknown key",
        )

    def test_devices_ref_matches_pin(self) -> None:
        data = valid_data()
        data["devices"] = {"ref": "f79e735", "framework": ">=0.2 <0.3"}
        full = "f79e73509c1a56105c2257029589db96f284c090"
        self.assertEqual(validate_compat(data, devices_ref=full), [])
        errors = validate_compat(data, devices_ref="deadbeef")
        self.assertTrue(any("does not match pinned" in error for error in errors))

    def test_devices_tag_matches_pin(self) -> None:
        self.assertEqual(validate_compat(valid_data(), devices_ref="v0.1.0"), [])
        errors = validate_compat(valid_data(), devices_ref="v0.2.0")
        self.assertTrue(any("does not match pinned tag" in error for error in errors))

    def test_version_in_range(self) -> None:
        self.assertTrue(version_in_range("0.2.0", ">=0.2 <0.3"))
        self.assertTrue(version_in_range("0.2.9", ">=0.2, <0.3"))
        self.assertFalse(version_in_range("0.3.0", ">=0.2 <0.3"))


class RenderTests(unittest.TestCase):
    def test_confluence(self) -> None:
        text = render_confluence(
            valid_data(), tag="v0.2.0", commit="abcdef01", date="2026-11-13"
        )
        self.assertIn("# OMR Compatibility v0.2.x", text)
        self.assertIn("[OMR v0.2.0]", text)
        self.assertIn("`v0.2.0` → `abcdef01`", text)
        self.assertIn("rm-a-board", text)
        self.assertIn("已验证", text)
        self.assertIn("器件库", text)

    def test_release_notes(self) -> None:
        text = render_release_notes(
            valid_data(), tag="v0.2.0", commit="abcdef01", date="2026-11-13"
        )
        self.assertIn("## ✅ 兼容性", text)
        self.assertIn("`v0.2.0`（`abcdef01`）", text)
        self.assertIn("测试证据", text)
        self.assertIn("ADR-0030", text)


if __name__ == "__main__":
    unittest.main()
