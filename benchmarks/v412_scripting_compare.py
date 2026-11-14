#!/usr/bin/env python3
"""Corrected scripting comparison, v4.12.0 versus Runtime 2.0 (owner decision D-097, 2026-11-14).

benchmarks/v43_scripting_perf.py (CP91, kept byte-identical) invokes `nift run <script>` and `nift sh`; both
v4.12.0 and the Runtime 2.0 candidate reject both commands ("unknown command 'run'", "unknown command 'sh'", exit 1),
so its rows timed an error exit. This file keeps its workloads (the same sources, inputs, sizes, operation counts and
samples; the original has no warmup, so neither does this) and fixes only the invocation:

  - a script runs as `nift <script>`, with the script's directory as the working directory (the workloads read and
    create files by relative path; the original ran from the caller's directory);
  - the REPL row pipes the same 500 statements into the plain shell, `nift` with no command (`nift commands`:
    "(no command)  Start the persistent native Nift shell"). `nift sh` is not a command on either version.

Validity oracle. Every workload is validated before anything is timed, and every timed sample is validated again:
exit status 0, the exact expected stdout, empty stderr, the exact expected files (names and sizes) in a work
directory that is recreated from the inputs before every sample, and the workload's operation count checked in its
source and its output. A workload that fails any check is FAILED, with the reason, and has no timing at all: the
output never carries a number for it.

Categories, reported separately and never aggregated:
  identical-source       the same program text on both versions;
  equivalent-operation   fs_10_touch_open only: `touch(...)` under --version-profile v412 and `native::touch(...)`
                         under --version-profile rt2 (Runtime 2.0 L6 Option B, D-075 #2); the only source difference.
                         --equivalence proves it first: the same stdout, stderr, files and sizes on both binaries.
The profile is chosen by flag only; there is no version detection and no fallback.

Correctness first. --validate-only runs one validation per workload and never times. Wall-clock results are evidence
only from the approved Linode measurement (owner rule); a run in a development sandbox is not.

  v412_scripting_compare.py --nift BIN --version-profile v412|rt2 [--validate-only] [--samples 15]
                            [--expect-sha256 HEX] [--json OUT]
  v412_scripting_compare.py --equivalence --v412-nift BIN --rt2-nift BIN

Exit status: 0 every workload PASS; 1 a workload FAILED (or equivalence not shown); 3 the binary changed during the
run (the whole run is DISCARDED).
"""
import argparse
import hashlib
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

SAMPLES = 15   # the original's sample count
WARMUP = 0     # the original has no warmup run
TOUCH = {"v412": "touch", "rt2": "native::touch"}
EVIDENCE_NOTE = ("correctness and methodology tool: timings are evidence only from the approved Linode measurement; "
                 "a FAILED workload has no timing")


def workloads(profile):
    """[(name, category, kind, inputs {file: text}, script, stdin, expected stdout, created files {name: size},
    (operation, expected count, count found in the source))]"""
    stmts = "\n".join("x%d := %d\nx%d += 1" % (i, i, i) for i in range(1000)) + "\nprint(0)\n"
    fn = "fn(acc(n)) { r := 0\nwhile(n > 0) { r += n\nn -= 1 }\nreturn r }\n" + "print(acc(200))\n" * 100
    lam = "lam := (n) => { r := 0\nwhile(n > 0) { r += n\nn -= 1 }\nreturn r }\n" + "print(lam(200))\n" * 100
    lib = "base := 10\nmul := (x) => x * base\nexport(mul)\n"
    imp = "@import(\"lib.nift\")\nprint(mul(2))\n"
    fs = "for(i : [1,2,3,4,5,6,7,8,9,10]) { %s(\"f\" + i) }\nprint(open(\"f5\") == \"\")\n" % TOUCH[profile]
    data = "".join("line-%d\n" % i for i in range(5000))
    stream = ("i := ifstream(\"data.txt\")\ncount := 0\nline := i.read_line()\nwhile(line != null) { count += 1\n"
              "line = i.read_line() }\nprint(count)\nclose(i)\n")
    vals = " ".join("true %d %d.5 \"s%d\"" % (i, i, i) for i in range(125))
    readval = ("v := ifstream(\"vals.txt\")\nn := 0\nval := v.read_val()\nwhile(val != null) { n += 1\n"
               "val = v.read_val() }\nprint(n)\nclose(v)\n")
    repl = "\n".join("y%d := %d" % (i, i) for i in range(500)) + "\nquit\n"
    created = {"f%d" % i: 0 for i in range(1, 11)}
    return [
        ("run_startup_empty", "identical-source", "script", {}, "\n", None, "", {}, ("statements", 0, 0)),
        ("statements_2k", "identical-source", "script", {}, stmts, None, "0\n", {},
         ("statements", 2000, stmts.count(" := ") + stmts.count(" += "))),
        ("named_fn_100_calls", "identical-source", "script", {}, fn, None, "20100\n" * 100, {},
         ("calls", 100, fn.count("print(acc(200))"))),
        ("lambda_100_calls", "identical-source", "script", {}, lam, None, "20100\n" * 100, {},
         ("calls", 100, lam.count("print(lam(200))"))),
        ("import_once", "identical-source", "script", {"lib.nift": lib}, imp, None, "20\n", {},
         ("imports", 1, imp.count("@import("))),
        ("fs_10_touch_open", "equivalent-operation", "script", {}, fs, None, "true\n", created,
         ("touched files", 10, len(created))),
        ("stream_read_5k_lines", "identical-source", "script", {"data.txt": data}, stream, None, "5000\n", {},
         ("lines", 5000, data.count("\n"))),
        ("read_val_500", "identical-source", "script", {"vals.txt": vals}, readval, None, "500\n", {},
         ("values", 500, len(vals.split(" ")))),
        ("repl_500_statements", "identical-source", "shell", {}, None, repl, "", {},
         ("statements", 500, repl.count(" := "))),
    ]


