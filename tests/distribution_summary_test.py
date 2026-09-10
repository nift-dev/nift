#!/usr/bin/env python3
"""Focused unit tests for the Distribution Verification summary logic.

The summary script must never report Snap edge success as stable success and
must distinguish stable-correct, edge-correct-with-stable-pending, version
mismatch and installation/runtime failure. No network access is used.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "distribution_summary.py"

BASE_ENV = {
    "EXPECTED_VERSION": "4.0.12",
    "VALIDATE_VERSION_RESULT": "success",
    "GITHUB_RELEASE_SELECTED": "true",
    "GITHUB_RELEASE_RESULT": "success",
    "HOMEBREW_SELECTED": "true",
    "HOMEBREW_RESULT": "success",
    "CHOCOLATEY_SELECTED": "true",
    "CHOCOLATEY_RESULT": "success",
}


def run_summary(**overrides) -> dict:
    env = dict(os.environ)
    env.update(BASE_ENV)
    env.update({k: str(v) for k, v in overrides.items()})
    with tempfile.TemporaryDirectory() as tmp:
        cwd = Path(tmp)
        proc = subprocess.run(
            [sys.executable, str(SCRIPT)],
            cwd=cwd,
            env=env,
            text=True,
            capture_output=True,
        )
        summary = (cwd / ".build" / "distribution" / "summary.json").read_text()
    return {"proc": proc, "summary": json.loads(summary)}


class DistributionSummaryTest(unittest.TestCase):
    def test_edge_success_is_not_stable_success(self) -> None:
        result = run_summary(
            SNAP_EDGE_SELECTED="true",
            SNAP_EDGE_RESULT="success",
            SNAP_EDGE_STATUS="passed",
            SNAP_STABLE_SELECTED="true",
            SNAP_STABLE_RESULT="failure",
            SNAP_STABLE_STATUS="version_mismatch",
        )
        self.assertEqual(result["proc"].returncode, 0)
        snap = result["summary"]["snap"]
        self.assertEqual(snap["edge"]["status"], "passed")
        self.assertEqual(snap["stable"]["status"], "version_mismatch")
        self.assertEqual(snap["classification"], "edge_correct_stable_pending")

    def test_stable_correct(self) -> None:
        result = run_summary(
            SNAP_EDGE_SELECTED="true",
            SNAP_EDGE_RESULT="success",
            SNAP_EDGE_STATUS="passed",
            SNAP_STABLE_SELECTED="true",
            SNAP_STABLE_RESULT="success",
            SNAP_STABLE_STATUS="passed",
        )
        self.assertEqual(result["summary"]["snap"]["classification"], "stable_correct")

    def test_version_mismatch_classified(self) -> None:
        result = run_summary(
            SNAP_EDGE_SELECTED="false",
            SNAP_EDGE_RESULT="skipped",
            SNAP_EDGE_STATUS="not_selected",
            SNAP_STABLE_SELECTED="true",
            SNAP_STABLE_RESULT="failure",
            SNAP_STABLE_STATUS="version_mismatch",
        )
        self.assertEqual(result["summary"]["snap"]["classification"], "version_mismatch")

    def test_install_runtime_failure_classified(self) -> None:
        result = run_summary(
            SNAP_EDGE_SELECTED="false",
            SNAP_EDGE_RESULT="skipped",
            SNAP_EDGE_STATUS="not_selected",
            SNAP_STABLE_SELECTED="true",
            SNAP_STABLE_RESULT="failure",
            SNAP_STABLE_STATUS="install_runtime_failure",
        )
        self.assertEqual(
            result["summary"]["snap"]["classification"], "install_runtime_failure"
        )

    def test_not_selected(self) -> None:
        result = run_summary(
            SNAP_EDGE_SELECTED="false",
            SNAP_EDGE_RESULT="skipped",
            SNAP_EDGE_STATUS="not_selected",
            SNAP_STABLE_SELECTED="false",
            SNAP_STABLE_RESULT="skipped",
            SNAP_STABLE_STATUS="not_selected",
        )
        self.assertEqual(result["summary"]["snap"]["classification"], "not_selected")

    def test_validate_version_failure_recorded(self) -> None:
        result = run_summary(
            VALIDATE_VERSION_RESULT="failure",
            SNAP_EDGE_SELECTED="false",
            SNAP_EDGE_RESULT="skipped",
            SNAP_EDGE_STATUS="not_selected",
            SNAP_STABLE_SELECTED="false",
            SNAP_STABLE_RESULT="skipped",
            SNAP_STABLE_STATUS="not_selected",
        )
        self.assertEqual(result["summary"]["validate_version_result"], "failure")


if __name__ == "__main__":
    unittest.main(verbosity=2)