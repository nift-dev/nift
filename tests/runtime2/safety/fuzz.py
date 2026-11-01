#!/usr/bin/env python3
"""B0 safety sweep (adapted from the independent review of 8a9dc923, finding F1):
value kinds x wrappers x delivery routes x operations, deterministic.

  fuzz.py --nift ./nift [--seed 1] [--sample 20000] [--session-sample 5000] [--jobs 2]

Three fixed slices, every program printing first:
  exhaustive  every kind x delivery x operation with the value alone (16 x 14 x 38)
  sample      a fixed-seed sample of the whole space (every wrapper, depth 4)
  session     a fixed-seed sample run as a piped `nift -i` session (no modules)
It asserts only the B0 invariants: no exit 70, no internal-failure text. The
outcome of each program (ordinary error, refusal, success) is not checked here;
safety.py checks the reviewed cases. Exit 0 when no program breaks them.
"""
import argparse
import concurrent.futures
import itertools
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile

INTERNAL = ("internal failure", "compiler defect", "unexpected operands")
VALS = {"int": "5", "str": '"s"', "null": "null", "bool": "true", "float": "1.5", "arr": "[1]",
        "obj": '{"k": 1}', "map": "MAP", "set": "SET", "stack": "STACK", "mutex": "mutex()",
        "chan": "channel()", "aint": "atomic<int>(1)", "abool": "atomic<bool>(true)", "fn": "(() => 1)",
        "range": "range(2)"}
SETUP = {"MAP": 'm0 := map()\nm0.set("q", 1)\n', "SET": 's0 := set()\ns0.add(1)\n',
         "STACK": 'k0 := stack()\nk0.push(1)\n'}
NAME = {"MAP": "m0", "SET": "s0", "STACK": "k0"}
WRAP = {"alone": "X", "arr": "[X]", "obj": '{"k": X}', "d3": "[[[X]]]", "d4obj": '{"a": [{"b": [X]}]}',
        "mixed": "[1, \"t\", X]"}
WRAPC = {"mapof": 'w1 := map()\nw1.set("k", X)\n', "stackof": 'w1 := stack()\nw1.push(X)\n',
         "mapmap": 'w2 := map()\nw2.set("j", X)\nw1 := map()\nw1.set("k", w2)\n',
         "stackarr": 'w1 := stack()\nw1.push([[X]])\n', "mapobjarr": 'w1 := map()\nw1.set("k", {"z": [X]})\n'}
# loopmix/loopmixs/param2/params (F1): the binding's kinds mix a scalar with the value's kind
DELIVER = ["direct", "param", "closure", "module", "mapget", "pop", "loop", "chan", "ternary", "ifelse",
           "loopmix", "loopmixs", "param2", "params"]
OPS = ["v.stringify()", "v.prettify()", "v.highlight()", "validate({}, [v])", "PRINT", '"i$[v.stringify()]"',
       "type(v)", "[v].stringify()", "{\"o\": v}.prettify()", "v == v", "[v, v].size()", "INTERP",
       "[v].contains(1)", "[v].join(\",\")", "[v].reverse()", "[v].sort()"]
MUT = ["v.push(1)", "v.pop()", "v.insert(0, 1)", "v.remove(0)", "v.splice(0, 1)", "v.splice(0, 1, [2])",
       "v.sort()", "v.sort((x, y) => x < y)", "v.reverse()", "v.clear()", 'v.set("k", 1)', 'v.remove("k")',
       "v.add(1)", "v.top()", 'v.get("k")', "v.contains(1)", "v[0] = 1", 'v.k = 1', "v += 1", 'v["k"] = 2',
       "v.size()", "v.length()"]