SCRIPT_NAME = "w.nift"


def prepare(work, inputs, script):
    """A fresh work directory (the inputs and the script) and a fresh, separate HOME (the shell keeps its history
    there), so every sample starts from the same state and the work directory holds only the workload's files."""
    for d in (work, work + ".home"):
        if os.path.exists(d):
            shutil.rmtree(d)
        os.makedirs(d)
    for name, text in inputs.items():
        with open(os.path.join(work, name), "w") as f:
            f.write(text)
    if script is not None:
        with open(os.path.join(work, SCRIPT_NAME), "w") as f:
            f.write(script)


def argv_for(nift, kind, prefix):
    """The supported invocations: `nift <script>`; the plain shell is `nift` with no command. `prefix` exists only
    for the selftest's negative controls (for example ["run"] or ["sh"])."""
    return [nift] + list(prefix) + ([SCRIPT_NAME] if kind == "script" else [])


def execute(nift, work, kind, stdin, prefix):
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(HOME=work + ".home", LANG="C")
    t0 = time.perf_counter()
    p = subprocess.run(argv_for(nift, kind, prefix), cwd=work, input=(stdin or "").encode(), capture_output=True,
                       env=env, timeout=120)
    return (time.perf_counter() - t0) * 1000.0, p


def oracle(work, p, expected_out, inputs, script, created, op):
    """None when the run is valid, else the reason."""
    name, want, found = op
    if found != want:
        return "operation count: the source has %d %s, expected %d" % (found, name, want)
    if p.returncode != 0:
        return "exit status %d (expected 0); stderr: %s" % (p.returncode, p.stderr.decode(errors="replace")[:300])
    if p.stderr:
        return "unexpected stderr: %s" % p.stderr.decode(errors="replace")[:300]
    out = p.stdout.decode(errors="replace")
    if out != expected_out:
        return "stdout differs from the expected %d bytes: got %r" % (len(expected_out), out[:200])
    want_files = {n: len(t.encode()) for n, t in inputs.items()}
    if script is not None:
        want_files[SCRIPT_NAME] = len(script.encode())
    want_files.update(created)
    have = {n: os.path.getsize(os.path.join(work, n)) for n in os.listdir(work)}
    if have != want_files:
        missing = sorted(set(want_files) - set(have))
        extra = sorted(set(have) - set(want_files))
        sizes = sorted(n for n in set(have) & set(want_files) if have[n] != want_files[n])
        return "filesystem effects differ: missing %s, unexpected %s, wrong size %s" % (missing, extra, sizes)
    return None


