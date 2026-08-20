#!/usr/bin/env python3
"""Structural validator for docs/guarantees/registry.json.

BH1 intentionally keeps this checker dumb and auditable. It verifies references,
allowed states, declared CI jobs, claim-source needles when sibling repositories
are available, and internal cross-links. It does not try to decide whether prose
semantically overstates evidence; that remains a human/agent audit backed by the
registry.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

ALLOWED_STATES = {"ESTABLISHED", "NOT_ESTABLISHED", "ACCEPTED_LIMITATION", "OUT_OF_SCOPE"}
ALLOWED_EVIDENCE = {"RETAINED", "CI_GATED", "CROSS_PLATFORM_GATED", "RELEASE_GATED", "CAMPAIGN", "FIELD"}
ALLOWED_TOT = {"VERIFIED_GUARD", "UNPROVEN", "PENDING_REVIEWER", "NOT_APPLICABLE"}
ALLOWED_ENFORCEMENT = {"MANUAL", "CI_GATED", "CROSS_PLATFORM_GATED", "RELEASE_GATED", "SCHEDULED"}
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*$")


class CheckFailure(Exception):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise CheckFailure(f"registry-not-found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise CheckFailure(f"registry-json-invalid: {path}:{exc.lineno}:{exc.colno}: {exc.msg}") from exc
    if not isinstance(data, dict):
        raise CheckFailure("registry-root-invalid: expected JSON object")
    return data


def repo_roots(registry_path: Path, data: dict[str, Any], args: argparse.Namespace) -> dict[str, Path | None]:
    nift_root = Path(args.nift_root).resolve() if args.nift_root else registry_path.resolve().parents[2]
    roots: dict[str, Path | None] = {"nift": nift_root}
    configured = data.get("repositories", {})
    if not isinstance(configured, dict):
        raise CheckFailure("repositories-invalid: expected object")
    explicit = {"website": args.website_root, "regression": args.regression_root}
    for name in ("website", "regression"):
        if explicit[name]:
            roots[name] = Path(explicit[name]).resolve()
            continue
        rel = configured.get(name)
        if isinstance(rel, str):
            candidate = (nift_root / rel).resolve()
            roots[name] = candidate if candidate.exists() else None
        else:
            roots[name] = None
    return roots


def path_for(ref: dict[str, Any], roots: dict[str, Path | None]) -> tuple[Path | None, str]:
    repo = ref.get("repo")
    path = ref.get("path")
    if repo not in roots:
        raise CheckFailure(f"reference-repo-invalid: {repo!r}")
    if not isinstance(path, str) or not path or Path(path).is_absolute() or ".." in Path(path).parts:
        raise CheckFailure(f"reference-path-invalid: repo={repo!r} path={path!r}")
    root = roots[repo]
    return ((root / path) if root is not None else None, repo)


def validate_ref(ref: Any, roots: dict[str, Path | None], label: str, require_needle: bool = False) -> bool:
    if not isinstance(ref, dict):
        raise CheckFailure(f"{label}-invalid: expected object")
    path, repo = path_for(ref, roots)
    needle = ref.get("needle")
    if require_needle and (not isinstance(needle, str) or not needle):
        raise CheckFailure(f"{label}-needle-missing")
    if needle is not None and not isinstance(needle, str):
        raise CheckFailure(f"{label}-needle-invalid")
    if path is None:
        return False
    if not path.is_file():
        raise CheckFailure(f"{label}-missing: {repo}:{ref['path']}")
    if needle is not None:
        text = path.read_text(encoding="utf-8", errors="replace")
        if needle not in text:
            raise CheckFailure(f"{label}-needle-not-found: {repo}:{ref['path']}: {needle!r}")
    return True



def validate_guard_ref(ref: Any, roots: dict[str, Path | None], label: str) -> bool:
    """Require guard refs to name executable/test artifacts, not passive prose."""
    checked = validate_ref(ref, roots, label)
    repo = ref.get("repo") if isinstance(ref, dict) else None
    path = ref.get("path") if isinstance(ref, dict) else None
    if repo not in {"nift", "regression"}:
        raise CheckFailure(f"{label}-repo-not-executable: {repo!r}")
    if not isinstance(path, str):
        raise CheckFailure(f"{label}-path-invalid")
    name = Path(path).name
    suffix = Path(path).suffix
    if name == "Makefile":
        needle = ref.get("needle")
        if not isinstance(needle, str) or not needle.strip():
            raise CheckFailure(f"{label}-make-target-needle-missing")
    elif suffix not in {".py", ".sh", ".cpp", ".cc", ".cxx", ".yml", ".yaml"}:
        raise CheckFailure(f"{label}-not-executable-artifact: {repo}:{path}")
    return checked


def make_target_from_command(command: str) -> tuple[Path, str] | None:
    """Extract the Makefile and target from the simple make commands used here."""
    parts = command.split()
    if not parts or parts[0] != "make":
        return None
    directory = Path(".")
    i = 1
    if i < len(parts) and parts[i] == "-C":
        if i + 1 >= len(parts):
            return None
        directory = Path(parts[i + 1])
        i += 2
    while i < len(parts) and (parts[i].startswith("-") or "=" in parts[i]):
        i += 1
    if i >= len(parts):
        return None
    return directory / "Makefile", parts[i]


def makefile_has_target(path: Path, target: str) -> bool:
    if not path.is_file():
        return False
    pattern = re.compile(rf"^{re.escape(target)}\s*:(?:\s|$)", re.MULTILINE)
    return bool(pattern.search(path.read_text(encoding="utf-8", errors="replace")))

def workflow_triggers(path: Path) -> set[str]:
    """Return top-level workflow triggers from the ordinary block-style or
    inline ``on:`` YAML forms used here. Unsupported valid GitHub Actions forms
    fail closed (reported as missing) rather than green.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    triggers: set[str] = set()
    in_on = False
    for line in lines:
        if re.match(r"^on:\s*(?:#.*)?$", line):
            in_on = True
            continue
        inline = re.match(r"^on:\s*\[([^\]]+)\]\s*$", line)
        if inline:
            triggers.update(t.strip() for t in inline.group(1).split(",") if t.strip())
            continue
        single = re.match(r"^on:\s*([A-Za-z_][A-Za-z0-9_-]*)\s*(?:#.*)?$", line)
        if single:
            triggers.add(single.group(1))
            continue
        if not in_on:
            continue
        if line and not line[0].isspace():
            break
        match = re.match(r"^  ([A-Za-z_][A-Za-z0-9_-]*):(?:\s|$)", line)
        if match:
            triggers.add(match.group(1))
    return triggers


