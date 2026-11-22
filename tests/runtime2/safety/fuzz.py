#!/usr/bin/env python3
"""B0 safety sweep (adapted from the independent review of 8a9dc923, finding F1):
value kinds x wrappers x delivery routes x operations, deterministic.

  fuzz.py --nift ./nift [--seed 1] [--sample 20000] [--session-sample 5000] [--jobs 2]

Three fixed slices, every program printing first:
  exhaustive  every kind x delivery x operation with the value alone (16 x 14 x 38)
  sample      a fixed-seed sample of the whole space (every wrapper, depth 4)
  session     a fixed-seed sample run as a piped `nift -i` session (no modules)
Checks (owner requirements, b0-safety.md section 8), for every program:
  internal    no exit 70;
  leak        no internal diagnostic ("internal failure", "compiler defect",
              "unexpected operands") in stdout or stderr;
  lost        output printed before the failing operation is present: a
              program that ran (not refused) and failed at a line after its
              `print("b")` has printed `b`; a session that was not refused
              printed `b` and `after`; a refused session statement after the
              `print("b")` line still shows `b`;
  crash       the exit status is one of the documented ones (0, 1); any other
              status, a signal, an abort or sanitizer report, or a timeout fails;
  determinism a fixed subset (every 20th program, at least 500) runs twice and
              must give the same exit status, stdout and stderr.
The outcome itself (ordinary error, refusal, success) is not checked here:
safety.py checks recorded transcripts and the legacy-matched cases.
`--selftest` makes each check fire on a crafted fake binary. Exit 0 when no
program breaks a check.
"""
import argparse
import concurrent.futures
import itertools
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from fixture_paths import strip_fixture_root


INTERNAL = ("internal failure", "compiler defect", "unexpected operands")
VALS = {"int": "5", "str": '"s"', "null": "null", "bool": "true", "float": "1.5", "arr": "[1]",
        "obj": '{"k": 1}', "map": "MAP", "set": "SET", "stack": "STACK", "mutex": "mutex()",
        "chan": "channel()", "aint": "atomic<int>(1)", "abool": "atomic<bool>(true)", "fn": "(() => 1)",
        "range": "range(2)"}
SETUP = {"MAP": 'm0 := map()\nm0.set("q", 1)\n', "SET": 's0 := set()\ns0.add(1)\n',
         "STACK": 'k0 := stack()\nk0.push(1)\n'}
NAME = {"MAP": "m0", "SET": "s0", "STACK": "k0"}
WRAP = {"alone": "X", "arr": "[X]", "obj": '{"k": X}', "d3": "[[[X]]]", "d4obj": '{"a": [{"b": [X]}]}',
        "mixed": "[1, \"t\", X]"}
WRAPC = {"mapof": 'w1 := map()\nw1.set("k", X)\n', "stackof": 'w1 := stack()\nw1.push(X)\n',
         "mapmap": 'w2 := map()\nw2.set("j", X)\nw1 := map()\nw1.set("k", w2)\n',
         "stackarr": 'w1 := stack()\nw1.push([[X]])\n', "mapobjarr": 'w1 := map()\nw1.set("k", {"z": [X]})\n'}
# loopmix/loopmixs/param2/params (F1): the binding's kinds mix a scalar with the value's kind
DELIVER = ["direct", "param", "closure", "module", "mapget", "pop", "loop", "chan", "ternary", "ifelse",
           "loopmix", "loopmixs", "param2", "params"]
OPS = ["v.stringify()", "v.prettify()", "v.highlight()", "validate({}, [v])", "PRINT", '"i$[v.stringify()]"',
       "type(v)", "[v].stringify()", "{\"o\": v}.prettify()", "v == v", "[v, v].size()", "INTERP",
       "[v].contains(1)", "[v].join(\",\")", "[v].reverse()", "[v].sort()"]
MUT = ["v.push(1)", "v.pop()", "v.insert(0, 1)", "v.remove(0)", "v.splice(0, 1)", "v.splice(0, 1, [2])",
       "v.sort()", "v.sort((x, y) => x < y)", "v.reverse()", "v.clear()", 'v.set("k", 1)', 'v.remove("k")',
       "v.add(1)", "v.top()", 'v.get("k")', "v.contains(1)", "v[0] = 1", 'v.k = 1', "v += 1", 'v["k"] = 2',
       "v.size()", "v.length()"]


