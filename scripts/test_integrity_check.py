#!/usr/bin/env python3
"""BH2 static test-integrity scanner.

Detects the false-green families that let machinery report success while
testing nothing, so a failing guard cannot disappear behind shell/CI plumbing
or a silent skip. This scanner is deliberately static, dependency-free and
auditable; it flags *patterns*, and every rule below is anchored to a concrete
misbehaviour that a guard author could commit.

Families (rule ids):
  skip-as-pass        a test prints/echoes a SKIP message and then exits 0
                      (via SystemExit(0), sys.exit(0), exit(0) or shell exit 0,
                      on the same line or the next statement)
  prereq-exit0        a missing-tool guard exits 0 instead of a non-green code
  pipefail-missing    a shell test pipelines a command whose failure can be
                      masked, and never enables pipefail (a failing upstream
                      segment must not be able to hide behind a green outcome)
  swallowed-returncode a python test runs a subprocess (run/call) without
                      check=True and never lets the result's returncode control
                      a conditional, assertion, raise or exit path
  vacuous-pass        a guard can only ever print PASS (no subprocess, no
                      assertion construct), i.e. it tests nothing

The swallowed-returncode rule is deliberately conservative in its idea of
"controls the outcome": it strips statically-dead blocks (if False:, if 0:,
if None:, ...) before reasoning, then follows value flow through assignments
(rc = p.returncode, tuple unpacking, flag = rc == 0, data = {... p.returncode
...}) and counts a reference as inspection only when it reaches a live use —
a line that is neither a pure print nor a dead store, so `ignored = rc` (never
read again) cannot green a guard. It is a pattern scanner with bounded
liveness, not a full data-flow analysis; BH3 owns runtime guard mutation.

Exit status: 0 when no findings, 1 when findings exist. A JSON report is
written to --output for retained evidence.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# --- Python false-green patterns --------------------------------------------

# SKIP-print followed by an exit-0 call, either later on the same line
# (print(...); sys.exit(0)) or as the next statement after optional raise.
PY_SKIP_EXIT0 = re.compile(
    r"print\(\s*[\"'][^\"']*SKIP[^\"']*[\"']\s*\)"
    r"(?:[^\n]*\n[ \t]*|[ \t]*;[ \t]*|[ \t]*\n[ \t]*)?"
    r"(?:raise\s+)?(?:SystemExit|sys\.exit|exit)\(\s*0\s*\)"
)
PY_PREREQ_EXIT0 = re.compile(
    r"(?:not\s+[A-Za-z_][A-Za-z0-9_]*\.exists\(\)|which\s*\([^)]*\)\s*is\s*None)\s*:?\s*\n"
    r"[ \t]*(?:print\([^\n]*\)\n)?[ \t]*(?:raise\s+)?(?:SystemExit|sys\.exit)\(0\)"
)
PY_VACUOUS = re.compile(r"(?m)\bPASS\b")

# Any assignment `target = expr` (including `a, b = p.returncode, p.stdout`
# tuple unpacking and `data = {...}` dict stores) forwards a result's value to
# the target name when the right-hand side mentions a result or alias.
ASSIGN_TARGETS = re.compile(r"^([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*=\s*(.*)$")

# Statically-false conditional headers whose bodies can never execute.
STATIC_FALSE = re.compile(
    r"^(?:if|elif|while|for)\s+(?:False|0|None|\(\)|\[\]|\{\}|''|\"\"|not\s+True|not\s+1)\s*:"
)

# subprocess result-producing calls; check_call/check_output raise on failure
# and are therefore inherently safe.
SUB_RESULT = re.compile(r"(\w+)\s*=\s*subprocess\.(run|call|check_call|check_output)\(")
# bare, discarded calls: subprocess.run(...) whose result is never bound
BARE_SUB_CALL = re.compile(r"subprocess\.(run|call)\(")


def _strip_py_comments(text: str) -> str:
    """Remove ``# ...`` comments while leaving strings/docstrings intact."""
    out: list[str] = []
    state: str | None = None
    i = 0
    n = len(text)
    while i < n:
        if state is None:
            if text.startswith('"""', i) or text.startswith("'''", i):
                tok = text[i:i + 3]
                state = tok
                out.append(tok)
                i += 3
                continue
            c = text[i]
            if c in "\"'":
                state = c
                out.append(c)
                i += 1
                continue
            if c == "#":
                while i < n and text[i] != "\n":
                    i += 1
                continue
            out.append(c)
            i += 1
        else:
            c = text[i]
            if c == "\\" and i + 1 < n:
                out.append(c)
                out.append(text[i + 1])
                i += 2
                continue
            if text.startswith(state, i):
                out.append(state)
                i += len(state)
                state = None
                continue
            out.append(c)
            i += 1
    return "".join(out)


