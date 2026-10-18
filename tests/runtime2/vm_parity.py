#!/usr/bin/env python3
"""Runtime 2.0 CP11: differential parity of the VM against the legacy engine.

  vm_parity.py --nift ./nift [--gen N] [--seed S] [--rt-diff] [--timeout T]

1. VM corpus (tests/runtime2/vm-corpus/cp11): every script runs on legacy and
   with NIFT_VM=1; stdout, stderr and exit status must be identical, and the
   VM outcome (vm / vm-error / fallback, with reason) must match the EXPECT
   manifest. Scripts expected to run on the VM must also produce the same
   output under NIFT_VM=require (genuine VM execution, no legacy at all).
2. Generated programs (vm_gen.py, the general-language generator: original
   distribution, representative Nift including unsupported constructs) and
   mutated near-miss programs: legacy vs NIFT_VM=1, all three channels
   identical. The VM-execution and fallback shares (by reason) are reported
   as the coverage metric; there is no coverage threshold (D-016). CP12: a
   `replayed` outcome cannot occur any more and fails the run; every unit
   must have executed exactly once (vm_executions + legacy_executions = 1).
3. --rt-diff: tools/rt-diff over the shared corpora with the CP11 and CP12
   policy files (D-001#4 recursion budget; D-018 class rules); per-script
   outcomes are collected through tools/rt-vm/nift-vm, and every class
   approval must be confirmed by the evidence in the VM statistics.

A difference is approved only through a named policy of vm_policies.py
(D-001#4 recursion budget; D-018c diagnostics checked against the program; no D-018a numbers policy, review round 2). Exit 1 on any
other difference or expectation mismatch.
"""
import argparse, collections, json, os, resource, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import vm_gen  # noqa: E402
import vm_policies  # noqa: E402

CORPUS = os.path.join(HERE, "vm-corpus", "cp11")


def run(nift, script, cwd, mode, stats, timeout):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
           "HOME": cwd, "TMPDIR": cwd}
    for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
        if k in os.environ:
            env[k] = os.environ[k]
    if mode:
        env["NIFT_VM"] = mode
        env["NIFT_VM_STATS"] = stats
    try:
        out_path, err_path = os.path.join(cwd, ".stdout"), os.path.join(cwd, ".stderr")
        with open(out_path, "wb") as out, open(err_path, "wb") as err:
            p = subprocess.run([nift, script], cwd=cwd, env=env, stdout=out, stderr=err, stdin=subprocess.DEVNULL,
                               timeout=timeout, preexec_fn=limit_child)
        # Compare whole outputs by size and hash, keep a prefix for reports.
        return capped(out_path), capped(err_path), p.returncode
    except subprocess.TimeoutExpired:
        return b"", b"timeout", "timeout"


def limit_child():
    """Generated programs can grow strings exponentially; bound both engines."""
    if "ASAN_OPTIONS" not in os.environ:  # ASan reserves terabytes of address space
        resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
    resource.setrlimit(resource.RLIMIT_FSIZE, (256 << 20, 256 << 20))


def capped(path):
    import hashlib
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        if size <= 1 << 20:
            data = f.read()
            os.unlink(path)
            return data
        head = f.read(4096)
    digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
    os.unlink(path)
    return head + f"...[{size} bytes, sha256 {digest}]".encode()


def last_stats(path):
    try:
        lines = open(path).read().splitlines()
    except FileNotFoundError:
        return None
    return json.loads(lines[-1]) if lines else None


def compare(nift, text, name, work, timeout, require=False):
    """Returns (same, legacy result, vm result, vm stats)."""
    d = tempfile.mkdtemp(dir=work)
    path = os.path.join(d, name)
    with open(path, "w") as f:
        f.write(text)
    stats = os.path.join(work, "stats.jsonl")
    if os.path.exists(stats):
        os.unlink(stats)
    a = run(nift, name, d, None, stats, timeout)
    b = run(nift, name, d, "1", stats, timeout)
    st = last_stats(stats)
    if st is not None:
        st["_source"] = text  # for the D-018c site checks (vm_policies.classify)
    c = run(nift, name, d, "require", stats, timeout) if require else None
    shutil.rmtree(d, ignore_errors=True)
    return a == b, a, b, st, c


LIMIT_EXITS = ("timeout", -25, -9)  # timeout, SIGXFSZ (output limit), SIGKILL


def nonterminating(a, b):
    """Both runs hit the time or output limit: an endless program (seen with
    mutants such as `c += 1` -> `c -= 1`); its output prefix depends on speed."""
    return a[2] in LIMIT_EXITS and b[2] in LIMIT_EXITS