def stats(vals):
    vals = sorted(vals)
    return {"min": round(vals[0], 3), "p25": round(vals[len(vals) // 4], 3),
            "median": round(statistics.median(vals), 3), "p75": round(vals[(3 * len(vals)) // 4], 3),
            "max": round(vals[-1], 3), "mean": round(statistics.mean(vals), 3),
            "stddev": round(statistics.pstdev(vals), 3), "samples": len(vals)}


def run_all(nift, profile, samples=SAMPLES, validate_only=False, prefix=(), patch=None):
    """{workload: {"category", "status": PASS|FAILED, "reason"?, "timing"? (only when timed and PASS)}}.
    `patch(list)` lets the selftest alter workloads (negative controls)."""
    rows = workloads(profile)
    if patch:
        rows = patch(rows)
    root = tempfile.mkdtemp(prefix="v412-scripting-")
    results = {}
    try:
        for name, category, kind, inputs, script, stdin, expected, created, op in rows:
            work = os.path.join(root, name)
            prepare(work, inputs, script)
            _, p = execute(nift, work, kind, stdin, prefix)
            why = oracle(work, p, expected, inputs, script, created, op)
            row = {"category": category}
            if why is None and not validate_only:
                vals = []
                for k in range(WARMUP + samples):
                    prepare(work, inputs, script)            # identical state for every sample
                    ms, p = execute(nift, work, kind, stdin, prefix)
                    why = oracle(work, p, expected, inputs, script, created, op)
                    if why is not None:
                        why = "sample %d: %s" % (k + 1, why)
                        break
                    if k >= WARMUP:
                        vals.append(ms)
                if why is None:
                    row["timing"] = stats(vals)
            row["status"] = "PASS" if why is None else "FAILED"
            if why is not None:
                row["reason"] = " / ".join(x for x in why.splitlines() if x.strip())
            results[name] = row
    finally:
        shutil.rmtree(root, ignore_errors=True)
    return results


def render(results, profile):
    lines = []
    for category in ("identical-source", "equivalent-operation"):
        lines.append("== %s (%s profile; reported separately, never aggregated)" % (category, profile))
        for name, row in results.items():
            if row["category"] != category:
                continue
            if row["status"] != "PASS":
                lines.append("FAILED %s: %s (no timing)" % (name, row["reason"]))
            elif "timing" in row:
                lines.append("PASS %s: median %.3f ms over %d samples" % (name, row["timing"]["median"],
                                                                           row["timing"]["samples"]))
            else:
                lines.append("PASS %s: valid (validate-only, not timed)" % name)
    return "\n".join(lines)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def observe_fs(nift, profile):
    """stdout, stderr, files and sizes of the fs workload, for the equivalence proof."""
    row = next(r for r in workloads(profile) if r[0] == "fs_10_touch_open")
    root = tempfile.mkdtemp(prefix="v412-equivalence-")
    try:
        work = os.path.join(root, "w")
        prepare(work, row[3], None)          # the script is passed by name below, so write it beside the inputs
        with open(os.path.join(work, SCRIPT_NAME), "w") as f:
            f.write(row[4])
        _, p = execute(nift, work, "script", None, ())
        files = {n: os.path.getsize(os.path.join(work, n)) for n in os.listdir(work) if n != SCRIPT_NAME}
        return p.returncode, p.stdout.decode(errors="replace"), p.stderr.decode(errors="replace"), files
    finally:
        shutil.rmtree(root, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift")
    ap.add_argument("--version-profile", choices=sorted(TOUCH))
    ap.add_argument("--samples", type=int, default=SAMPLES)
    ap.add_argument("--validate-only", action="store_true")
    ap.add_argument("--expect-sha256")
    ap.add_argument("--json")
    ap.add_argument("--equivalence", action="store_true")
    ap.add_argument("--v412-nift")
    ap.add_argument("--rt2-nift")
    a = ap.parse_args()
    if a.equivalence:
        if not (a.v412_nift and a.rt2_nift):
            ap.error("--equivalence needs --v412-nift and --rt2-nift")
        old, new = observe_fs(os.path.abspath(a.v412_nift), "v412"), observe_fs(os.path.abspath(a.rt2_nift), "rt2")
        ok = old == new and old[0] == 0
        print("%s equivalence fs_10_touch_open: v412 touch() %r; rt2 native::touch() %r" %
              ("PASS" if ok else "FAILED", old, new))
        return 0 if ok else 1
    if not (a.nift and a.version_profile):
        ap.error("--nift and --version-profile are required")
    nift = os.path.abspath(a.nift)
    before = sha256(nift)
    if a.expect_sha256 and before != a.expect_sha256:
        print("DISCARDED: %s has sha256 %s, expected %s; nothing was run" % (nift, before, a.expect_sha256))
        return 3
    results = run_all(nift, a.version_profile, a.samples, a.validate_only)
    after = sha256(nift)
    if after != before:
        print("DISCARDED: %s changed during the run (sha256 %s, then %s); no result is valid" % (nift, before, after))
        return 3
    print("v412 scripting compare: profile %s, binary sha256 %s, %s; %s" % (
        a.version_profile, before, "validate-only" if a.validate_only else "%d samples, warmup %d" % (a.samples, WARMUP),
        EVIDENCE_NOTE))
    print(render(results, a.version_profile))
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"profile": a.version_profile, "binary_sha256": before, "validate_only": a.validate_only,
                       "note": EVIDENCE_NOTE, "results": results}, f, indent=2, sort_keys=True)
    return 0 if all(r["status"] == "PASS" for r in results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
