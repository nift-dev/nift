#!/usr/bin/env python3
"""CP17 W13 script-level handle ownership checks.

Differential where legacy's observable result is defined (nested frames,
abandoned frames), VM-only where CP17's D-039 close/closed semantics or the
runtime.handle_closed error are the contract (legacy handles are strings).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

CASES = [
    ("nested_local_close",
     'fn(inner()) {\n  f := file("p")\n  f.open("r")\n  line := f.read_line()\n  f.close()\n  return line\n}\n'
     'print(inner())\n', "vm"),
    ("nested_frames",
     'fn(in_open()) {\n  f := file("p")\n  f.open("r")\n  line := f.read_line()\n  f.close()\n  return line\n}\n'
     'fn(in_outer()) { return in_open() }\nprint(in_outer())\n', "vm"),
    ("abandoned_frame",
     'fn(inner()) {\n  f := file("p")\n  f.open("r")\n  return f.read_all()\n}\nprint(inner())\n',
     "managed-error"),
    ("alias_close", 'f := file("p")\nf.open("r")\ng := f\nf.close()\ng.close()\nprint("closed")\n', "vm-only"),
    ("stale_after_close",
     'f := file("p")\nf.open("r")\nprint(f.read_all())\nf.close()\nprint(f.read_all())\n',
     "vm-error-only"),
]


def run(nift, root, source, vm):
    (root / "case.f").write_text(source)
    env = dict(os.environ)
    env["NIFT_VM"] = "require" if vm else "0"
    env.pop("NIFT_VM_STATS", None)
    stats = root / "stats.jsonl"
    if vm:
        env["NIFT_VM_STATS"] = str(stats)
    proc = subprocess.run([nift, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                          capture_output=True, timeout=60)
    record = None
    if stats.exists():
        lines = [json.loads(line) for line in stats.read_text().splitlines() if line.strip()]
        record = lines[-1] if lines else None
    return (proc.stdout, proc.stderr, proc.returncode), record


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = 0
    for name, source, mode in CASES:
        with tempfile.TemporaryDirectory(prefix="cp17-lifecycle-") as tmp:
            root = Path(tmp)
            (root / "p").write_text("one\ntwo\n")
            legacy, _ = run(nift, root, source, False)
            vm, record = run(nift, root, source, True)
            problems = []
            if record is None:
                problems.append("no statistics")
            elif mode == "vm":
                if record.get("outcome") != "vm":
                    problems.append(f"expected vm, got {record.get('outcome')}/{record.get('reason')}")
                elif vm != legacy:
                    problems.append(f"legacy {legacy!r} != vm {vm!r}")
            elif mode == "vm-only":
                if record.get("outcome") != "vm" or vm[2] != 0:
                    problems.append(f"expected successful vm, got {record.get('outcome')}/{record.get('reason')}")
            elif mode == "vm-error-only":
                if record.get("outcome") != "vm-error":
                    problems.append(f"expected vm-error, got {record.get('outcome')}/{record.get('reason')}")
                elif "runtime.handle_closed" not in (record.get("reason") or ""):
                    problems.append(f"expected runtime.handle_closed, got {record.get('reason')!r}")
                elif b"handle is closed" not in vm[1]:
                    problems.append(f"missing closed-handle wording: {vm[1]!r}")
            elif mode == "managed-error":
                if record.get("outcome") != "vm-error" or b"managed file left open" not in vm[1]:
                    problems.append(f"expected managed-file error, got {record.get('outcome')}/{record.get('reason')}")
            if problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name} ({mode})")
    print(f"{'PASS' if not failures else 'FAIL'} cp17 script handle lifecycle: {len(CASES)} cases, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
