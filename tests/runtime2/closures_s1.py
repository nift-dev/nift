#!/usr/bin/env python3
"""CP12C Slice 1 closure fixtures (Phase A harness).

  closures_s1.py --nift ./nift --prepare   # validate legacy goldens + current refusal
  closures_s1.py --nift ./nift             # Phase B differential run
  closures_s1.py --nift ./nift --mutant N  # a -DNIFT_CLOSURE_MUTANT=N build must fail

Fixtures live in closures-s1/: EXPECT pins the NIFT_VM=1 outcome; .stdout,
.stderr and .exit are the legacy oracle. D-038 caller-local lookup is the one
approved fallback (legacy output, VM refusal before execution); every other
difference goes through vm_policies (D-001#4 recursion, D-018c diagnostics).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "closures-s1")
sys.path.insert(0, HERE)
import vm_policies  # noqa: E402

# Phase A: the feature is not implemented yet. A fixture must be refused by
# the current compiler for one of the reasons this slice will remove, except
# the D-038 fixture, which must keep its documented caller-local refusal.
PREPARE_REASONS = {"expression:Lambda", "fn-as-value", "call-binding", "method:f"}
MUTANT_CASES = {
    1: {"g1_shared_escape.f"},
    2: {"g2_isolation.f", "g2_cell_isolation.f"},
    3: {"g1_self_cycle.f", "g1_mutual_cycles.f"},
    4: {"g4_closure_restore.f"},
    5: {"g12_cell_not_observable.f"},
    6: {"g7_lambda_too_few.f", "g7_named_too_many.f"},
}


def cases():
    out = []
    for line in open(os.path.join(FIXTURES, "EXPECT")):
        line = line.split("#")[0].strip()
        if line:
            out.append(tuple(line.split()))
    return out


def run(nift, work, name, mode, env_extra=None):
    d = tempfile.mkdtemp(dir=work)
    shutil.copy(os.path.join(FIXTURES, name), os.path.join(d, name))
    module = os.path.join(FIXTURES, "g11_module_lambda.m.nft")
    if os.path.exists(module):
        shutil.copy(module, os.path.join(d, "m.nft"))
    stats = os.path.join(d, "stats.jsonl")
    if os.path.exists(stats):
        os.remove(stats)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d,
           "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "NIFT_PREVALIDATE": "off"}
    env.update(env_extra or {})
    if mode:
        env.update({"NIFT_VM": mode, "NIFT_VM_STATS": stats})
    p = subprocess.run(["timeout", "60", nift, name], cwd=d, env=env, stdin=subprocess.DEVNULL,
                       capture_output=True)
    st = None
    if mode and os.path.exists(stats):
        try:
            st = json.loads(open(stats).read().splitlines()[-1])
        except (OSError, IndexError, ValueError):
            st = None
    return (p.stdout.replace(d.encode(), b"$ROOT"), p.stderr.replace(d.encode(), b"$ROOT"), p.returncode), st


def golden(name):
    stem = os.path.join(FIXTURES, name[:-2])
    return (open(stem + ".stdout", "rb").read(), open(stem + ".stderr", "rb").read(),
            int(open(stem + ".exit").read().strip()))


def check(nift, work, name, want, prepare):
    legacy, _ = run(nift, work, name, None)
    g = golden(name)
    problems = []
    if legacy != g:
        problems.append(f"legacy oracle differs: {legacy!r} != {g!r}")
    if name.startswith("g10_"):
        _, inv = run(nift, work, name, "classify-all")
        items = (inv or {}).get("reasons") or []
        if not items or items[0].get("reason") != "free-name:dynamic-caller":
            problems.append(f"D-038 inventory item 0 is not free-name:dynamic-caller: {items[:1]}")
    if prepare:
        classify, cst = run(nift, work, name, "classify")
        if not cst or cst.get("outcome") != "fallback":
            problems.append(f"prepare: expected a refusal, got {cst and (cst.get('outcome'), cst.get('reason'))}")
        elif want.startswith("fallback:"):
            if cst.get("reason") != want.split(":", 1)[1]:
                problems.append(f"prepare: D-038 reason {cst.get('reason')} != {want.split(':', 1)[1]}")
        elif cst.get("reason") not in PREPARE_REASONS:
            problems.append(f"prepare: first reason {cst.get('reason')} is outside the closure slice")
        return problems, cst.get("reason") if cst else None, cst

    vm, st = run(nift, work, name, "1" if want.startswith("fallback:") else "require")
    if st is None:
        return ["no NIFT_VM=1 statistics"], None, None
    if vm[2] < 0 or vm[2] >= 128:
        return ["harness: VM process crashed"], st.get("outcome"), st
    outcome = st.get("outcome")
    if want == "cycle":
        if (outcome != "vm-error" or st.get("code") != "runtime.reference_cycle" or
                "reference cycle: the stored value would contain itself" not in (st.get("reason") or "") or
                st.get("vm_executions") != 1):
            problems.append(f"expected runtime.reference_cycle, got {outcome}/{st.get('reason')} code={st.get('code')}")
        return problems, outcome, st
    verdict, policy = vm_policies.classify(legacy, vm, st, open(os.path.join(FIXTURES, name), "rb").read())
    if verdict not in ("same", "approved"):
        problems.append(f"legacy {legacy!r} != VM {vm!r} (verdict {verdict})")
    if want == "vm" and outcome != "vm":
        problems.append(f"expected vm, got {outcome}/{st.get('reason')}")
    elif want == "vm-error" and outcome != "vm-error":
        problems.append(f"expected vm-error, got {outcome}/{st.get('reason')}")
    elif want == "recursion":
        if not st.get("beyond_legacy_depth") or policy != "D-001#4 recursion":
            problems.append(f"expected D-001#4 recursion policy, got {outcome}/{st.get('reason')} policy={policy}")
    elif want.startswith("fallback:"):
        expected = want.split(":", 1)[1]
        if outcome != "fallback" or st.get("reason") != expected or st.get("vm_executions") != 0:
            problems.append(f"expected fallback:{expected}, got {outcome}/{st.get('reason')} "
                            f"vm_executions={st.get('vm_executions')}")
        if vm != legacy:
            problems.append("D-038 fallback output differs from legacy")
    return problems, outcome, st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--prepare", action="store_true")
    ap.add_argument("--mutant", type=int, choices=range(1, 7))
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures, tally = 0, {}
    detected = set()
    with tempfile.TemporaryDirectory(prefix="closures-s1-") as work:
        for name, want in cases():
            problems, outcome, st = check(nift, work, name, want, args.prepare)
            if args.mutant:
                # A mutant is detected only by a semantic differential failure
                # with a statistics line. A crash, timeout or missing statistics
                # is a harness failure, never a detection (review R2).
                if problems and any("no NIFT_VM=1 statistics" in p or "harness:" in p for p in problems):
                    failures += 1
                    print(f"FAIL mutant {args.mutant} {name}: harness: " + "; ".join(problems))
                elif name in MUTANT_CASES.get(args.mutant, set()):
                    if problems:
                        detected.add(name)
                        st = st or {}
                        print(f"ok   mutant {args.mutant} {name}: rejected "
                              f"(outcome={st.get('outcome')}, reason={st.get('reason')}, "
                              f"vm_exec={st.get('vm_executions')}, peak={st.get('heap_peak')})")
                    else:
                        print(f"miss mutant {args.mutant} {name}: still correct")
                continue
            tally[outcome or "?"] = tally.get(outcome or "?", 0) + 1
            if problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name} ({want}, {outcome})")
    if args.mutant:
        expected = MUTANT_CASES.get(args.mutant, set())
        missed = sorted(expected - set(detected))
        if failures or missed:
            print(f"FAIL closure mutant {args.mutant}: missed {missed}; {failures} harness failure(s)")
            return 1
        print(f"PASS closure mutant {args.mutant}: detected in {len(detected)} cases "
              f"(required: {', '.join(sorted(expected))})")
        return 0
    label = "prepare" if args.prepare else "differential"
    print(f"{'PASS' if not failures else 'FAIL'} closures-s1 {label}: {len(cases())} cases, {failures} failure(s); "
          f"outcomes {dict(sorted(tally.items(), key=str))}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
