#!/usr/bin/env python3
"""Named deferrals (owner 2026-11-10 13:26, item 7; decisions.md D-066; known-limitations.md).

Each deferred construct must be refused clearly and deterministically under the VM default
(NIFT_VM unset): exit 1, no stdout, `not compiled for the VM (<reason>)` and the "no part of it
ran" note, a statistics row with no VM and no legacy execution (no silent fallback), and the same
result on a second run. The legacy channel (NIFT_VM=0) must still run the construct: the feature
is deferred on the VM, not removed.

usage: named_deferrals.py --nift NIFT
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

MODULE = "fn(add(a, b)) { return a + b }\nexport(add)\n"
# name -> (files, reason, legacy stdout, needs a project)
CASES = {
    "delayed import inside a function": (
        {"m.f": MODULE, "main.f": 'print("before")\nfn(f()) { import("./m.f"); return add(2, 3) }\nprint(f())\n'},
        "import-nested", "before\n5\n", False),
    "run()": ({"main.f": 'print("before")\nr := run("echo hi")\nprint("done")\n'}, "callable:run", "before\ndone\n", False),
    "cmd()": ({"main.f": 'print("before")\nc := cmd("echo", "hi")\nc.run()\nprint("done")\n'}, "callable:cmd",
              "before\ndone\n", False),
    "VM-native FFI (ffi_open)": ({"main.f": 'print("before")\nlib := ffi_open("libc.so.6")\nprint(type(lib))\n'},
                                 "callable:ffi_open", "before\nstring\n", False),
    "VM-native timers (timer)": ({"main.f": 'print("before")\nt := timer()\nt.start()\nprint(t.elapsed() >= 0)\n'},
                                 "callable:timer", "before\ntrue\n", False),
    "project_root() (D-053)": ({"main.f": 'print("before")\nprint(project_root() != "")\n'}, "callable:project_root",
                               "before\ntrue\n", True),
}


def run(nift, cwd, mode):
    stats = os.path.join(cwd, "stats.jsonl")
    if os.path.exists(stats):
        os.remove(stats)
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(HOME=cwd, NIFT_VM_STATS=stats)
    if mode is not None:
        env["NIFT_VM"] = mode
    p = subprocess.run([nift, "main.f"], cwd=cwd, env=env, capture_output=True, text=True, timeout=60)
    rows = [json.loads(x) for x in open(stats).read().splitlines() if x.strip()] if os.path.exists(stats) else []
    return p.returncode, p.stdout, p.stderr.replace(cwd, "$ROOT"), (rows[-1] if rows else {})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    nift = os.path.abspath(ap.parse_args().nift)
    failures = 0
    for name, (files, reason, legacy_out, project) in CASES.items():
        with tempfile.TemporaryDirectory(prefix="named-deferral-") as d:
            d = os.path.realpath(d)
            if project:
                subprocess.run([nift, "init"], cwd=d, capture_output=True, check=True,
                               env={"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "NIFT_VM": "0"})
            for rel, text in files.items():
                with open(os.path.join(d, rel), "w") as f:
                    f.write(text)
            first, second = run(nift, d, None), run(nift, d, None)
            rc, out, err, row = first
            problems = []
            if (rc, out) != (1, "") or "not compiled for the VM (%s)" % reason not in err \
                    or "no part of it ran" not in err:
                problems.append("not refused cleanly: exit %d stdout %r stderr %r" % (rc, out, err[:200]))
            if (row.get("reason"), row.get("vm_executions"), row.get("legacy_executions")) != (reason, 0, 0):
                problems.append("statistics %r" % {k: row.get(k) for k in ("outcome", "reason", "vm_executions",
                                                                         "legacy_executions")})
            if first[:3] != second[:3]:
                problems.append("not deterministic: %r then %r" % (first[:3], second[:3]))
            lrc, lout, lerr, _ = run(nift, d, "0")
            if (lrc, lout) != (0, legacy_out):
                problems.append("legacy channel: exit %d stdout %r stderr %r" % (lrc, lout, lerr[:200]))
            print("%s %s: VM default refuses %s before execution; legacy runs it%s" % (
                "FAIL" if problems else "PASS", name, reason, "" if not problems else ": " + "; ".join(problems)))
            failures += bool(problems)
    print("%s named deferrals: %d construct(s), %d failure(s)" % ("PASS" if not failures else "FAIL", len(CASES),
                                                                  failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
