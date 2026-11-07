#!/usr/bin/env python3
"""cp19 admission mutant m12: the permanent reachable-path regression probe (b5-refsem.md 13.1).

  m12_reachable.py --nift ./nift

m12 (`-DNIFT_SESSION_ADMISSION_MUTANT=12`, src/rt/session_callable.cpp) makes `may_call` always
return true. Its one call site is the upvalue scan's `is_cell(capture) && !may_call(bound)` (a
captured cell that `cell_plain_capture` does not resolve). B5 21 (12.1) recorded that site as
unreachable; the re-review (REVIEW-R2 R2-1) showed it IS reached: a session closure capturing a
function parameter that holds a mutex, a channel or an atomic (`host-handle-flow` refuses only
host handles such as an ofstream). Owner 2026-11-07 15:25: m12 is OBSERVATIONALLY EQUIVALENT to
production on the tested reachable paths (not unreachable, not proven universally equivalent);
it stays an informational negative control, with this probe.

The probe builds, in a temporary directory (deleted afterwards; nothing is written to the
tree), from the production build plan of `nift` (`make -n nift`):
  instr  production objects plus a copy of session_callable.cpp that logs every arrival at the
         call site with production's `may_call(bound)` value (logging only);
  m12    production objects plus session_callable.cpp compiled with the m12 define.
For each session in m12_reachable/ (the reviewer's p1-p10, fed to `nift -i`, NIFT_VM=require):
  - p4-p10 (reachable): instr arrives at the call site, and production's may_call is already
    true at every arrival (so forcing it true changes nothing there);
  - p1-p3 (controls: ofstream captures): refused `host-handle-flow`, no arrival;
  - production, instr (log lines removed) and m12 give identical stdout, stderr and exit status,
    equal to the pinned result.
FAIL (exit 1) if any of these stops holding: in particular an arrival where production's
may_call is false means m12 may become observable and its classification (13.1) must be
revisited. Exit 2 is a harness failure (no build plan, the call-site anchor moved, a build
failed), never a pass.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
PROBES = os.path.join(ROOT, "tests", "runtime2", "cp19", "m12_reachable")
SOURCE = "src/rt/session_callable.cpp"
OBJECT = "src/rt/session_callable.o"
ANCHOR = ("                    auto shape = cell ? visit(cell->value, depth + 1) :\n"
          "                        is_cell(capture) && !may_call(bound) ? Shape{} : incomplete();\n")
LOG = ("                    if (!cell && is_cell(capture)) std::fprintf(stderr, \"M12-PROBE may_call=%d\\n\","
       " (int)may_call(bound));\n")
REFUSED = "NIFT_VM session refused: host-handle-flow"
# name -> (reachable, pinned stdout, exit); stderr is pinned by the refusal text for the controls.
CASES = {
    "p1": (False, "", 1), "p2": (False, "", 1), "p3": (False, "", 1),
    "p4": (True, "1\n", 0), "p5": (True, "ok\n", 0), "p6": (True, "1\n", 0), "p7": (True, "1\n", 0),
    "p8": (True, "2\n", 0), "p9": (True, "2\n", 0), "p10": (True, "2\n", 0),
}


def harness(msg):
    print("FAIL m12 reachable probe (harness): " + msg)
    sys.exit(2)


def sh(cmd, log):
    with open(log, "ab") as f:
        return subprocess.run(cmd, shell=True, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode == 0


def build(tmp):
    current = subprocess.run(["make", "-s", "-n", "nift"], cwd=ROOT, capture_output=True, text=True)
    if current.returncode != 0 or re.search(r" -o nift$", current.stdout, re.M):
        harness("./nift is not current (run make nift)")
    plan = subprocess.run(["make", "-s", "-n", "nift", "-W", SOURCE], cwd=ROOT, capture_output=True, text=True).stdout
    compile_cmd = link = None
    for line in plan.splitlines():
        m = re.match(r"^bash scripts/build/compile-object\.sh --production " + re.escape(OBJECT) +
                     r" -- (.*) -c " + re.escape(SOURCE) + r"$", line)
        if m:
            compile_cmd = m.group(1)
        if line.endswith(" -o nift") and (" " + OBJECT + " ") in line:
            link = line
    if not compile_cmd or not link:
        harness("no build plan for %s in `make -n nift`" % SOURCE)
    text = open(os.path.join(ROOT, SOURCE)).read()
    if text.count(ANCHOR) != 1:
        harness("the may_call call-site anchor is not found exactly once in %s: revisit the probe and "
                "b5-refsem.md 13.1" % SOURCE)
    copy = os.path.join(tmp, "session_callable_probe.cpp")
    open(copy, "w").write("#include <cstdio>\n" + text.replace(ANCHOR, LOG + ANCHOR))
    log = os.path.join(tmp, "build.log")
    bins = {}
    for name, flags, src in (("instr", "", copy), ("m12", " -DNIFT_SESSION_ADMISSION_MUTANT=12", SOURCE)):
        obj = os.path.join(tmp, name + ".o")
        out = os.path.join(tmp, "nift-" + name)
        if not sh("%s%s -c %s -o %s" % (compile_cmd, flags, src, obj), log) or \
                not sh(link.replace(" " + OBJECT + " ", " " + obj + " ")[:-len(" -o nift")] + " -o " + out, log):
            sys.stdout.write(open(log).read()[-2000:])
            harness("building the %s CLI failed" % name)
        bins[name] = out
    return bins


def run(nift, probe, tmp):
    work = tempfile.mkdtemp(dir=tmp)
    try:
        env = {"PATH": "/usr/bin:/bin", "HOME": work, "TMPDIR": work, "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
               "NO_COLOR": "1", "NIFT_VM": "require"}
        p = subprocess.run(["timeout", "60", nift, "-i"], cwd=work, env=env, stdin=open(probe, "rb"),
                           capture_output=True)
        written = sorted(f for f in os.listdir(work) if f != ".nift_history")  # the session history file
        return p.stdout.decode(), p.stderr.decode(), p.returncode, written
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    a = ap.parse_args()
    prod = os.path.abspath(a.nift)
    tmp = tempfile.mkdtemp(prefix="nift-m12-reachable-")
    failures, arrivals = [], 0
    try:
        bins = build(tmp)
        for name, (reachable, stdout, status) in sorted(CASES.items(), key=lambda kv: int(kv[0][1:])):
            probe = os.path.join(PROBES, name + ".in")
            if not os.path.exists(probe):
                harness("missing probe " + probe)
            base = run(prod, probe, tmp)
            ins = run(bins["instr"], probe, tmp)
            mut = run(bins["m12"], probe, tmp)
            logs = [l for l in ins[1].splitlines() if l.startswith("M12-PROBE ")]
            ins = (ins[0], "".join(l + "\n" for l in ins[1].splitlines() if not l.startswith("M12-PROBE ")),
                   ins[2], ins[3])
            if (base[0], base[2]) != (stdout, status) or base[3]:
                failures.append("%s: production %r exit %d (wrote %s), pinned %r exit %d" %
                                (name, base[0], base[2], base[3], stdout, status))
            if not reachable and REFUSED not in base[1]:
                failures.append("%s: control not refused host-handle-flow: %r" % (name, base[1][-200:]))
            if reachable and (base[1] or not logs):
                failures.append("%s: the may_call call site was not reached (%d arrivals; stderr %r)" %
                                (name, len(logs), base[1][-200:]))
            if not reachable and logs:
                failures.append("%s: control arrived at the call site: %s" % (name, logs))
            if any(l != "M12-PROBE may_call=1" for l in logs):
                failures.append("%s: production's may_call is false at an arrival (%s): m12 may now be "
                                "observable, revisit b5-refsem.md 13.1" % (name, sorted(set(logs))))
            if ins != base:
                failures.append("%s: the instrumented production build differs from production: %r vs %r" %
                                (name, ins, base))
            if mut != base:
                failures.append("%s: m12 differs from production (m12 is now OBSERVABLE: revisit "
                                "b5-refsem.md 13.1): m12 %r, production %r" % (name, mut, base))
            arrivals += len(logs)
            print("%s %s: %s, %d arrival(s)%s; production = m12 = %r exit %d" %
                  ("FAIL" if any(f.startswith(name + ":") for f in failures) else "ok  ", name,
                   "reachable" if reachable else "control", len(logs),
                   " (may_call already true)" if logs else "", base[0], base[2]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for f in failures:
        print("FAIL " + f)
    if failures:
        print("FAIL m12 reachable probe: %d failure(s)" % len(failures))
        return 1
    print("PASS m12 reachable probe: 7 reachable sessions (%d call-site arrivals, production may_call already "
          "true at each), 3 refused controls; production, instrumented and m12 CLIs identical "
          "(m12 observationally equivalent on these paths; temporary CLIs deleted)" % arrivals)
    return 0


if __name__ == "__main__":
    sys.exit(main())
