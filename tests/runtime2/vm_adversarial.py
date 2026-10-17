#!/usr/bin/env python3
"""Runtime 2.0 CP11: adversarial differential gate, legacy vs NIFT_VM=1.

  vm_adversarial.py --nift ./nift [--seed S] [--count N] [--families a,b] [--jobs J]

Programs come from vm_adversarial_gen.py (deterministic for a seed). Each
program is first classified with NIFT_VM=classify (parse + compile only, no
execution). A static fallback hands the unit to legacy before anything runs,
so NIFT_VM=1 is legacy by construction and the program is only counted.
Every program that compiles is run on legacy and with NIFT_VM=1; stdout,
stderr and exit status must be identical, except for:

  approved   the D-001#4 recursion budget (classify_difference): legacy
             fails with "callable recursion depth exceeded", the VM ran the
             program to completion past legacy's 64-frame limit
             (beyond_legacy_depth in its statistics), with legacy's output
             as a prefix of the VM's and no diagnostic.
  endless    both engines hit the time limit (not compared).

Any other difference, any `internal-error` outcome and any missing statistics
line fail the gate. A program may also name the outcome the VM statistics must
show (e.g. the memory family must end in a recursion-budget bail); a
different outcome fails the gate. Exit 1 on failure.
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

RECURSION_ERROR = re.compile(rb"^error: [^\n]*: callable recursion depth exceeded: [A-Za-z_][A-Za-z0-9_]*\n\Z")


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
    """'same', 'approved' (D-001#4), 'endless' or 'diff'."""
    if legacy == vm:
        return "same"
    if legacy[2] == "timeout" and vm[2] == "timeout":
        return "endless"
    if (st and st.get("outcome") == "vm" and st.get("beyond_legacy_depth") and legacy[2] == 1 and vm[2] == 0
            and vm[1] == b"" and RECURSION_ERROR.match(legacy[1]) and vm[0].startswith(legacy[0])):
        return "approved"
    return "diff"


def outcome_key(st):
    if not st:
        return "no-stats"
    if st["outcome"] in ("vm", "compiled"):
        return st["outcome"]
    return f"{st['outcome']}:{st['reason']}"


def check(nift, item, work, timeout):
    family, name, source, extra = item[:4]
    expect = item[4] if len(item) > 4 else None
    d = tempfile.mkdtemp(dir=work)
    path = os.path.join(d, "p.f")
    with open(path, "w") as f:
        f.write(source)
    try:
        _, cst = run(nift, path, d, "classify", extra, timeout)
        if not cst:
            return family, name, "no-stats", "fail", source, None, None
        if cst["outcome"] == "fallback":
            verdict = "fail" if expect else "fallback"
            return family, name, outcome_key(cst), verdict, source, None, None
        t = timeout * (8 if family == "output" else 1)
        legacy, _ = run(nift, path, d, None, extra, t)
        vm, st = run(nift, path, d, "1", extra, t)
        verdict = classify_difference(legacy, vm, st)
        key = outcome_key(st) if vm[2] != "timeout" else "timeout"
        if st and st["outcome"] == "internal-error":
            verdict = "diff"
        if not st and vm[2] != "timeout":
            verdict = "diff"
        if expect and not key.startswith(expect):
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
        ran = sum(n for k, n in tally[family].items() if k == "vm")
        replay = sum(n for k, n in tally[family].items() if k.startswith("replayed"))
        print(f"{family}: {sum(v.values())} programs; ran on the VM {ran}, replayed {replay}, "
              f"fallback {v['fallback']}; same {v['same']}, approved D-001#4 {v['approved']}, "
              f"endless {v['endless']}, DIFF {v['diff'] + v['fail']}")
        top = ", ".join(f"{k} {n}" for k, n in tally[family].most_common(8))
        print(f"  outcomes: {top}")
    if args.report:
        with open(args.report, "w") as f:
            json.dump({"seed": args.seed, "count": args.count,
                       "families": {k: dict(v) for k, v in tally.items()},
                       "verdicts": {k: dict(v) for k, v in verdicts.items()}}, f, indent=1, sort_keys=True)
    bad = total["diff"] + total["fail"]
    print(("PASS" if not bad else "FAIL") + f" vm adversarial: {sum(total.values())} programs, {bad} non-approved "
          f"differences, {total['approved']} approved (D-001#4), {total['endless']} endless, "
          f"{total['fallback']} static fallbacks")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
