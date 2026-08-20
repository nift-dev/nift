#!/usr/bin/env python3
"""BH2 static test-integrity scanner.

Detects the false-green families that let machinery report success while
testing nothing, so a failing guard cannot disappear behind shell/CI plumbing
or a silent skip. This scanner is deliberately static, dependency-free and
auditable; it flags *patterns*, and every rule below is anchored to a concrete
misbehaviour that a guard author could commit.

Families (rule ids):
  skip-as-pass        a test prints/echoes a SKIP message and then exits 0
  prereq-exit0        a missing-tool guard exits 0 instead of a non-green code
  pipefail-missing    a shell test uses a pipeline whose status matters but
                      never enables pipefail
  swallowed-returncode a python test runs a subprocess without check=True and
                      never inspects the result's returncode
  vacuous-pass        a guard can only ever print PASS (no subprocess, no
                      assertion construct), i.e. it tests nothing

Exit status: 0 when no findings, 1 when findings exist. A JSON report is
written to --output for retained evidence.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

PY_SKIP_EXIT0 = re.compile(
    r"print\(\s*[\"'][^\"']*SKIP[^\"']*[\"']\s*\)[^\n]{0,120}\n[ \t]*"
    r"(?:raise\s+)?SystemExit\(0\)"
)
SH_SKIP_EXIT0 = re.compile(
    r"(?:echo|printf)\s+[\"'][^\"']*SKIP[^\"']*[\"']\s*(?:[|>][^\n]*)?\n[ \t]*exit 0"
)
PY_PREREQ_EXIT0 = re.compile(
    r"(?:not\s+[A-Za-z_][A-Za-z0-9_]*\.exists\(\)|which\s*\([^)]*\)\s*is\s*None)\s*:?\s*\n"
    r"[ \t]*(?:print\([^\n]*\)\n)?[ \t]*(?:raise\s+)?SystemExit\(0\)"
)
SH_EXIT0_AFTER_TOOL = re.compile(
    r"command\s+-v\s+\S+\s*>?/dev/null\s*2>&1\s*\|\|\s*\{[^}]*exit\s+0|"
    r"if\s+!?\s*command\s+-v\s+\S+[^\n]*\n[^\n]*exit\s+0"
)
PY_SWALLOWED_RC = re.compile(
    r"(\w+)\s*=\s*subprocess\.(?:run|call)\([^\)]*\)(?![\s\S]{0,400}\1\.returncode)"
)
PY_VACUOUS = re.compile(r"(?m)\bPASS\b")


def scan_python(text: str, path: Path) -> list[dict]:
    findings: list[dict] = []
    if PY_SKIP_EXIT0.search(text):
        findings.append({"rule": "skip-as-pass", "file": str(path),
                         "detail": "SKIP message immediately followed by SystemExit(0) (silent green)"})
    if PY_PREREQ_EXIT0.search(text):
        findings.append({"rule": "prereq-exit0", "file": str(path),
                         "detail": "missing-tool guard exits 0 instead of a non-green code"})
    has_subprocess = "subprocess." in text
    if has_subprocess and not re.search(r"check=True|check_returncode|\.returncode|check_call|check_output", text):
        findings.append({"rule": "swallowed-returncode", "file": str(path),
                         "detail": "subprocess invoked without check=True and no returncode inspection anywhere"})
    # vacuous-pass: guard-looking file (mentions PASS) with no subprocess and no
    # assertion construct; it cannot fail, therefore it tests nothing.
    if PY_VACUOUS.search(text) and "PASS:" in text and not has_subprocess and not any(
        token in text for token in ("raise ", "assert ", "!=", "==", ">=", "<=", "> ", "< ")):
        findings.append({"rule": "vacuous-pass", "file": str(path),
                         "detail": "prints PASS but invokes nothing and asserts nothing"})
    return findings


def scan_shell(text: str, path: Path) -> list[dict]:
    findings: list[dict] = []
    if SH_SKIP_EXIT0.search(text):
        findings.append({"rule": "skip-as-pass", "file": str(path),
                         "detail": "SKIP message immediately followed by exit 0 (silent green)"})
    if SH_EXIT0_AFTER_TOOL.search(text):
        findings.append({"rule": "prereq-exit0", "file": str(path),
                         "detail": "missing-tool guard exits 0 instead of a non-green code"})
    has_pipe = bool(re.search(r"\| *\S+", text))
    # Only a real `set -o pipefail` / `set -euo pipefail` counts; a mention in a
    # comment or echo string does not.
    has_pipefail = bool(re.search(r"^\s*set\s+(-\S+\s+)*.*pipefail", text, re.MULTILINE))
    if has_pipe and not has_pipefail and re.search(r"\b(grep|wc|head|tail|diff|cmp)\b", text):
        findings.append({"rule": "pipefail-missing", "file": str(path),
                         "detail": "uses pipelines but never enables pipefail; a failing upstream command can be masked"})
    return findings


def scan_file(path: Path) -> list[dict]:
    suffix = path.suffix
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return [{"rule": "unreadable", "file": str(path), "detail": str(exc)}]
    if suffix == ".py":
        return scan_python(text, path)
    if suffix in {".sh", ".bash"}:
        return scan_shell(text, path)
    return []


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+", type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()

    findings: list[dict] = []
    scanned: list[str] = []
    for root in args.paths:
        if root.is_dir():
            files = sorted(root.rglob("*"))
        else:
            files = [root]
        for f in files:
            if not f.is_file() or f.suffix not in {".py", ".sh", ".bash"}:
                continue
            scanned.append(str(f))
            findings.extend(scan_file(f))

    # A test file may be either an executable test or a support module; only
    # *.py under tests/ and scripts/ that are guards are scanned for the
    # vacuous-pass family, and only when they are not obviously utility modules.
    report = {
        "schema": 1,
        "campaign": "bh2",
        "scanned_files": len(scanned),
        "findings": sorted(findings, key=lambda d: (d["rule"], d["file"])),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    if findings:
        print(f"test-integrity findings: {len(findings)}")
        for f in report["findings"]:
            print(f"  [{f['rule']}] {f['file']}: {f['detail']}")
        print(f"report={args.output}")
        return 1
    print(f"test-integrity clean: {len(scanned)} files scanned, 0 findings")
    print(f"report={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())