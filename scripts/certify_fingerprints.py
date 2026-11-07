"""Per-gate failure fingerprints (owner 2026-11-08 01:06, Option A).

A gate that fails on the candidate is FAILED-BASELINE only when the fingerprint of its candidate log
equals the fingerprint of the baseline's log of the same gate, both computed by this module during
the campaign (never a stored fingerprint). Anything else is FAILED-NEW, and so is a failing log
without an extractable fingerprint. Whole-log identity stays in the report as a stricter extra
signal; it does not decide the class.

A fingerprint is:
- `cases`: the multiset of failing cases, each (step, case id, category, status, detail):
  - step: the harness the case printed under (the test script or unit binary make ran);
  - case id: the name the harness prints for the case (a per-gate pattern below);
  - category: refusal, crash, signal, timeout, build-refused, exception, finding, unclassified,
    diff, or assertion (the first rule in DIAGNOSTICS that matches the case's lines);
  - status: the exit status or signal the case's lines state (`exit N`, `rc=N`, `N != M`);
  - detail: what tells two failures of one case apart (refusal code and source position,
    expected/got, the exception message, the refused objects, the harness's own detail text).
- `count`: the number of failure lines, counted by a separate per-gate pattern, not from `cases`.
- `exit`: the gate's own status (make's `Error N` or the signal on the top-level target).

Normalisation is narrow: the checkout roots and absolute paths under a temporary directory (the
file's base name is kept, with its line and column). Nothing else is rewritten.

A gate whose log cannot identify its failing cases has no SPEC: it is listed in STOPPED with the
reason and the proposed adapter, and is not expected-baseline.
"""
import collections
import re

# --- narrow normalisation -------------------------------------------------------------------
_TMP_PATH = re.compile(r"/tmp/[^\s'\":]*?/([^/\s'\":]+)(?=[\s'\":]|$)")


def narrow(text, roots=()):
    for r in roots:
        if str(r):
            text = text.replace(str(r), "<ROOT>")
    return _TMP_PATH.sub(r"<TMP>/\1", text).strip()


# --- diagnostics: (category, pattern, detail) in priority order -------------------------------
_LOC = r"(?:(?:\S*/)?(?P<file>[\w.<>-]+)(?::(?P<line>\d+):(?P<col>\d+))?: )?"
DIAGNOSTICS = [
    # the switch only: which objects get refused before make stops depends on -j scheduling
    ("build-refused", re.compile(r"production object \S+: the command line sets the test-only switch (?P<d>\w+)")),
    ("refusal", re.compile(_LOC + r"not compiled for the VM \((?P<d>[^)]+)\)")),
    ("refusal", re.compile(_LOC + r"NIFT_VM session refused: (?P<d>[\w-]+:[^:\s]+):")),
    ("crash", re.compile(_LOC + r"NIFT_VM internal failure: (?P<d>[^(\n]+?)(?: \(|$)")),
    ("signal", re.compile(r"(?P<d>Segmentation fault|Aborted|Bus error|Floating point exception|core dumped)")),
    ("timeout", re.compile(r"(?P<d>timed out|TimeoutExpired)")),
    ("exception", re.compile(r"^(?P<e>\w+(?:Error|Exception)): (?P<d>.*)$", re.M)),
]
_STATUS = re.compile(r"\(exit (-?\d+)\)|\(rc=(-?\d+)\)|\bexit (-?\d+) != (-?\d+)|\brc=(-?\d+)")
_MAKE_TOP = re.compile(r"^make: \*\*\* \[(?:[^\]:]*:\d+: )?([^\]]+)\] (.+)$")
# a harness invocation make echoes (or certify's `$ cmd` header of a recipe line run on its own)
_STEP = re.compile(r"""^(?:\$ )?(?:\w+="[^"]*" |\w+=\S* )*(?:(?:bash|sh|\S*python3?|"?\$\(\w+\)"?) )?"?"""
                   r"""(?P<step>(?:\S*/)?(?:tests|scripts|tools)/[\w./-]+\.(?:sh|py)|\.build/[\w./-]+)"?(?:\s|$)""")


def diagnose(block, roots=()):
    """(category, status, detail) of one case's lines."""
    text = "\n".join(block)
    status = tuple(sorted({next(g for g in m.groups() if g is not None) if not m.group(3)
                           else f"{m.group(3)}!={m.group(4)}" for m in _STATUS.finditer(text)}))
    for cat, pat in DIAGNOSTICS:
        found = list(pat.finditer(text))
        if found:
            parts = set()
            for m in found:
                g = m.groupdict()
                where = f"@{g['file']}:{g['line']}:{g['col']}" if g.get("file") and g.get("line") else (
                    f"@{g['file']}" if g.get("file") else "")
                parts.add((f"{g['e']}: " if g.get("e") else "") + narrow(g["d"], roots) + where)
            return cat, ",".join(status), " | ".join(sorted(parts))
    return None, ",".join(status), None


