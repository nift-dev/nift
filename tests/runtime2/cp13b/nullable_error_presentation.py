#!/usr/bin/env python3
"""CP13B R1-1: nullable Error.cause presentation on the VM and legacy.

  nullable_error_presentation.py --nift ./nift --switch-nift ./.build/nift-switch

For stringify(), prettify() and highlight(), compare an absent cause (null)
and a present cause against the legacy engine in NIFT_VM=require mode.
"""
import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path

CASES = {
    "absent": 'e := error("made")\nprint(e.cause.%s())\n',
    "present": 'c := error("inner")\ne := error("outer", "user.app.failed", c)\nprint(e.cause.%s())\n',
}
METHODS = ("stringify", "prettify", "highlight")


def run(nift, source, mode):
    with tempfile.TemporaryDirectory(prefix="cp13b-null-") as folder:
        root = Path(folder)
        path = root / "case.f"
        path.write_text(source)
        stats = root / ".stats.jsonl"
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": folder, "TMPDIR": folder, "LANG": "C", "LC_ALL": "C",
            "TZ": "UTC", "NIFT_PREVALIDATE": "off", "NIFT_VM": mode,
        }
        if mode != "0":
            env["NIFT_VM_STATS"] = str(stats)
        proc = subprocess.run([nift, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                              capture_output=True, timeout=60)
        st = None
        if stats.exists() and stats.read_text().strip():
            st = json.loads(stats.read_text().splitlines()[-1])
        return proc.stdout.decode(), proc.stderr.decode(), proc.returncode, st


def check(nift, label):
    failures = 0
    for method in METHODS:
        for kind, template in CASES.items():
            source = template % method
            legacy = run(nift, source, "0")
            vm = run(nift, source, "require")
            ok = vm[:3] == legacy[:3]
            ok = ok and vm[3] is not None and vm[3].get("outcome") == "vm"
            ok = ok and vm[3].get("vm_executions") == 1
            ok = ok and vm[0] == ("null\n" if kind == "absent" else legacy[0])
            print(f"{'ok  ' if ok else 'FAIL'} {label} {method} {kind}: legacy={legacy[:3]!r} vm={vm[:3]!r}")
            failures += 0 if ok else 1
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True)
    parser.add_argument("--switch-nift", required=True)
    args = parser.parse_args()
    failures = check(str(Path(args.nift).resolve()), "default")
    failures += check(str(Path(args.switch_nift).resolve()), "switch")
    print(f"{'PASS' if failures == 0 else 'FAIL'} nullable Error presentation: "
          f"{len(METHODS) * len(CASES) * 2} cases, {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
