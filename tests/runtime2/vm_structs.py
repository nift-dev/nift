#!/usr/bin/env python3
"""CP12 phase B: structs on the VM (rt/structs.h, compiler_structs.cpp)
against legacy.

  vm_structs.py --nift ./nift [--jobs 2] [--table] [--gen N --seed S]

Programs: the legacy struct probes (tests/runtime2/semantics-structs) and
the shapes below (fields, methods, implicit members and `this`,
constructors, private members, binding-type errors, method calls from
loops and functions, instances held by globals). Each runs on legacy and on
NIFT_VM=1: the results must be identical or match a CP12 policy
(vm_policies.py), and the outcome is pinned (`vm`, or the refusal reason).
--gen adds random struct programs (agreement with legacy only).
"""
import argparse
import concurrent.futures as cf
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vm_policies  # noqa: E402

COUNTER = 'struct (Counter) {\n  n := 0\n  step := 1\n  fn(Counter(s)) {\n    step = s\n  }\n' \
          '  fn(next()) {\n    n = n + step\n    return n\n  }\n  fn(twice()) {\n    next()\n    return this.next()\n  }\n}\n'
CONTEXTS = {
    "top": "{decl}c := Counter(2)\n{use}\n",
    "loop": "{decl}c := Counter(2)\ni0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n  {use}\n}}\n",
    "fn": "{decl}fn(w0()) {{\n  c := Counter(2)\n  {use}\n  return 0\n}}\nprint(w0())\n",
    "fnloop": "{decl}fn(w0()) {{\n  c := Counter(2)\n  {use}\n  return 0\n}}\ni0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n"
              "  print(w0())\n}}\n",
    "global": "{decl}c := Counter(2)\nfn(w0()) {{\n  {use}\n  return 0\n}}\nprint(w0())\nprint(w0())\n",
}
USES = {
    "method": ("print(c.next())", {"*": "vm"}),
    "nested-method": ("print(c.twice())", {"*": "vm"}),
    # Legacy fails on field access in loop code (L42): refused there.
    "field-read": ("print(c.n + c.step)", {"*": "vm", "loop": "context:struct-field", "fnloop": "context:struct-field"}),
    "field-write": ("c.n = c.n + 10\n  print(c.next())",
                    {"*": "vm", "loop": "context:struct-field", "fnloop": "context:struct-field"}),
    "field-type-error": ('c.n = "s"', {"*": "vm", "loop": "context:struct-field", "fnloop": "context:struct-field"}),
    # An instance is never copied: legacy would share it.
    "copy": ("d := c\n  print(1)", {"*": "aliasing", "fnloop": "prepared-decl-type"}),
    "print": ("print(c)", {"*": "type:print"}),
}

SHAPES = {
    "double-field": ('struct (P) {\n  x := 1.5\n}\np := P()\np.x = 2\nprint(p.x)\np.x += 1\nprint(p.x)\n', "vm"),
    "string-field": ('struct (P) {\n  s := "a"\n  fn(add(t)) {\n    s += t\n    return s\n  }\n}\np := P()\n'
                     'print(p.add("b"))\nprint(p.add("c"))\n', "vm"),
    "this-shadow": ('struct (P) {\n  x := 1\n  fn(set(x)) {\n    this.x = x\n    return this.x\n  }\n}\np := P()\n'
                    'print(p.set(4))\nprint(p.x)\n', "vm"),
    "ctor-arity": ('struct (P) {\n  x := 1\n}\np := P(3)\n', "static-error:struct-arity"),
    "private-field": ('struct (C) {\n  private n := 0\n  fn(next()) {\n    n = n + 1\n    return n\n  }\n}\n'
                      'c := C()\nprint(c.next())\nprint(c.next())\n', "vm"),
    "private-read": ('struct (C) {\n  private n := 0\n}\nc := C()\nprint(c.n)\n', "struct-private"),
    "private-method": ('struct (C) {\n  private fn(hid()) {\n    return 5\n  }\n  fn(pub()) {\n    return hid() + 1\n  }\n}\n'
                       'c := C()\nprint(c.pub())\n', "vm"),
    "no-field": ('struct (P) {\n  x := 1\n}\np := P()\nprint(p.zz)\n', "struct-no-field"),
    "two-instances": ('struct (P) {\n  x := 0\n  fn(inc()) {\n    x++\n    return x\n  }\n}\na := P()\nb := P()\n'
                      'print(a.inc())\nprint(a.inc())\nprint(b.inc())\nprint(a.x)\nprint(b.x)\n', "vm"),
    "method-loop-inside": ('struct (P) {\n  x := 1\n  fn(get()) {\n    i := 0\n    while (i < 2) {\n      i += 1\n'
                           '      x = x + 1\n    }\n    return x\n  }\n}\np := P()\nprint(p.get())\n',
                           "context:struct-field"),
    "method-calls-fn": ('fn(sq(v)) {\n  return v * v\n}\nstruct (P) {\n  x := 3\n  fn(area()) {\n    return sq(x)\n  }\n}\n'
                        'p := P()\nprint(p.area())\n', "vm"),
    "index-error-in-method": ('struct (P) {\n  x := 0\n  fn(at(k)) {\n    a := [1, 2]\n    return a[k]\n  }\n}\n'
                              'p := P()\nprint(p.at(1))\nprint(p.at(7))\n', "vm"),
}

