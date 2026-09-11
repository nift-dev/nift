#!/usr/bin/env python3
"""Fail-closed Nift version-consistency checker.

Validates that every authoritative version location in the repository agrees:

* the Nift executable version (src/CLI.cpp version_text);
* the Snap metadata version (snap/snapcraft.yaml `version:`);
* any resolved release/tag version passed on the command line.

The exact failure mode of the v4.0.12 Snap incident was
``Snap metadata = 4.0.12`` while ``binary/source version = 4.0.13``. This
checker must reject that disagreement before any packaging or publication step
can run.

Usage:
  check_version_consistency.py [--tag vX.Y.Z] [--expected X.Y.Z|vX.Y.Z]

Exit codes:
  0  all authoritative versions agree
  1  disagreement, malformed, or missing value
  2  usage error
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
TAG_RE = re.compile(r"^v\d+\.\d+\.\d+$")


def normalize_version(value: str | None) -> str | None:
    """Accept X.Y.Z or vX.Y.Z and return bare X.Y.Z, else None.

    The reusable Chocolatey/Homebrew workflows document accepting versions with
    or without a leading 'v', and release.yml passes github.ref_name (e.g.
    v4.0.13) through to them, so the checker must accept both forms.
    """
    if value is None:
        return None
    v = value[1:] if value.startswith("v") else value
    if VERSION_RE.match(v):
        return v
    return None


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def executable_version() -> str | None:
    """Return X.Y.Z from src/CLI.cpp version_text, or None if malformed/missing."""
    path = repo_root() / "src" / "CLI.cpp"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r'version_text\s*=\s*"Nift v(\d+\.\d+\.\d+)"', text)
    if not match:
        return None
    return match.group(1)


def snap_version() -> str | None:
    """Return X.Y.Z from snap/snapcraft.yaml version:, or None if malformed/missing."""
    path = repo_root() / "snap" / "snapcraft.yaml"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r'^version:\s*["\']?([^"\'\s#]+)["\']?\s*(?:#.*)?$', text, re.MULTILINE)
    if not match:
        return None
    return match.group(1)


def validate_version(value: str | None, label: str) -> int:
    if value is None:
        print(f"FAIL: {label} version is missing", file=sys.stderr)
        return 1
    if not VERSION_RE.match(value):
        print(f"FAIL: {label} version is malformed: {value!r}", file=sys.stderr)
        return 1
    return 0


def check(expected: str | None = None, tag: str | None = None) -> int:
    failures = 0

    exe = executable_version()
    snap = snap_version()

    failures += validate_version(exe, "executable (src/CLI.cpp)")
    failures += validate_version(snap, "Snap metadata (snap/snapcraft.yaml)")

    if exe is not None and snap is not None and VERSION_RE.match(exe) and VERSION_RE.match(snap):
        if exe != snap:
            print(
                f"FAIL: version disagreement: executable={exe} Snap-metadata={snap}",
                file=sys.stderr,
            )
            failures += 1

    if expected is not None:
        expected_bare = normalize_version(expected)
        if expected_bare is None:
            print(f"FAIL: expected/release version is malformed: {expected!r}",
                  file=sys.stderr)
            failures += 1
        else:
            for value, label in ((exe, "executable"), (snap, "Snap metadata")):
                if value is not None and value != expected_bare:
                    print(
                        f"FAIL: {label} {value} != expected {expected_bare}",
                        file=sys.stderr,
                    )
                    failures += 1

    if tag is not None:
        if not TAG_RE.match(tag):
            print(f"FAIL: tag is malformed: {tag!r} (expected vX.Y.Z)", file=sys.stderr)
            failures += 1
        else:
            tag_version = tag[1:]
            if expected is not None:
                expected_bare = normalize_version(expected)
                if expected_bare is not None and tag_version != expected_bare:
                    print(
                        f"FAIL: tag version {tag_version} != expected {expected_bare}",
                        file=sys.stderr,
                    )
                    failures += 1
            for value, label in ((exe, "executable"), (snap, "Snap metadata")):
                if value is not None and value != tag_version:
                    print(
                        f"FAIL: {label} {value} != tag version {tag_version}",
                        file=sys.stderr,
                    )
                    failures += 1

    if failures == 0:
        parts = [f"executable={exe}", f"Snap-metadata={snap}"]
        if expected is not None:
            parts.append(f"expected={expected}")
        if tag is not None:
            parts.append(f"tag={tag}")
        print("PASS: version consistency (" + ", ".join(parts) + ")")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected", help="expected release version X.Y.Z or vX.Y.Z")
    parser.add_argument("--tag", help="release tag vX.Y.Z")
    args = parser.parse_args(argv)
    return check(expected=args.expected, tag=args.tag)


if __name__ == "__main__":
    raise SystemExit(main())