def _workflow_job_block(lines: list[str], job: str) -> list[str] | None:
    """Return the indented lines of a job block, or None if the job key is absent."""
    start = None
    for i, line in enumerate(lines):
        if re.match(rf"^  {re.escape(job)}:\s*(?:#.*)?$", line):
            start = i
            break
    if start is None:
        return None
    block: list[str] = []
    for line in lines[start + 1:]:
        if line.strip() and not line[0].isspace():
            break
        block.append(line)
    return block


def _expr_truth(expr: str):
    """Conservative three-valued truth of a GitHub Actions expression.

    Returns True / False / None (unknown). Only expressions that are provably
    always-false yield False; anything the small evaluator cannot decide yields
    None, which callers treat as "live" (never a false rejection). This is a
    definite-false subset of the Actions expression language, not a full
    evaluator: function calls other than always() are unknown, github.*/secrets/
    vars/inputs/matrix/needs/environment lookups are unknown, and comparisons
    are only resolved for the always-empty event-name shape.
    """
    v = expr.strip()
    if v.startswith("${{") and v.endswith("}}"):
        v = v[3:-2].strip()
    if v in {"true", "True"}:
        return True
    if v in {"false", "False", "'false'", '"false"'}:
        return False
    if v in {"0"}:
        return False
    if re.fullmatch(r"[+-]?[1-9][0-9]*", v):
        return True
    if v in {"''", '""'}:
        return False
    if re.fullmatch(r"'.+'|\".+\"", v):
        return True
    if v == "always()":
        return True
    # anything else is a lookup/comparison/call we cannot decide statically
    return None


def _if_statically_false(value: str) -> bool:
    """True when a job-level ``if:`` condition is provably always-false.

    The evaluator only returns False (definitely disabled) for a small,
    documented subset of the Actions expression grammar — literals,
    ``always()``, ``&&``/``||``/``!`` over those — and treats anything
    ambiguous as live. The boundary is intentional: the checker does not
    advertise full Actions-expression semantics.
    """
    v = value.strip()
    if v.startswith("${{") and v.endswith("}}"):
        v = v[3:-2].strip()
    if re.search(r"\.event_name\s*==\s*''", v):
        return True
    # tokenize a boolean expression over (&&, ||, !/not, parentheses)
    try:
        result = _eval_bool(v)
        return result is False
    except Exception:
        return False


def _matching_paren(v: str) -> int | None:
    """Index of the closing paren matching the first '(' at depth 0, or None."""
    if not v.startswith("("):
        return None
    depth = 0
    for i, ch in enumerate(v):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return i
    return None


def _eval_bool(v: str):
    v = v.strip()
    if not v:
        return None
    for op in ("||", "&&"):
        parts = _split_top_level(v, op)
        if parts is not None and len(parts) > 1:
            values = [_eval_bool(p) for p in parts]
            if op == "&&":
                if any(x is False for x in values):
                    return False
                if all(x is True for x in values):
                    return True
                return None
            else:
                if any(x is True for x in values):
                    return True
                if all(x is False for x in values):
                    return False
                return None
    if re.match(r"^!(?!=)", v):
        return not _eval_bool(v[1:])
    if re.match(r"^not\s+", v):
        return not _eval_bool(re.sub(r"^not\s+", "", v, count=1).strip())
    close = _matching_paren(v)
    if close is not None and close == len(v) - 1:
        return _eval_bool(v[1:close])
    return _expr_truth(v)


def _split_top_level(v: str, op: str) -> list[str] | None:
    """Split on an operator at parenthesis depth 0."""
    parts: list[str] = []
    depth = 0
    current: list[str] = []
    i = 0
    while i < len(v):
        c = v[i]
        if c == "(":
            depth += 1
            current.append(c)
            i += 1
            continue
        if c == ")":
            depth -= 1
            current.append(c)
            i += 1
            continue
        if depth == 0 and v.startswith(op, i):
            parts.append("".join(current))
            current = []
            i += len(op)
            continue
        current.append(c)
        i += 1
    parts.append("".join(current))
    if len(parts) == 1:
        return None
    return parts


