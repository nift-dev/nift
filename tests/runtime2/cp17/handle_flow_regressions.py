#!/usr/bin/env python3
"""CP17 R1: W13 result-kind and captured-handle regressions.

Each engine gets fresh filesystem state. Refused units must refuse before any
marker/copy/move side effect with NIFT_VM=require; admitted local results must
actually execute on the VM.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REFUSE = [
    ("copy_type", 'touch("marker"); f := file("p"); print(type(f.copy("q")))'),
    ("move_type", 'touch("marker"); f := file("p"); print(type(f.move("q")))'),
    ("copy_aggregate", 'touch("marker"); f := file("p"); a := [f.copy("q")]; print(a.stringify())'),
    ("copy_return", 'touch("marker"); fn(make()) { f := file("p"); return f.copy("q") }; print(type(make()))'),
    ("copy_native_arg", 'touch("marker"); f := file("p"); print(len(f.copy("q")))'),
    ("copy_presentation", 'touch("marker"); f := file("p"); print(f.copy("q").stringify())'),
    ("capture_method", 'touch("marker"); f := file("p"); g := () => f.path(); print(g())'),
    ("capture_close", 'touch("marker"); f := file("p"); f.open("r"); g := () => close(f); g(); print("done")'),
    ("escaping_capture",
     'touch("marker"); fn(outer()) { f := file("p"); g := () => f.path(); return g }; g := outer(); print(g())'),
    ("capture_top_level",
     'touch("marker"); fn(use()) { return f.path() }; f := file("p"); print(use())'),
    ("capture_alias", 'touch("marker"); f := file("p"); g := () => { h := f; return h.path() }; print(g())'),
    ("nested_capture",
     'touch("marker"); f := file("p"); g := () => { h := () => f.path(); return h() }; print(g())'),
]

ALLOW = [
    ("copy_local", 'f := file("p"); g := f.copy("q"); print(g.path() != "")', "q"),
    ("copy_alias", 'f := file("p"); g := f.copy("q"); h := g; print(h.path() != "")', "q"),
    ("copy_receiver", 'f := file("p"); print(f.copy("q").path() != "")', "q"),
    ("move_local", 'f := file("p"); g := f.move("q"); print(g.path() != "")', "q"),
    ("ordinary_closure", 'x := 3; f := () => x + 1; print(f() == 4)', None),
]


def run(nift, root, source):
    (root / "case.f").write_text(source + "\n")
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_VM")}
    env["NIFT_VM"] = "require"
    env["NIFT_VM_STATS"] = str(root / "stats.jsonl")
    env["NIFT_PREVALIDATE"] = "off"
    proc = subprocess.run([nift, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                          capture_output=True, timeout=60)
    rows = [json.loads(line) for line in (root / "stats.jsonl").read_text().splitlines()
            if line.strip()] if (root / "stats.jsonl").exists() else []
    return proc, (rows[-1] if rows else None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = 0
    for name, source in REFUSE:
        with tempfile.TemporaryDirectory(prefix="cp17-flow-") as tmp:
            root = Path(tmp)
            (root / "p").write_text("abc\n")
            proc, record = run(nift, root, source)
            problems = []
            if record is None:
                problems.append("no statistics")
            else:
                if record.get("outcome") != "require-error" or record.get("reason") != "host-handle-flow":
                    problems.append(f"expected host-handle-flow require-error, got "
                                    f"{record.get('outcome')}/{record.get('reason')}")
                if record.get("vm_executions", 0) != 0:
                    problems.append("forbidden unit executed")
            for side in ("marker", "q"):
                if (root / side).exists():
                    problems.append(f"side effect {side} exists")
            if problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name} (refused before effects)")
    for name, source, product in ALLOW:
        with tempfile.TemporaryDirectory(prefix="cp17-flow-") as tmp:
            root = Path(tmp)
            (root / "p").write_text("abc\n")
            proc, record = run(nift, root, source)
            problems = []
            if record is None:
                problems.append("no statistics")
            elif record.get("outcome") != "vm" or proc.returncode != 0:
                problems.append(f"expected vm success, got {record.get('outcome')}/{record.get('reason')}")
            if proc.stdout != b"true\n":
                problems.append(f"unexpected stdout {proc.stdout!r}")
            if product and not (root / product).exists():
                problems.append(f"missing {product}")
            if problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name} (vm)")
    print(f"{'PASS' if not failures else 'FAIL'} cp17 handle-flow regressions: "
          f"{len(REFUSE) + len(ALLOW)} cases, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
