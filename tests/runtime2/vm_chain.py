#!/usr/bin/env python3
"""CP12 phase B: postfix links on call results and literals (chained postfix).

  vm_chain.py --nift ./nift [--jobs 2]

Every shape below runs in four contexts (top level, loop body, a function
called from the top level, a function called from a loop) on legacy and on
NIFT_VM=1; stdout, stderr and exit status must be identical or match a CP12
policy (vm_policies.py: loop code's `invalid index` wording, D-018c). The
shapes the VM admits must run on the VM (outcome vm/vm-error) — except that
`fo`, which returns an object literal, cannot be prepared by legacy and is
refused in loop-called code (`prepared-fn-unprepared`, phase A); member
reads on call results and literals must be refused before execution
(`postfix-receiver:member`, language-consistency L43).
"""
import argparse
import concurrent.futures as cf
import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vm_policies  # noqa: E402

FUNCS = ('fn(fa(n)) { return [n, [n, n + 1], "ab"] }\n'
         'fn(fo(n)) { return {"k": [n, 2], "s": "xy", "n": n} }\n'
         'fn(fs(n)) { return "héllo" }\n')

# (expression, expected: "vm" = admitted, or the refusal reason)
SHAPES = [
    ("fa(1)[0]", "vm"), ("fa(1)[1][1]", "vm"), ("fa(1)[2].length()", "vm"), ("fa(1).length()", "vm"),
    ("fa(1).size()", "vm"), ("fa(1)[1].length()", "vm"), ("fo(2)[\"k\"][0]", "vm"),
    ("fo(2)[\"s\"].length()", "vm"), ("fs(3).length()", "vm"), ("[1, [2, 3]][1][0]", "vm"),
    ("[1, 2, 3].length()", "vm"), ("\"héllo\".length()", "vm"), ("{\"k\": [4, 5]}[\"k\"][1]", "vm"),
    ("{\"a\": 1}.size()", "vm"),
    # method chains over natives (CP14 string methods on a call result)
    ("\" a \".trim().to_upper()", "vm"), ("fa(1)[2].to_upper().length()", "vm"),
    # run-time errors, reported by the VM as legacy does
    ("fa(1)[7]", "vm"), ("fa(1)[-1]", "type:print"), ("fo(2)[\"zz\"]", "vm"), ("[1, 2][5]", "vm"),
    ("{\"k\": 1}[\"q\"]", "vm"), ("fa(1)[1][9]", "vm"),
    # member reads on call results and literals stay refused
    ("fo(2).n", "postfix-receiver:member"), ("fo(2).k[0]", "postfix-receiver:member"),
    ("fo(2).zz", "postfix-receiver:member"), ("{\"a\": 1}.a", "postfix-receiver:member"),
]

CONTEXTS = {
    "top": "{f}print({e})\n",
    "loop": "{f}i0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n  print({e})\n}}\n",
    "fn": "{f}fn(w0()) {{\n  print({e})\n  return 0\n}}\nprint(w0())\n",
    "fnloop": "{f}fn(w0()) {{\n  print({e})\n  return 0\n}}\ni0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n  print(w0())\n}}\n",
}


def run(nift, work, src, env):
    d = tempfile.mkdtemp(dir=work)
    with open(os.path.join(d, "t.f"), "w") as f:
        f.write(src)
    p = subprocess.run(["timeout", "30", nift, "t.f"], cwd=d, capture_output=True,
                       env=dict({"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d}, **env))
    stats = os.path.join(d, "stats.jsonl")
    st = json.loads(open(stats).read().splitlines()[-1]) if os.path.exists(stats) else {}
    return p.returncode, p.stdout, p.stderr.replace(d.encode(), b"$D"), st


def check(args, work, item):
    expr, want, ctx = item
    src = CONTEXTS[ctx].format(f=FUNCS, e=expr)
    a = run(args.nift, work, src, {})
    b = run(args.nift, work, src, {"NIFT_VM": "1", "NIFT_VM_STATS": "stats.jsonl"})
    st = b[3]
    problems = []
    verdict, policy = vm_policies.classify((a[1], a[2], a[0]), (b[1], b[2], b[0]), st, src)
    if verdict not in ("same", "approved"):
        problems.append(f"legacy {a[:3]!r} != vm {b[:3]!r}")
    if "fo(" in expr and ctx in ("loop", "fnloop"):
        want = "prepared-fn-unprepared"
    outcome = st.get("outcome")
    if want == "vm" and outcome not in ("vm", "vm-error"):
        problems.append(f"expected on the VM, got {outcome} {st.get('reason')}")
    if want != "vm" and (outcome != "fallback" or st.get("reason") != want or st.get("vm_executions") != 0):
        problems.append(f"expected refusal {want}, got {outcome} {st.get('reason')}")
    return expr, ctx, outcome if verdict != "approved" else f"{outcome} ({policy})", problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--jobs", type=int, default=2)
    args = ap.parse_args()
    args.nift = os.path.abspath(args.nift)
    items = [(e, w, c) for e, w in SHAPES for c in CONTEXTS]
    failures, tally = 0, {}
    with tempfile.TemporaryDirectory(prefix="vm-chain-") as work:
        with cf.ThreadPoolExecutor(args.jobs) as pool:
            for expr, ctx, outcome, problems in pool.map(lambda it: check(args, work, it), items):
                tally[outcome] = tally.get(outcome, 0) + 1
                if problems:
                    failures += 1
                    print(f"FAIL {ctx}: {expr}: " + "; ".join(problems))
    status = "PASS" if failures == 0 else "FAIL"
    print(f"{status} vm chain: {len(items)} programs ({len(SHAPES)} shapes x {len(CONTEXTS)} contexts); "
          f"outcomes {dict(sorted(tally.items(), key=str))}; {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
