#!/usr/bin/env python3
"""Verify the exact vendored libffi 3.8.0 release source tree."""

import argparse
import hashlib
from pathlib import Path


EXPECTED_TREE_SHA256 = "5dbcaaf332ef970cba42bca8e65a0c39c6b72d1550f4ef67bbc35c3ed09e77b8"


def tree_digest(root: Path) -> str:
    digest = hashlib.sha256()
    paths = sorted(root.rglob("*"), key=lambda path: path.relative_to(root).as_posix())
    for path in paths:
        if not path.is_file() or path.name == "NIFT-PROVENANCE.md":
            continue
        relative = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(relative)
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--print-digest", action="store_true")
    parser.add_argument("root", nargs="?", default="third_party/libffi")
    args = parser.parse_args()
    root = Path(args.root)
    actual = tree_digest(root)
    if args.print_digest:
        print(actual)
    if actual != EXPECTED_TREE_SHA256:
        print(
            f"vendored libffi source integrity failure: expected "
            f"{EXPECTED_TREE_SHA256}, got {actual}",
            file=__import__("sys").stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
