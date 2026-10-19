#!/usr/bin/env python3
"""Runtime 2.0 CP11: adversarial differential gate, legacy vs NIFT_VM=1.

  vm_adversarial.py --nift ./nift [--seed S] [--count N] [--families a,b] [--jobs J]

Programs come from vm_adversarial_gen.py (deterministic for a seed). Each
program is first classified with NIFT_VM=classify (parse + compile only, no
execution). A static fallback hands the unit to legacy before anything runs,
so NIFT_VM=1 is legacy by construction and the program is only counted.
Every program that compiles is run on legacy and with NIFT_VM=1; stdout,
stderr and exit status must be identical, except for:

  approved   one of the named approved-difference policies of
             vm_policies.py (D-001#4 recursion budget; CP12 D-018a numbers,
             D-018c diagnostics), each keyed to evidence
             in the VM statistics; counted per policy.
  endless    both engines hit the time limit (not compared).

Any other difference, any `internal-error` outcome, any missing statistics
line and any `replayed` outcome (CP12: replay no longer exists) fail the
gate. A program may also name the outcome the VM statistics must
show (e.g. the memory family must end in a recursion-budget bail), or a
tuple of acceptable outcome prefixes; `fallback:<reason>` requires a static
refusal with that reason before execution (review round 3). A different
outcome fails the gate. Exit 1 on failure.
"""
import argparse
import collections
import concurrent.futures as cf
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vm_adversarial_gen as gen  # noqa: E402
import vm_policies  # noqa: E402

def base_env(cwd, extra):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
           "HOME": cwd, "TMPDIR": cwd}
    for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
        if k in os.environ:
            env[k] = os.environ[k]
    env.update(extra)
    return env


def run(nift, path, cwd, mode, extra, timeout):
    env = base_env(cwd, extra)
    stats = os.path.join(cwd, ".stats.jsonl")
    if mode:
        env["NIFT_VM"] = mode
        env["NIFT_VM_STATS"] = stats
    if os.path.exists(stats):
        os.unlink(stats)
    try:
        p = subprocess.run([nift, os.path.basename(path)], cwd=cwd, env=env, capture_output=True,
                           stdin=subprocess.DEVNULL, timeout=timeout)
        result = (p.stdout, p.stderr, p.returncode)
    except subprocess.TimeoutExpired:
        result = (b"", b"timeout", "timeout")
    st = None
    if mode and os.path.exists(stats):
        lines = open(stats).read().splitlines()
        st = json.loads(lines[-1]) if lines else None
    return result, st


def classify_difference(legacy, vm, st):
    """'same', 'approved:<policy>' (vm_policies.py), 'endless' or 'diff'."""
    verdict, policy = vm_policies.classify(legacy, vm, st)
    return f"approved:{policy}" if verdict == "approved" else verdict


def outcome_key(st):
    if not st:
        return "no-stats"
    if st["outcome"] in ("vm", "compiled", "vm-error"):
        return st["outcome"]
    return f"{st['outcome']}:{st['reason']}"


