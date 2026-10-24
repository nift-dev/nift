#!/usr/bin/env python3
"""CP12c slice 0: the refusal inventory follows compiler linking.

  vm_inventory_links.py --nift ./nift [--mutant]

For every case, run `NIFT_VM=classify` (the compiler's actual first refusal)
and `NIFT_VM=classify-all` (the inventory).  A linked unit's item 0 must equal
classify's reason at the same module and source position; later blockers in
the entry or an imported module must still be present; a link refusal stops
the inventory and must say so (`inventory_complete:false`, named stop).

`--mutant` runs the test-only build that deliberately inventories unlinked
source (`-DNIFT_INVENTORY_MUTANT=1`).  It must be detected by these checks;
a missing statistics line or a binary failure is a harness failure, not a
detection.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

CASES = [
    {
        "name": "import-package",
        "files": {"main.f": 'import("nopkg")\nprint(1)\n'},
        "first": "import-package",
        "certain": {"import-package"},
        "complete": False,
        "stop": "import-package",
        "item0_line": 1,
        "item0_column": 1,
    },
    {
        "name": "import-nested",
        "files": {"main.f": 'if (true) {\n  import("./m.nft")\n}\nprint(1)\n', "m.nft": "x := 1\n"},
        "first": "import-nested",
        "certain": {"import-nested"},
        "complete": False,
        "stop": "import-nested",
        "item0_line": 2,
        "item0_column": 3,
    },
    {
        "name": "import-missing",
        "files": {"main.f": 'import("./nope.nft")\nprint(1)\n'},
        "first": "import-missing",
        "certain": {"import-missing"},
        "complete": False,
        "stop": "import-missing",
        "item0_line": 1,
        "item0_column": 1,
    },
    {
        "name": "import-dynamic",
        "files": {"main.f": 'p := "./m.nft"\nimport(p)\nprint(1)\n', "m.nft": "x := 1\n"},
        "first": "import-dynamic",
        "certain": {"import-dynamic"},
        "complete": False,
        "stop": "import-dynamic",
        "item0_line": 2,
        "item0_column": 1,
    },
    {
        "name": "import-collision",
        "files": {
            "main.f": 'mf := 1\nimport("m.nft")\n',
            "m.nft": 'print("m runs")\nmv := [1]\nfn(mf(x)) { return x + 1 }\nexport(mv)\nexport(mf)\n',
        },
        "first": "import-collision",
        "certain": {"import-collision"},
        "complete": False,
        "stop": "import-collision",
        "item0_line": 2,
        "item0_column": 1,
    },
    {
        "name": "module-return",
        "files": {"main.f": 'import("m.nft")\nprint("b")\n', "m.nft": 'print("a")\nreturn\n'},
        "first": "module-return",
        "certain": {"module-return"},
        "complete": False,
        "stop": "module-return",
        "item0_path": "m.nft",
        "item0_line": 2,
        "item0_column": 1,
    },
    {
        "name": "export-struct",
        "files": {
            "main.f": 'import("s.nft")\np := P()\nprint(p.x)\n',
            "s.nft": "struct (P) { x := 3 }\nexport(P)\n",
        },
        "first": "export-struct",
        "certain": {"export-struct"},
        "complete": False,
        "stop": "export-struct",
        "item0_path": "s.nft",
        "item0_line": 2,
        "item0_column": 1,
    },
    {
        "name": "module-callable",
        "files": {
            "main.f": 'import("m.nft")\nprint(mf())\n',
            "m.nft": 'fn(mf()) {\n  return getenv("X")\n}\nexport(mf)\n',
        },
        # CP16 compiles getenv as a host native; R1 then refuses the linked
        # unit (rt/module_host_op.h). The case still checks that item 0 is the
        # module's call, with module provenance, not the module function name.
        "first": "module-host-op:getenv",
        "certain": {"module-host-op:getenv"},
        "complete": True,
        "item0_path": "m.nft",
        "item0_line": 2,
        "item0_column": 10,
        "module_paths": {"m.nft"},
    },
    {
        "name": "call-binding-later-compiled",
        "files": {
            "main.f": 'import("m.nft")\nf := 1\nprint(f())\ng := () => 2\nprint(g())\n',
            "m.nft": "fn(mf()) {\n  return 1\n}\nexport(mf)\n",
        },
        "compiled": True,
    },
    {
        "name": "fn-flags-module",
        "files": {
            "main.f": 'import("m.nft")\nprint(af())\n',
            "m.nft": "fn[async](af()) { return 1 }\nexport(af)\n",
        },
        "first": "fn-flags",
        "certain": {"fn-flags"},
        "later": ["callable:af"],
        "complete": True,
        "item0_path": "m.nft",
        "item0_line": 1,
        "item0_column": 1,
        "module_paths": {"m.nft"},
    },
]


def run(nift, directory, entry, mode):
    stats = os.path.join(directory, "stats.jsonl")
    if os.path.exists(stats):
        os.remove(stats)
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": directory,
        "LANG": "C",
        "NIFT_PREVALIDATE": "off",
        "NIFT_VM": mode,
        "NIFT_VM_STATS": stats,
    }
    proc = subprocess.run(["timeout", "60", nift, entry], cwd=directory, env=env, capture_output=True, text=True)
    try:
        return proc, json.loads(open(stats).read().splitlines()[-1])
    except (OSError, IndexError, ValueError):
        return proc, None


def write_case(directory, files):
    for rel, text in files.items():
        path = os.path.join(directory, rel)
        os.makedirs(os.path.dirname(path) or directory, exist_ok=True)
        with open(path, "w") as f:
            f.write(text)


def check_case(nift, work, case):
    directory = tempfile.mkdtemp(dir=work)
    write_case(directory, case["files"])
    classify_proc, classify = run(nift, directory, "main.f", "classify")
    every_proc, every = run(nift, directory, "main.f", "classify-all")
    problems = []
    if classify is None or every is None:
        return [f"no statistics (classify {classify_proc.returncode}, every {every_proc.returncode})"]
    if case.get("compiled"):
        if classify.get("outcome") != "compiled":
            return [f"expected compiled, got {classify.get('outcome')}/{classify.get('reason')}"]
        if every.get("outcome") != "compiled" or every.get("reasons"):
            return [f"classify-all expected compiled with no blockers, got {every.get('outcome')}/{every.get('reasons')}"]
        return []
    if classify.get("outcome") != "fallback":
        problems.append(f"classify unexpectedly {classify.get('outcome')}/{classify.get('reason')}")
    if every_proc.returncode != 0 or every_proc.stdout:
        problems.append(f"classify-all executed or failed (exit {every_proc.returncode}, stdout {every_proc.stdout[:80]!r})")
    if (every.get("outcome"), every.get("reason")) != (classify.get("outcome"), classify.get("reason")):
        problems.append(f"classify-all {every.get('outcome')}/{every.get('reason')} != classify "
                        f"{classify.get('outcome')}/{classify.get('reason')}")
    reasons = every.get("reasons") or []
    if not reasons:
        problems.append("no inventory items")
        return problems
    if reasons[0].get("reason") != classify.get("reason"):
        problems.append(f"item 0 {reasons[0].get('reason')} != classify {classify.get('reason')}")
    if reasons[0].get("line") != classify.get("line") or reasons[0].get("column") != classify.get("column"):
        problems.append(f"item 0 position {reasons[0].get('line')}:{reasons[0].get('column')} != classify "
                        f"{classify.get('line')}:{classify.get('column')}")
    want_path = case.get("item0_path")
    if want_path:
        expected = os.path.realpath(os.path.join(directory, want_path))
        if os.path.realpath(reasons[0].get("path") or "") != expected:
            problems.append(f"item 0 path {reasons[0].get('path')!r} != {expected!r}")
    if reasons[0].get("line") != case["item0_line"] or reasons[0].get("column") != case["item0_column"]:
        problems.append(f"item 0 expected {case['item0_line']}:{case['item0_column']}, got "
                        f"{reasons[0].get('line')}:{reasons[0].get('column')}")
    got_certain = {r.get("reason") for r in reasons if not r.get("consequential")}
    if got_certain != case["certain"]:
        problems.append(f"certain reasons {sorted(got_certain)} != {sorted(case['certain'])}")
    if bool(every.get("inventory_complete")) != case["complete"]:
        problems.append(f"inventory_complete {every.get('inventory_complete')} != {case['complete']}")
    if "stop" in case and every.get("inventory_stop") != case["stop"]:
        problems.append(f"inventory_stop {every.get('inventory_stop')!r} != {case['stop']!r}")
    got_later = [r.get("reason") for r in reasons[1:]]
    for want in case.get("later", []):
        if want not in got_later:
            problems.append(f"later blocker {want!r} missing from {got_later}")
    paths = {os.path.realpath(r["path"]) for r in reasons if r.get("path")}
    for want in case.get("module_paths", set()):
        expected = os.path.realpath(os.path.join(directory, want))
        if expected not in paths:
            problems.append(f"module path {want!r} missing from items")
    for item in reasons:
        if item.get("path") and (not item.get("line") or not item.get("column")):
            problems.append(f"module item without position: {item.get('reason')}")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--mutant", action="store_true")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = 0
    detected = []
    with tempfile.TemporaryDirectory(prefix="vm-inventory-links-") as work:
        for case in CASES:
            problems = check_case(nift, work, case)
            if args.mutant:
                # The unlinked mutant must contradict the compiler on import
                # cases. A case that still passes is not a detection; a case
                # that fails only because the binary did not run is a harness
                # failure, reported separately by check_case's no-statistics
                # problem.
                if problems and not any("no statistics" in p for p in problems):
                    detected.append(case["name"])
                elif not problems:
                    print(f"MISS {case['name']}: mutant still agrees with classify")
                else:
                    failures += 1
                    print(f"FAIL {case['name']}: harness: " + "; ".join(problems))
                continue
            if problems:
                failures += 1
                print(f"FAIL {case['name']}: " + "; ".join(problems))
            else:
                print(f"ok   {case['name']}")
    if args.mutant:
        want = {case["name"] for case in CASES}
        missing = sorted(want - set(detected))
        if missing or failures:
            print(f"FAIL inventory mutant: missing detection in {missing}; {failures} harness failure(s)")
            return 1
        print(f"PASS inventory mutant: detected in {len(detected)}/{len(want)} cases")
        return 0
    print(f"{'PASS' if not failures else 'FAIL'} vm inventory links: {len(CASES)} cases, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
