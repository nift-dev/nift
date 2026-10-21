#!/usr/bin/env python3
"""CP12 phase B: modules on the VM (rt/module_link.h) against legacy.

  vm_modules.py --nift ./nift [--jobs 2] [--table]

Programs: every legacy semantics probe (tests/runtime2/semantics-modules),
the script fixtures of tests/runtime2/modules-d041 that agree with today's
legacy, and the shapes below (module functions and exported bindings used
in every context, nested modules, errors in a module's initialisation and
in its functions). Each runs in a fresh directory on legacy and on
NIFT_VM=1: the results must be identical or match a CP12 policy
(vm_policies.py: D-018c positions, legacy's `return: ` prefixes; the
`import: ` prefixes must be the same), and the outcome is pinned (`vm`:
ran on the VM; otherwise the refusal reason). --table prints outcomes
instead of checking the pins.
"""
import argparse
import concurrent.futures as cf
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vm_policies  # noqa: E402

COUNTER = 'n := 0\nfn(inc()) {\n  n += 1\n  return n\n}\nexport(n)\nexport(inc)\n'
LOOPS = {
    "top": "{call}\n",
    "loop": "i0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n  {call}\n}}\n",
    "fn": "fn(wcall()) {{\n  {call}\n  return 0\n}}\nprint(wcall())\n",
    "fnloop": "fn(wcall()) {{\n  {call}\n  return 0\n}}\ni0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n  print(wcall())\n}}\n",
}

# name -> ({file: text}, expected outcome); main.f is the entry
SHAPES = {}
for ctx, tmpl in LOOPS.items():
    SHAPES[f"counter-{ctx}"] = ({"m.nft": COUNTER,
                                 "main.f": 'import("m.nft")\n' + tmpl.format(call="print(inc())") + "print(n)\n"}, "vm")
    SHAPES[f"exported-write-{ctx}"] = ({"m.nft": COUNTER,
                                        "main.f": 'import("m.nft")\n' + tmpl.format(call="n += 10") +
                                        "print(inc())\n"}, "vm")
    SHAPES[f"string-fn-{ctx}"] = ({"m.nft": 'sep := "-"\nfn(join2(a, b)) {\n  return a + sep + b\n}\nexport(join2)\n',
                                   "main.f": 'import("m.nft")\n' + tmpl.format(call='print(join2("x", "y"))')}, "vm")
SHAPES.update({
    "nested-chain": ({"sub/a.nft": 'import("../c.nft")\nfn(af()) {\n  return cf() * 2\n}\nexport(af)\n',
                      "c.nft": 'k := 21\nfn(cf()) {\n  return k\n}\nexport(cf)\n',
                      "main.f": 'import("sub/a.nft")\nprint(af())\n'}, "vm"),
    "init-order": ({"m.nft": 'print("m runs")\nv := [1, 2]\nexport(v)\n',
                    "main.f": 'print("before")\nimport("m.nft")\nprint(v.length())\nprint("after")\n'}, "vm"),
    "error-in-init": ({"m.nft": 'q := [1]\nx := q[3]\n', "main.f": 'print("pre")\nimport("m.nft")\nprint("post")\n'},
                      "vm"),
    "error-in-nested-init": ({"b.nft": 'import("c.nft")\n', "c.nft": 'x := [1]\nprint(x[2])\n',
                              "main.f": 'import("b.nft")\nprint(1)\n'}, "vm"),
    "error-in-fn-during-init": ({"m.nft": 'fn(mf(x)) {\n  y := [x]\n  return y[x]\n}\nz := mf(1)\n',
                                 "main.f": 'import("m.nft")\n'}, "vm"),
    "error-in-fn-after-init": ({"m.nft": 'fn(mf(x)) {\n  y := [x]\n  return y[x]\n}\nexport(mf)\n',
                                "main.f": 'print(1)\nimport("m.nft")\nprint(mf(2))\n'}, "vm"),
    "private-helper": ({"m.nft": 'fn(helper(x)) {\n  return x * 3\n}\nfn(api(x)) {\n  return helper(x) + 1\n}\nexport(api)\n',
                        "main.f": 'import("m.nft")\nprint(api(4))\n'}, "vm"),
    # Refused: the importer names the module's private function.
    "private-helper-used": ({"m.nft": 'fn(helper(x)) {\n  return x * 3\n}\nfn(api(x)) {\n  return helper(x)\n}\n'
                                      'export(api)\n',
                             "main.f": 'import("m.nft")\nprint(helper(4))\n'}, "import-scope"),
    # records20k: a user-call argument path rooted at a module's binding is
    # refused (the stage 4 lift passes a value; legacy a location).
    "path-arg-imported": ({"m.nft": 'o := {"k": 1}\nexport(o)\n',
                           "main.f": 'fn(f(s)) {\n  return s + 1\n}\nimport("m.nft")\nprint(f(o.k))\n'},
                          "path-in-call-argument:imported"),
    "path-arg-own-global": ({"main.f": 'fn(f(s)) {\n  return s + 1\n}\no := {"k": 1}\nprint(f(o.k))\n'}, "vm"),
    # Refused: two units declare the same top-level name.
    "name-clash": ({"m.nft": 'tmp := 1\nfn(mf()) {\n  return tmp\n}\nexport(mf)\n',
                    "main.f": 'tmp := 2\nimport("m.nft")\nprint(mf() + tmp)\n'}, "import-name-clash"),
    # Refused: a module's bare top-level return ends only the module in legacy.
    "module-bare-return": ({"m.nft": 'print("a")\nreturn\n', "main.f": 'import("m.nft")\nprint("b")\n'}, "module-return"),
})

