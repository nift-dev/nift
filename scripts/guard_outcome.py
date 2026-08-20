#!/usr/bin/env python3
"""BH2 shared guard outcome semantics.

A guard is any executable that decides whether a guarantee held during a run.
Nift guards agree on one exit-code contract so that Makefile targets, CI jobs,
reviewers, and humans classify every result identically. Nothing here may ever
report a skipped or unsupported run as success.

Exit codes (stable contract; do not reuse):
    0   PASS         the guard executed its assertions and all of them held
    1   FAIL         the guard executed its assertions and at least one broke
    2   SKIP         the guard could not execute (prerequisite/tool absent);
                     deliberately NOT a success result
    3   UNSUPPORTED  the platform/toolchain cannot run this guard at all
    124 TIMEOUT      the guard exceeded its time budget (conventional)

Consumers must treat 0 as the only green code. In a gating context a guard
that SKIPs is not green unless the gate explicitly records an exception.
"""
from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path
from typing import NoReturn

PASS = 0
FAIL = 1
SKIP = 2
UNSUPPORTED = 3
TIMEOUT = 124

NAMES = {
    PASS: "PASS",
    FAIL: "FAIL",
    SKIP: "SKIP",
    UNSUPPORTED: "UNSUPPORTED",
    TIMEOUT: "TIMEOUT",
}


class GuardSkip(Exception):
    """Prerequisite absent; callers should finish with SKIP."""


class GuardUnsupported(Exception):
    """Platform/toolchain cannot run this guard; callers finish with UNSUPPORTED."""


def classify(rc: int) -> str:
    return NAMES.get(rc, f"ERROR({rc})")


def finish(outcome: int, message: str) -> "NoReturn":
    """Print '<OUTCOME>: message' and exit with the contract code."""
    print(f"{NAMES[outcome]}: {message}")
    raise SystemExit(outcome)


def require_tool(path: str | Path, label: str | None = None) -> Path:
    """Return the tool path or raise GuardSkip (never silently continue)."""
    p = Path(path)
    if not p.exists():
        raise GuardSkip(f"required tool unavailable: {label or p} ({p})")
    return p


def run(argv, *, cwd=None, env=None, timeout: float | None = None):
    """Run a subprocess and map its outcome onto the guard contract.

    TimeoutExpired -> (TIMEOUT, partial stdout/stderr); FileNotFoundError for
    the executable itself -> GuardSkip. The tested command's exit status is
    preserved exactly when it finishes on its own.
    """
    started = time.monotonic()
    try:
        p = subprocess.run(
            argv, cwd=cwd, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        return TIMEOUT, exc.stdout or "", exc.stderr or "", time.monotonic() - started
    except FileNotFoundError as exc:
        raise GuardSkip(f"command not found: {argv[0]}") from exc
    return p.returncode, p.stdout, p.stderr, time.monotonic() - started


def run_and_report(argv, *, cwd=None, env=None, timeout: float | None = None,
                   label: str | None = None) -> int:
    """Run a command, print its outcome line, and return the contract code.

    The command's own stdout/stderr pass through so a reviewer can see why a
    guard failed; the guard outcome line is printed last.
    """
    name = label or (argv[0] if isinstance(argv, list) else str(argv))
    rc, out, err, elapsed = run(argv, cwd=cwd, env=env, timeout=timeout)
    if out:
        sys.stdout.write(out if out.endswith("\n") else out + "\n")
    if err:
        sys.stderr.write(err if err.endswith("\n") else err + "\n")
    finish(rc, f"{name} finished in {elapsed:.3f}s (exit {rc})")
    return rc  # unreachable; finish() always exits