def validate_workflow_job_executable(path: Path, job: str, klass: str, gid: str) -> None:
    """A registered CI enforcement job must not be statically disabled."""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    block = _workflow_job_block(lines, job)
    if block is None:
        raise CheckFailure(f"workflow-job-missing: {gid}: {path}#{job}")
    for line in block:
        m = re.match(r"^\s+if:\s*(.*)$", line)
        if m and _if_statically_false(m.group(1).strip()):
            raise CheckFailure(
                f"workflow-job-statically-disabled: {gid}: {path}#{job}: if: {m.group(1).strip()}"
            )


def _runners_from_block(block: list[str]) -> set[str]:
    runners: set[str] = set()
    in_include = False
    include_indent = -1
    for line in block:
        m = re.match(r"^(\s*)include:\s*$", line)
        if m:
            in_include = True
            include_indent = len(m.group(1))
            continue
        if in_include:
            if line.strip() and len(line) - len(line.lstrip()) <= include_indent:
                in_include = False
                continue
            rm = re.match(r"^\s*(?:-\s+)?runner:\s*([^\s#]+)", line)
            if rm:
                runner = rm.group(1).lower()
                if "ubuntu" in runner:
                    runners.add("linux")
                elif "macos" in runner:
                    runners.add("macos")
                elif "windows" in runner:
                    runners.add("windows")
    return runners


def _needs_from_block(block: list[str]) -> set[str]:
    needs: set[str] = set()
    for i, line in enumerate(block):
        m = re.match(r"^(\s*)needs:\s*(\[.*\]|.*)$", line)
        if not m:
            continue
        inline = m.group(2).strip()
        if inline:
            if inline.startswith("[") and inline.endswith("]"):
                needs.update(x.strip() for x in inline[1:-1].split(",") if x.strip())
            else:
                needs.update(x.strip() for x in inline.split(",") if x.strip())
        else:
            indent = len(m.group(1))
            for sub in block[i + 1:]:
                if sub.strip() and len(sub) - len(sub.lstrip()) <= indent:
                    break
                sm = re.match(r"^\s*-\s*([A-Za-z0-9_.-]+)\s*$", sub)
                if sm:
                    needs.add(sm.group(1))
    return needs


def workflow_job_runners(path: Path, job: str, _memo: dict[str, set[str]] | None = None) -> set[str]:
    """OS families the enforcement job itself spans: its own matrix runners
    unioned (transitively) with the runners of jobs it ``needs:``. A matrix
    sitting in an unrelated workflow job does not count for this job."""
    if _memo is None:
        _memo = {}
    if job in _memo:
        return _memo[job]
    _memo[job] = set()
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    block = _workflow_job_block(lines, job)
    if block is None:
        return set()
    runners = _runners_from_block(block)
    for need in _needs_from_block(block):
        runners |= workflow_job_runners(path, need, _memo)
    _memo[job] = runners
    return runners


def validate_cross_platform_matrix(path: Path, klass: str, gid: str, platforms, job: str) -> None:
    """A CROSS_PLATFORM_GATED enforcement job must prove OS breadth structurally.

    The breadth must belong to the enforcement path itself: the registered job's
    own ``strategy.matrix.include`` runners, or (transitively) the runners of
    jobs it ``needs:``. A matrix that merely appears elsewhere in the workflow
    does not satisfy the job.
    """
    if klass != "CROSS_PLATFORM_GATED":
        return
    declared = set(p.lower() for p in platforms) if platforms else set()
    runner_families = workflow_job_runners(path, job)
    if not runner_families:
        raise CheckFailure(
            f"cross-platform-matrix-missing: {gid}: {path}#{job}: no matrix include "
            "runners on the job or its transitive needs"
        )
    missing = declared - runner_families
    if missing:
        raise CheckFailure(
            f"cross-platform-matrix-incomplete: {gid}: {path}#{job}: declared platforms "
            f"missing from the job's matrix runners: {sorted(missing)}"
        )


def validate_workflow_trigger(path: Path, klass: str, gid: str) -> None:
    triggers = workflow_triggers(path)
    if klass in {"CI_GATED", "CROSS_PLATFORM_GATED"}:
        if not triggers.intersection({"push", "pull_request"}):
            raise CheckFailure(f"workflow-trigger-not-automatic: {gid}: {klass}: {path}")
    elif klass == "RELEASE_GATED":
        if not triggers.intersection({"workflow_dispatch", "release", "push"}):
            raise CheckFailure(f"workflow-trigger-not-release-capable: {gid}: {path}")
    elif klass == "SCHEDULED" and "schedule" not in triggers:
        raise CheckFailure(f"workflow-trigger-not-scheduled: {gid}: {path}")


def _glob_to_regex(pattern: str) -> re.Pattern:
    """Translate the GitHub Actions minimatch subset used in workflow ``paths:``
    filters to an anchored regular expression.

    Supported: ``**`` (any characters including ``/``), ``*`` (any characters
    within one path segment), ``?`` (one character within a segment). Anything
    else is escaped literally. ``**/`` is treated as ``**`` for directories.
    """
    out: list[str] = []
    i = 0
    n = len(pattern)
    while i < n:
        c = pattern[i]
        if c == "*":
            if i + 1 < n and pattern[i + 1] == "*":
                i += 2
                out.append(".*")
            else:
                out.append("[^/]*")
                i += 1
        elif c == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(c))
            i += 1
    return re.compile("^" + "".join(out) + "$")


