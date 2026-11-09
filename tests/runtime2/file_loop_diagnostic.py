#!/usr/bin/env python3
"""Owner 2026-11-10 (docs/runtime-2/compatibility.md, "loop write diagnostics"; combined-b3-b5.md
section 55): when write or write_line fails inside a while/for body, legacy's loop executor drops
the method's message and reports "script failed"; the VM reports the method's own error at the
same position. The approval covers only that message text. Pinned here against legacy, for each
program: the same exit status, the same stdout (everything printed before the error), the same
file bytes, nothing extra printed, legacy's text exactly "script failed" at the position the VM
reports, the VM's message exactly as pinned, with no internal VM tag or compiler diagnostic in it;
and the same programs with the file open for writing succeed identically on both engines.

usage: file_loop_diagnostic.py --nift NIFT
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

PREFIX = 'f:=file("data");n:=0;fn(d()){n+=1;print(n);return "xy"};'
LOOPS = {
    "while": 'i:=0;while(i<1){f.%s(d());i+=1}',
    "for": 'for(i:[1]){f.%s(d())}',
}
STATES = {"closed": "", "readonly": 'f.open("r");'}
WANT = {"closed": "file is not open", "readonly": "file is not open for writing"}
INTERNAL = ("NIFT_VM", "compiler defect", "not compiled", "runtime.", "(file handle", "internal")


def run(nift, engine, source):
    with tempfile.TemporaryDirectory(prefix="file-loop-diag-") as d:
        (Path(d) / "data").write_bytes(b"ABCDEFGH")
        (Path(d) / "case.f").write_text(source + "\n")
        p = subprocess.run([nift, "case.f"], cwd=d, env=dict(os.environ, NIFT_VM=engine),
                           capture_output=True, text=True, timeout=60)
        files = {f.name: f.read_bytes() for f in sorted(Path(d).iterdir()) if f.name != "case.f"}
        return p.returncode, p.stdout, p.stderr.replace(d, "$W"), files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    nift = os.path.abspath(ap.parse_args().nift)
    fails = count = 0

    def check(ok, label, detail=""):
        nonlocal fails, count
        count += 1
        if not ok:
            fails += 1
            print("FAIL %s %s" % (label, detail))

    for state, setup in STATES.items():
        for loop, body in LOOPS.items():
            for method in ("write", "write_line"):
                src = PREFIX + setup + body % method
                label = "%s/%s/%s" % (state, loop, method)
                vm, legacy = run(nift, "require", src), run(nift, "0", src)
                check(vm[0] == legacy[0] == 1, label, "exit %r vs %r" % (vm[0], legacy[0]))
                check(vm[1] == legacy[1] == "1\n", label, "stdout %r vs %r" % (vm[1], legacy[1]))
                check(vm[3] == legacy[3] == {"data": b"ABCDEFGH"}, label, "files differ")
                head, _, tail = legacy[2].partition(": script failed")
                check(tail == "\n" and head.startswith("error: $W/case.f:"), label, "legacy %r" % legacy[2])
                want = "%s: %s: %s\n" % (head, method, WANT[state])
                check(vm[2] == want, label, "VM %r, want %r" % (vm[2], want))
                check(not any(t in vm[2] for t in INTERNAL), label, "internal text in %r" % vm[2])
                ok_src = PREFIX + 'f.open("rw");' + body % method + ';print(f.read_all());f.revert();f.close()'
                vm_ok, legacy_ok = run(nift, "require", ok_src), run(nift, "0", ok_src)
                check(vm_ok == legacy_ok and vm_ok[0] == 0, label + " (open: succeeds)",
                      "%r vs %r" % (vm_ok[:3], legacy_ok[:3]))
    print(("FAIL" if fails else "PASS") + " loop write diagnostics: %d checks, %d failure(s)" % (count, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