def build(kind, wrap, deliver, op):
    raw = VALS[kind]
    pre = ""
    if raw in SETUP:
        pre = SETUP[raw]
        raw = NAME[raw]
    if wrap in WRAP:
        expr = WRAP[wrap].replace("X", raw)
    else:
        pre += WRAPC[wrap].replace("X", raw)
        expr = "w1"

    def stmt(target):
        if op == "PRINT":
            return "print(%s)" % target
        if op == "INTERP":
            return 'print("i$[%s]")' % target
        o = op.replace("v", target)
        return o if op in MUT else "print(%s)" % o

    files = {}
    if deliver == "direct":
        prog = pre + 'v := %s\nprint("b")\n%s\n' % (expr, stmt("v"))
    elif deliver in ("param", "param2", "params"):
        first = {"param": "", "param2": "f(1)\n", "params": 'f("s")\n'}[deliver]
        prog = 'fn(f(v)) {\n  %s\n  return 0\n}\nprint("b")\n%s%sf(%s)\n' % (stmt("v"), pre, first, expr)
    elif deliver == "closure":
        prog = pre + 'g := () => %s\nprint("b")\nv := g()\n%s\n' % (expr, stmt("v"))
    elif deliver == "module":
        files["m.f"] = "fn(mk()) {\n  %s  return %s\n}\nexport(mk)\n" % (pre.replace("\n", "\n  "), expr)
        prog = 'import("m.f")\nprint("b")\nv := mk()\n%s\n' % stmt("v")
    elif deliver == "mapget":
        prog = pre + 'mm := map()\nmm.set("x", %s)\nprint("b")\nv := mm.get("x")\n%s\n' % (expr, stmt("v"))
    elif deliver == "pop":
        prog = pre + 'aa := [1, %s]\nprint("b")\nv := aa.pop()\n%s\n' % (expr, stmt("v"))
    elif deliver == "loop":
        prog = pre + 'aa := [%s]\nprint("b")\nfor (v : aa) { %s }\n' % (expr, stmt("v"))
    elif deliver == "loopmix":
        prog = pre + 'aa := [1, %s]\nprint("b")\nfor (v : aa) { %s }\n' % (expr, stmt("v"))
    elif deliver == "loopmixs":
        prog = pre + 'aa := ["s", %s]\nprint("b")\nfor (v : aa) { %s }\n' % (expr, stmt("v"))
    elif deliver == "chan":
        prog = pre + 'cc := channel()\ncc.send(%s)\nprint("b")\nv := cc.recv()\n%s\n' % (expr, stmt("v"))
    elif deliver == "ternary":
        prog = pre + ('fn(pick(n)) {\n  if (n > 0) { return %s }\n  return [1]\n}\nprint("b")\nv := pick(1)\n%s\n'
                      % (expr, stmt("v")))
    else:  # ifelse
        prog = pre + 'v := [1]\nif (args.size() == 0) { v = %s }\nprint("b")\n%s\n' % (expr, stmt("v"))
    files["p.f"] = prog
    return files


DOCUMENTED_EXITS = (0, 1)
CRASH_TEXT = ("AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:", "Segmentation fault", "Aborted",
              "terminate called")


def run(nift, files, session, timeout):
    d = tempfile.mkdtemp(prefix="nift-safety-fuzz-")
    try:
        for f, t in files.items():
            with open(os.path.join(d, f), "w") as fh:
                fh.write(t)
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C", "LC_ALL": "C",
               "TZ": "UTC", "NIFT_VM": "require"}
        if session:
            argv, stdin = [nift, "-i"], files["p.f"].encode() + b'print("after")\n'
        else:
            argv, stdin = [nift, "p.f"], b""
        try:
            p = subprocess.run(argv, cwd=d, env=env, input=stdin, capture_output=True, timeout=timeout)
            rc, out, err = p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired as e:
            rc, out, err = "timeout", e.stdout or b"", e.stderr or b""
        norm = lambda b: strip_fixture_root(b.decode("utf-8", "replace"), d)
        return rc, norm(out), norm(err)
    finally:
        shutil.rmtree(d, ignore_errors=True)


def violations(mode, files, rc, out, err):
    """The checks this one result breaks (internal, leak, lost, crash)."""
    v = []
    if rc == 70:
        v.append("internal")
    if any(t in out or t in err for t in INTERNAL):
        v.append("leak")
    if rc == "timeout":
        v.append("crash:timeout")
    elif rc not in DOCUMENTED_EXITS and rc != 70:
        v.append("crash:exit %s" % rc)
    if any(t in err for t in CRASH_TEXT):
        v.append("crash:report")
    lines = files["p.f"].split("\n")
    printed_at = lines.index('print("b")') + 1 if 'print("b")' in lines else None
    refused = "no part of it ran" in err or "NIFT_VM session refused" in err
    m = re.search(r"(?:^|\n)error: (?:p\.f|<stdin>):(\d+):\d+: ", err)
    module_error = re.search(r"(?:^|\n)error: m\.f:\d+:\d+: ", err) is not None
    if printed_at is not None and rc != "timeout":
        if mode == "session":
            if not refused and not ("b\n" in out and out.endswith("after\n")):
                v.append("lost")
            elif refused and m and int(m.group(1)) > printed_at and "b\n" not in out:
                v.append("lost")
        elif not refused and rc != 0:
            # the run failed: a failure after the print line must leave `b`
            after = (m is not None and int(m.group(1)) > printed_at) or (module_error and "import" not in err)
            if after and not out.startswith("b\n"):
                v.append("lost")
        elif rc == 0 and not out.startswith("b\n"):
            v.append("lost")
    return v


def sweep(nift, jobs, workers, timeout):
    def one(job):
        mode, case, files = job
        return job, run(nift, files, mode == "session", timeout)

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        return list(pool.map(one, jobs))