def outcome_key(st):
    if not st:
        return "no-stats"
    if st["outcome"] in ("vm", "vm-error"):
        return st["outcome"]
    return f"{st['outcome']}:{st['reason']}"


def single_execution(st):
    """CP12 invariants A-C: one engine, once, chosen before execution."""
    return st is not None and st["outcome"] != "replayed" and \
        st.get("vm_executions", 0) + st.get("legacy_executions", 0) == 1


def classify_difference(a, b, st):
    v, policy = vm_policies.classify(a, b, st)
    return f"approved {policy}" if v == "approved" else v


def verdict(same, a, b, st):
    """'same', 'approved <policy>' (vm_policies.py), 'endless' or 'diff'."""
    if st is not None and not single_execution(st):
        return "diff"
    if same:
        return "same"
    if nonterminating(a, b):
        return "endless"
    return classify_difference(a, b, st)


def show(label, a, b):
    print(f"FAIL {label}")
    for i, ch in enumerate(("stdout", "stderr", "exit")):
        if a[i] != b[i]:
            print(f"  {ch}: legacy={a[i]!r:.300} vm={b[i]!r:.300}")


def corpus(args, work):
    expect = {}
    for line in open(os.path.join(CORPUS, "EXPECT")):
        line = line.split("#")[0].strip()
        if line:
            parts = line.split()
            expect[parts[0]] = (parts[1], parts[2] if len(parts) > 2 else "")
    names = sorted(f for f in os.listdir(CORPUS) if f.endswith(".f"))
    failures = 0
    tally = collections.Counter()
    for name in names:
        want = expect.get(name)
        text = open(os.path.join(CORPUS, name)).read()
        same, a, b, st, c = compare(args.nift, text, name, work, args.timeout,
                                    require=bool(want and want[0] in ("vm", "vm-error")))
        got = outcome_key(st)
        tally[got] += 1
        v = verdict(same, a, b, st)
        if v.startswith("approved"):
            tally[v] += 1
        elif v != "same":
            show(f"corpus {name}", a, b)
            failures += 1
        if not want:
            print(f"FAIL corpus {name}: no EXPECT entry (outcome {got})")
            failures += 1
        elif st is None or st["outcome"] != want[0] or not st["reason"].startswith(want[1]):
            print(f"FAIL corpus {name}: expected {want[0]} {want[1]}, got {got}")
            failures += 1
        elif c is not None and c != a and not classify_difference(a, c, st).startswith("approved"):
            show(f"corpus {name} (NIFT_VM=require)", a, c)
            failures += 1
    print(f"vm corpus: {len(names)} scripts, {failures} failures; outcomes {dict(sorted(tally.items()))}")
    return failures


def generated(args, work):
    failures = 0
    tally, mtally = collections.Counter(), collections.Counter()
    for i in range(args.gen):
        text = vm_gen.program(args.seed, i)
        same, a, b, st, _ = compare(args.nift, text, "gen.f", work, args.timeout)
        v = verdict(same, a, b, st)
        tally["nonterminating" if v == "endless" else v if v.startswith("approved") else outcome_key(st)] += 1
        if v == "diff":
            show(f"generated seed {args.seed} index {i}", a, b)
            failures += 1
            if failures > 20:
                break
        mtext, kind = vm_gen.mutate(text, args.seed, i)
        same, a, b, st, _ = compare(args.nift, mtext, "mut.f", work, args.timeout)
        v = verdict(same, a, b, st)
        mtally["nonterminating" if v == "endless" else v if v.startswith("approved") else outcome_key(st)] += 1
        if v == "diff":
            show(f"mutant ({kind}) seed {args.seed} index {i}", a, b)
            print("  program:\n" + "\n".join("    " + l for l in mtext.splitlines()))
            failures += 1
    print(f"generated: {args.gen} programs; outcomes {dict(tally.most_common())}")
    print(f"mutants: {args.gen} programs; outcomes {dict(mtally.most_common())}")
    # Coverage of the general generator is a measured outcome, never a
    # threshold (D-016): report the shares, fail only on differences.
    share = coverage_shares(tally, args.gen)
    print(f"general generator coverage (seed {args.seed}): ran on the VM {share['vm']} of {args.gen} "
          f"({share['vm_pct']:.1f} %; {share['vm_error']} of them ending in a program error), "
          f"fell back {share['fallback']} ({share['fallback_pct']:.1f} %)")
    print(f"  fallback reasons: {share['fallback_reasons']}")
    if args.gen_report:
        with open(args.gen_report, "w") as f:
            json.dump({"seed": args.seed, "count": args.gen, "generated": share,
                       "mutants": coverage_shares(mtally, args.gen)}, f, indent=1, sort_keys=True)
    return failures


