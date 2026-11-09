#!/usr/bin/env python3
"""merge / merge_deep contract through the CLI (owner 2026-11-10 08:57, combined-b3-b5.md section
65: the one-step clone). Where legacy defines the behaviour (result contents and order, the
sources unchanged, o.merge(o), merge_deep's recursive merge) the VM must print exactly what
legacy prints. Where B2 defines it (identity, nested sharing) the VM's output is pinned by hand.

usage: merge_contract.py --nift NIFT
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

LEGACY_EQUAL = """a := {"x": 1, "y": {"z": 2}, "w": [1]}
b := {"y": {"q": 3}, "v": 4}
r := a.merge(b)
print(r.stringify())
print(a.stringify())
print(b.stringify())
d := a.merge_deep(b)
print(d.stringify())
print(a.stringify())
s := a.merge(a)
print(s.stringify())
e := {}.merge(a)
print(e.stringify())
"""
# B2: a fresh outer object; merge shares nested values; merge_deep merges nested objects into new
# objects and shares the other values; the receiver's own entries are unchanged.
B2_PINNED = [
    ('a := {"k": 1}\nb := {"j": 2}\nr := a.merge(b)\nprint(same(r, a))\nprint(same(r, b))\n'
     'd := a.merge_deep(b)\nprint(same(d, a))\ns := a.merge(a)\nprint(same(s, a))\n',
     "false\nfalse\nfalse\nfalse\n"),
    ('a := {"l": [1], "o": {"k": 1}}\nr := a.merge({})\nx := r["l"]\nx.push(2)\nprint(a.stringify())\n'
     'd := a.merge_deep({"o": {"m": 2}})\ny := d["l"]\ny.push(3)\nprint(a.stringify())\nprint(d.stringify())\n',
     '{"l":[1,2],"o":{"k":1}}\n{"l":[1,2,3],"o":{"k":1}}\n{"l":[1,2,3],"o":{"k":1,"m":2}}\n'),
]


def run(nift, source, mode, work):
    path = os.path.join(work, "p.f")
    with open(path, "w") as handle:
        handle.write(source)
    stats = os.path.join(work, "stats.jsonl")
    if os.path.exists(stats):
        os.remove(stats)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": work, "LANG": "C", "NIFT_VM": mode}
    if mode != "0":
        env["NIFT_VM_STATS"] = stats
    p = subprocess.run([nift, "p.f"], cwd=work, env=env, capture_output=True, text=True, timeout=60)
    st = None
    if mode != "0" and os.path.exists(stats):
        with open(stats) as handle:
            lines = [json.loads(line) for line in handle if line.strip()]
        st = lines[-1] if lines else None
    return p.returncode, p.stdout, p.stderr, st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    failures = 0
    with tempfile.TemporaryDirectory(prefix="merge-contract-") as work:
        legacy = run(nift, LEGACY_EQUAL, "0", work)
        vm = run(nift, LEGACY_EQUAL, "1", work)
        ok = legacy[:3] == vm[:3] and vm[0] == 0 and vm[3] and vm[3]["outcome"] == "vm"
        print("%s legacy-equal contents, order, sources unchanged, o.merge(o), merge_deep (%d lines)"
              % ("PASS" if ok else "FAIL", vm[1].count("\n")))
        if not ok:
            print("  legacy %r\n  vm     %r" % (legacy[:3], vm[:3]))
            failures += 1
        for i, (source, expected) in enumerate(B2_PINNED):
            rc, out, err, st = run(nift, source, "1", work)
            ok = rc == 0 and out == expected and st and st["outcome"] == "vm"
            print("%s B2 pinned case %d" % ("PASS" if ok else "FAIL", i + 1))
            if not ok:
                print("  got %r %r (stats %s)" % (out, err, st and st.get("outcome")))
                failures += 1
    print("%s merge contract: %d failure(s)" % ("PASS" if not failures else "FAIL", failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