def _strip_dead_blocks(text: str) -> str:
    """Remove bodies of statically-false if/while blocks so dead references
    (e.g. ``if False: print(p.returncode)``) never satisfy an inspection rule."""
    out: list[str] = []
    dead_stack: list[int] = []
    for line in text.splitlines():
        stripped = line.lstrip()
        indent = len(line) - len(stripped)
        if not stripped:
            out.append(line)
            continue
        while dead_stack and indent <= dead_stack[-1]:
            dead_stack.pop()
        if STATIC_FALSE.match(stripped):
            dead_stack.append(indent)
            continue
        if dead_stack:
            continue
        out.append(line)
    return "\n".join(out)


def _pure_print(line: str) -> bool:
    return bool(re.match(r"^\s*print\(", line))


def _store_targets(line: str) -> list[str]:
    m = ASSIGN_TARGETS.match(line)
    if not m:
        return []
    return [t.strip() for t in m.group(1).split(",") if t.strip()]


def _var_inspected(refs: set[str], bind: set[int], lines: list[str]) -> bool:
    """True when a result's value reaches a live use.

    A reference counts as inspection only when it is neither a pure print nor a
    dead store. A store is dead when none of its targets is subsequently read
    on a live line, so ``ignored = rc`` (never read again) cannot green a
    guard, while ``data = {..., "exit": p.returncode}`` feeding a later check
    is a genuine inspection. Decided by a backward liveness fixpoint over
    reference lines.
    """
    ref_pat = re.compile(r"\b(?:" + "|".join(re.escape(r) for r in refs) + r")\b")
    candidates = [
        i
        for i, line in enumerate(lines)
        if i not in bind and ref_pat.search(line) and not _pure_print(line)
    ]
    live: set[int] = set()
    changed = True
    while changed:
        changed = False
        for i in sorted(candidates, reverse=True):
            if i in live:
                continue
            if not _store_targets(lines[i]):
                live.add(i)
                changed = True
                continue
            for j in range(i + 1, len(lines)):
                if not ref_pat.search(lines[j]):
                    continue
                if _pure_print(lines[j]):
                    continue
                if _store_targets(lines[j]):
                    if j in live:
                        live.add(i)
                        changed = True
                        break
                    continue
                live.add(i)
                changed = True
                break
    return any(i in live for i in candidates)


