#!/usr/bin/env python3
"""Focused tests for the Nift version-consistency checker.

Covers the required cases:
* matching development versions;
* matching release versions;
* the exact v4.0.12 failure: Snap metadata = 4.0.12, binary/source = 4.0.13;
* malformed and missing version values;
* tag/version mismatch.

The checker is pure and importable; tests exercise the real repo files plus
temporary fixture trees for the failure cases.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class VersionConsistencyTest(unittest.TestCase):
    def setUp(self) -> None:
        import sys

        sys.path.insert(0, str(ROOT / "scripts"))
        import check_version_consistency as vc

        self.vc = vc

    def test_real_repo_development_versions_match(self) -> None:
        vc = self.vc
        exe = vc.executable_version()
        snap = vc.snap_version()
        self.assertEqual(exe, "4.0.13")
        self.assertEqual(snap, "4.0.13")
        self.assertEqual(vc.check(expected="4.0.13", tag="v4.0.13"), 0)

    def test_exact_412_413_failure_rejected(self) -> None:
        vc = self.vc

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = root / "src"
            snap = root / "snap"
            src.mkdir()
            snap.mkdir()
            (src / "CLI.cpp").write_text(
                'constexpr const char* version_text = "Nift v4.0.13";\n',
                encoding="utf-8",
            )
            (snap / "snapcraft.yaml").write_text("version: '4.0.12'\n", encoding="utf-8")

            old = vc.repo_root
            vc.repo_root = lambda: root
            try:
                self.assertEqual(vc.executable_version(), "4.0.13")
                self.assertEqual(vc.snap_version(), "4.0.12")
                self.assertEqual(vc.check(expected="4.0.13"), 1)
                self.assertEqual(vc.check(expected="4.0.12"), 1)
                self.assertEqual(vc.check(), 1)
            finally:
                vc.repo_root = old

    def test_matching_release_versions_agree(self) -> None:
        vc = self.vc
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "src").mkdir()
            (root / "snap").mkdir()
            (root / "src" / "CLI.cpp").write_text(
                'constexpr const char* version_text = "Nift v4.0.13";\n',
                encoding="utf-8",
            )
            (root / "snap" / "snapcraft.yaml").write_text(
                "version: '4.0.13'\n", encoding="utf-8"
            )
            old = vc.repo_root
            vc.repo_root = lambda: root
            try:
                self.assertEqual(vc.check(expected="4.0.13", tag="v4.0.13"), 0)
            finally:
                vc.repo_root = old

    def test_malformed_version_rejected(self) -> None:
        vc = self.vc
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "src").mkdir()
            (root / "snap").mkdir()
            (root / "src" / "CLI.cpp").write_text(
                'constexpr const char* version_text = "Nift v4.0.x";\n',
                encoding="utf-8",
            )
            (root / "snap" / "snapcraft.yaml").write_text(
                "version: 'four.zero.thirteen'\n", encoding="utf-8"
            )
            old = vc.repo_root
            vc.repo_root = lambda: root
            try:
                self.assertEqual(vc.check(), 1)
            finally:
                vc.repo_root = old

    def test_missing_version_rejected(self) -> None:
        vc = self.vc
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "src").mkdir()
            (root / "snap").mkdir()
            (root / "src" / "CLI.cpp").write_text(
                "constexpr const char* version_text = 0;\n", encoding="utf-8"
            )
            (root / "snap" / "snapcraft.yaml").write_text(
                "# no version\n", encoding="utf-8"
            )
            old = vc.repo_root
            vc.repo_root = lambda: root
            try:
                self.assertEqual(vc.check(), 1)
            finally:
                vc.repo_root = old

    def test_tag_version_mismatch_rejected(self) -> None:
        vc = self.vc
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "src").mkdir()
            (root / "snap").mkdir()
            (root / "src" / "CLI.cpp").write_text(
                'constexpr const char* version_text = "Nift v4.0.13";\n',
                encoding="utf-8",
            )
            (root / "snap" / "snapcraft.yaml").write_text(
                "version: '4.0.13'\n", encoding="utf-8"
            )
            old = vc.repo_root
            vc.repo_root = lambda: root
            try:
                # Tag disagrees with the repo versions.
                self.assertEqual(vc.check(tag="v4.0.12"), 1)
            finally:
                vc.repo_root = old


if __name__ == "__main__":
    unittest.main(verbosity=2)