# Expected outcomes of the legacy probes and fixtures (directory -> outcome).
PROBES = {
    "cycle": "import-cycle", "diamond": "import-reinitialised", "dynamic_path": "import-dynamic",
    "export_collision": "import-collision", "export_copy": "binding-name-reserved", "export_missing": "export-missing",
    "in_block": "import-nested", "in_function": "import-nested", "missing": "import-missing",
    "module_bare_return": "module-return", "module_error": "type:-", "module_free_unbound": "free-name",
    # module_private: the importer declares `hidden` too (refused: names stay apart).
    "module_private": "import-name-clash", "module_private_late": "vm", "module_return": "module-return",
    "module_vs_caller_local": "import-scope", "nested_relative": "vm", "not_reexported": "import-scope",
    "package_missing": "import-package", "repeated": "import-reinitialised",
    "same_file_two_spellings": "import-reinitialised", "self_cycle": "import-cycle", "side_effect_order": "vm",
    "struct_export": "export-struct", "template_import": "binding-name-reserved",
}


def run(nift, d, env):
    p = subprocess.run(["timeout", "30", nift, "main.f"], cwd=d, capture_output=True,
                       env=dict({"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "LANG": "C"}, **env))
    stats = os.path.join(d, "stats.jsonl")
    st = json.loads(open(stats).read().splitlines()[-1]) if os.path.exists(stats) else {}
    if os.path.exists(stats):
        os.remove(stats)
    return p.returncode, p.stdout, p.stderr, st


def check(args, work, item):
    name, files, want = item
    d = tempfile.mkdtemp(dir=work)
    if isinstance(files, str):
        shutil.copytree(files, d, dirs_exist_ok=True)
    else:
        for rel, text in files.items():
            os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
            with open(os.path.join(d, rel), "w") as f:
                f.write(text)
    a = run(args.nift, d, {})
    b = run(args.nift, d, {"NIFT_VM": "1", "NIFT_VM_STATS": os.path.join(d, "stats.jsonl")})
    src = open(os.path.join(d, "main.f"), "rb").read()
    verdict, policy = vm_policies.classify((a[1], a[2], a[0]), (b[1], b[2], b[0]), b[3], src)
    outcome = b[3].get("outcome")
    label = "vm" if outcome in ("vm", "vm-error") else (b[3].get("reason") or outcome)
    problems = [] if verdict in ("same", "approved") else [f"legacy {a[:3]!r} != vm {b[:3]!r}"]
    if not args.table and label != want:
        problems.append(f"expected {want}, got {label}")
    return name, label if verdict != "approved" else f"{label} ({policy})", problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--table", action="store_true")
    args = ap.parse_args()
    args.nift = os.path.abspath(args.nift)
    items = [(n, files, want) for n, (files, want) in SHAPES.items()]
    probes = os.path.join(HERE, "semantics-modules")
    for d in sorted(os.listdir(probes)):
        if os.path.isdir(os.path.join(probes, d, "p")):
            items.append((f"probe:{d}", os.path.join(probes, d, "p"), PROBES.get(d, "?")))
    fixtures = os.path.join(HERE, "modules-d041")
    for d in sorted(os.listdir(fixtures)):
        spec = json.load(open(os.path.join(fixtures, d, "spec.json")))
        if spec["legacy"] == "agrees" and spec["steps"][0].get("run") == ["main.f"]:
            items.append((f"d041:{d}", os.path.join(fixtures, d, "p"), FIXTURES.get(d, "vm")))
    failures, tally = 0, {}
    with tempfile.TemporaryDirectory(prefix="vm-modules-") as work:
        with cf.ThreadPoolExecutor(args.jobs) as pool:
            for name, label, problems in pool.map(lambda it: check(args, work, it), items):
                tally[label] = tally.get(label, 0) + 1
                if args.table:
                    print(f"{name:36} {label}{'  DIFF' if problems else ''}")
                if problems:
                    failures += 1
                    print(f"FAIL {name}: " + "; ".join(problems))
    status = "PASS" if failures == 0 else "FAIL"
    print(f"{status} vm modules: {len(items)} programs; outcomes {dict(sorted(tally.items(), key=str))}; "
          f"{failures} failure(s)")
    return 1 if failures else 0


# Expected outcomes of the agreeing D-041 fixtures that are not on the VM.
FIXTURES = {"export_clash": "import-collision", "circular": "import-cycle", "self_cycle": "import-cycle",
            "doc_try_import": "import-nested", "doc_math": "expression:Lambda", "import_path_print": "type:print",
            "import_path_read": "path-in-call-argument", "import_path_write": "alias-argument",
            "nested_chain": "import-scope"}

if __name__ == "__main__":
    sys.exit(main())
