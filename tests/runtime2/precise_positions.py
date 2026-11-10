#!/usr/bin/env python3
"""Precise VM positions of the five mixed rows (owner 2026-11-11 05:41, decisions.md D-068 #7; D-067 #1).

For their certain-type cases, the VM's refusal points at the actual offending construct, where legacy points at
the enclosing statement. The owner approved these positions under the common diagnostic decision (D-067 #1); the
rows stay PENDING-OWNER because of their union gap (D-068 #6). This test pins, for each row, the VM position and
category (admission refusal before execution, exit 1, nothing printed) and legacy's position and category (a
run-time error, exit 1). The runtime is not changed by this test.

usage: precise_positions.py --nift NIFT
"""
import argparse
import os
import subprocess
import sys
import tempfile

ERROR_MODULE = "fn(mk_error()) { return x => x.missing }\nexport(mk_error)\n"

CASES = [
    # (row, files, VM "<file>:<line>:<col>", legacy "<file>:<line>:<col>" or None, legacy message fragment)
    ("coro-future-flow:ForBinding",
     {"main.f": "fn[async](af(x)) { return x }\nfu := af(1)\nfor (e : fu) { print(e) }\n"},
     "main.f:3:10", "main.f:3:1", "@for can only iterate over JSON arrays or objects"),
    ("coro-future-flow:Throw",
     {"main.f": "fn[async](af(x)) { return x }\nfu := af(1)\nthrow fu\n"},
     "main.f:3:7", "main.f:3:1", "throw expression must evaluate to Error"),
    ("interpolation:non-scalar",
     {"main.f": 'fn(f(x)) { return x }\nv := f([1])\nprint("v=$[v]")\n'},
     "main.f:3:12", "main.f:3:1", "print: parameter expression must resolve to a scalar value"),
    ("type:member",
     {"main.f": 'import("./error-module.f")\nf := mk_error(); print([1].sort_by(f).stringify())\n',
      "error-module.f": ERROR_MODULE},
     "error-module.f:1:30", "main.f:2:24", "value has no member: missing"),
    ("type:script-return", {"main.f": "return [1]\n"},
     "main.f:1:1", None, "script return value is not directly renderable"),
]


def run(nift, d, legacy):
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env["HOME"] = d
    if legacy:
        env["NIFT_VM"] = "0"
    p = subprocess.run([nift, "main.f"], cwd=d, env=env, capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout, p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    nift = os.path.abspath(ap.parse_args().nift)
    failures = []
    for row, files, vm_pos, legacy_pos, legacy_msg in CASES:
        with tempfile.TemporaryDirectory(prefix="precise-pos-") as d:
            for name, text in files.items():
                with open(os.path.join(d, name), "w") as f:
                    f.write(text)
            rc, out, err = run(nift, d, False)
            first = err.split("\n")[0]
            want = "%s: not compiled for the VM (%s)" % (vm_pos, row)
            if rc != 1 or out != "" or want not in first or "the program was not executed" not in err:
                failures.append("%s: VM exit %d stdout %r first line %r (want %r, admission refusal, exit 1)" %
                                (row, rc, out, first, want))
            rc, out, err = run(nift, d, True)
            first = err.split("\n")[0]
            pos_ok = (legacy_pos is None and ".f:" not in first) or (legacy_pos is not None and
                                                                      (legacy_pos + ": ") in first)
            if rc != 1 or legacy_msg not in first or not pos_ok:
                failures.append("%s: legacy exit %d first line %r (want %s, %r, run-time error exit 1)" %
                                (row, rc, first, legacy_pos or "no position", legacy_msg))
    for f in failures:
        print("FAIL precise positions: " + f)
    print("%s precise positions: %d mixed row(s), VM position and category pinned against legacy" %
          ("FAIL" if failures else "PASS", len(CASES)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