def _path_covered(pattern: str, path: str) -> bool:
    return bool(_glob_to_regex(pattern).match(path))


def _pattern_covers(required: str, provided: str) -> bool:
    """True when every file the ``required`` pattern denotes is also matched by
    ``provided`` — a language-subset test, not a literal string match.

    A literal required path is matched concretely. A recursive tree requirement
    ``root/**`` is covered only by ``**`` or by a ``proot/**`` whose root is the
    same or an ancestor of ``root``; a flat pattern like ``src/*`` does not
    cover ``src/**`` because it excludes nested files.
    """
    if not any(ch in required for ch in "*?["):
        return _path_covered(provided, required)
    m = re.match(r"^([^*?]+)/\*\*$", required)
    if not m:
        return False
    root = m.group(1)
    if provided == "**":
        return True
    pm = re.match(r"^([^*?]+)/\*\*$", provided)
    if not pm:
        return False
    proot = pm.group(1)
    return root == proot or root.startswith(proot + "/")


def _regex_can_extend(atoms: list, i: int, s: str, j: int) -> bool:
    """Whether the glob regex ``atoms`` (from index i) can match a string whose
    prefix is ``s[j:]`` — i.e. ``s[j:]`` is a prefix of some accepted string.
    Used to decide whether a negative path pattern overlaps a required tree."""
    if i == len(atoms):
        return j == len(s)
    a = atoms[i]
    if a[0] == "lit":
        t = a[1]
        if s[j:].startswith(t):
            return _regex_can_extend(atoms, i + 1, s, j + len(t))
        if t.startswith(s[j:]):
            return True
        return False
    if a[0] == "dst":  # **  -> .*
        if _regex_can_extend(atoms, i + 1, s, j):
            return True
        if j < len(s):
            return _regex_can_extend(atoms, i, s, j + 1)
        return False
    if a[0] == "star":  # *  -> [^/]*
        if j < len(s):
            if s[j] == "/":
                return _regex_can_extend(atoms, i + 1, s, j)
            if _regex_can_extend(atoms, i, s, j + 1):
                return True
        return _regex_can_extend(atoms, i + 1, s, j)
    if a[0] == "qst":  # ?  -> [^/]
        if j < len(s) and s[j] != "/":
            return _regex_can_extend(atoms, i + 1, s, j + 1)
        return j == len(s)
    return False


def _glob_atoms(pattern: str) -> list:
    """Decompose a glob pattern into (lit, star, dst, qst) atoms for the
    prefix-matching decision above (mirrors ``_glob_to_regex``)."""
    atoms: list = []
    buf: list[str] = []
    i = 0
    n = len(pattern)
    while i < n:
        c = pattern[i]
        if c == "*":
            if buf:
                atoms.append(("lit", "".join(buf)))
                buf = []
            if i + 1 < n and pattern[i + 1] == "*":
                atoms.append(("dst", ""))
                i += 2
                continue
            atoms.append(("star", ""))
            i += 1
            continue
        if c == "?":
            if buf:
                atoms.append(("lit", "".join(buf)))
                buf = []
            atoms.append(("qst", ""))
            i += 1
            continue
        buf.append(c)
        i += 1
    if buf:
        atoms.append(("lit", "".join(buf)))
    return atoms


def _tree_overlaps_negative(root: str, neg: str) -> bool:
    """True when the negative pattern ``neg`` matches at least one file under
    ``root/**`` — any overlap breaks the guarantee that the whole required tree
    triggers the workflow."""
    if neg == "**":
        return True
    return _regex_can_extend(_glob_atoms(neg), 0, root + "/", 0)


def _trigger_covers(
    required: str, state: str, paths: list[str] | None, ignore: list[str] | None
) -> bool:
    """True when a single trigger's path filters cover ``required``.

    ``state`` distinguishes three cases that must never collapse into one:
    "unfiltered" (genuinely no filter — covers everything), "filtered" (parsed
    filters applied below), and "unparsed" (a filter construct the parser could
    not understand — fails closed, covers nothing).

    GitHub ``paths:`` semantics: a file triggers only if it matches a positive
    pattern and matches no negative (``!``-prefixed) pattern; ``paths-ignore:``
    entries are implicit negatives. For a literal required file this is a
    concrete match; for a required tree ``root/**`` a positive must cover the
    whole tree and no negative may overlap any file in it.
    """
    if state == "unparsed":
        return False
    if state == "unfiltered":
        return True
    pos = [p for p in (paths or []) if not p.startswith("!")]
    neg = [p[1:] for p in (paths or []) if p.startswith("!")] + list(ignore or [])
    if not pos:
        pos = ["**"]  # paths-ignore-only: covers everything except the negatives
    positive = any(_pattern_covers(required, p) for p in pos)
    if not positive:
        return False
    for n in neg:
        if not any(ch in required for ch in "*?["):
            if _path_covered(n, required):
                return False
        else:
            m = re.match(r"^([^*?]+)/\*\*$", required)
            if m and _tree_overlaps_negative(m.group(1), n):
                return False
    return True


