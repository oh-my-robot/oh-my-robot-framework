#!/usr/bin/env python3
"""Validate tools/omcheck/compat.yaml.

Checks the schema, version axis declarations, build combinations and evidence.
Pass ``--tag vX.Y.Z`` during release preparation to also require that the tag
matches the declared framework version.
"""

from __future__ import annotations

import argparse
import sys

from compat_lib import CompatError, load_compat, validate_compat


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
        "--tag",
        help="release tag to check against the framework version (e.g. v0.2.0)",
    )
    parser.add_argument(
        "--devices-ref",
        help="CI-pinned devices commit; must match devices.ref",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="only print errors",
    )
    args = parser.parse_args()

    try:
        data = load_compat(args.compat)
    except CompatError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    errors = validate_compat(data, tag=args.tag, devices_ref=args.devices_ref)
    if errors:
        for error in errors:
            print(f"{args.compat}: {error}", file=sys.stderr)
        return 1

    if not args.quiet:
        builds = data.get("builds", [])
        print(
            f"{args.compat}: OK "
            f"(schema {data['schema']}, framework {data['framework']}, "
            f"{len(builds)} builds)"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
