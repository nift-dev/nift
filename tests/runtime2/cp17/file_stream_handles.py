#!/usr/bin/env python3
"""CP17 W13 file/stream handle differential fixtures.

Runs after the isolated W13 compiler commit (not on the pre-CP12C base, where
file()/ifstream()/ofstream() are still refused). Every admitted case must run
on the VM with NIFT_VM=require and match legacy; every forbidden handle flow
must refuse the whole unit before execution with `host-handle-flow`, leaving a
side-effect marker absent.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

FILES = {"p": "one\ntwo\n", "a": "A\n", "b": "B\n"}

CASES = [
    ("local_file", 'f := file("p"); f.open("r"); x := f.read_all(); f.close(); print(x)\n', "vm"),
    ("file_write_save", 'f := file("w"); f.open("w"); f.write("x"); f.save(); f.close(); print("ok")\n', "vm"),
    ("file_read_line", 'f := file("p"); f.open("r"); print(f.read_line()); f.close()\n', "vm"),
    ("file_local_reassign", 'f := file("a"); f = file("b"); f.open("r"); print(f.read_all()); f.close()\n', "vm"),
    ("ifstream", 's := ifstream("p"); print(s.read_line()); close(s)\n', "vm"),
    ("ofstream", 'o := ofstream("o"); o.write("y"); close(o); print(open("o"))\n', "vm"),
    ("close_alias", 'f := file("p"); f.open("r"); g := f; g.close(); print("closed")\n', "vm"),
    ("array_store", 'a := [file("p")]\n', "refuse"),
    ("object_store", 'o := {"f": file("p")}\n', "refuse"),
    ("call_arg", 'fn(use(h)) { return 1 }\nuse(file("p"))\n', "refuse"),
    ("call_return", 'fn(make()) { return file("p") }\n', "refuse"),
    ("lambda_capture", 'f := file("p"); g := () => f\n', "refuse"),
    ("lambda_return", 'g := () => file("p")\n', "refuse"),
    ("comparison", 'print(file("p") == file("p"))\n', "refuse"),
    ("print", 'print(file("p"))\n', "refuse"),
    ("stringify", 'print(file("p").stringify())\n', "refuse"),
    ("type", 'print(type(file("p")))\n', "refuse"),
    ("interpolation", 'print("$[file(\'p\')]")\n', "refuse"),
    ("native_arg", 'print(len(file("p")))\n', "refuse"),
    ("merge", 'x := 0\nif (true) { x = file("p") }\nprint(x)\n', "refuse"),
    ("side_effect_before_refusal", 'touch("marker"); a := [file("p")]\n', "refuse"),
]


def run(nift, root, source, mode):
    (root / "case.f").write_text(source)
    env = dict(os.environ)
    env["NIFT_VM"] = mode
    env.pop("NIFT_VM_STATS", None)
    stats = root / "stats.jsonl"
    if mode != "0":
        env["NIFT_VM_STATS"] = str(stats)
    proc = subprocess.run([nift, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                          capture_output=True, timeout=60)
    record = None
    if stats.exists():
        rows = [json.loads(line) for line in stats.read_text().splitlines() if line.strip()]
        record = rows[-1] if rows else None
    return (proc.stdout, proc.stderr, proc.returncode), record


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = 0
    for name, source, mode in CASES:
        with tempfile.TemporaryDirectory(prefix="cp17-handles-") as tmp:
            root = tempfile.mkdtemp(dir=tmp)
            from pathlib import Path
            root = Path(root)
            for path, text in FILES.items():
                (root / path).write_text(text)
            legacy, _ = run(nift, root, source, "0")
            # Side-effect marker before a refusal must not appear (the legacy
            # oracle above may legitimately have created it).
            marker = root / "marker"
            if marker.exists():
                marker.unlink()
            vm, record = run(nift, root, source, "require" if mode == "vm" else "classify")
            problems = []
            if record is None:
                problems.append("no statistics")
            elif mode == "refuse":
                if record.get("outcome") != "fallback" or record.get("reason") != "host-handle-flow":
                    problems.append(f"expected host-handle-flow refusal, got {record.get('outcome')}/{record.get('reason')}")
                if record.get("vm_executions", 0) != 0:
                    problems.append("refused unit still executed on the VM")
                if marker.exists():
                    problems.append("side effect ran before the refusal")
            else:
                if record.get("outcome") != "vm":
                    problems.append(f"expected vm, got {record.get('outcome')}/{record.get('reason')}")
                if vm != legacy:
                    problems.append(f"legacy {legacy!r} != vm {vm!r}")
            if problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name} ({mode})")
    print(f"{'PASS' if not failures else 'FAIL'} cp17 file/stream handles: {len(CASES)} cases, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