def _find_swallowed_returncode(text: str) -> list[str]:
    body = _strip_dead_blocks(text)
    lines = body.splitlines()
    # var -> (set of refs that name it, set of binding-line indices)
    result_vars: dict[str, set[str]] = {}
    binding_lines: dict[str, set[int]] = {}
    safe: set[str] = set()
    for m in SUB_RESULT.finditer(body):
        var, method = m.group(1), m.group(2)
        result_vars.setdefault(var, set()).add(var)
        binding_lines.setdefault(var, set()).add(body.count("\n", 0, m.start()))
        if method in ("check_call", "check_output"):
            safe.add(var)
        elif "check=True" in body[m.start(): m.start() + 200]:
            safe.add(var)
    # Alias / derivation fixpoint: `rc = p.returncode`, tuple unpacking
    # (`rc, out = p.returncode, p.stdout`), `flag = rc == 0`, and
    # `data = {... p.returncode ...}` all forward a result's value to the
    # target name; the forwarding line is then a binding, never a use.
    changed = True
    while changed:
        changed = False
        for idx, line in enumerate(lines):
            targets = _store_targets(line)
            if not targets:
                continue
            for tgt in targets:
                for var, refs in list(result_vars.items()):
                    if tgt in refs:
                        continue
                    ref_pat = re.compile(
                        r"\b(?:" + "|".join(re.escape(r) for r in refs) + r")\b"
                    )
                    if ref_pat.search(line):
                        result_vars[var].add(tgt)
                        binding_lines.setdefault(var, set()).add(idx)
                        changed = True
    flagged: list[str] = []
    for var, refs in result_vars.items():
        if var in safe:
            continue
        bind = binding_lines.get(var, set())
        if not _var_inspected(refs, bind, lines):
            flagged.append(var)
    # Discarded bare calls: subprocess.run(...) not bound to a variable and not
    # returned/used. A `return subprocess.run(...)` hands the result upward and
    # a chained .attr/[...] uses it; neither is discarded.
    for m in BARE_SUB_CALL.finditer(body):
        line_start = body[: m.start()].rfind("\n") + 1
        line = body[line_start:]
        line = line[: line.find("\n")] if "\n" in line else line
        if re.search(r"\w+\s*=\s*$", line[: m.start() - line_start]):
            continue
        if re.search(r"(?:^|[;\s])(?:return|yield)\s+[^;\n]*$", line[: m.start() - line_start]):
            continue
        call_end = body.find(")", m.end() - 1)
        if call_end != -1 and line[call_end - line_start + 1:call_end - line_start + 2] in {".", "["}:
            continue
        if "check=True" not in body[m.start(): m.start() + 200]:
            flagged.append("(discarded)")
    return sorted(set(flagged))


def scan_python(text: str, path: Path) -> list[dict]:
    findings: list[dict] = []
    clean = _strip_py_comments(text)
    if PY_SKIP_EXIT0.search(clean):
        findings.append({"rule": "skip-as-pass", "file": str(path),
                         "detail": "SKIP message immediately followed by an exit-0 call (silent green)"})
    if PY_PREREQ_EXIT0.search(clean):
        findings.append({"rule": "prereq-exit0", "file": str(path),
                         "detail": "missing-tool guard exits 0 instead of a non-green code"})
    if "subprocess." in clean:
        for var in _find_swallowed_returncode(clean):
            findings.append({"rule": "swallowed-returncode", "file": str(path),
                             "detail": f"subprocess result {var!r} never controls a failure path (no check=True, "
                                       "no gating use of its returncode)"})
    if PY_VACUOUS.search(clean) and "PASS:" in clean and "subprocess." not in clean and not any(
        token in clean for token in ("raise ", "assert ", "!=", "==", ">=", "<=", "> ", "< ")):
        findings.append({"rule": "vacuous-pass", "file": str(path),
                         "detail": "prints PASS but invokes nothing and asserts nothing"})
    return findings


# --- Shell false-green patterns ---------------------------------------------

SH_SKIP_EXIT0 = re.compile(
    r"(?:echo|printf)\s+[\"'][^\"']*SKIP[^\"']*[\"']\s*(?:[|>][^\n]*)?\n[ \t]*exit 0"
)
SH_EXIT0_AFTER_TOOL = re.compile(
    r"command\s+-v\s+\S+\s*>?/dev/null\s*2>&1\s*\|\|\s*\{[^}]*exit\s+0|"
    r"if\s+!?\s*command\s+-v\s+\S+[^\n]*\n[^\n]*exit\s+0"
)

# Commands whose non-zero status inside a pipeline is conventionally
# insignificant (pure text transforms / sinks). Anything else piped without
# pipefail is a candidate for a masked failure.
SHELL_PIPE_FILTERS = {
    "sed", "awk", "tr", "sort", "cut", "head", "tail", "grep", "egrep", "fgrep",
    "wc", "cat", "tee", "uniq", "printf", "echo", "xargs", "diff", "cmp", "comm",
    "join", "paste", "rev", "fold", "nl", "column", "sha256sum", "md5sum",
}


def _logical_lines(text: str) -> str:
    """Join shell physical lines continued with a trailing backslash so a
    pipeline split across lines (``false \\\\`` then ``| cat``) is analyzed as
    the single logical line the shell actually runs."""
    out: list[str] = []
    buf = ""
    for raw in text.splitlines():
        buf = (buf + " " + raw) if buf else raw
        if re.search(r"\\\s*$", buf):
            continue
        out.append(buf)
        buf = ""
    if buf:
        out.append(buf)
    return "\n".join(out)


