#!/usr/bin/env python3
"""Record the selected-channel results of the Distribution Verification workflow.

Consumes the per-channel selection/result/status environment produced by the
summary job and writes a normalized summary JSON plus a plain-text human
classification. Maintained release channels are GitHub release archives, the
curl installer (via the release job), Homebrew, Chocolatey and Snap stable.
Snap edge is a promotion-readiness check and is never reported as stable
success.

No network access and no store operations are performed.
"""

from __future__ import annotations

import json
import os
from pathlib import Path


def env_bool(name: str) -> bool:
    return os.environ.get(f"{name.upper()}_SELECTED", "false").strip().lower() == "true"


def env_result(name: str) -> str:
    return os.environ.get(f"{name.upper()}_RESULT", "skipped")


def classify_snap(edge: str | None, stable: str | None) -> dict[str, str]:
    """Classify the combined Snap edge/stable state.

    `edge` and `stable` are each one of 'passed', 'version_mismatch',
    'install_runtime_failure', 'not_selected' or None. The classification
    prioritises the maintained public-release gate (stable) and never reports
    edge as stable.
    """
    if stable == "passed":
        return {"classification": "stable_correct"}
    if edge == "passed":
        return {"classification": "edge_correct_stable_pending"}
    mismatches = [v for v in (edge, stable) if v == "version_mismatch"]
    if mismatches:
        return {"classification": "version_mismatch"}
    failures = [v for v in (edge, stable) if v == "install_runtime_failure"]
    if failures:
        return {"classification": "install_runtime_failure"}
    return {"classification": "not_selected"}


def main() -> int:
    expected_version = os.environ.get("EXPECTED_VERSION", "")
    validate_result = env_result("VALIDATE_VERSION")

    maintained = ["github_release", "homebrew", "chocolatey"]
    channels: dict[str, dict[str, object]] = {}
    for name in maintained:
        channels[name] = {
            "selected": env_bool(name),
            "result": env_result(name),
        }

    edge = {
        "selected": env_bool("SNAP_EDGE"),
        "result": env_result("SNAP_EDGE"),
        "status": os.environ.get("SNAP_EDGE_STATUS", "not_selected"),
    }
    stable = {
        "selected": env_bool("SNAP_STABLE"),
        "result": env_result("SNAP_STABLE"),
        "status": os.environ.get("SNAP_STABLE_STATUS", "not_selected"),
    }
    snap_class = classify_snap(edge["status"], stable["status"])

    data: dict[str, object] = {
        "schema": 2,
        "expected_version": expected_version,
        "validate_version_result": validate_result,
        "maintained_channels": channels,
        "snap": {
            "edge": edge,
            "stable": stable,
            "classification": snap_class["classification"],
        },
    }

    out = Path(".build/distribution/summary.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(json.dumps(data, indent=2, sort_keys=True))
    print(f"snap classification: {snap_class['classification']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())