#!/usr/bin/env python3
"""A legacy built-in name declared by the user keeps legacy's built-in precedence (D-073 #5, owner 2026-11-11 22:23).

`sorted_set` is a legacy built-in (ParserExpression.cpp lists the collection constructors as
`{std::string("stack"), ..., std::string("sorted_set")}`), which tools/rt-vm/legacy_names.py did not extract before
C136. The VM then compiled a user `fn(sorted_set())`, a `sorted_set := ...` binding or a parameter named
`sorted_set` as the user's callable, while legacy's built-in wins (it returns a sorted set, which print cannot show).
Since C136 the name is in legacy_special_name, so those programs are refused before they run, as `sorted_map` is;
member names (a struct field, a struct method called as a method) are not affected and run legacy-equal.

Each case records legacy's exact bytes (stdout, stderr with the path removed, exit status), and the VM outcome:
  "vm"      the VM runs it and its bytes equal legacy's
  <reason>  the VM refuses before running anything, with that reason and position (line, column)

  legacy_name_registration.py --nift NIFT            the cases
  legacy_name_registration.py --mutants              NIFT_LEGACY_NAMES_MUTANT=1 (the registration removed, built by
                                                     sanctioned_mutant.py) must fail every refused case
The mutant directory is deleted afterwards.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
UNRENDERABLE = b"print: value is not directly renderable\n"
CASES = {
    # name: (program, legacy (rc, stdout, stderr suffix after "main.f:"), VM outcome, VM line:column)
    "user-fn": ('fn(sorted_set()) {\n  return 9\n}\nprint(sorted_set())\n',
                (1, b"", b"4:1: " + UNRENDERABLE), "fn-name-reserved", "1:1"),
    "binding": ('sorted_set := () => 9\nprint(sorted_set())\n',
                (1, b"", b"2:1: " + UNRENDERABLE), "binding-name-reserved", "1:1"),
    "parameter": ('fn(f(sorted_set)) {\n  return sorted_set()\n}\nprint(f(() => 9))\n',
                  (1, b"", b"4:1: " + UNRENDERABLE), "param-name-reserved", "1:6"),
    "struct-field": ('struct(S) {\n  sorted_set := 1\n  fn(run()) {\n    return 2\n  }\n}\ns := S()\nprint(s.run())\n',
                     (0, b"2\n", b""), "vm", None),
    "struct-method": ('struct(S) {\n  k := 1\n  fn(sorted_set()) {\n    return 9\n  }\n}\ns := S()\nprint(s.sorted_set())\n',
                      (0, b"9\n", b""), "vm", None),
}
MUTANT_CAUGHT_BY = ["user-fn", "binding", "parameter"]


def run(nift, src, mode):
    d = tempfile.mkdtemp(prefix="lnr-")
    try:
        with open(os.path.join(d, "main.f"), "w") as f:
            f.write(src)
        st = os.path.join(d, "st.jsonl")
        env = {"PATH": "/usr/bin:/bin", "HOME": d, "LANG": "C", "NIFT_VM": mode, "NIFT_VM_STATS": st}
        p = subprocess.run(["timeout", "30", nift, "main.f"], cwd=d, env=env, capture_output=True,
                           stdin=subprocess.DEVNULL)
        stats = json.loads(open(st).read().splitlines()[-1]) if os.path.exists(st) else {}
        err = p.stderr.replace((d + "/").encode(), b"")
        return p.returncode, p.stdout, err, stats
    finally:
        shutil.rmtree(d, ignore_errors=True)


def check(nift):
    failing = []
    for name, (src, (lrc, lout, lerr), want, at) in CASES.items():
        problems = []
        rc, out, err, _ = run(nift, src, "0")
        want_err = (b"error: main.f:" + lerr) if lerr else b""
        if (rc, out, err) != (lrc, lout, want_err):
            problems.append("legacy %r, recorded %r" % ((rc, out, err), (lrc, lout, want_err)))
        vrc, vout, verr, st = run(nift, src, "require")
        if want == "vm":
            if st.get("outcome") != "vm" or (vrc, vout, verr) != (rc, out, err):
                problems.append("VM %r %r, legacy %r" % (st.get("outcome"), (vrc, vout, verr), (rc, out, err)))
        else:
            got = "%s %s:%s" % (st.get("reason"), st.get("line"), st.get("column"))
            if got != "%s %s" % (want, at) or vrc != 1 or vout:
                problems.append("VM %s rc %d stdout %r, expected the refusal %s at %s" % (got, vrc, vout, want, at))
        print(("PASS " if not problems else "FAIL ") + name)
        for p in problems:
            print("    " + p)
        if problems:
            failing.append(name)
    print("legacy-name-registration: %d/%d pass" % (len(CASES) - len(failing), len(CASES)))
    return failing


def mutants():
    out = os.path.join(ROOT, ".build", "mutant-nift", "legacy-names-1")
    try:
        b = subprocess.run([sys.executable, "-B", os.path.join(HERE, "sanctioned_mutant.py"), "--out", out,
                            "--define=-DNIFT_LEGACY_NAMES_MUTANT=1"], capture_output=True, text=True)
        if b.returncode != 0:
            print("FAIL legacy-names mutant: harness: build failed: %s" % (b.stderr[-400:] or b.stdout[-400:]))
            return 1
        failing = check(os.path.join(out, "nift"))
        missed = [c for c in MUTANT_CAUGHT_BY if c not in failing]
        if missed:
            print("FAIL legacy-names mutant 1: not caught by %s" % missed)
            return 1
        print("PASS legacy-names mutant 1 (sorted_set unregistered): caught by %s" % ", ".join(MUTANT_CAUGHT_BY))
        return 0
    finally:
        shutil.rmtree(out, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift")
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()
    if args.mutants:
        return mutants()
    return 1 if check(os.path.abspath(args.nift)) else 0


if __name__ == "__main__":
    sys.exit(main())