def build(kind, wrap, deliver, op):
    raw = VALS[kind]
    pre = ""
    if raw in SETUP:
        pre = SETUP[raw]
        raw = NAME[raw]
    if wrap in WRAP:
        expr = WRAP[wrap].replace("X", raw)
    else:
        pre += WRAPC[wrap].replace("X", raw)
        expr = "w1"

    def stmt(target):
        if op == "PRINT":
            return "print(%s)" % target
        if op == "INTERP":
            return 'print("i$[%s]")' % target
        o = op.replace("v", target)
        return o if op in MUT else "print(%s)" % o

    files = {}
    if deliver == "direct":
        prog = pre + 'v := %s\nprint("b")\n%s\n' % (expr, stmt("v"))
    elif deliver in ("param", "param2", "params"):
        first = {"param": "", "param2": "f(1)\n", "params": 'f("s")\n'}[deliver]
        prog = 'fn(f(v)) {\n  %s\n  return 0\n}\nprint("b")\n%s%sf(%s)\n' % (stmt("v"), pre, first, expr)
    elif deliver == "closure":
        prog = pre + 'g := () => %s\nprint("b")\nv := g()\n%s\n' % (expr, stmt("v"))
    elif deliver == "module":
        files["m.f"] = "fn(mk()) {\n  %s  return %s\n}\nexport(mk)\n" % (pre.replace("\n", "\n  "), expr)
        prog = 'import("m.f")\nprint("b")\nv := mk()\n%s\n' % stmt("v")
    elif deliver == "mapget":
        prog = pre + 'mm := map()\nmm.set("x", %s)\nprint("b")\nv := mm.get("x")\n%s\n' % (expr, stmt("v"))
    elif deliver == "pop":
        prog = pre + 'aa := [1, %s]\nprint("b")\nv := aa.pop()\n%s\n' % (expr, stmt("v"))
    elif deliver == "loop":
        prog = pre + 'aa := [%s]\nprint("b")\nfor (v : aa) { %s }\n' % (expr, stmt("v"))
    elif deliver == "loopmix":
        prog = pre + 'aa := [1, %s]\nprint("b")\nfor (v : aa) { %s }\n' % (expr, stmt("v"))
    elif deliver == "loopmixs":
        prog = pre + 'aa := ["s", %s]\nprint("b")\nfor (v : aa) { %s }\n' % (expr, stmt("v"))
    elif deliver == "chan":
        prog = pre + 'cc := channel()\ncc.send(%s)\nprint("b")\nv := cc.recv()\n%s\n' % (expr, stmt("v"))
    elif deliver == "ternary":
        prog = pre + ('fn(pick(n)) {\n  if (n > 0) { return %s }\n  return [1]\n}\nprint("b")\nv := pick(1)\n%s\n'
                      % (expr, stmt("v")))
    else:  # ifelse
        prog = pre + 'v := [1]\nif (args.size() == 0) { v = %s }\nprint("b")\n%s\n' % (expr, stmt("v"))
    files["p.f"] = prog
    return files


def run(nift, files, session):
    d = tempfile.mkdtemp(prefix="nift-safety-fuzz-")
    try:
        for f, t in files.items():
            with open(os.path.join(d, f), "w") as fh:
                fh.write(t)
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C", "LC_ALL": "C",
               "TZ": "UTC", "NIFT_VM": "require"}
        if session:
            p = subprocess.run(["timeout", "30", nift, "-i"], cwd=d, env=env, capture_output=True,
                               input=files["p.f"].encode() + b'print("after")\n')
        else:
            p = subprocess.run(["timeout", "30", nift, "p.f"], cwd=d, env=env, stdin=subprocess.DEVNULL,
                               capture_output=True)
        return p.returncode, p.stderr.decode("utf-8", "replace")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--sample", type=int, default=20000)
    ap.add_argument("--session-sample", type=int, default=5000)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--show", type=int, default=20)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    ops = OPS + MUT
    space = list(itertools.product(VALS, list(WRAP) + list(WRAPC), DELIVER, ops))
    rng = random.Random(a.seed)
    exhaustive = [(k, "alone", d, o) for k, d, o in itertools.product(VALS, DELIVER, ops)]
    rest = [c for c in space if c[1] != "alone"]
    sample = rng.sample(rest, min(a.sample, len(rest)))
    session = rng.sample([c for c in space if c[2] != "module"], a.session_sample)
    jobs = [("file", c) for c in exhaustive + sample] + [("session", c) for c in session]
    bad, stats = [], {}

    def one(job):
        mode, case = job
        rc, err = run(nift, build(*case), mode == "session")
        return mode, case, rc, err

    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for mode, case, rc, err in pool.map(one, jobs):
            broken = rc == 70 or any(t in err for t in INTERNAL)
            key = "%s:%s" % (mode, "BROKEN" if broken else ("ok" if rc == 0 else "error-or-refusal"))
            stats[key] = stats.get(key, 0) + 1
            if broken:
                bad.append({"mode": mode, "case": list(case), "exit": rc, "stderr": err.strip()[-240:]})
    for b in bad[:a.show]:
        print("FAIL " + json.dumps(b))
    summary = "%d programs (%d exhaustive, %d sampled, %d session; seed %d) %s" % (
        len(jobs), len(exhaustive), len(sample), len(session), a.seed, json.dumps(stats, sort_keys=True))
    if bad:
        print("FAIL runtime2 safety fuzz: %d program(s) exit 70 or report an internal failure; %s" % (len(bad), summary))
        return 1
    print("PASS runtime2 safety fuzz: %s" % summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