# The legacy probes (directory -> outcome on the VM, or the refusal reason).
PROBES = {
    "basic": "vm", "concat": "type:+", "constructor": "static-error:arity",
    "element_member_assign": "struct-construction", "equality": "aliasing",
    "field_init_time": "struct-after-statement", "field_type": "vm", "loop": "vm", "method_arity": "static-error:arity",
    "method_free_caller": "free-name", "method_free_global": "struct-after-statement",
    "method_on_call": "opaque:legacy-runtime-error", "nested": "struct-field-init", "new_field": "struct-no-field",
    "no_field": "struct-no-field", "print": "struct-field-init", "private_field": "struct-private",
    "private_method": "struct-private", "redeclared": "struct-redeclared", "reference": "aliasing",
    "self_call": "vm", "shadows_fn": "struct-shadows-fn", "stringify": "struct-field-init",
    "this_assign": "assign-free-name", "this_outside": "free-name", "type": "struct-value",
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
    name, src, want = item
    d = tempfile.mkdtemp(dir=work)
    if os.path.isdir(src):
        shutil.copytree(src, d, dirs_exist_ok=True)
    else:
        with open(os.path.join(d, "main.f"), "w") as f:
            f.write(src)
    a = run(args.nift, d, {})
    b = run(args.nift, d, {"NIFT_VM": "1", "NIFT_VM_STATS": os.path.join(d, "stats.jsonl")})
    source = open(os.path.join(d, "main.f"), "rb").read()
    verdict, policy = vm_policies.classify((a[1], a[2], a[0]), (b[1], b[2], b[0]), b[3], source)
    outcome = b[3].get("outcome")
    label = "vm" if outcome in ("vm", "vm-error") else (b[3].get("reason") or outcome)
    problems = [] if verdict in ("same", "approved") else [f"legacy {a[:3]!r} != vm {b[:3]!r}"]
    if not args.table and want is not None and label != want:
        problems.append(f"expected {want}, got {label}")
    return name, label if verdict != "approved" else f"{label} ({policy})", problems, source.decode()


def random_struct(seed, index):
    rng = random.Random(seed * 31337 + index)
    fields = [(f"f{k}", rng.choice(["0", "1", "2.5", '"s"', "true"])) for k in range(rng.randint(1, 3))]
    nums = [n for n, v in fields if v in ("0", "1", "2.5")]
    strs = [n for n, v in fields if v == '"s"']
    lines = ["struct (S) {"] + [f"  {n} := {v}" for n, v in fields]
    methods = []
    for m in range(rng.randint(1, 3)):
        body = []
        for _ in range(rng.randint(1, 3)):
            opts = []
            if nums:
                n = rng.choice(nums)
                opts += [f"{n} = {n} + a", f"this.{n} += 1", f"{n} *= 2"]
            if strs:
                opts.append(f'{rng.choice(strs)} += "x"')
            if m > 0:
                opts.append(f"m{rng.randrange(m)}(1)")
            if rng.random() < 0.05:
                opts.append(f'{rng.choice([n for n, v in fields])} = [1]')
            body.append(rng.choice(opts) if opts else "q := 1")
        ret = rng.choice([n for n, v in fields])
        methods.append(f"m{m}")
        lines.append(f"  fn(m{m}(a)) {{\n    " + "\n    ".join(body) + f"\n    return {ret}\n  }}")
    lines.append("}")
    main = ["s := S()"]
    for _ in range(rng.randint(1, 4)):
        call = f"print(s.{rng.choice(methods)}({rng.randint(0, 3)}))"
        r = rng.random()
        if r < 0.3:
            call = "i0 = 0\nwhile (i0 < 2) {\n  i0 += 1\n  " + call + "\n}"
        elif r < 0.45 and nums:
            n = rng.choice(nums)
            call = f"s.{n} = s.{n} + 1\nprint(s.{n})"
        main.append(call)
    main.insert(1, "i0 := 0")
    return "\n".join(lines + main) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--table", action="store_true")
    ap.add_argument("--gen", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    args.nift = os.path.abspath(args.nift)
    items = []
    for use, (code, wants) in USES.items():
        for ctx, tmpl in CONTEXTS.items():
            items.append((f"{use}-{ctx}", tmpl.format(decl=COUNTER, use=code), wants.get(ctx, wants["*"])))
    items += [(n, src, want) for n, (src, want) in SHAPES.items()]
    probes = os.path.join(HERE, "semantics-structs")
    for d in sorted(os.listdir(probes)):
        if os.path.isdir(os.path.join(probes, d, "p")):
            items.append((f"probe:{d}", os.path.join(probes, d, "p"), PROBES.get(d)))
    items += [(f"random #{i}", random_struct(args.seed, i), None) for i in range(args.gen)]
    failures, tally = 0, {}
    with tempfile.TemporaryDirectory(prefix="vm-structs-") as work:
        with cf.ThreadPoolExecutor(args.jobs) as pool:
            for name, label, problems, src in pool.map(lambda it: check(args, work, it), items):
                tally[label] = tally.get(label, 0) + 1
                if args.table:
                    print(f"{name:32} {label}{'  DIFF' if problems else ''}")
                if problems:
                    failures += 1
                    print(f"FAIL {name}: " + "; ".join(problems) + "\n" + src)
    status = "PASS" if failures == 0 else "FAIL"
    print(f"{status} vm structs: {len(items)} programs; outcomes {dict(sorted(tally.items(), key=str))}; "
          f"{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
