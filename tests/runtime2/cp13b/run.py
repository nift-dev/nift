#!/usr/bin/env python3
"""CP13B exception fixtures: legacy oracle, structured refusal and future VM.

Modes
  legacy   run the oracle and compare with expected/ (fixture sanity)
  current  NIFT_VM=require: the pre-implementation VM must refuse the unit
           *before execution*: outcome require-error, vm_executions = 0,
           legacy_executions = 0, no stdout, no Host trace entry, no file
           side effect, exit 1, and the manifest's exact refusal reason.
  future   NIFT_VM=1: compare the VM with the legacy oracle. A difference is
           accepted only through an approved vm_policies.py policy mapped from
           the EXPECT_FUTURE.tsv id; anything else is a regression. A
           `fallback:<reason>` case is a unit the VM refuses statically while
           legacy executes it: the exact reason is pinned. `limit` cases prove
           an uncatchable resource-limit stop.

Mutants
  --mutant N            future checks with a -DNIFT_EXC_MUTANT=N build. The
                        unmutated oracle must PASS the future gate first. A case
                        detects only after genuine VM execution and a semantic
                        failure; crashes, signals, sanitizer reports,
                        unexpected exits, timeouts, fallbacks, refusals and
                        missing/malformed statistics are harness failures, never
                        detections.
  --checked-mutant N    checked-build E2 return path: only the fixed reason
                        `stale exception handler`, outcome internal-error,
                        exit 70 and written statistics count.
  --asan-mutant N       ASan-only cases: only an AddressSanitizer
                        heap-use-after-free naming op_get_upvalue counts.
  `--oracle` is mandatory in every mutant mode.

The suite is evidence, not part of `make test`.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CASES = HERE / "cases"
EXPECTED = HERE / "expected"
MANIFEST = HERE / "MANIFEST.tsv"
FUTURE = HERE / "EXPECT_FUTURE.tsv"
MUTANTS = HERE / "MUTANTS.json"
sys.path.insert(0, str(HERE.parent))
import vm_policies  # noqa: E402

# The adversarial self-test shortens this with NIFT_CP13B_TIMEOUT.
TIMEOUT = int(os.environ.get("NIFT_CP13B_TIMEOUT", "120"))
# EXPECT_FUTURE.tsv uses short ids; vm_policies.POLICIES are the exact names.
POLICY_IDS = {
    "D-001#4": "D-001#4 recursion",
    "D-018c": "D-018c diagnostics",
}
LIMIT_SLACK = 1 << 20  # the budget also charges the register/frame/handler stacks
SANITIZER_MARKERS = (
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
    "LeakSanitizer",
    "MemorySanitizer",
    "runtime error:",
)
STALE_HANDLER = "stale exception handler"


class Result:
    def __init__(self, out, err, rc, stats, trace, created):
        self.out = out
        self.err = err
        self.rc = rc
        self.stats = stats
        self.trace = trace
        self.created = created

    def triple(self):
        return (self.out, self.err, self.rc)

    def triple_bytes(self):
        return (self.out.encode(), self.err.encode(), self.rc)


class Evaluation:
    """One return type for every checker (B1)."""

    def __init__(self, problems, executed):
        self.problems = problems
        self.executed = executed


class HarnessFailure(Exception):
    pass


def load_tsv(path):
    rows = {}
    for line in path.read_text().splitlines():
        if line.strip().startswith("#"):
            continue
        line = line.strip()
        if not line:
            continue
        parts = line.split("\t")
        rows[parts[0]] = parts[1:]
    return rows


def env_from(text):
    out = {}
    for item in (text or "").split(";"):
        if item.strip():
            key, _, value = item.partition("=")
            out[key.strip()] = value.strip()
    return out


def stem(name):
    return name[:-2] if name.endswith(".f") else name


def case_source(name):
    return CASES / (name + ".f")


def copy_case_inputs(name, folder):
    shutil.copy(case_source(name), folder / (name + ".f"))
    for module in CASES.glob(name + ".*.nft"):
        target = module.name[len(name) + 1:]
        shutil.copy(module, folder / target)


def has_sanitizer(text):
    return any(marker in text for marker in SANITIZER_MARKERS)


def check_process(result, context):
    """B2: signals, sanitizers and unexpected exits are never semantic."""
    if result.rc < 0:
        raise HarnessFailure(f"{context}: terminated by signal {-result.rc}")
    if result.rc > 70:
        raise HarnessFailure(f"{context}: unexpected exit {result.rc} (signal/timeout)")
    if has_sanitizer(result.err):
        raise HarnessFailure(f"{context}: sanitizer report in stderr")


def run(nift, name, mode, extra_env=None):
    with tempfile.TemporaryDirectory(prefix="cp13b-") as folder:
        root = Path(folder)
        copy_case_inputs(name, root)
        before = set(root.iterdir())
        stats = root / ".stats.jsonl"
        trace = root / ".trace"
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": folder,
            "TMPDIR": folder,
            "LANG": "C",
            "LC_ALL": "C",
            "TZ": "UTC",
            "NIFT_PREVALIDATE": "off",
        }
        for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
            if key in os.environ:
                env[key] = os.environ[key]
        if mode is None:
            env["NIFT_VM"] = "0"
        else:
            env["NIFT_VM"] = mode
            env["NIFT_VM_STATS"] = str(stats)
        env.update(extra_env or {})
        # NIFT_TRACE_HOST is a file descriptor number, not a path.
        fd = os.open(trace, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        try:
            env["NIFT_TRACE_HOST"] = str(fd)
            try:
                proc = subprocess.run(
                    ["timeout", str(TIMEOUT), nift, name + ".f"],
                    cwd=root,
                    env=env,
                    stdin=subprocess.DEVNULL,
                    capture_output=True,
                    timeout=TIMEOUT + 5,
                    pass_fds=(fd,),
                )
            except subprocess.TimeoutExpired:
                raise HarnessFailure("timeout")
        finally:
            os.close(fd)
        out = proc.stdout.decode(errors="replace").replace(folder, "$ROOT")
        err = proc.stderr.decode(errors="replace").replace(folder, "$ROOT")
        st = None
        if stats.exists():
            lines = stats.read_text().splitlines()
            if lines:
                try:
                    st = json.loads(lines[-1])
                except ValueError:
                    raise HarnessFailure("malformed NIFT_VM_STATS")
        trace_lines = []
        if trace.exists():
            trace_lines = [line for line in trace.read_text().splitlines() if line.strip()]
        created = sorted(str(p.relative_to(root)) for p in root.iterdir()
                         if p not in before and p.name not in (".stats.jsonl", ".trace"))
        return Result(out, err, proc.returncode, st, trace_lines, created)


def golden(name, stream):
    path = EXPECTED / f"{name}.{stream}"
    return path.read_text() if path.exists() else ""


def expect_golden(name):
    return (golden(name, "stdout"), golden(name, "stderr"), int(golden(name, "exit").strip() or 0))


def check_legacy(nift, name):
    want = expect_golden(name)
    got = run(nift, name, None)
    if got.triple() != want:
        raise HarnessFailure(f"legacy oracle differs from expected: {got.triple()!r} != {want!r}")
    return got


def check_current(nift, oracle, name, reason):
    check_legacy(oracle, name)
    got = run(nift, name, "require")
    problems = []
    if got.stats is None:
        raise HarnessFailure("no NIFT_VM_STATS")
    try:
        check_process(got, "current")
    except HarnessFailure:
        raise
    if reason == "@vm":
        # Already executable on the current VM: execution fixture, not a refusal.
        if got.stats.get("outcome") not in ("vm", "vm-error"):
            problems.append(f"outcome {got.stats.get('outcome')}, expected genuine VM execution")
        if got.stats.get("vm_executions") != 1:
            problems.append(f"vm_executions {got.stats.get('vm_executions')}, expected 1")
        if got.triple() != expect_golden(name):
            want = expect_golden(name)
            verdict, policy = vm_policies.classify(
                (want[0].encode(), want[1].encode(), want[2]), got.triple_bytes(), got.stats,
                case_source(name).read_bytes())
            if verdict != "approved":
                problems.append(f"current VM differs from legacy: {got.triple()!r} != {expect_golden(name)!r}")
        if got.created:
            problems.append(f"side-effect files created: {got.created}")
        return problems
    if got.stats.get("outcome") != "require-error":
        problems.append(f"outcome {got.stats.get('outcome')}, expected require-error")
    if got.stats.get("vm_executions") != 0:
        problems.append(f"vm_executions {got.stats.get('vm_executions')}, expected 0")
    if got.stats.get("legacy_executions") != 0:
        problems.append(f"legacy_executions {got.stats.get('legacy_executions')}, expected 0")
    if got.stats.get("reason") != reason:
        problems.append(f"reason {got.stats.get('reason')!r}, expected {reason!r}")
    if got.out != "":
        problems.append(f"stdout is not empty: {got.out!r}")
    if got.trace:
        problems.append(f"Host trace is not empty: {got.trace[:3]}")
    if got.rc != 1:
        problems.append(f"exit {got.rc}, expected 1")
    if got.created:
        problems.append(f"side-effect files created: {got.created}")
    if reason not in got.err or "not compiled for the VM" not in got.err:
        problems.append(f"stderr does not report the refusal: {got.err!r}")
    return problems


def policy_names(ids):
    names = set()
    for raw in (ids or "").split(","):
        raw = raw.strip()
        if not raw or raw == "-":
            continue
        if raw not in POLICY_IDS:
            raise HarnessFailure(f"unknown policy id {raw!r}")
        names.add(POLICY_IDS[raw])
    return names


def evaluate_future(legacy, vm, name, outcome, policy_ids, checks, env):
    """Return Evaluation(problems, executed). B1 uses one return type."""
    if vm.stats is None:
        raise HarnessFailure("no NIFT_VM_STATS")
    check_process(vm, "future")
    st = vm.stats
    actual = st.get("outcome")
    problems = []
    if "error_origin" not in st:
        problems.append("statistics missing error_origin")
    elif not isinstance(st["error_origin"], bool):
        problems.append(f"error_origin {st['error_origin']!r} is not a bool")
    checks = {c.strip() for c in (checks or "").split(",") if c.strip()}
    if outcome.startswith("fallback:"):
        expected_reason = outcome.split(":", 1)[1]
        if expected_reason in ("statement:Try", "statement:Throw", "callable:error"):
            raise HarnessFailure(f"fallback reason {expected_reason!r} is never accepted")
        executed = st.get("vm_executions") == 1 and st.get("legacy_executions") == 0
        if actual != "fallback":
            problems.append(f"outcome {actual}, expected fallback")
        if st.get("reason") != expected_reason:
            problems.append(f"fallback reason {st.get('reason')!r}, expected {expected_reason!r}")
        if st.get("vm_executions") != 0:
            problems.append(f"vm_executions {st.get('vm_executions')}, expected 0 on fallback")
        if st.get("legacy_executions") != 1:
            problems.append(f"legacy_executions {st.get('legacy_executions')}, expected 1 on fallback")
        if vm.triple() != legacy.triple():
            problems.append(f"fallback output differs from legacy: {vm.triple()!r} != {legacy.triple()!r}")
        # An executed fallback case with an unexpected status is a harness
        # failure, never a detected mutation (R2-1).
        if vm.rc not in (0, 1):
            raise HarnessFailure(f"fallback: unexpected exit {vm.rc}")
        return Evaluation(problems, executed)
    if outcome == "limit":
        executed = st.get("vm_executions") == 1 and st.get("legacy_executions") == 0
        budget = int(env.get("NIFT_VM_HEAP_BUDGET", "0") or 0)
        if actual != "internal-error":
            problems.append(f"outcome {actual}, expected internal-error")
        # R2-2: exits 0/1 are valid executions whose semantic deviation must
        # be detected; only statuses outside (0, 1, 70) are a harness failure.
        if executed and vm.rc not in (0, 1, 70):
            raise HarnessFailure(f"limit: unexpected exit {vm.rc}")
        if vm.rc != 70:
            problems.append(f"exit {vm.rc}, expected 70")
        if "memory budget exceeded" not in vm.err:
            problems.append(f"stderr does not name the memory limit: {vm.err!r}")
        if vm.out != "":
            problems.append(f"stdout is not empty before the limit: {vm.out!r}")
        if st.get("legacy_executions") != 0:
            problems.append(f"legacy_executions {st.get('legacy_executions')}, expected 0")
        if not executed:
            problems.append(f"vm_executions {st.get('vm_executions')}, expected 1")
        if st.get("catchable") is not False:
            problems.append(f"catchable {st.get('catchable')!r}, expected false")
        peak = st.get("heap_peak")
        live = st.get("heap_live")
        if peak is None or live is None:
            raise HarnessFailure("missing heap_peak/heap_live in NIFT_VM_STATS")
        if budget and peak <= budget - LIMIT_SLACK:
            problems.append(f"heap_peak {peak} does not prove the {budget}-byte limit was reached")
        if budget and peak > budget:
            problems.append(f"heap_peak {peak} exceeds the {budget}-byte budget")
        return Evaluation(problems, executed)
    if actual != outcome:
        problems.append(f"outcome {actual}, expected {outcome}")
    executed = st.get("vm_executions") == 1 and st.get("legacy_executions") == 0
    if st.get("vm_executions") != 1:
        problems.append(f"vm_executions {st.get('vm_executions')}, expected 1 (genuine VM execution)")
    if st.get("legacy_executions") != 0:
        problems.append(f"legacy_executions {st.get('legacy_executions')}, expected 0")
    if "uncatchable" in checks and st.get("catchable") is not False:
        problems.append(f"catchable {st.get('catchable')!r}, expected false (D7)")
    if vm.rc not in (0, 1):
        # A semantic error is exit 1; anything else is an execution failure.
        raise HarnessFailure(f"future: unexpected exit {vm.rc}")
    if actual not in ("vm", "vm-error"):
        return Evaluation(problems, executed)
    if vm.triple() == legacy.triple():
        return Evaluation(problems, executed)
    try:
        source = case_source(name).read_bytes()
    except OSError:
        raise HarnessFailure("cannot read the fixture source")
    verdict, policy = vm_policies.classify(legacy.triple_bytes(), vm.triple_bytes(), st, source)
    allowed = policy_names(policy_ids)
    if verdict != "approved" or policy not in allowed:
        problems.append(f"unexplained difference (verdict {verdict}, policy {policy}, allowed {sorted(allowed)}): "
                        f"legacy {legacy.triple()!r} VM {vm.triple()!r}")
    return Evaluation(problems, executed)


def check_future(nift, oracle, name, outcome, policy_ids, env, checks):
    legacy = check_legacy(oracle, name)
    vm = run(nift, name, "1", env)
    return evaluate_future(legacy, vm, name, outcome, policy_ids, checks, env)


def mutant_verdict(executed, problems):
    """Detected only by a semantic failure after genuine VM execution."""
    if not problems:
        return "missed"
    if not executed:
        return "harness"
    return "detected"


def checked_verdict(stats, rc):
    """B5: the only accepted checked-build E2 detection."""
    return (stats is not None and rc == 70 and stats.get("outcome") == "internal-error"
            and stats.get("reason") == STALE_HANDLER
            and stats.get("vm_executions") == 1 and stats.get("legacy_executions") == 0)


def asan_verdict(rc, err):
    """B5: the only accepted ASan E7 detection."""
    return (rc != 0 and "AddressSanitizer" in err and "heap-use-after-free" in err
            and "op_get_upvalue" in err)


def mutant_gate(cases, checker, required, controls):
    """Run every case and every control; any harness failure fails the gate."""
    harness = 0
    detected = set()
    missed = set()
    control_failures = set()
    for name in cases:
        try:
            result = checker(name)
        except HarnessFailure as error:
            harness += 1
            print(f"HARNESS mutant case {name}: {error}")
            continue
        verdict = mutant_verdict(result.executed, result.problems)
        if verdict == "harness":
            harness += 1
            print(f"HARNESS mutant case {name}: " + "; ".join(result.problems))
        elif verdict == "detected":
            detected.add(name + ".f")
        else:
            missed.add(name + ".f")
    for control in controls:
        name = stem(control)
        try:
            result = checker(name)
        except HarnessFailure as error:
            harness += 1
            print(f"HARNESS mutant control {name}: {error}")
            continue
        if result.problems:
            control_failures.add(name + ".f")
            print(f"CONTROL mutant {name}: " + "; ".join(result.problems))
    return harness, detected, missed, control_failures


def selftest():
    """B1/B5 pure rules: one return type, checked/ASan/control classifiers."""
    assert mutant_verdict(True, ["x"]) == "detected"
    assert mutant_verdict(False, ["x"]) == "harness"
    assert mutant_verdict(True, []) == "missed"
    ok = {"outcome": "internal-error", "reason": STALE_HANDLER,
          "vm_executions": 1, "legacy_executions": 0}
    assert checked_verdict(ok, 70)
    assert not checked_verdict(dict(ok, reason="other"), 70)
    assert not checked_verdict(ok, 1)
    assert asan_verdict(1, "==1==ERROR: AddressSanitizer: heap-use-after-free on ... op_get_upvalue")
    assert not asan_verdict(0, "")
    assert not asan_verdict(1, "")
    assert not asan_verdict(1, "AddressSanitizer: global-buffer-overflow in op_get_upvalue")
    # H2 policy path: approved vs unapproved difference.
    recursion = b"error: /x/a.f:3:5: callable recursion depth exceeded: f\n"
    legacy = Result("start\n", recursion.decode(), 1, None, [], [])
    vm_stats = {"outcome": "vm", "vm_executions": 1, "legacy_executions": 0,
                "beyond_legacy_depth": True, "catchable": False,
                "heap_peak": 0, "heap_live": 0, "error_origin": False}
    vm = Result("start\n", "", 0, vm_stats, [], [])
    ev = evaluate_future(legacy, vm, "nested_rethrow", "vm", "D-001#4", "", {})
    assert ev.executed and not ev.problems, (ev.problems, ev.executed)
    ev = evaluate_future(legacy, vm, "nested_rethrow", "vm", "-", "", {})
    assert ev.executed and ev.problems, (ev.problems, ev.executed)
    # E3: a baseline fallback that a mutant turns into genuine execution is a detection.
    fallback_stats = {"outcome": "fallback", "reason": "unresolved-name",
                      "vm_executions": 0, "legacy_executions": 1}
    fallback = Result("x\n", "", 0, fallback_stats, [], [])
    leaked = Result("x\nx\n", "", 0, vm_stats, [], [])
    ev = evaluate_future(fallback, leaked, "nested_rethrow", "fallback:unresolved-name", "-", "", {})
    assert ev.executed and ev.problems and mutant_verdict(ev.executed, ev.problems) == "detected"
    # R2-1: an executed fallback with an unexpected exit status is a harness
    # failure, not a detection. This regression control is retained.
    for status in (2, 3, 69):
        bad = Result("x\nx\n", "", status, dict(vm_stats, outcome="vm"), [], [])
        try:
            evaluate_future(fallback, bad, "nested_rethrow", "fallback:unresolved-name", "-", "", {})
        except HarnessFailure:
            pass
        else:
            raise AssertionError(f"fallback executed with exit {status} was not a harness failure")
    # R2-2: six permanent resource-limit classifier controls, using the real
    # evaluate_future/mutant_verdict path. Valid exits 0/1 are semantic
    # detections even when the script output is unexpected; only exits outside
    # (0, 1, 70) are invalid execution.
    limit_legacy = Result("", "nift: memory budget exceeded\n", 70, None, [], [])
    limit_env = {"NIFT_VM_HEAP_BUDGET": "8000000"}

    def limit_stats(outcome):
        return {"outcome": outcome, "vm_executions": 1, "legacy_executions": 0,
                "catchable": False, "heap_peak": 7999000, "heap_live": 1000,
                "error_origin": False}

    limit_cases = [
        ("E1-style fault fabricated catchable, caught",
         Result("caught\nafter\n", "", 0, limit_stats("ok"), [], []), "detected"),
        ("budget not enforced, runs to end",
         Result("after\n", "", 0, limit_stats("ok"), [], []), "detected"),
        ("limit fault reported as script error rc 1",
         Result("", "error: memory budget exceeded\n", 1, limit_stats("error"), [], []), "detected"),
        ("correct limit termination (control)",
         Result("", "nift: memory budget exceeded\n", 70, limit_stats("internal-error"), [], []), "missed"),
        ("unexpected exit 3", Result("", "", 3, limit_stats("ok"), [], []), "harness"),
        ("unexpected exit 69", Result("", "", 69, limit_stats("ok"), [], []), "harness"),
    ]
    for label, vm, want in limit_cases:
        try:
            ev = evaluate_future(limit_legacy, vm, "uncatchable_heap_budget", "limit", "-", "",
                                 limit_env)
            got = mutant_verdict(ev.executed, ev.problems)
        except HarnessFailure:
            got = "harness"
        if got != want:
            raise AssertionError(f"limit classifier {label!r}: want {want}, got {got}")
    print("PASS harness self-test: one return type; checked/ASan/control rules; "
          "policy mapping; fallback-to-execution detection; fallback statuses and "
          "resource-limit classifications")
    return 0


def trace_selftest(nift):
    """H3: the fd-based Host trace captures a successful legacy Host read."""
    with tempfile.TemporaryDirectory(prefix="cp13b-trace-") as folder:
        root = Path(folder)
        source = root / "trace_positive.f"
        source.write_text('t := cat("trace_positive.f")\nprint("ok")\n')
        trace = root / ".trace"
        fd = os.open(trace, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": folder,
            "TMPDIR": folder,
            "LANG": "C",
            "LC_ALL": "C",
            "NIFT_VM": "0",
            "NIFT_PREVALIDATE": "off",
            "NIFT_TRACE_HOST": str(fd),
        }
        try:
            proc = subprocess.run([nift, source.name], cwd=root, env=env,
                                  stdin=subprocess.DEVNULL, capture_output=True,
                                  pass_fds=(fd,), timeout=30)
        finally:
            os.close(fd)
        lines = [line for line in trace.read_text().splitlines() if line.strip()]
        if proc.returncode != 0 or not lines:
            print(f"FAIL trace self-test: a successful legacy cat produced no Host trace "
                  f"(exit {proc.returncode}, stderr {proc.stderr[:120]!r})", file=sys.stderr)
            return 1
        if not any('"op"' in line for line in lines):
            print(f"FAIL trace self-test: malformed Host trace: {lines[:3]}", file=sys.stderr)
            return 1
    print(f"PASS trace self-test: fd-based NIFT_TRACE_HOST captured {len(lines)} Host entries")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True)
    parser.add_argument("--oracle", default=None)
    parser.add_argument("--mode", choices=["legacy", "current", "future"], default="current")
    parser.add_argument("--mutant", type=int, default=None)
    parser.add_argument("--checked-mutant", type=int, default=None)
    parser.add_argument("--asan-mutant", type=int, default=None)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--trace-selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    nift = str(Path(args.nift).resolve())
    if args.trace_selftest:
        return trace_selftest(nift)
    mutant_modes = [m for m in (args.mutant, args.checked_mutant, args.asan_mutant) if m is not None]
    if len(mutant_modes) > 1:
        print("FAIL choose one of --mutant, --checked-mutant, --asan-mutant", file=sys.stderr)
        return 2
    oracle = str(Path(args.oracle).resolve()) if args.oracle else nift
    manifest = load_tsv(MANIFEST)
    future = load_tsv(FUTURE)
    cases = sorted(p.stem for p in CASES.glob("*.f"))

    def row_for(name):
        row = future.get(name)
        if row is None:
            raise HarnessFailure("no EXPECT_FUTURE.tsv row")
        return row

    mutant_id = args.mutant if args.mutant is not None else (
        args.checked_mutant if args.checked_mutant is not None else args.asan_mutant)
    mutant_spec = json.loads(MUTANTS.read_text()) if mutant_id is not None else {}

    def baseline_gate():
        problems = []
        harness = []
        for name in cases:
            row = row_for(name)
            try:
                result = check_future(oracle, oracle, name, row[0],
                                      row[1] if len(row) > 1 else "-",
                                      env_from(row[2] if len(row) > 2 else ""),
                                      row[3] if len(row) > 3 else "")
            except HarnessFailure as error:
                harness.append(f"{name}: harness: {error}")
                continue
            if result.problems:
                problems.append(f"{name}: " + "; ".join(result.problems))
        return problems, harness

    if mutant_id is not None:
        if not args.oracle:
            print("FAIL mutant mode requires --oracle (the unmutated build)", file=sys.stderr)
            return 2
        spec = mutant_spec.get(str(mutant_id), {})
        if not spec:
            print(f"FAIL no MUTANTS.json entry for mutant {mutant_id}", file=sys.stderr)
            return 2
        # B5: fallback_reason is enforced against the baseline expectation.
        if spec.get("fallback_reason"):
            for case in spec.get("cases", []):
                row = row_for(stem(case))
                if row[0] != "fallback:" + spec["fallback_reason"]:
                    print(f"FAIL mutant {mutant_id}: {case} is not fallback:{spec['fallback_reason']}", file=sys.stderr)
                    return 1
        baseline, baseline_harness = baseline_gate()
        if baseline_harness:
            print(f"FAIL baseline future gate: {len(baseline_harness)} harness failure(s); no mutant can be credited")
            for line in baseline_harness[:8]:
                print("  " + line)
            return 1
        if baseline:
            if spec.get("requires_cp16") and spec.get("cases") and all(
                    "cp16" in (row_for(stem(c))[3] if len(row_for(stem(c))) > 3 else "")
                    for c in spec["cases"]):
                print(f"PENDING-CP16 mutant {mutant_id}: required cases need CP16; "
                      "no mutant can be credited")
            else:
                print(f"PENDING-PHASE-B mutant {mutant_id}: baseline future gate does not pass "
                      f"({len(baseline)} case(s)); no mutant can be credited")
            for line in baseline[:8]:
                print("  " + line)
            if len(baseline) > 8:
                print(f"  ... {len(baseline) - 8} more")
            return 1
        if args.checked_mutant is not None:
            harness = 0
            detected = set()
            for case in spec.get("checked_cases", []):
                name = stem(case)
                try:
                    check_legacy(oracle, name)
                    vm = run(nift, name, "1", env_from(row_for(name)[2] if len(row_for(name)) > 2 else ""))
                except HarnessFailure as error:
                    harness += 1
                    print(f"HARNESS checked {name}: {error}")
                    continue
                if checked_verdict(vm.stats, vm.rc):
                    detected.add(name + ".f")
                else:
                    print(f"MISSED checked {name}: outcome={vm.stats and vm.stats.get('outcome')} "
                          f"reason={vm.stats and vm.stats.get('reason')} exit={vm.rc}")
            control_failures = set()
            for control in spec.get("controls", []):
                name = stem(control)
                try:
                    row = row_for(name)
                    result = check_future(nift, oracle, name, row[0],
                                          row[1] if len(row) > 1 else "-",
                                          env_from(row[2] if len(row) > 2 else ""),
                                          row[3] if len(row) > 3 else "")
                except HarnessFailure as error:
                    harness += 1
                    print(f"HARNESS checked control {name}: {error}")
                    continue
                if result.problems:
                    control_failures.add(control)
                    print(f"CONTROL checked {name}: " + "; ".join(result.problems))
            required = set(spec.get("checked_cases", []))
            if harness or required - detected or control_failures:
                print(f"FAIL checked mutant {mutant_id}: harness {harness}; "
                      f"missed {sorted(required - detected)}; controls {sorted(control_failures)}")
                return 1
            print(f"PASS checked mutant {mutant_id}: detected in {', '.join(sorted(detected))}")
            return 0
        if args.asan_mutant is not None:
            harness = 0
            detected = set()
            for case in spec.get("asan_cases", []):
                name = stem(case)
                try:
                    vm = run(nift, name, "1", env_from(row_for(name)[2] if len(row_for(name)) > 2 else ""))
                except HarnessFailure as error:
                    harness += 1
                    print(f"HARNESS asan {name}: {error}")
                    continue
                if asan_verdict(vm.rc, vm.err):
                    detected.add(name + ".f")
                else:
                    print(f"MISSED asan {name}: exit={vm.rc}")
            required = set(spec.get("asan_cases", []))
            if harness or required - detected:
                print(f"FAIL asan mutant {mutant_id}: harness {harness}; missed {sorted(required - detected)}")
                return 1
            print(f"PASS asan mutant {mutant_id}: detected in {', '.join(sorted(detected))}")
            return 0
        def checker(name):
            row = row_for(name)
            return check_future(nift, oracle, name, row[0],
                                row[1] if len(row) > 1 else "-",
                                env_from(row[2] if len(row) > 2 else ""),
                                row[3] if len(row) > 3 else "")
        required = set(spec.get("cases", []))
        controls = set(spec.get("controls", []))
        # ASan-only cases are reported by --asan-mutant; a release build of the
        # same mutant may legitimately crash through UB and must not turn the
        # release gate into a harness failure.
        asan_only = {stem(c) for c in spec.get("asan_cases", [])}
        # Checked-only cases are exercised by --checked-mutant (the release
        # mutant may pop a stale handler defensively, as the design says).
        checked_only = {stem(c) for c in spec.get("checked_cases", [])}
        normal_cases = [c for c in cases if c not in asan_only and c not in checked_only]
        harness, detected, missed, control_failures = mutant_gate(normal_cases, checker, required, controls)
        required_missed = sorted(required - detected)
        if harness or required_missed or control_failures or not required:
            print(f"FAIL exception mutant {mutant_id}: harness failures {harness}; "
                  f"missed required {required_missed}; control failures {sorted(control_failures)}")
            return 1
        print(f"PASS exception mutant {mutant_id}: detected in {', '.join(sorted(detected))}")
        return 0

    failures = 0
    passed = 0
    for name in cases:
        try:
            if args.mode == "legacy":
                check_legacy(oracle, name)
                passed += 1
                continue
            if args.mode == "current":
                reason = (manifest.get(name) or [""])[0]
                if not reason:
                    raise HarnessFailure("no current-refusal manifest entry")
                problems = check_current(nift, oracle, name, reason)
            else:
                row = row_for(name)
                result = check_future(nift, oracle, name, row[0],
                                      row[1] if len(row) > 1 else "-",
                                      env_from(row[2] if len(row) > 2 else ""),
                                      row[3] if len(row) > 3 else "")
                problems = result.problems
        except HarnessFailure as error:
            failures += 1
            print(f"FAIL {name}: harness: {error}")
            continue
        if problems:
            failures += 1
            print(f"FAIL {name}: " + "; ".join(problems))
        else:
            passed += 1
            if args.verbose:
                print(f"ok   {name}")
    print(f"PASS {passed}, FAIL {failures} ({args.mode})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