def _strip_dead_shell_blocks(text: str) -> str:
    """Remove bodies of statically-dead shell conditionals so a ``set -o
    pipefail`` inside ``if false; then ...; fi`` (or a one-line
    ``if false; then set -o pipefail; fi``) can never count as enabled.

    The ``else``/``elif`` branches of a dead ``if`` are live and preserved.
    """
    lines = text.splitlines()
    out: list[str] = []
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        s = line.strip()
        one_line = re.match(
            r"^(?:if\s+false\s*;\s*then|while\s+false\s*;\s*do)\s+[^;]*;\s*(?:fi|done)\s*$",
            s,
        )
        if one_line:
            i += 1
            continue
        if re.match(r"^(?:if\s+false\s*;?\s*then|while\s+false\s*;\s*do)\s*$", s):
            i += 1
            depth = 1
            while i < n and depth > 0:
                sub = lines[i].strip()
                if re.match(r"^(?:if|while|for)\b", sub):
                    depth += 1
                elif re.match(r"^(?:fi|done)\b", sub):
                    depth -= 1
                elif re.match(r"^(?:else|elif)\b", sub) and depth == 1:
                    break
                i += 1
            continue
        out.append(line)
        i += 1
    return "\n".join(out)


def _split_pipeline(line: str) -> list[str]:
    # Remove simple quoted strings so Nift template text (" | ") does not count.
    stripped = re.sub(r"'[^']*'|\"[^\"]*\"", "", line)
    return stripped.split("|")


def _shell_has_unsafe_pipeline(text: str) -> str | None:
    """Return a reason string when a pipeline can mask a failing upstream
    segment (no pipefail, and an upstream command outside the pure-filter set)."""
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or "|" not in line:
            continue
        segs = _split_pipeline(line)
        if len(segs) < 2:
            continue
        for seg in segs[:-1]:
            seg = seg.strip().lstrip("$( ").lstrip("!").strip()
            if not seg:
                continue
            first = seg.split()[0]
            if first in {">", "<", "2>", "1>", ">>", "&&", "||"} or first.endswith("\\"):
                continue
            if first not in SHELL_PIPE_FILTERS:
                return f"pipeline {line!r} can mask failure of upstream {first!r}"
    return None


def scan_shell(text: str, path: Path) -> list[dict]:
    findings: list[dict] = []
    if SH_SKIP_EXIT0.search(text):
        findings.append({"rule": "skip-as-pass", "file": str(path),
                         "detail": "SKIP message immediately followed by exit 0 (silent green)"})
    if SH_EXIT0_AFTER_TOOL.search(text):
        findings.append({"rule": "prereq-exit0", "file": str(path),
                         "detail": "missing-tool guard exits 0 instead of a non-green code"})
    # Join continued physical lines, then drop statically-dead blocks, so a
    # pipefail enabled only in an `if false; then ... fi` block or a pipeline
    # hidden behind a trailing backslash can neither count nor escape.
    analyzed = _strip_dead_shell_blocks(_logical_lines(text))
    has_pipe = bool(re.search(r"\| *\S+", analyzed))
    # Only a real `set -o pipefail` / `set -euo pipefail` counts; a mention in a
    # comment or echo string does not.
    has_pipefail = bool(re.search(r"^\s*set\s+(-\S+\s+)*.*pipefail", analyzed, re.MULTILINE))
    if has_pipe and not has_pipefail:
        reason = _shell_has_unsafe_pipeline(analyzed)
        if reason:
            findings.append({"rule": "pipefail-missing", "file": str(path),
                             "detail": f"uses pipelines but never enables pipefail; {reason}"})
        elif re.search(r"\b(grep|wc|head|tail|diff|cmp)\b", text):
            findings.append({"rule": "pipefail-missing", "file": str(path),
                             "detail": "uses pipelines but never enables pipefail; a failing upstream command can be masked"})
    return findings


# --- dispatch ---------------------------------------------------------------

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