def prevalidated(nift, path, cwd, extra, timeout):
    """CP13 (D-022 #3): the unit is rejected by syntax prevalidation before
    engine selection (NIFT_PREVALIDATE=only exits 65), on both engines alike:
    nothing executes and no VM statistics exist. A static refusal."""
    env = base_env(cwd, extra)
    env["NIFT_PREVALIDATE"] = "only"
    try:
        p = subprocess.run([nift, os.path.basename(path)], cwd=cwd, env=env, capture_output=True,
                           stdin=subprocess.DEVNULL, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False
    return p.returncode == 65


def check(nift, item, work, timeout):
    family, name, source, extra = item[:4]
    expect = item[4] if len(item) > 4 else None
    if isinstance(expect, str):
        expect = (expect,)
    d = tempfile.mkdtemp(dir=work)
    path = os.path.join(d, "p.f")
    with open(path, "w") as f:
        f.write(source)
    try:
        _, cst = run(nift, path, d, "classify", extra, timeout)
        if not cst:
            if prevalidated(nift, path, d, extra, timeout):
                return family, name, "prevalidated", "fallback", source, None, None
            return family, name, "no-stats", "fail", source, None, None
        if cst["outcome"] == "fallback":
            ok = not expect or any(outcome_key(cst).startswith(e) for e in expect)
            return family, name, outcome_key(cst), "fallback" if ok else "fail", source, None, None
        t = timeout * (8 if family == "output" else 1)
        legacy, _ = run(nift, path, d, None, extra, t)
        vm, st = run(nift, path, d, "1", extra, t)
        verdict = classify_difference(legacy, vm, st)
        key = outcome_key(st) if vm[2] != "timeout" else "timeout"
        if st and st["outcome"] in ("internal-error", "replayed"):
            verdict = "diff"
        # Single execution: the unit ran on exactly one engine, once.
        if st and (st.get("vm_executions", 0) + st.get("legacy_executions", 0)) != 1:
            verdict = "diff"
        if not st and vm[2] != "timeout":
            verdict = "diff"
        if expect and not any(key.startswith(e) for e in expect):
            verdict = "fail"
        return family, name, key, verdict, source, legacy, vm
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--count", type=int, default=100, help="programs per random family (typed, context, ...)")
    ap.add_argument("--families", default=",".join(gen.FAMILIES))
    ap.add_argument("--per-family", type=int, default=0,
                    help="keep an evenly spaced sample of at most this many programs per family (sanitizer runs)")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--timeout", type=float, default=5)
    ap.add_argument("--keep", help="directory to keep failing programs in")
    ap.add_argument("--report", help="write per-family outcome counts (JSON)")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    families = [f for f in args.families.split(",") if f]
    items = []
    for f in families:
        programs = list(gen.FAMILIES[f](args.seed, args.count))
        if args.per_family and len(programs) > args.per_family:
            stride = len(programs) / args.per_family
            programs = [programs[int(k * stride)] for k in range(args.per_family)]
        items += programs
    work = tempfile.mkdtemp(prefix="nift-vm-adv-")
    tally = collections.defaultdict(collections.Counter)
    verdicts = collections.defaultdict(collections.Counter)
    failures = []
    try:
        with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for family, name, key, verdict, source, legacy, vm in pool.map(
                    lambda item: check(nift, item, work, args.timeout), items):
                tally[family][key] += 1
                verdicts[family][verdict] += 1
                if verdict in ("diff", "fail"):
                    failures.append((family, name, key, source, legacy, vm))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for family, name, key, source, legacy, vm in failures[:30]:
        print(f"FAIL {name} [{key}]")
        print("  program:\n" + "\n".join("    " + l for l in source.splitlines()[:40]))
        if legacy:
            print(f"  legacy: exit={legacy[2]} out={legacy[0][:300]!r} err={legacy[1][:300]!r}")
            print(f"  vm:     exit={vm[2]} out={vm[0][:300]!r} err={vm[1][:300]!r}")
    if args.keep and failures:
        os.makedirs(args.keep, exist_ok=True)
        for family, name, *_rest in failures:
            with open(os.path.join(args.keep, name + ".f"), "w") as f:
                f.write(_rest[1])
    total = collections.Counter()
    for family in families:
        v = verdicts[family]
        total.update(v)
        ran = sum(n for k, n in tally[family].items() if k in ("vm", "vm-error"))
        errors = sum(n for k, n in tally[family].items() if k == "vm-error")
        approved = ", ".join(f"{k[9:]} {n}" for k, n in sorted(v.items()) if k.startswith("approved:")) or "none"
        print(f"{family}: {sum(v.values())} programs; ran on the VM {ran} ({errors} ending in a program error), "
              f"fallback {v['fallback']}; same {v['same']}, approved ({approved}), "
              f"endless {v['endless']}, DIFF {v['diff'] + v['fail']}")
        top = ", ".join(f"{k} {n}" for k, n in tally[family].most_common(8))
        print(f"  outcomes: {top}")
    if args.report:
        with open(args.report, "w") as f:
            json.dump({"seed": args.seed, "count": args.count,
                       "families": {k: dict(v) for k, v in tally.items()},
                       "verdicts": {k: dict(v) for k, v in verdicts.items()}}, f, indent=1, sort_keys=True)
    bad = total["diff"] + total["fail"]
    approved = {k[9:]: n for k, n in sorted(total.items()) if k.startswith("approved:")}
    print(("PASS" if not bad else "FAIL") + f" vm adversarial: {sum(total.values())} programs, {bad} non-approved "
          f"differences, {sum(approved.values())} approved {approved}, {total['endless']} endless, "
          f"{total['fallback']} static fallbacks")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