def _split_flow_items(inner: str) -> list[str]:
    """Split a YAML flow-sequence body on top-level commas, quote-aware."""
    items: list[str] = []
    buf: list[str] = []
    q: str | None = None
    i = 0
    n = len(inner)
    while i < n:
        c = inner[i]
        if q:
            buf.append(c)
            if c == "\\" and i + 1 < n:
                buf.append(inner[i + 1])
                i += 2
                continue
            if c == q:
                q = None
            i += 1
            continue
        if c in "\"'":
            q = c
            buf.append(c)
            i += 1
            continue
        if c == ",":
            items.append("".join(buf))
            buf = []
            i += 1
            continue
        buf.append(c)
        i += 1
    items.append("".join(buf))
    return items


def _parse_flow_list(value: str) -> list[str] | None:
    """Parse a flow-style list ``['src/**', 'docs/**']``; None if not a list."""
    value = value.strip()
    if not (value.startswith("[") and value.endswith("]")):
        return None
    out: list[str] = []
    for part in _split_flow_items(value[1:-1]):
        part = part.strip()
        if not part:
            continue
        if len(part) >= 2 and (
            (part.startswith("'") and part.endswith("'"))
            or (part.startswith('"') and part.endswith('"'))
        ):
            part = part[1:-1]
        out.append(part)
    return out


