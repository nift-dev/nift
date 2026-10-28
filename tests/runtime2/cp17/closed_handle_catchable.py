#!/usr/bin/env python3
"""CP17 x CP13B decision D (owner, 2026-10-27): runtime.handle_closed is a
catchable Error (D-035) on the combined tree.

CP13B's catchable-Error construction admits a code only through
natives::is_recoverable_code; decision D derives the handle codes it admits
from CP17's handle_error_class (handle_code_recoverable), with no second list.
Before D, using a closed handle stopped the VM with an internal "catchable
Error construction: code is not catchable" (exit 70).

VM-only (NIFT_VM=require): legacy handles are strings and have no closed
state, so there is no legacy oracle for these programs. Every case also proves
that no internal failure occurred (exit is never 70, no "internal" text).
  uncaught      exit 1; stderr names the use site (line 5, column of the call)
                and "handle is closed"; statistics vm-error, code
                runtime.handle_closed.
  caught        the Error reaches catch with code runtime.handle_closed, its
                message, and the origin of the failing call (e.stack-free:
                re-throw case); the run then completes.
  caught-twice  a second closed use after catching is caught again (the
                handler and the closed slot are both still valid).
  cleanup       after catching, a fresh file opens/reads/closes normally and
                the run ends with exit 0: no left-open or cleanup failure.
  rethrow       a caught handle_closed Error re-thrown uncaught keeps its
                original origin (CP13B: origin stamped once, at raise).
  fatal-limit   limit.exceeded (NIFT_HANDLE_LIMIT=1) is NOT catchable: the
                catch block never runs, exit 1, code limit.exceeded.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

READ = "one\ntwo\n\n"  # print(f.read_all()) of "one\ntwo\n"
# The Error message of a closed-handle use (D-035 wording plus the handle identity).
MESSAGE = r"read_all: handle is closed \(file handle \d+/\d+, runtime\.handle_closed\)"
USE = 'f := file("p")\nf.open("r")\nprint(f.read_all())\nf.close()\n'

CASES = [
    ("uncaught", USE + 'print(f.read_all())\nprint("unreachable")\n', {}),
    ("caught", USE + 'try {\n  print(f.read_all())\n} catch(e) {\n  print(e.code)\n  print(e.message)\n}\nprint("after")\n', {}),
    ("caught-twice", USE + 'n := 0\ntry { f.read_all() } catch(e) { n = n + 1 }\n'
     'try { f.read_line() } catch(e) { n = n + 1; print(e.code) }\nprint(n)\n', {}),
    ("cleanup", USE + 'try { f.read_all() } catch(e) { print(e.code) }\n'
     'g := file("p")\ng.open("r")\nprint(g.read_line())\ng.close()\n', {}),
    ("rethrow", USE + 'try {\n  f.read_all()\n} catch(e) {\n  throw e\n}\n', {}),
    ("fatal-limit", 'f := file("p")\ntry {\n  g := file("p")\n} catch(e) {\n  print("caught " + e.code)\n}\nprint("after")\n',
     {"NIFT_HANDLE_LIMIT": "1"}),
]


def run(nift, root, source, extra):
    (root / "case.f").write_text(source)
    env = dict(os.environ)
    env["NIFT_VM"] = "require"
    env.update(extra)
    stats = root / "stats.jsonl"
    if stats.exists():
        stats.unlink()
    env["NIFT_VM_STATS"] = str(stats)
    proc = subprocess.run([nift, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                          capture_output=True, text=True, timeout=60)
    record = None
    if stats.exists():
        lines = [json.loads(line) for line in stats.read_text().splitlines() if line.strip()]
        record = lines[-1] if lines else None
    return proc.stdout, proc.stderr, proc.returncode, record or {}


def check(name, out, err, rc, st):
    problems = []
    if rc == 70 or "internal" in err.lower() or "not catchable" in err:
        return [f"internal failure: rc={rc} err={err!r}"]
    site = re.compile(r"case\.f:5:\d+: .*handle is closed")
    if name == "uncaught":
        if rc != 1 or out != READ or not site.search(err):
            problems.append(f"uncaught shape rc={rc} out={out!r} err={err!r}")
        if st.get("outcome") != "vm-error" or st.get("code") != "runtime.handle_closed":
            problems.append(f"stats {st!r}")
    elif name == "caught":
        if rc != 0 or err != "" or not re.fullmatch(re.escape(READ + "runtime.handle_closed\n") + MESSAGE + r"\nafter\n", out):
            problems.append(f"caught shape rc={rc} out={out!r} err={err!r}")
        if st.get("outcome") != "vm":
            problems.append(f"stats {st!r}")
    elif name == "caught-twice":
        if rc != 0 or out != READ + "runtime.handle_closed\n2\n" or st.get("outcome") != "vm":
            problems.append(f"rc={rc} out={out!r} err={err!r} st={st!r}")
    elif name == "cleanup":
        if rc != 0 or out != READ + "runtime.handle_closed\none\n" or err != "" or st.get("outcome") != "vm":
            problems.append(f"rc={rc} out={out!r} err={err!r} st={st!r}")
    elif name == "rethrow":
        if rc != 1 or not re.search(r"case\.f:6:\d+: .*handle is closed", err) or \
                st.get("code") != "runtime.handle_closed":
            problems.append(f"origin not kept: rc={rc} err={err!r} st={st!r}")
    elif name == "fatal-limit":
        if rc != 1 or "caught" in out or "after" in out or "limit" not in err or \
                st.get("code") != "limit.exceeded":
            problems.append(f"limit must stay fatal: rc={rc} out={out!r} err={err!r} st={st!r}")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    if not os.access(nift, os.X_OK):
        print(f"FAIL: no executable {nift}")
        return 1
    failures = 0
    for name, source, extra in CASES:
        with tempfile.TemporaryDirectory(prefix="cp17-closed-catch-") as tmp:
            root = Path(tmp)
            (root / "p").write_text("one\ntwo\n")
            out, err, rc, st = run(nift, root, source, extra)
            problems = check(name, out, err, rc, st)
            if problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name}")
    print(f"{'PASS' if not failures else 'FAIL'} cp17 closed-handle catchability (decision D): "
          f"{len(CASES)} cases, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
