#!/usr/bin/env python3
"""Fail-closed macOS runner-policy consistency guard.

Nift's official macOS build/verify surfaces must use a runner whose Apple
toolchain provides the floating-point std::from_chars overload used by the
released Jsonic++ v1.0.0 payload. GitHub's old macOS runners (e.g. macos-15 /
macos-15-intel) ship an older Apple libc++ that lacks that overload, so
packaging validation, the release archive build, public-installer smoke and
distribution verification must all agree on the current runners:

    macOS arm64  -> macos-latest
    macOS x86-64 -> macos-26-intel

This guard scans every maintained workflow under .github/workflows/ for matrix
include entries that name an official macOS platform and asserts the runner
policy, so the matrices cannot silently drift apart again.

Runs from the repository root:  python3 tests/macos_runner_policy_test.py
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKFLOWS = os.path.join(ROOT, ".github", "workflows", "*.yml")

# The single source of truth for the supported macOS runner policy.
RUNNER_POLICY = {
    "macos-arm64": "macos-latest",
    "macos-x86_64": "macos-26-intel",
}


def matrix_entries(path):
    """Return [(job, platform, runner)] for every matrix include entry that
    names an official macOS platform, using a light structured line scan of the
    workflow's strategy.matrix.include blocks (no third-party YAML parser).

    Handles both field-name conventions in use:
      release/distribution/homebrew:  - runner: X / platform: Y
      packaging:                      - target: Y   / os: X
    """
    entries = []
    job = None
    in_include = False
    pending_platform = None
    pending_runner = None

    def flush():
        nonlocal pending_platform, pending_runner
        if pending_platform in RUNNER_POLICY and pending_runner is not None:
            entries.append((job, pending_platform, pending_runner))
        pending_platform = None
        pending_runner = None

    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if re.match(r"^  [a-zA-Z0-9_-]+:\s*$", line):
                job = line[2:].rstrip(": \n")
                in_include = False
                flush()
                continue
            if re.match(r"^    (strategy|matrix):", line) or \
               re.match(r"^      matrix:", line):
                in_include = False
                continue
            if re.match(r"^        include:", line):
                in_include = True
                continue
            if not in_include:
                continue
            match = re.match(r"^          - (target|platform|runner|os):\s*(\S+)\s*$",
                             line)
            if match:
                flush()
                key, value = match.group(1), match.group(2)
                if key in ("target", "platform"):
                    pending_platform = value
                else:
                    pending_runner = value
                continue
            match = re.match(r"^            (target|platform|runner|os):\s*(\S+)\s*$",
                             line)
            if match:
                key, value = match.group(1), match.group(2)
                if key in ("target", "platform"):
                    pending_platform = value
                else:
                    pending_runner = value
    flush()
    return entries


def main():
    failures = []
    checked = 0
    for workflow in sorted(glob.glob(WORKFLOWS)):
        for job, platform, runner in matrix_entries(workflow):
            checked += 1
            expected = RUNNER_POLICY[platform]
            if runner != expected:
                failures.append(
                    "%s job %r: %s uses runner '%s', expected '%s'"
                    % (os.path.basename(workflow), job, platform,
                       runner, expected))
    if failures:
        print("macOS runner-policy guard FAILED (%d macOS matrix entries):"
              % checked, file=sys.stderr)
        for failure in failures:
            print("  " + failure, file=sys.stderr)
        return 1
    print("macOS runner-policy guard passed (%d official macOS matrix entries)"
          % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())