def _trigger_configs(path: Path) -> dict[str, tuple[str, list[str] | None, list[str] | None]]:
    """Parse the ``on:`` block into per-trigger path filters.

    Returns {trigger: (state, paths, paths_ignore)} where state is one of:
      - "unfiltered" — genuinely no path filter; the trigger covers every file;
      - "filtered" — parsed ``paths:`` / ``paths-ignore:`` filters;
      - "unparsed" — a filter construct was seen but could not be understood;
        it fails closed (never treated as covering everything).
    Both the block style (``on:\\n  push:\\n    paths:\\n      - 'src/**'``)
    and the flow-style sequence ``paths: ['src/**']`` are parsed. Inline
    mapping forms (``on: {push: {...}}``) produce no triggers here and are
    rejected by the not-automatic check.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    configs: dict[str, tuple[str, list[str] | None, list[str] | None]] = {}
    in_on = False
    current_trigger: str | None = None
    for idx, line in enumerate(lines):
        inline = re.match(r"^on:\s*\[([^\]]+)\]\s*$", line)
        if inline:
            for t in (x.strip() for x in inline.group(1).split(",")):
                if t:
                    configs[t] = ("unfiltered", None, None)
            continue
        single = re.match(r"^on:\s*([A-Za-z_][A-Za-z0-9_-]*)\s*(?:#.*)?$", line)
        if single:
            configs[single.group(1)] = ("unfiltered", None, None)
            continue
        if re.match(r"^on:\s*(?:#.*)?$", line):
            in_on = True
            continue
        if not in_on:
            continue
        if line and not line[0].isspace():
            break
        tmatch = re.match(r"^  ([A-Za-z_][A-Za-z0-9_-]*):(?:\s|$)", line)
        if tmatch:
            current_trigger = tmatch.group(1)
            configs.setdefault(current_trigger, ("unfiltered", None, None))
            continue
        if current_trigger is None:
            continue
        pmatch = re.match(r"^(\s+)paths(?:-ignore)?:\s*$", line)
        if pmatch:
            key = "ignore" if "paths-ignore" in pmatch.group(0) else "paths"
            items: list[str] = []
            indent = len(pmatch.group(1))
            for sub in lines[idx + 1:]:
                if sub.strip() and not sub.startswith(" " * (indent + 2)):
                    break
                m = re.match(r"^\s+-\s+['\"]?([^'\"]+)['\"]?\s*(?:#.*)?$", sub)
                if m:
                    items.append(m.group(1))
            state, paths, ignore = configs[current_trigger]
            if key == "ignore":
                configs[current_trigger] = ("filtered", paths, items)
            else:
                configs[current_trigger] = ("filtered", items, ignore)
            continue
        fpmatch = re.match(r"^(\s+)paths(?:-ignore)?:\s*(.+)$", line)
        if fpmatch:
            key = "ignore" if "paths-ignore" in fpmatch.group(0) else "paths"
            flow = _parse_flow_list(fpmatch.group(2))
            if flow is not None:
                state, paths, ignore = configs[current_trigger]
                if key == "ignore":
                    configs[current_trigger] = ("filtered", paths, flow)
                else:
                    configs[current_trigger] = ("filtered", flow, ignore)
            else:
                configs[current_trigger] = ("unparsed", None, None)
            continue
    return configs


def validate_workflow_path_coverage(
    wf_path: Path,
    gid: str,
    required: list[str],
    workflow_file: str,
) -> None:
    """A CI_GATED / CROSS_PLATFORM_GATED workflow's automatic triggers must
    cover the enforcement architecture: the guarantee's nift guard refs, the
    implementation tree, the guarantee registry, the Makefile that invokes the
    guards, and every enforcement workflow referenced by the registry.

    Coverage is the union across automatic triggers (push / pull_request): for
    each required path at least one such trigger must cover it — a positive
    ``paths:`` match with no negative (``!``-prefixed or ``paths-ignore:``)
    match, where a tree requirement ``root/**`` must be covered in full, not by
    a flat or literal-string approximation. A trigger with genuinely no filter
    covers everything; a filter the parser could not understand fails closed.
    Static filters only: runtime event shape is out of scope for this
    structural contract.
    """
    triggers = _trigger_configs(wf_path)
    auto = {t for t in triggers if t in {"push", "pull_request"}}
    if not auto:
        raise CheckFailure(f"workflow-path-coverage-not-automatic: {gid}: {workflow_file}")
    uncovered = [
        r
        for r in required
        if not any(
            _trigger_covers(r, *triggers[t])
            for t in auto
        )
    ]
    if uncovered:
        raise CheckFailure(
            f"workflow-path-coverage-gap: {gid}: {workflow_file}: "
            f"uncovered by automatic trigger paths: {', '.join(sorted(uncovered))}"
        )


def workflow_has_job(path: Path, job: str) -> bool:
    # Workflows in this repository use ordinary two-space job keys. Avoid a YAML
    # dependency for a deliberately tiny structural checker.
    pattern = re.compile(rf"^  {re.escape(job)}:\s*(?:#.*)?$", re.MULTILINE)
    return bool(pattern.search(path.read_text(encoding="utf-8", errors="replace")))


def validate_id(value: Any, label: str, seen: set[str]) -> str:
    if not isinstance(value, str) or not ID_RE.fullmatch(value):
        raise CheckFailure(f"{label}-id-invalid: {value!r}")
    if value in seen:
        raise CheckFailure(f"duplicate-id: {value}")
    seen.add(value)
    return value


def check(registry_path: Path, args: argparse.Namespace) -> dict[str, Any]:
    data = load_json(registry_path)
    if data.get("schema_version") != 1:
        raise CheckFailure(f"schema-version-unsupported: {data.get('schema_version')!r}")
    roots = repo_roots(registry_path, data, args)

    declared_states = data.get("states")
    declared_evidence = data.get("evidence_classes")
    declared_tot = data.get("test_of_test_states")
    if set(declared_states or []) != ALLOWED_STATES:
        raise CheckFailure("states-declaration-mismatch")
    if set(declared_evidence or []) != ALLOWED_EVIDENCE:
        raise CheckFailure("evidence-classes-declaration-mismatch")
    if set(declared_tot or []) != ALLOWED_TOT:
        raise CheckFailure("test-of-test-states-declaration-mismatch")

    baseline = data.get("baseline")
    if not isinstance(baseline, dict):
        raise CheckFailure("baseline-invalid: expected object")
    dev_version = baseline.get("development_version")
    if not isinstance(dev_version, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", dev_version):
        raise CheckFailure(f"baseline-development-version-invalid: {dev_version!r}")
    nift_root = roots["nift"]
    assert nift_root is not None
    cli = nift_root / "src/CLI.cpp"
    if not cli.is_file() or f"Nift v{dev_version}" not in cli.read_text(encoding="utf-8", errors="replace"):
        raise CheckFailure(f"baseline-development-version-stale: Nift v{dev_version} not found in src/CLI.cpp")

    guarantees = data.get("guarantees")
    claims = data.get("public_claims")
    claim_surfaces = data.get("public_claim_surfaces")
    discrepancies = data.get("known_discrepancies")
    meta_guards = data.get("meta_guards")
    if not isinstance(guarantees, list) or not guarantees:
        raise CheckFailure("guarantees-invalid: expected non-empty list")
    if not isinstance(claims, list):
        raise CheckFailure("public-claims-invalid: expected list")
    if not isinstance(claim_surfaces, list) or not claim_surfaces:
        raise CheckFailure("public-claim-surfaces-invalid: expected non-empty list")
    if not isinstance(discrepancies, list):
        raise CheckFailure("known-discrepancies-invalid: expected list")
    if not isinstance(meta_guards, list):
        raise CheckFailure("meta-guards-invalid: expected list")

    pinned_surface_keys = {
        (surface.get("repo"), surface.get("path"))
        for surface in claim_surfaces if isinstance(surface, dict)
    }

    guarantee_ids: set[str] = set()
    external_skips: set[str] = set()
    ci_refs = 0
    local_refs = 0
    gated_items: list[tuple[str, Path, list[str]]] = []
    enforcement_workflows: set[str] = set()

    for g in guarantees:
        if not isinstance(g, dict):
            raise CheckFailure("guarantee-invalid: expected object")
        gid = validate_id(g.get("id"), "guarantee", guarantee_ids)
        if g.get("state") not in ALLOWED_STATES:
            raise CheckFailure(f"guarantee-state-invalid: {gid}: {g.get('state')!r}")
        if not isinstance(g.get("guarantee"), str) or not g["guarantee"].strip():
            raise CheckFailure(f"guarantee-text-missing: {gid}")
        if not isinstance(g.get("scope"), str) or not g["scope"].strip():
            raise CheckFailure(f"guarantee-scope-missing: {gid}")
        evidence_classes = g.get("evidence_classes")
        if not isinstance(evidence_classes, list) or any(x not in ALLOWED_EVIDENCE for x in evidence_classes):
            raise CheckFailure(f"evidence-class-invalid: {gid}: {evidence_classes!r}")
        if g.get("test_of_test") not in ALLOWED_TOT:
            raise CheckFailure(f"test-of-test-invalid: {gid}: {g.get('test_of_test')!r}")
        evidence_refs = g.get("evidence_refs")
        guard_refs = g.get("guard_refs")
        if not isinstance(evidence_refs, list):
            raise CheckFailure(f"evidence_refs-invalid: {gid}")
        if not isinstance(guard_refs, list):
            raise CheckFailure(f"guard_refs-invalid: {gid}")
        if any(x in {"RETAINED", "CAMPAIGN"} for x in evidence_classes) and not evidence_refs:
            raise CheckFailure(f"retained-evidence-ref-missing: {gid}")
        if any(x in {"RETAINED", "CAMPAIGN"} for x in evidence_classes):
            retained_refs = [
                ref for ref in evidence_refs
                if isinstance(ref, dict)
                and isinstance(ref.get("path"), str)
                and (ref["path"].startswith("docs/evidence/") or "/docs/evidence/" in ref["path"])
                and ref["path"].endswith(".json")
            ]
            if not retained_refs:
                raise CheckFailure(f"retained-evidence-artifact-missing: {gid}")
        for ref in evidence_refs:
            checked = validate_ref(ref, roots, f"{gid}:evidence_refs")
            local_refs += int(checked)
            if not checked:
                external_skips.add(ref["repo"])
        for ref in guard_refs:
            checked = validate_guard_ref(ref, roots, f"{gid}:guard_refs")
            local_refs += int(checked)
            if not checked:
                external_skips.add(ref["repo"])
        if g.get("test_of_test") == "VERIFIED_GUARD":
            red_refs = g.get("test_of_test_evidence_refs")
            if not isinstance(red_refs, list) or not red_refs:
                raise CheckFailure(f"verified-guard-redrun-evidence-missing: {gid}")
            for ref in red_refs:
                if ref.get("repo") != "nift" or "docs/evidence" not in ref.get("path", "") or not ref.get("path", "").endswith(".json"):
                    raise CheckFailure(f"verified-guard-redrun-evidence-invalid: {gid}")
                checked = validate_ref(ref, roots, f"{gid}:test_of_test_evidence_refs")
                local_refs += int(checked)
                if not checked:
                    external_skips.add(ref["repo"])
        enforcement = g.get("enforcement")
        if not isinstance(enforcement, list) or not enforcement:
            raise CheckFailure(f"enforcement-missing: {gid}")
        for item in enforcement:
            if not isinstance(item, dict) or item.get("class") not in ALLOWED_ENFORCEMENT:
                raise CheckFailure(f"enforcement-invalid: {gid}: {item!r}")
            klass = item["class"]
            if klass == "MANUAL":
                if not isinstance(item.get("command"), str) or not item["command"].strip():
                    raise CheckFailure(f"manual-command-missing: {gid}")
                make_ref = make_target_from_command(item["command"])
                if make_ref is None:
                    raise CheckFailure(f"manual-command-unsupported: {gid}: {item['command']!r}")
                makefile_rel, target = make_ref
                makefile = nift_root / makefile_rel
                if not makefile_has_target(makefile, target):
                    raise CheckFailure(f"manual-make-target-missing: {gid}: {makefile_rel}:{target}")
                continue
            workflow = item.get("workflow")
            job = item.get("job")
            if not isinstance(workflow, str) or not isinstance(job, str):
                raise CheckFailure(f"workflow-ref-invalid: {gid}: {item!r}")
            wf_ref = {"repo": "nift", "path": workflow}
            wf_path, _ = path_for(wf_ref, roots)
            assert wf_path is not None
            if not wf_path.is_file():
                raise CheckFailure(f"workflow-missing: {gid}: {workflow}")
            if not workflow_has_job(wf_path, job):
                raise CheckFailure(f"workflow-job-missing: {gid}: {workflow}#{job}")
            validate_workflow_trigger(wf_path, klass, gid)
            validate_workflow_job_executable(wf_path, job, klass, gid)
            validate_cross_platform_matrix(wf_path, klass, gid, g.get("platforms"), job)
            enforcement_workflows.add(workflow)
            if klass in {"CI_GATED", "CROSS_PLATFORM_GATED"}:
                guard_paths = [
                    ref.get("path")
                    for ref in g.get("guard_refs", [])
                    if ref.get("repo") == "nift" and ref.get("path")
                ]
                gated_items.append((gid, wf_path, guard_paths))
            ci_refs += 1

    enforcement_file_set = sorted(enforcement_workflows)
    common_required = ["src/**", "docs/guarantees/**", "Makefile"] + enforcement_file_set
    seen_coverage: set[tuple[str, str]] = set()
    for gid, wf_path, guard_paths in gated_items:
        key = (gid, str(wf_path))
        if key in seen_coverage:
            continue
        seen_coverage.add(key)
        required = list(dict.fromkeys(common_required + guard_paths))
        validate_workflow_path_coverage(wf_path, gid, required, str(wf_path.name))

    audited_surfaces = 0
    for surface in claim_surfaces:
        if not isinstance(surface, dict):
            raise CheckFailure("public-claim-surface-invalid: expected object")
        expected_hash = surface.get("sha256")
        if not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            raise CheckFailure(f"public-claim-surface-hash-invalid: {expected_hash!r}")
        path, repo = path_for(surface, roots)
        if path is None:
            external_skips.add(repo)
            continue
        if not path.is_file():
            raise CheckFailure(f"public-claim-surface-missing: {repo}:{surface['path']}")
        observed = hashlib.sha256(path.read_bytes()).hexdigest()
        if observed != expected_hash:
            raise CheckFailure(f"public-claim-surface-stale: {repo}:{surface['path']}: expected {expected_hash}, observed {observed}")
        audited_surfaces += 1

    claim_ids: set[str] = set()
    for claim in claims:
        if not isinstance(claim, dict):
            raise CheckFailure("public-claim-invalid: expected object")
        cid = validate_id(claim.get("id"), "public-claim", claim_ids)
        if claim.get("state") != "ESTABLISHED":
            raise CheckFailure(f"public-claim-not-established: {cid}: {claim.get('state')!r}")
        gid = claim.get("guarantee_id")
        if gid not in guarantee_ids:
            raise CheckFailure(f"public-claim-guarantee-missing: {cid}: {gid!r}")
        guarantee = next(g for g in guarantees if g.get("id") == gid)
        if guarantee.get("state") != "ESTABLISHED":
            raise CheckFailure(f"public-claim-guarantee-not-established: {cid}: {gid}")
        source = claim.get("source")
        if not isinstance(source, dict) or (source.get("repo"), source.get("path")) not in pinned_surface_keys:
            raise CheckFailure(f"public-claim-source-not-pinned: {cid}")
        checked = validate_ref(source, roots, f"{cid}:source", require_needle=True)
        local_refs += int(checked)
        if not checked:
            external_skips.add(claim["source"]["repo"])

    discrepancy_ids: set[str] = set()
    for item in discrepancies:
        if not isinstance(item, dict):
            raise CheckFailure("known-discrepancy-invalid: expected object")
        did = validate_id(item.get("id"), "known-discrepancy", discrepancy_ids)
        if item.get("state") not in {"NOT_ESTABLISHED", "ACCEPTED_LIMITATION", "OUT_OF_SCOPE"}:
            raise CheckFailure(f"known-discrepancy-state-invalid: {did}: {item.get('state')!r}")
        refs = item.get("refs")
        if not isinstance(refs, list) or not refs:
            raise CheckFailure(f"known-discrepancy-refs-missing: {did}")
        for ref in refs:
            checked = validate_ref(ref, roots, f"{did}:ref", require_needle="needle" in ref)
            local_refs += int(checked)
            if not checked:
                external_skips.add(ref["repo"])

    meta_ids: set[str] = set()
    for item in meta_guards:
        if not isinstance(item, dict):
            raise CheckFailure("meta-guard-invalid: expected object")
        mid = validate_id(item.get("id"), "meta-guard", meta_ids)
        if item.get("test_of_test") not in ALLOWED_TOT:
            raise CheckFailure(f"meta-guard-test-of-test-invalid: {mid}")
        checked = validate_guard_ref(item.get("guard_ref"), roots, f"{mid}:guard-ref")
        local_refs += int(checked)
        if not checked:
            external_skips.add(item["guard_ref"]["repo"])

    return {
        "guarantees": len(guarantees),
        "public_claims": len(claims),
        "audited_public_claim_surfaces": audited_surfaces,
        "known_discrepancies": len(discrepancies),
        "meta_guards": len(meta_guards),
        "ci_job_refs": ci_refs,
        "checked_file_refs": local_refs,
        "unavailable_sibling_repositories": sorted(external_skips),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", default="docs/guarantees/registry.json")
    parser.add_argument("--nift-root")
    parser.add_argument("--website-root")
    parser.add_argument("--regression-root")
    parser.add_argument("--local", action="store_true",
                        help="single-repository CI mode: assert only what the Nift "
                             "checkout itself can prove and PASS, never SKIP for "
                             "absent sibling repositories; the public-claim surface "
                             "audit is reported as deferred")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        summary = check(Path(args.registry), args)
    except CheckFailure as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    unavailable = summary["unavailable_sibling_repositories"]
    base = (
        f"guarantee registry structurally valid "
        f"({summary['guarantees']} guarantees, {summary['public_claims']} public claims, "
        f"{summary['known_discrepancies']} known discrepancies, {summary['ci_job_refs']} CI job refs)"
    )
    if args.local:
        # Local CI mode: a single-repo checkout cannot audit sibling public-claim
        # surfaces. That audit is deferred and reported truthfully, not skipped
        # into a silent green of the local integrity claims.
        if args.json:
            print(json.dumps({"status": "PASS", "deferred_public_claim_surface_audit": unavailable, **summary},
                             indent=2, sort_keys=True))
        elif unavailable:
            print(f"PASS: {base} (local); public-claim surface audit deferred "
                  f"(sibling repositories absent: {', '.join(unavailable)})")
        else:
            print(f"PASS: {base}")
        return 0
    if args.json:
        status = "SKIP" if unavailable else "PASS"
        print(json.dumps({"status": status, **summary}, indent=2, sort_keys=True))
    elif unavailable:
        print(
            "SKIP: guarantee registry structural check incomplete; required sibling repositories unavailable: "
            + ", ".join(unavailable)
        )
        print(
            f"checked {summary['audited_public_claim_surfaces']} public claim surfaces; "
            "public-claim completeness is not asserted"
        )
    else:
        print(f"PASS: {base}")
    return 2 if unavailable else 0


if __name__ == "__main__":
    raise SystemExit(main())
