#!/usr/bin/env python3
"""Small, dependency-free memory-safety evidence runner.

This records reproducible process-level evidence. It is intentionally not a leak
oracle by itself: sanitizer/Valgrind findings determine memory-safety failure;
RSS measurements are operational observations used by longer soak workloads.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

FINDING_PATTERNS = {
    "address_sanitizer": re.compile(r"AddressSanitizer", re.I),
    "leak_sanitizer": re.compile(r"LeakSanitizer|detected memory leaks", re.I),
    "undefined_behavior": re.compile(r"runtime error:|UndefinedBehaviorSanitizer", re.I),
    "valgrind_error": re.compile(r"ERROR SUMMARY:\s*[1-9][0-9]* errors", re.I),
    "valgrind_leak": re.compile(r"definitely lost:\s*(?!0 bytes)|indirectly lost:\s*(?!0 bytes)|possibly lost:\s*(?!0 bytes)", re.I),
}


def git_commit(cwd: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=cwd, text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except Exception:
        return "unknown"


def version_line(argv: list[str]) -> str | None:
    try:
        p = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=5)
        line = p.stdout.strip().splitlines()
        return line[0] if line else None
    except Exception:
        return None


def parse_peak_rss(stderr: str) -> int | None:
    m = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", stderr)
    if m:
        return int(m.group(1))
    return None


def detect_findings(text: str) -> list[str]:
    return [name for name, pattern in FINDING_PATTERNS.items() if pattern.search(text)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--mode", choices=("sanitizer", "valgrind", "rss"), required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--cwd", default=".")
    ap.add_argument("--iterations", type=int, default=1)
    ap.add_argument("--duration-seconds", type=float, default=0.0,
                    help="Continue whole-command repetitions until this duration is reached; 0 disables.")
    ap.add_argument("--warmup-iterations", type=int, default=0)
    ap.add_argument("--command", required=True,
                    help="Shell-style command string; parsed with shlex, not executed through a shell.")
    args = ap.parse_args()

    cwd = Path(args.cwd).resolve()
    command = shlex.split(args.command)
    if not command:
        ap.error("--command must not be empty")

    env = os.environ.copy()
    if args.mode == "sanitizer":
        env.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1:abort_on_error=1")
        env.setdefault("LSAN_OPTIONS", "exitcode=99")
        env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")

    time_bin = "/usr/bin/time" if Path("/usr/bin/time").exists() else None
    if args.mode == "valgrind":
        vg = shutil.which("valgrind")
        if not vg:
            print("error: valgrind not found", file=sys.stderr)
            return 2
        base = [vg, "--leak-check=full", "--show-leak-kinds=all",
                "--errors-for-leak-kinds=definite,indirect,possible",
                "--track-origins=yes", "--error-exitcode=99"] + command
    else:
        base = command

    compiler = os.environ.get("CXX", "c++")
    metadata = {
        "schema_version": 1,
        "project": args.project,
        "commit": git_commit(cwd),
        "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "toolchain": {
            "compiler": version_line([compiler, "--version"]),
            "valgrind": version_line(["valgrind", "--version"]) if shutil.which("valgrind") else None,
            "time": time_bin,
        },
        "mode": args.mode,
        "command": command,
        "warmup_iterations": args.warmup_iterations,
        "requested_iterations": args.iterations,
        "requested_duration_seconds": args.duration_seconds,
        "runs": [],
    }

    def run_once(kind: str, index: int) -> dict:
        wrapped = ([time_bin, "-v"] + base) if time_bin else base
        started = time.monotonic()
        p = subprocess.run(wrapped, cwd=cwd, env=env, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        elapsed = time.monotonic() - started
        combined = p.stdout + "\n" + p.stderr
        return {
            "kind": kind,
            "index": index,
            "exit_status": p.returncode,
            "elapsed_seconds": round(elapsed, 6),
            "peak_rss_kib": parse_peak_rss(p.stderr),
            "findings": detect_findings(combined),
            "stdout_tail": "\n".join(p.stdout.splitlines()[-20:]),
            "stderr_tail": "\n".join(p.stderr.splitlines()[-40:]),
        }

    for i in range(args.warmup_iterations):
        run = run_once("warmup", i + 1)
        if run["exit_status"] != 0 or run["findings"]:
            metadata["runs"].append(run)
            break
    else:
        started_all = time.monotonic()
        i = 0
        while True:
            if i >= args.iterations and (not args.duration_seconds or time.monotonic() - started_all >= args.duration_seconds):
                break
            i += 1
            run = run_once("measured", i)
            metadata["runs"].append(run)
            if run["exit_status"] != 0 or run["findings"]:
                break

    measured = [r for r in metadata["runs"] if r["kind"] == "measured"]
    peaks = [r["peak_rss_kib"] for r in measured if r["peak_rss_kib"] is not None]
    statuses = [r["exit_status"] for r in metadata["runs"]]
    findings = sorted({f for r in metadata["runs"] for f in r["findings"]})
    metadata["summary"] = {
        "completed_iterations": len(measured),
        "duration_seconds": round(sum(r["elapsed_seconds"] for r in measured), 6),
        "peak_rss_kib": max(peaks) if peaks else None,
        "rss_samples_kib": peaks,
        "findings": findings,
        "pass": bool(metadata["runs"]) and all(s == 0 for s in statuses) and not findings,
        "note": "RSS samples are process-level observations, not a leak oracle. Long-run settled-memory analysis is workload-specific.",
    }

    out = Path(args.output)
    if not out.is_absolute():
        out = cwd / out
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

    print(f"memory-safety evidence: {'PASS' if metadata['summary']['pass'] else 'FAIL'}")
    print(f"project={args.project} mode={args.mode} iterations={metadata['summary']['completed_iterations']} peak_rss_kib={metadata['summary']['peak_rss_kib']}")
    print(f"evidence={out}")
    return 0 if metadata["summary"]["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