def check_all(nift, jobs, workers, timeout, repeat_every, show):
    bad, stats = [], {}
    results = sweep(nift, jobs, workers, timeout)
    for (mode, case, files), (rc, out, err) in results:
        v = violations(mode, files, rc, out, err)
        key = "%s:%s" % (mode, "BROKEN" if v else ("ok" if rc == 0 else "error-or-refusal"))
        stats[key] = stats.get(key, 0) + 1
        if v:
            bad.append({"checks": v, "mode": mode, "case": list(case), "exit": rc, "stdout": out[-120:],
                        "stderr": err.strip()[-240:]})
    again = [j for k, j in enumerate(jobs) if k % repeat_every == 0]
    first = {id(j): r for j, r in results}
    rerun = sweep(nift, again, workers, timeout)
    for job, r in rerun:
        if first[id(job)] != r:
            bad.append({"checks": ["determinism"], "mode": job[0], "case": list(job[1]), "first": first[id(job)],
                        "second": r})
    stats["repeated"] = len(again)
    return bad, stats


def selftest(timeout):
    """Each check must fire: crafted fake binaries (shell scripts) stand in for nift."""
    d = tempfile.mkdtemp(prefix="nift-safety-fuzz-selftest-")
    fakes = {
        "internal": 'echo b; echo "error: p.f: oops" >&2; exit 70',
        "leak": 'echo b; echo "unexpected operands (compiler defect): x"; exit 0',
        "lost": 'echo "error: p.f:9:1: boom" >&2; exit 1',
        "crash:exit 3": 'echo b; exit 3',
        "crash:exit -11": 'echo b; kill -SEGV $$',
        "crash:report": 'echo b; echo "==1==ERROR: AddressSanitizer: heap-use-after-free" >&2; exit 1',
        "crash:timeout": 'echo b; sleep 30',
        "determinism": 'echo b; if [ -e "$HOME/../nift-fuzz-flip" ]; then rm "$HOME/../nift-fuzz-flip"; '
                       'echo x; else touch "$HOME/../nift-fuzz-flip"; fi; exit 0',
        "pass": 'echo b; echo "error: p.f:9:1: an ordinary error" >&2; exit 1',
    }
    case = ("int", "alone", "direct", "PRINT")
    files = build(*case)
    files["p.f"] = "\n" * 3 + files["p.f"]  # the print line is line 4 or later; the fake fails at line 9
    failed = 0
    try:
        for want, body in fakes.items():
            path = os.path.join(d, want.replace(":", "_").replace(" ", "_") + ".sh")
            with open(path, "w") as fh:
                fh.write("#!/bin/sh\n" + body + "\n")
            os.chmod(path, 0o755)
            jobs = [("file", case, files)]
            bad, _ = check_all(path, jobs, 1, timeout, 1, 0)
            fired = sorted({c for b in bad for c in b["checks"]})
            ok = (not fired) if want == "pass" else (want in fired)
            print("%s selftest %-15s fired %s" % ("PASS" if ok else "FAIL", want, fired))
            failed += not ok
    finally:
        shutil.rmtree(d, ignore_errors=True)
        try:
            os.remove(os.path.join(tempfile.gettempdir(), "nift-fuzz-flip"))
        except OSError:
            pass
    print(("PASS" if not failed else "FAIL") + " runtime2 safety fuzz selftest: %d check(s) did not fire" % failed)
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--sample", type=int, default=20000)
    ap.add_argument("--session-sample", type=int, default=5000)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--timeout", type=float, default=30)
    ap.add_argument("--repeat-every", type=int, default=20)
    ap.add_argument("--show", type=int, default=20)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest(2)
    nift = os.path.abspath(a.nift)
    ops = OPS + MUT
    space = list(itertools.product(VALS, list(WRAP) + list(WRAPC), DELIVER, ops))
    rng = random.Random(a.seed)
    exhaustive = [(k, "alone", d, o) for k, d, o in itertools.product(VALS, DELIVER, ops)]
    rest = [c for c in space if c[1] != "alone"]
    sample = rng.sample(rest, min(a.sample, len(rest)))
    session = rng.sample([c for c in space if c[2] != "module"], a.session_sample)
    jobs = [("file", c, build(*c)) for c in exhaustive + sample] + [("session", c, build(*c)) for c in session]
    bad, stats = check_all(nift, jobs, a.jobs, a.timeout, a.repeat_every, a.show)
    for b in bad[:a.show]:
        print("FAIL " + json.dumps(b))
    by = {}
    for b in bad:
        for c in b["checks"]:
            by[c.split(":")[0]] = by.get(c.split(":")[0], 0) + 1
    summary = "%d programs (%d exhaustive, %d sampled, %d session; seed %d; %d run twice) %s" % (
        len(jobs), len(exhaustive), len(sample), len(session), a.seed, stats.pop("repeated"),
        json.dumps(stats, sort_keys=True))
    if bad:
        print("FAIL runtime2 safety fuzz: %d violation(s) %s; %s" % (len(bad), json.dumps(by, sort_keys=True), summary))
        return 1
    print("PASS runtime2 safety fuzz: %s" % summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