class Spec:
    def __init__(self, cases, count=r"^\s*(?:FAIL|not ok)\b", summary=(), category=None, abort=False,
                 block_detail=False, step=None, step_scope=False):
        self.cases = [re.compile(c) for c in cases]  # each has (?P<id>) and may have (?P<detail>)
        self.count = re.compile(count)               # failure lines, or (?P<n>) for a stated count
        self.summary = [re.compile(s) for s in summary]  # failure summary lines: never a case
        self.category = category                     # the category of every case (finding lists)
        self.abort = abort                           # a step that dies with no case line is a case
        self.block_detail = block_detail             # the case's following lines are its detail (a diff)
        self.step = re.compile(step) if step else _STEP  # which echoed commands start a step
        self.step_scope = step_scope                 # diagnose a case from its whole step (interleaved output)


_FAILLINE = r"^FAIL (?P<id>.+?)(?:: (?P<detail>.*))?$"
SPECS = {
    "test-v44-shell-restricted": Spec([r"^FAIL (?P<id>[\w-]+)$"], summary=[r"^FAILED: "]),
    "test-v46-relative-imports": Spec([], count=r"^(?:error: |\w+Error: )", abort=True),
    "test-runtime2-cp03-simplify": Spec([_FAILLINE], abort=True),
    # bad "$1 (expected '$2', got '$3')": $3 may span lines, so the id ends at " (expected '"
    "test-sha256": Spec([r"^FAIL (?P<id>.+?) \((?P<detail>expected '.*)$", _FAILLINE],
                        count=r"^FAIL "),
    "test-runtime2-cp01-simplify": Spec([r"^\s*FAIL (?P<id>S\w+|settle|shared \w+ repair|syscalls \S+|\.nift file count)"
                                         r"(?::? (?P<detail>.*))?$"], summary=[r"^FAILS \(\d+\):$"]),
    "test-cp19-s1-dispatch": Spec([_FAILLINE], summary=[r"^FAILED: "]),
    "test-rt-parser": Spec([r"^FAIL (?P<id>dump of \[.*?\]) lacks (?P<detail>.*)$", r"^FAIL (?P<id>.+)$"],
                           count=r"^(?P<n>\d+) rt parser unit failure\(s\)$"),
    "test-test-integrity": Spec([r"^\s+\[(?P<cat>[\w-]+)\] (?P<id>\S+): (?P<detail>.*)$"],
                                count=r"^test-integrity findings: (?P<n>\d+)$", category="finding"),
    "test-v44-packages": Spec([_FAILLINE], count=r"^(?:FAIL |error: |\w+Error: )", abort=True),
    # host_mutants.sh runs make -j4 without -k: compile-object.sh lines interleave with its own
    # output, so only tests/ scripts start a step and a case is diagnosed from its whole step
    "test-rt-host-mutants": Spec([r"^FAIL (?P<id>host mutant \d+): (?P<detail>.*)$"],
                                 step=r"^(?:\$ )?(?:\w+=\S* )*(?:bash )?(?P<step>tests/[\w./-]+\.sh)\b", step_scope=True),
    "cp25r-inventory": Spec([r"^INVENTORY: unclassified reason: (?P<id>\S+)$"], count=r"^INVENTORY: ",
                            category="unclassified"),
    "test-runtime2-rt-diff-selftest": Spec([r"^FAIL (?P<id>.+?)(?: \(rc=-?\d+\))?$",
                                            r"^DIFF (?P<id>\S+ \w+) (?P<detail>.*)$"],
                                           summary=[r"^FAIL rt-diff self-tests\b"], count=r"^(?:FAIL|DIFF) "),
    "test-runtime2-scanner-parity": Spec([r"^FAIL (?P<id>(?:site|script) \S+)$"],
                                         summary=[r"^FAIL scanner parity\b", r"^scanner parity: \d+ sites"], block_detail=True),
}

STOPPED = {
    "test-v44-language-foundation": (
        "tests/v44_ast_expression_smoke.sh fails with no output at all (set -euo pipefail; the failing "
        "command prints nothing): no failing case is named",
        "adapter: certify re-runs that recipe line under `bash -x` on both trees and takes the last "
        "traced command as the case (or a reviewed test-only change printing the failing step)"),
    "test-rt-vm": (
        "vm_parity's --rt-diff part runs rt-diff --quiet and deletes its report: the log says only "
        "`FAIL vm parity: 1 failure(s)`, never which entries differ (vm_modules alone is extractable)",
        "adapter: certify runs vm_parity with a kept rt-diff report (an existing --report path in a "
        "certify directory) on both trees and fingerprints its unapproved differing entries (id, channel)"),
    "test-runtime2-host-deps": (
        "read_limit.py prints `FAIL <name> <detail>`: names contain colons and the exit code, the detail "
        "is raw stderr cut to a fixed tail whose start moves with the length of TMPDIR (it holds a pid), "
        "so neither the case id nor the detail can be split out reliably",
        "adapter: match read_limit.py's case names from its check() calls (a reviewed name table) and "
        "take only the refusal code / exit status of each, not the cut stderr"),
}

