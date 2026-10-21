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
    if not args.table and want is not None and label != want:
        problems.append(f"expected {want}, got {label}")
    return name, label if verdict != "approved" else f"{label} ({policy})", problems


# ---- random module programs (--gen) ------------------------------------

def random_modules(seed, index):
    """A random import tree: modules with private and exported bindings and
    functions (names prefixed per unit, so they stay apart), the entry
    calling exported functions and writing exported bindings in every
    context, occasional run-time errors."""
    import random
    rng = random.Random(seed * 7919 + index)
    files = {}
    count = rng.randint(1, 3)
    exported = {}  # module -> [(name, kind)]

    def body_for(prefix, names):
        nums = [n for n, k in names if k == "int"]
        arrs = [n for n, k in names if k == "array"]
        strs = [n for n, k in names if k == "string"]
        options = []
        if nums:
            n = rng.choice(nums)
            options += [f"{n} += {rng.randint(1, 3)}", f"{n} = {n} * 2", f"{n}++"]
        if arrs:
            a = rng.choice(arrs)
            options += [f"{a}.push({rng.randint(0, 9)})", f"{a}[0] = {rng.randint(0, 9)}"]
        if strs:
            options.append(f'{rng.choice(strs)} += "{rng.choice("xyz")}"')
        return rng.choice(options) if options else "q0 := 1"

    def show(n, k):
        return f"{n}.stringify()" if k == "array" else n

    for m in range(count):
        prefix = f"m{m}"
        names = [(f"{prefix}v{k}", rng.choice(["int", "array", "string"])) for k in range(rng.randint(1, 3))]
        lines = []
        if m + 1 < count and rng.random() < 0.5:
            lines.append(f'import("m{m + 1}.nft")')
            exported_below = exported.setdefault(m + 1, [])
        else:
            exported_below = None
        for n, k in names:
            lines.append(f"{n} := " + {"int": str(rng.randint(0, 5)), "array": "[1, 2]", "string": '"s"'}[k])
        if rng.random() < 0.3:
            lines.append(f'print("{prefix} init")')
        fns = []
        for f in range(rng.randint(1, 2)):
            fname = f"{prefix}f{f}"
            stmts = [body_for(prefix, names) for _ in range(rng.randint(1, 2))]
            if rng.random() < 0.2:
                stmts.append(f"e0 := [1]\n  print(e0[{rng.randint(0, 3)}])")
            n, k = rng.choice(names)
            stmts.append(f"return {show(n, k)}")
            lines.append(f"fn({fname}()) {{\n  " + "\n  ".join(stmts) + "\n}")
            fns.append(fname)
        if exported_below is not None and rng.random() < 0.5:
            lines.append("placeholder-calls")
        for fname in fns:
            lines.append(f"export({fname})")
        for n, k in names:
            if rng.random() < 0.5:
                lines.append(f"export({n})")
                exported.setdefault(m, []).append((n, k))
        exported.setdefault(m, [])
        exported[m] += [(fname, "fn") for fname in fns]
        files[f"m{m}.nft"] = lines
    # Calls of the imported module's functions inside a module (after its import).
    for m in range(count):
        lines = files[f"m{m}.nft"]
        if "placeholder-calls" in lines:
            below = [n for n, k in exported.get(m + 1, []) if k == "fn"]
            lines[lines.index("placeholder-calls")] = f"print({rng.choice(below)}())" if below else ""
        files[f"m{m}.nft"] = "\n".join(lines) + "\n"
    top = [n for n, k in exported[0]]
    calls = []
    fns = [n for n, k in exported[0] if k == "fn"]
    vals = [(n, k) for n, k in exported[0] if k != "fn"]
    for _ in range(rng.randint(1, 4)):
        stmt = f"print({rng.choice(fns)}())"
        if vals and rng.random() < 0.3:
            n, k = rng.choice(vals)
            stmt = {"int": f"{n} += 5", "array": f"{n}.push(8)", "string": f'{n} += "w"'}[k]
        r = rng.random()
        if r < 0.25:
            stmt = "i0 = 0\nwhile (i0 < 2) {\n  i0 += 1\n  " + stmt + "\n}"
        elif r < 0.4:
            stmt = "fn(w%d()) {\n  %s\n  return 0\n}\nprint(w%d())" % (len(calls), stmt, len(calls))
        calls.append(stmt)
    shows = [f"print({show(n, k)})" for n, k in vals]
    files["main.f"] = 'import("m0.nft")\ni0 := 0\n' + "\n".join(calls + shows) + "\n"
    del top
    return files


def check_random(args, work, index):
    files = random_modules(args.seed, index)
    name, label, problems = check(args, work, (f"random #{index}", files, None))
    return index, label, [p for p in problems if not p.startswith("expected")], files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--table", action="store_true")
    ap.add_argument("--gen", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
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
            if args.gen:
                gen_tally = {}
                for index, label, problems, files in pool.map(lambda i: check_random(args, work, i), range(args.gen)):
                    gen_tally[label] = gen_tally.get(label, 0) + 1
                    if problems:
                        failures += 1
                        print(f"FAIL random seed {args.seed} #{index}: " + "; ".join(problems))
                        for rel, text in sorted(files.items()):
                            print(f"--- {rel}\n{text}")
                on_vm = sum(n for k, n in gen_tally.items() if k.startswith("vm"))
                print(f"random: {args.gen} module programs (seed {args.seed}); on the VM {on_vm}; "
                      f"outcomes {dict(sorted(gen_tally.items(), key=str))}")
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
