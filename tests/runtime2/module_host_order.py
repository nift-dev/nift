#!/usr/bin/env python3
"""R1 (owner, 2026-10-21): imports and host operations keep legacy's order.

A linked unit (CP12b modules) reads every module before it runs, so a host
operation that legacy runs before or between imports would see different
files and trace in a different order. Such units are refused before they run
(`module-host-op:<built-in>`), unless no host op can run before the last
module read (slot 2, owner 2026-11-11 05:41, D-068 #2; src/rt/module_host_order.h):
a strict leading import block in the entry, and module initialisation, across
the whole graph, free of host ops and calls (pure natives and print allowed;
callbacks, struct construction and shadowable built-ins refused).

Each case runs in two fresh copies of its files: legacy (NIFT_VM=0) and the
VM in auto mode (NIFT_VM=1, which falls back to legacy when refused). stdout,
stderr, the exit status, the Host trace and the resulting tree must be
identical, and the VM's stats must show the expected outcome. A third run
with NIFT_VM=require must refuse refused cases before any effect (no trace
line, unchanged tree).

Negative controls: a build with -DNIFT_R1_MUTANT (the refusal scan disabled)
must fail this test (test-runtime2-module-host-order-mutant), and each
-DNIFT_HOST_ORDER_MUTANT=N build must fail exactly its named cases
(tests/runtime2/module_host_order_mutants.py, MUTANTS below).

The `classify` cases run NIFT_VM=classify-all, which masks earlier refusals
(here param-name-reserved), so the guard's own decision is visible on its own.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

M = 'x := 1\nexport(x)\n'
CASES = {
    # name: (files, expected outcome under NIFT_VM=1)
    "host-before-import-top": ({"m.nft": M, "main.f": 'print(exists("a.txt"))\nimport("m.nft")\nprint(x)\n'},
                               "module-host-op:exists"),
    "host-before-import-fn": ({"m.nft": M, "main.f": 'fn(probe()) {\n  return exists("a.txt")\n}\n'
                               'print(probe())\nimport("m.nft")\nprint(x)\n'}, "module-host-op:exists"),
    "host-before-import-loop": ({"m.nft": M, "main.f": 'i0 := 0\nwhile (i0 < 2) {\n  i0 += 1\n'
                                 '  print(exists("a.txt"))\n}\nimport("m.nft")\nprint(x)\n'},
                                "module-host-op:exists"),
    "copy-over-module": ({"m.nft": M, "m2.nft": 'x := 2\nexport(x)\n',
                          "main.f": 'cp("m2.nft", "m.nft")\nimport("m.nft")\nprint(x)\n'}, "module-host-op:cp"),
    "remove-module": ({"m.nft": M, "main.f": 'print("go")\nremove("m.nft")\nimport("m.nft")\nprint(x)\n'},
                      "module-host-op:remove"),
    "host-in-module-init": ({"m.nft": 'print(exists("a.txt"))\nk := 1\nexport(k)\n',
                             "n.nft": 'j := 2\nexport(j)\n',
                             "main.f": 'import("m.nft")\nimport("n.nft")\nprint(k + j)\n'},
                            "module-host-op:exists"),
    "host-in-nested-module-fn": ({"sub/a.nft": 'import("../c.nft")\nfn(af()) {\n  return cf()\n}\nexport(af)\n',
                                  "c.nft": 'fn(cf()) {\n  return is_file("a.txt")\n}\nexport(cf)\n',
                                  "main.f": 'import("sub/a.nft")\nprint(af())\n'},
                                 "vm"),
    "host-in-uncalled-fn": ({"m.nft": M, "main.f": 'import("m.nft")\nfn(never()) {\n  return pwd()\n}\nprint(x)\n'},
                            "vm"),
    "host-in-untaken-branch": ({"m.nft": M, "main.f": 'import("m.nft")\nif (false) {\n  print(pwd())\n}\nprint(x)\n'},
                               "vm"),
    "os-guarded-branch": ({"m.nft": M, "main.f": 'import("m.nft")\nif (os() == "windows") {\n  print(pwd())\n'
                           '} else {\n  print("other")\n}\nprint(x)\n'}, "vm"),
    "lambda-after-import": ({"m.nft": M, "main.f": 'import("m.nft")\nf := () => exists("a.txt")\nprint(f())\nprint(x)\n'},
                            "vm"),
    "module-exports-host-fn": ({"m.nft": 'fn(probe()) {\n  return exists("a.txt")\n}\nexport(probe)\n',
                                "main.f": 'import("m.nft")\nprint(probe())\n'}, "vm"),
    "module-init-print-pure": ({"m.nft": 'print("loading")\nk := "ab".to_upper()\nh := sha256("a")\nexport(k)\nexport(h)\n',
                                "main.f": 'import("m.nft")\nprint(exists("a.txt"))\nprint(k + h)\n'}, "vm"),
    "cwd-change-after-import": ({"m.nft": M, "sub/t.txt": "t", "main.f": 'import("m.nft")\ncd("sub")\n'
                                 'print(exists("t.txt"))\nprint(x)\n'}, "vm"),
    "nested-import-clean": ({"a.nft": 'import("c.nft")\nk := 1\nexport(k)\n', "c.nft": 'q := 2\nexport(q)\n',
                             "main.f": 'import("a.nft")\nprint(exists("a.txt"))\nprint(k)\n'}, "vm"),
    # Still refused: host effects before the last import (also in branches), in module initialisation
    # (transitively), and calls, callbacks or struct construction there.
    "host-before-import-untaken": ({"m.nft": M, "main.f": 'if (false) {\n  cd("..")\n}\nimport("m.nft")\nprint(x)\n'},
                                   "module-host-op:cd"),
    "host-before-import-taken": ({"m.nft": M, "main.f": 'if (true) {\n  setenv("NIFT_R1_X", "1")\n}\n'
                                  'import("m.nft")\nprint(x)\n'}, "module-host-op:setenv"),
    "host-before-empty-module": ({"e.nft": "// nothing\n", "main.f": 'print(exists("e.nft"))\nimport("e.nft")\nprint(1)\n'},
                                 "module-host-op:exists"),
    "declaration-before-import": ({"m.nft": M, "main.f": 'fn(g()) {\n  return pwd()\n}\nimport("m.nft")\nprint(x)\n'},
                                  "module-host-op:pwd"),
    "env-in-module-init": ({"m.nft": 'setenv("NIFT_R1_Y", "1")\nk := 1\nexport(k)\n',
                            "main.f": 'import("m.nft")\nprint(k)\n'}, "module-host-op:setenv"),
    "host-in-nested-module-init": ({"a.nft": 'import("c.nft")\nk := 1\nexport(k)\n',
                                    "c.nft": 'q := exists("a.txt")\nexport(q)\n',
                                    "main.f": 'import("a.nft")\nprint(k)\n'}, "module-host-op:exists"),
    "call-in-module-init": ({"m.nft": 'fn(f()) {\n  return 1\n}\nv := f()\nexport(v)\n',
                             "main.f": 'import("m.nft")\nprint(pwd() != "")\nprint(v)\n'}, "module-host-op:pwd"),
    "interpolation-call-in-init": ({"m.nft": 'fn(f()) {\n  return 1\n}\ns := "v=$[f()]"\nexport(s)\n',
                                    "main.f": 'import("m.nft")\nprint(pwd() != "")\nprint(s)\n'}, "module-host-op:pwd"),
    "callback-host-in-init": ({"m.nft": 'ys := [1].map((v) => exists("a.txt"))\nexport(ys)\n',
                               "main.f": 'import("m.nft")\nprint(ys.size())\n'}, "module-host-op:exists"),
    "comparator-in-init": ({"m.nft": 'zs := [3, 1, 2].sort_by((v) => v)\nexport(zs)\n',
                            "main.f": 'import("m.nft")\nprint(pwd() != "")\nprint(zs.size())\n'}, "module-host-op:pwd"),
    "struct-presentation-in-init": ({"m.nft": 'struct(S) {\n  v := 1\n  fn(to_string()) {\n    return "s"\n  }\n}\n'
                                     's := S()\nt := s.to_string()\nexport(t)\n',
                                     "main.f": 'import("m.nft")\nprint(pwd() != "")\nprint(t)\n'}, "module-host-op:pwd"),
    # Indirect user code (D-070 #4: effects, not names): a struct's own presentation method behind `.stringify()`,
    # reached in module initialisation, is a user call (struct construction and the method call are refused).
    "struct-stringify-method-in-init": ({"m.nft": 'struct(S) {\n  v := 1\n  fn(stringify()) {\n    return "custom"\n  }\n}\n'
                                         's := S()\nt := s.stringify()\nexport(t)\n',
                                         "main.f": 'import("m.nft")\nprint(pwd() != "")\nprint(t)\n'}, "module-host-op:pwd"),
    "diamond-import": ({"b.nft": 'import("d.nft")\nbk := 1\nexport(bk)\n', "c.nft": 'import("d.nft")\nck := 2\nexport(ck)\n',
                        "d.nft": 'dk := 3\nexport(dk)\n',
                        "main.f": 'import("b.nft")\nimport("c.nft")\nprint(pwd() != "")\nprint(bk + ck)\n'},
                       "import-reinitialised"),
    # Slot 2 (D-068 #2): admitted, legacy-equal (before slot 2 both were refused, module-host-op:is_file and
    # module-host-op:exists).
    "host-after-import": ({"m.nft": M, "main.f": 'import("m.nft")\nprint(exists("a.txt"))\nprint(x)\n'}, "vm"),
    # Controls: the same host op without an import, and an import without a
    # host op, still run on the VM.
    "host-no-import": ({"main.f": 'print(exists("a.txt"))\n'}, "vm"),
    "import-no-host": ({"m.nft": M, "main.f": 'print("a")\nimport("m.nft")\nprint(x)\n'}, "vm"),
}


def materialise(files):
    d = tempfile.mkdtemp(prefix="r1-")
    for rel, text in files.items():
        path = os.path.join(d, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)
    return d


def tree(d):
    out = {}
    for base, _, names in os.walk(d):
        for n in names:
            if n in ("trace.jsonl", "stats.jsonl"):
                continue
            p = os.path.join(base, n)
            out[os.path.relpath(p, d)] = open(p, "rb").read()
    return out


def run(nift, files, mode):
    d = materialise(files)
    try:
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "LANG": "C", "NIFT_VM": mode,
               "NIFT_TRACE_HOST": "3", "NIFT_VM_STATS": os.path.join(d, "stats.jsonl")}
        fd = os.open(os.path.join(d, "trace.jsonl"), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        try:
            p = subprocess.run(["timeout", "30", nift, "main.f"], cwd=d, env=env, capture_output=True,
                               preexec_fn=lambda: os.dup2(fd, 3), pass_fds=(fd,))
        finally:
            os.close(fd)
        trace = open(os.path.join(d, "trace.jsonl")).read().replace(d, "<root>")
        stats_path = os.path.join(d, "stats.jsonl")
        stats = json.loads(open(stats_path).read().splitlines()[-1]) if os.path.exists(stats_path) else {}
        return {"rc": p.returncode, "out": p.stdout, "err": p.stderr.replace(d.encode(), b"<root>"),
                "trace": trace, "tree": tree(d), "stats": stats}
    finally:
        shutil.rmtree(d, ignore_errors=True)


# classify-all cases (D-069 C2, independent of param-name-reserved): name -> (files, reason that must be present
# or absent among the non-consequential classify-all reasons).
CLASSIFY = {
    "shadowed-builtin-in-init": ({"m.nft": 'h := sha256("a")\nexport(h)\n',
                                  "main.f": 'import("m.nft")\nfn(g(sha256)) {\n  return 1\n}\nprint(pwd() != "")\nprint(h)\n'},
                                 "module-host-op:pwd", True),
    "unshadowed-builtin-in-init": ({"m.nft": 'h := sha256("a")\nexport(h)\n',
                                    "main.f": 'import("m.nft")\nprint(pwd() != "")\nprint(h)\n'},
                                   "module-host-op:pwd", False),
}
# The NIFT_HOST_ORDER_MUTANT builds (src/rt/module_host_order.cpp) and the cases each must fail.
MUTANTS = {
    1: ["host-before-empty-module", "declaration-before-import"],  # no leading-import-block check (E)
    2: ["env-in-module-init", "host-in-nested-module-init", "callback-host-in-init"],  # no module-init check (M)
    3: ["call-in-module-init", "interpolation-call-in-init", "struct-presentation-in-init",
        "struct-stringify-method-in-init"],  # calls and struct construction safe
    4: ["comparator-in-init"],  # callbacks count as safe
    5: ["shadowed-builtin-in-init"],  # no shadowing check for the 11 binding-wins built-ins
}


def classify(nift, files):
    d = materialise(files)
    try:
        st = os.path.join(d, "st.jsonl")
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "LANG": "C", "NIFT_VM": "classify-all",
               "NIFT_VM_STATS": st}
        p = subprocess.run(["timeout", "30", nift, "main.f"], cwd=d, env=env, capture_output=True)
        reasons = set()
        if os.path.exists(st):
            for line in open(st):
                reasons |= {r["reason"] for r in json.loads(line).get("reasons") or [] if not r.get("consequential")}
        return p.returncode, reasons
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    args.nift = os.path.abspath(args.nift)
    failures = []
    for name, (files, want) in CASES.items():
        legacy = run(args.nift, files, "0")
        auto = run(args.nift, files, "1")
        st = auto["stats"]
        got = "vm" if st.get("outcome") in ("vm", "vm-error") else st.get("reason", st.get("outcome"))
        problems = []
        if got != want:
            problems.append(f"outcome {got!r}, expected {want!r}")
        for key in ("rc", "out", "err", "trace", "tree"):
            if legacy[key] != auto[key]:
                problems.append(f"{key} differs: legacy {legacy[key]!r} vm {auto[key]!r}")
        if want != "vm":
            req = run(args.nift, files, "require")
            if req["rc"] == 0 or req["trace"] or req["tree"] != {k: v.encode() for k, v in files.items()}:
                problems.append(f"NIFT_VM=require not refused before effects: rc {req['rc']} trace {req['trace']!r}")
        print(("PASS " if not problems else "FAIL ") + name + (f" ({got})" if not problems else ""))
        for p in problems:
            print("   ", p)
        if problems:
            failures.append(name)
    for name, (files, reason, present) in CLASSIFY.items():
        rc, reasons = classify(args.nift, files)
        ok = rc == 0 and (reason in reasons) == present
        print(("PASS " if ok else "FAIL ") + name + (f" ({reason} {'listed' if present else 'absent'})" if ok else ""))
        if not ok:
            print("    ", f"classify-all exit {rc}, reasons {sorted(reasons)}, expected {reason} "
                  f"{'listed' if present else 'absent'}")
            failures.append(name)
    total = len(CASES) + len(CLASSIFY)
    print(f"module-host-order: {total - len(failures)}/{total} pass")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