# The gates expected to fail identically on a baseline (record section 17). The guard fails if any
# of them has no extractor or no table test, or is also STOPPED.
EXPECTED_BASELINE = [
    "cp25r-inventory", "test-cp19-s1-dispatch", "test-rt-host-mutants", "test-rt-parser",
    "test-runtime2-cp01-simplify", "test-runtime2-cp03-simplify", "test-runtime2-rt-diff-selftest",
    "test-runtime2-scanner-parity", "test-sha256", "test-test-integrity", "test-v44-packages",
    "test-v44-shell-restricted", "test-v46-relative-imports",
]


def _cases(spec, lines, roots):
    out, step = [], "-"
    i = 0
    stepped = {}  # step -> whether a case line was seen under it
    abort_blocks = collections.defaultdict(list)
    step_lines = collections.defaultdict(list)
    for line in lines:  # the lines of each step (step_scope)
        sm = spec.step.match(line)
        if sm and not line.startswith(("FAIL", "PASS")):
            step = narrow(sm.group("step"), roots).split("/")[-1]
        step_lines[step].append(line)
    step = "-"
    while i < len(lines):
        line = lines[i]
        sm = spec.step.match(line)
        if sm and not line.startswith(("FAIL", "PASS")):
            step = narrow(sm.group("step"), roots).split("/")[-1]
            stepped.setdefault(step, False)
            i += 1
            continue
        if any(s.match(line) for s in spec.summary):
            i += 1
            continue
        m = next((m for c in spec.cases for m in [c.match(line)] if m), None)
        if not m:
            abort_blocks[step].append(line)
            i += 1
            continue
        stepped[step] = True
        block = [line]
        i += 1
        while i < len(lines) and lines[i].strip() and not re.match(r"^\s*(?:PASS|FAIL|SKIP|DIFF|ok|note)\b", lines[i]) \
                and not spec.step.match(lines[i]) and not lines[i].startswith("make") \
                and not any(c.match(lines[i]) for c in spec.cases) and not any(x.match(lines[i]) for x in spec.summary):
            block.append(lines[i])
            i += 1
        g = m.groupdict()
        cat, status, detail = diagnose(step_lines[step] if spec.step_scope else block, roots)
        if spec.block_detail:
            cat, detail = cat or "diff", " / ".join(narrow(x, roots) for x in block[1:])
        elif spec.category:
            cat = spec.category + (f":{g['cat']}" if g.get("cat") else "")
            detail = narrow(g.get("detail") or "", roots)
        elif cat is None:  # the harness's own detail: the rest of the line and the lines under it
            cat, detail = "assertion", " / ".join(narrow(x, roots) for x in [g.get("detail") or ""] + block[1:])
        elif g.get("detail"):  # the diagnostic and the case line's own detail (`exit 1 != 77`, expected)
            detail = f"{detail} / {narrow(g['detail'], roots)}"
        out.append((step, narrow(g["id"], roots), cat, status, detail))
    if spec.abort:
        for step, block in abort_blocks.items():
            if not stepped.get(step):
                cat, status, detail = diagnose(block, roots)
                if cat:
                    out.append((step, step, cat, status, detail))
    return out


def fingerprint(gate, text, roots=(), rc=None):
    """The gate's fingerprint, or None (no extractor, or no failing case found)."""
    spec = SPECS.get(gate)
    if spec is None:
        return None
    lines = text.splitlines()
    cases = _cases(spec, lines, roots)
    if not cases:
        return None
    count = 0
    for line in lines:
        m = spec.count.match(line)
        if m:
            count += int(m.group("n")) if "n" in m.groupdict() else 1
    top = tuple(f"{m.group(1)}: {m.group(2)}" for m in map(_MAKE_TOP.match, lines) if m)
    return {"cases": collections.Counter(cases), "count": count, "exit": top or (rc,)}


def differences(a, b):
    """Why two fingerprints differ ([] when equal; a missing fingerprint always differs)."""
    if a is None or b is None:
        return ["no extractable failure fingerprint on the " + ("candidate" if a is None else "baseline")]
    out = []
    if a["count"] != b["count"]:
        out.append(f"failure count {a['count']} != {b['count']}")
    if a["exit"] != b["exit"]:
        out.append(f"exit {a['exit']} != {b['exit']}")
    only_a, only_b = a["cases"] - b["cases"], b["cases"] - a["cases"]
    out += [f"candidate only: {c}" for c in sorted(only_a)] + [f"baseline only: {c}" for c in sorted(only_b)]
    return out


def same(a, b):
    return not differences(a, b)
