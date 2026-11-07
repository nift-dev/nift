#!/usr/bin/env python3
"""Table test of scripts/certify_fingerprints.py (owner 2026-11-08 01:06).

For every expected-baseline gate, on a real log of that gate (tests/runtime2/certify-fingerprints/):
- positive: the log against itself, and against a copy from another checkout root and other
  temporary paths, has equal fingerprints;
- new: a previously passing (or absent) case starts failing alongside the existing failures;
- diag: an existing failure changes its diagnostic or category;
- subst: one failing case disappears and a different one fails, with the same count;
  (new, diag and subst must differ: the three mandatory negative controls)
- wording: only wording outside the extracted fields changes (summaries, notes, category totals,
  headers): must still be equal.
Also: every expected-baseline gate has an extractor and a row here, and none is also STOPPED.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import certify_fingerprints as F  # noqa: E402

LOGS = os.path.join(HERE, "certify-fingerprints")
ROOT = "/home/claude/review/cand-b5"

# gate: {new|diag|subst|wording: [(old, new), ...]} (each old must occur in the log; first
# occurrence replaced). `diag: None` states why the gate has no diagnostic beyond its case id.
TABLE = {
    "test-v44-shell-restricted": {
        "new": [("PASS ctrl-d-exits", "FAIL ctrl-d-exits")],
        "diag": [("FAIL run-captures-not-tty\n", "FAIL run-captures-not-tty\nSegmentation fault\n")],
        "subst": [("FAIL user-function-runs", "FAIL completed-command-runs"),
                  ("PASS completed-command-runs", "PASS user-function-runs")],
        "wording": [("FAILED: tab-completes-user-function, user-function-runs",
                     "FAILED (2): tab-completes-user-function and user-function-runs"), ("MARK run1", "MARK first")],
    },
    "test-v46-relative-imports": {
        "new": [("make: ***", 'NIFT="x" bash tests/v46_import_worker_ownership_smoke.sh\nerror: /tmp/a/tmp.QQ/w.f:2:1: '
                 'not compiled for the VM (import-nested): this form\nmake: ***')],
        "diag": [("(fn-flags)", "(fn-name-reserved)")],
        "subst": [("bash tests/v44_relative_import_ownership_smoke.sh", "bash tests/v46_import_worker_ownership_smoke.sh")],
        "wording": [("note: the program was not executed: no part of it ran", "note: nothing ran"),
                    ("is not supported by the Runtime 2.0 VM yet", "is not supported yet")],
    },
    "test-runtime2-cp03-simplify": {
        "new": [("PASS missing-tracer caller: explicitly unmeasured (77)", "FAIL missing-tracer caller: exit 0 != 77")],
        "diag": [("(callable:module_path)", "(callable:inject)")],
        "subst": [("FAIL CP03 missing-tracer caller", "FAIL CP03 present-tracer caller")],
        "wording": [("note: please report unsupported programs at", "note: report unsupported programs to")],
    },
    "test-sha256": {
        "new": [("PASS number rejected", "FAIL number rejected (expected 'error', got '')")],
        "diag": [("FAIL line endings not normalised (expected 'false', got '')",
                  "FAIL line endings not normalised (expected 'false', got 'true')")],
        "subst": [("FAIL NIST one million a", "FAIL NIST two million a")],
        "wording": [("sha256 builtins: 61 passed, fail=1", "sha256 builtins: 61 passed; 1 failed"),
                    ("PASS null rejected", "PASS null is rejected")],
    },
    "test-runtime2-cp01-simplify": {
        "new": [("FAILS (3):\n", "FAILS (3):\n  FAIL S5 rebuilds per no-op build: [1, 0]\n")],
        "diag": [("rebuilt ['p1'] expected []", "rebuilt ['p2'] expected []")],
        "subst": [("FAIL S4a create unreferenced file", "FAIL S4b create unreferenced file")],
        "wording": [("FAILS (3):", "FAILS (three):"), ("note S8a switch modified->hash rebuilt 31", "note S8a rebuilt 32")],
    },
    "test-cp19-s1-dispatch": {
        "new": [("PASS rm missing fails", "FAIL rm missing fails")],
        "diag": [("builtin-lazy-args:setenv", "builtin-lazy-args:getenv")],
        "subst": [("FAIL failed user function status non-zero", "FAIL failed user function status zero")],
        "wording": [("FAILED: user function side effect once", "FAILED (9): user function side effect once")],
    },
    "test-rt-parser": {
        "new": [("1 rt parser unit failure(s)", "FAIL dump of [x()] lacks 'Call':\n2 rt parser unit failure(s)")],
        "diag": [("'Opaque [legacy-runtime-error]'", "'Opaque [legacy-parse]'")],
        "subst": [("dump of [print(await(f()))]", "dump of [print(await(g()))]")],
        "wording": [("(cwd=", "(directory=")],
    },
    "test-test-integrity": {
        "new": [("test-integrity findings: 58", "test-integrity findings: 59\n  [pipefail-missing] tests/new.sh: pipeline "
                 "runs without pipefail provably active")],
        "diag": [("[swallowed-returncode] scripts/certify.py", "[pipefail-missing] scripts/certify.py")],
        "subst": [("scripts/build/check-production-tu.py", "scripts/build/check-production-tv.py")],
        "wording": [('--output ".build/bh2/test-integrity-report.json"', '--output ".build/bh2/report.json"')],
        "count": [("test-integrity findings: 58", "test-integrity findings: 59")],
    },
    "test-v44-packages": {
        "new": [("PASS package metadata and lock schema", "FAIL package metadata and lock schema: lock missing")],
        "diag": [("(import-nested)", "(import-dynamic)")],
        "subst": [("/usr/bin/python3 tests/package_transaction_smoke.py", "/usr/bin/python3 tests/package_tools_dogfood.py")],
        "wording": [("added demo", "added package demo")],
    },
    "test-rt-host-mutants": {
        "new": [("make[1]: Target 'nift' not remade", "FAIL host mutant 4: observer test passed (mutant not caught)\n"
                 "make[1]: Target 'nift' not remade")],
        "diag": [("FAIL host mutant 1: build failed (harness failure)",
                  "FAIL host mutant 1: vm_host tests passed (mutant not caught)")],
        "subst": [("FAIL host mutant 1:", "FAIL host mutant 2:")],
        # wording, and the -j4 scheduling: which objects are refused before make stops
        "wording": [("make[1]: Target 'nift' not remade because of errors.", "make[1]: Target 'nift' not remade."),
                    ("error: production object src/ParserScript.o: the command line sets the test-only switch "
                     "NIFT_HOST_MUTANT (tooling.md 3)\n", "")],
    },
    "cp25r-inventory": {
        "new": [("INVENTORY: unclassified reason: method:write", "INVENTORY: unclassified reason: method:write\n"
                 "INVENTORY: unclassified reason: method:read")],
        "diag": None,  # the unclassified reason is the case id and its whole diagnostic
        "subst": [("method:save", "method:load")],
        "wording": [("classify-all: 789 units", "classify-all: 790 units")],
    },
    "test-runtime2-rt-diff-selftest": {
        "new": [("PASS caught: file-mode (files)", "FAIL caught: file-mode (files)")],
        "diag": [("FAIL D-009 policies (rc=1)", "FAIL D-009 policies (rc=2)")],
        "subst": [("trace op not exercised: touch", "trace op not exercised: chmod")],
        "wording": [("PASS caught: tmp-write (tmp)", "PASS caught tmp-write"),
                    ("FAIL rt-diff self-tests", "FAIL rt-diff self-tests (see above)")],
    },
    "test-runtime2-scanner-parity": {
        "new": [("scanner parity: 88 sites", "FAIL script tests/x.f\n0a1\n> 1\nscanner parity: 88 sites")],
        "diag": [("(prepared-decl-type)", "(callable:inject)")],
        "subst": [("probes/frequency-2000.f\n", "probes/frequency-4000.f\n")],
        "wording": [("scanner parity: 88 sites, 2604 scripts compared", "scanner parity: 88 sites and 2604 scripts")],
    },
}

failures = []


def check(name, cond, why=""):
    if not cond:
        failures.append(f"{name}: {why}")


def mutate(text, edits, gate, kind):
    for old, new in edits:
        if old not in text:
            failures.append(f"{gate} {kind}: table text not in the log: {old[:60]!r}")
        text = text.replace(old, new, 1)
    return text


for gate in F.EXPECTED_BASELINE:
    check(f"{gate} has an extractor", gate in F.SPECS, "expected-baseline gate without an extractor")
    check(f"{gate} has a table row", gate in TABLE, "expected-baseline gate without a table test")
    check(f"{gate} not stopped", gate not in F.STOPPED, "expected-baseline gate is also STOPPED")
for gate in F.SPECS:
    check(f"{gate} is expected-baseline", gate in F.EXPECTED_BASELINE, "an extractor for a gate not listed")

n = 0
for gate, row in sorted(TABLE.items()):
    text = open(os.path.join(LOGS, gate + ".txt"), errors="replace").read()
    base = F.fingerprint(gate, text, [ROOT])
    check(f"{gate} positive", base is not None, "no fingerprint from the real log")
    if base is None:
        continue
    check(f"{gate} positive", F.same(base, F.fingerprint(gate, text, [ROOT])), "not equal to itself")
    moved = re.sub(r"/tmp/tmp\.\w+", "/tmp/tmp.ZZZZZZZZZZ", text.replace(ROOT, "/srv/other/checkout"))
    moved = re.sub(r"nift-certify-\d+", "nift-certify-1", moved)
    check(f"{gate} another root", F.same(base, F.fingerprint(gate, moved, ["/srv/other/checkout"])),
          "; ".join(F.differences(base, F.fingerprint(gate, moved, ["/srv/other/checkout"])))[:300])
    n += 2
    for kind in ("new", "diag", "subst", "count"):
        if row.get(kind) is None:
            continue
        got = F.fingerprint(gate, mutate(text, row[kind], gate, kind), [ROOT])
        check(f"{gate} {kind}", not F.same(base, got), "still equal: the change is not detected")
        n += 1
    if row.get("subst"):
        got = F.fingerprint(gate, mutate(text, row["subst"], gate, "subst"), [ROOT])
        check(f"{gate} subst keeps the count", got is not None and got["count"] == base["count"]
              and sum(got["cases"].values()) == sum(base["cases"].values()), "the substitution changed the count")
    got = F.fingerprint(gate, mutate(text, row["wording"], gate, "wording"), [ROOT])
    check(f"{gate} wording", F.same(base, got), "; ".join(F.differences(base, got))[:300])
    n += 1

# no failing case found: no fingerprint, never equal (never "same step")
check("no case: no fingerprint", F.fingerprint("test-v44-shell-restricted", "PASS a\nmake: *** [x] Error 1\n") is None)
check("no fingerprint never matches", not F.same(None, None))
check("no extractor: no fingerprint", F.fingerprint("test-not-listed", "FAIL a\n") is None)
n += 3

if failures:
    print("FAIL certify fingerprints self-test:\n  " + "\n  ".join(failures))
    sys.exit(1)
print(f"PASS certify fingerprints self-test ({n} checks over {len(TABLE)} expected-baseline gates; "
      f"{len(F.STOPPED)} gates stopped: {', '.join(sorted(F.STOPPED))})")