def coverage_shares(tally, total):
    fallback = collections.Counter({k.split(":", 1)[1]: n for k, n in tally.items() if k.startswith("fallback:")})
    vm = tally.get("vm", 0) + tally.get("vm-error", 0)
    pct = lambda n: 100.0 * n / max(1, total)  # noqa: E731
    return {"vm": vm, "vm_pct": pct(vm), "vm_error": tally.get("vm-error", 0),
            "fallback": sum(fallback.values()), "fallback_pct": pct(sum(fallback.values())),
            "fallback_reasons": dict(fallback.most_common()),
            "other": {k: n for k, n in tally.items() if k not in ("vm", "vm-error") and not k.startswith("fallback:")}}


def rt_diff(args, work):
    stats_dir = os.path.join(REPO, ".build", "rt-vm")
    os.makedirs(stats_dir, exist_ok=True)
    stats = os.path.join(work, "rt-diff-stats.jsonl")
    with open(os.path.join(stats_dir, "stats-path"), "w") as f:
        f.write(stats)
    corpora = ["tests/runtime2/vm-corpus", "tests/runtime2/semantics", "tests/runtime2/rt-diff-corpus",
               "tests/conformance/cases", "docs/evidence/cp410-competitiveness/probes"]
    cmd = [sys.executable, "tools/rt-diff/rt-diff", "--a", args.nift, "--b", os.path.join(REPO, "tools/rt-vm/nift-vm"),
           "--ignore", "tools/rt-diff/ignore.txt", "--policy", "tools/rt-diff/policies/cp11-vm.json",
           "--policy", "tools/rt-diff/policies/cp12-vm.json",
           "--extract-smoke", "tests", "--gen", str(args.rt_diff_gen), "--seed", str(args.seed), "--quiet",
           "--b-vm-stats",
           "--report", os.path.join(work, "rt-diff-report.json")]
    for c in corpora:
        cmd += ["--corpus", c]
    env = dict(os.environ, NIFT_VM_BIN=args.nift)
    try:
        p = subprocess.run(cmd, cwd=REPO, env=env)
    finally:
        os.unlink(os.path.join(stats_dir, "stats-path"))
    tally = collections.Counter()
    seen = {}
    report = json.load(open(os.path.join(work, "rt-diff-report.json")))
    # Outcomes from each entry's own main runs (rt-diff --b-vm-stats); the
    # wrapper's shared stats file only receives the combined-channel runs.
    for r in report["results"]:
        for st in r.get("vm_stats") or []:
            seen[r["id"] + "|" + os.path.relpath(st["path"], os.path.dirname(st["path"]))] = outcome_key(st)
    tally.update(seen.values())
    print(f"rt-diff VM outcomes over {len(seen)} distinct scripts: {dict(tally.most_common())}")
    # The class rules classify with the entry's own statistics; confirm each approval here too.
    bad_class = 0
    evidence = {
        "cp11-recursion-budget-class": lambda v: v.get("beyond_legacy_depth"),
        "cp12-d018-diagnostics": lambda v: v["outcome"] == "vm-error",
    }
    approvals = collections.Counter()
    for r in report["results"]:
        for policy in sorted({d["policy"] for d in r["approved"] if d["policy"] in evidence}):
            approvals[policy] += 1
            # The entry's own VM statistics (rt-diff --b-vm-stats), never
            # matched by file name: 12 corpus scripts are called main.f
            # (review CP12 round 1, R2).
            own = r.get("vm_stats") or []
            if len(own) != 1 or not (own[0]["outcome"] in ("vm", "vm-error") and evidence[policy](own[0])):
                print(f"FAIL rt-diff class approval {policy} for {r['id']} not confirmed by VM statistics")
                bad_class += 1
    print(f"rt-diff class approvals (confirmed by VM statistics): {dict(sorted(approvals.items()))}")
    if bad_class:
        return 1
    if args.outcomes:
        with open(args.outcomes, "w") as f:
            json.dump({"by_script": seen, "tally": dict(tally)}, f, indent=1, sort_keys=True)
    return 0 if p.returncode == 0 else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--gen", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--timeout", type=float, default=30)
    ap.add_argument("--rt-diff", action="store_true")
    ap.add_argument("--rt-diff-gen", type=int, default=200)
    ap.add_argument("--outcomes", help="write rt-diff per-script VM outcomes (JSON)")
    ap.add_argument("--gen-report", help="write the general generator's coverage shares (JSON)")
    args = ap.parse_args()
    args.nift = os.path.abspath(args.nift)
    work = tempfile.mkdtemp(prefix="nift-vm-parity-")
    try:
        failures = corpus(args, work) + generated(args, work)
        if args.rt_diff:
            failures += rt_diff(args, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("PASS vm parity" if failures == 0 else f"FAIL vm parity: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
