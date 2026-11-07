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
    # owner 03:27: the runner keeps rt-diff's report and prints each unapproved difference
    "test-rt-vm": {
        "count_source": "own: rt-diff's `differing` count (CERTIFY-RTDIFF-SUMMARY differing=N)",
        "new": [("CERTIFY-RTDIFF-SUMMARY", 'CERTIFY-RTDIFF semantics/new_case.f stdout {"a": "1", "b": "2", "line": 1}\nCERTIFY-RTDIFF-SUMMARY')],
        "diag": [('"b": "[1,2,99]", "line": 4}', '"b": "[1,2,98]", "line": 4}')],
        "subst": [("CERTIFY-RTDIFF gen/g00002/main.f combined", "CERTIFY-RTDIFF gen/g00003/main.f combined")],
        # vm_parity's category totals (the HEAD-only 'approved B5 Q3') are wording, not failures
        "wording": [("{'approved B5 Q3': 1, ", "{")],
    },
    # owner 03:27: the runner's errtrace (BASH_ENV) names the silent smoke failure
    "test-v44-language-foundation": {
        "count_source": "none: the smoke scripts print no failure count",
        "new": [("make: ***", "tests/v44_ast_constant_fold_smoke.sh\nCERTIFY-ERR v44_ast_constant_fold_smoke.sh:12 rc=1 "
                 "cmd=check x\nmake: ***")],
        "diag": [("rc=1 cmd=", "rc=139 cmd=")],
        "subst": [("v44_ast_expression_smoke.sh:52", "v44_ast_expression_smoke.sh:60")],
        "wording": [("(cwd=", "(directory=")],
    },
    # owner 03:27: the approved read_limit.py case-ID table
    "test-runtime2-host-deps": {
        "count_source": "own: `N failure(s)` (read_limit.py)",
        "new": [("PASS open: file of exactly the limit is read whole", "FAIL open: file of exactly the limit is read whole 99")],
        "diag": [("FAIL open() of a regular file 3 x the limit: fatal open limit error (exit 70)",
                  "FAIL open() of a regular file 3 x the limit: fatal open limit error (exit 71)")],
        "subst": [("FAIL read_line: line of exactly the limit", "FAIL ifstream read_all: exact limit")],
        "wording": [("baseline VmPeak 14 MiB", "baseline VmPeak 15 MiB"),
                    ("FAIL run(yes) never ends: fatal run limit error (exit 1, 0.01s)",
                     "FAIL run(yes) never ends: fatal run limit error (exit 1, 0.04s)")],
    },
    "test-v44-shell-restricted": {
        "count_source": "own: the harnesses' `FAILED: a, b` lines (names counted)",
        "new": [("PASS ctrl-d-exits", "FAIL ctrl-d-exits")],
        "diag": [("FAIL run-captures-not-tty\n", "FAIL run-captures-not-tty\nSegmentation fault\n")],
        "subst": [("FAIL user-function-runs", "FAIL completed-command-runs"),
                  ("PASS completed-command-runs", "PASS user-function-runs")],
        "wording": [("MARK run1", "MARK first"), ("PASS history-up-arrow", "PASS history up-arrow")],
    },
    "test-v46-relative-imports": {
        "count_source": 'none: the smoke script stops at its first error and prints no count',
        "new": [("make: ***", 'NIFT="x" bash tests/v46_import_worker_ownership_smoke.sh\nerror: /tmp/a/tmp.QQ/w.f:2:1: '
                 'not compiled for the VM (import-nested): this form\nmake: ***')],
        "diag": [("(fn-flags)", "(fn-name-reserved)")],
        "subst": [("bash tests/v44_relative_import_ownership_smoke.sh", "bash tests/v46_import_worker_ownership_smoke.sh")],
        "wording": [("note: the program was not executed: no part of it ran", "note: nothing ran"),
                    ("is not supported by the Runtime 2.0 VM yet", "is not supported yet")],
    },
    "test-runtime2-cp03-simplify": {
        "count_source": 'none: the scripts print FAIL lines only',
        "new": [("PASS missing-tracer caller: explicitly unmeasured (77)", "FAIL missing-tracer caller: exit 0 != 77")],
        "diag": [("(callable:module_path)", "(callable:inject)")],
        "subst": [("FAIL CP03 missing-tracer caller", "FAIL CP03 present-tracer caller")],
        "wording": [("note: please report unsupported programs at", "note: report unsupported programs to")],
    },
    "test-sha256": {
        "count_source": 'own: the unit steps\' `sha256 unit: N checks, M failures` lines (the builtins step prints `fail=1`, a flag)',
        "new": [("PASS number rejected", "FAIL number rejected (expected 'error', got '')")],
        "diag": [("FAIL line endings not normalised (expected 'false', got '')",
                  "FAIL line endings not normalised (expected 'false', got 'true')")],
        "subst": [("FAIL NIST one million a", "FAIL NIST two million a")],
        "wording": [("sha256 builtins: 61 passed, fail=1", "sha256 builtins: 61 passed; 1 failed"),
                    ("PASS null rejected", "PASS null is rejected")],
    },
    "test-runtime2-cp01-simplify": {
        "count_source": 'own: `FAILS (N):` (the S scenarios; settle/shared/syscalls lines are counted as failure lines)',
        "new": [("FAILS (3):\n", "FAILS (3):\n  FAIL S5 rebuilds per no-op build: [1, 0]\n")],
        "diag": [("rebuilt ['p1'] expected []", "rebuilt ['p2'] expected []")],
        "subst": [("FAIL S4a create unreferenced file", "FAIL S4b create unreferenced file")],
        "wording": [("note S8a switch modified->hash rebuilt 31", "note S8a rebuilt 32"),
                    ("label=ours cycles=1000", "label=ours cycles: 1000")],
    },
    "test-cp19-s1-dispatch": {
        "count_source": 'own: the `FAILED: a, b` line (names counted)',
        "new": [("PASS rm missing fails", "FAIL rm missing fails")],
        "diag": [("builtin-lazy-args:setenv", "builtin-lazy-args:getenv")],
        "subst": [("FAIL failed user function status non-zero", "FAIL failed user function status zero")],
        "wording": [("PASS rm missing fails", "PASS rm of a missing file fails")],
    },
    "test-rt-parser": {
        "count_source": 'own: `N rt parser unit failure(s)`',
        "new": [("1 rt parser unit failure(s)", "FAIL dump of [x()] lacks 'Call':\n2 rt parser unit failure(s)")],
        "diag": [("'Opaque [legacy-runtime-error]'", "'Opaque [legacy-parse]'")],
        "subst": [("dump of [print(await(f()))]", "dump of [print(await(g()))]")],
        "wording": [("(cwd=", "(directory=")],
    },
    "test-test-integrity": {
        "count_source": 'own: `test-integrity findings: N`',
        "new": [("test-integrity findings: 58", "test-integrity findings: 59\n  [pipefail-missing] tests/new.sh: pipeline "
                 "runs without pipefail provably active")],
        "diag": [("[swallowed-returncode] scripts/certify.py", "[pipefail-missing] scripts/certify.py")],
        "subst": [("scripts/build/check-production-tu.py", "scripts/build/check-production-tv.py")],
        "wording": [('--output ".build/bh2/test-integrity-report.json"', '--output ".build/bh2/report.json"')],
        "count": [("test-integrity findings: 58", "test-integrity findings: 59")],
    },
    "test-v44-packages": {
        "count_source": 'none: the smoke scripts stop at their first error',
        "new": [("PASS package metadata and lock schema", "FAIL package metadata and lock schema: lock missing")],
        "diag": [("(import-nested)", "(import-dynamic)")],
        "subst": [("/usr/bin/python3 tests/package_transaction_smoke.py", "/usr/bin/python3 tests/package_tools_dogfood.py")],
        "wording": [("added demo", "added package demo")],
    },
    "test-rt-host-mutants": {
        "count_source": 'none: host_mutants.sh stops at the first failing mutant',
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
        "count_source": 'none: inventory.py prints one line per unclassified reason, no total',
        "new": [("INVENTORY: unclassified reason: method:write", "INVENTORY: unclassified reason: method:write\n"
                 "INVENTORY: unclassified reason: method:read")],
        "diag": None,  # the unclassified reason is the case id and its whole diagnostic
        "subst": [("method:save", "method:load")],
        "wording": [("classify-all: 789 units", "classify-all: 790 units")],
    },
    "test-runtime2-rt-diff-selftest": {
        "count_source": 'none: the closing `FAIL rt-diff self-tests` has no count',
        "new": [("PASS caught: file-mode (files)", "FAIL caught: file-mode (files)")],
        "diag": [("FAIL D-009 policies (rc=1)", "FAIL D-009 policies (rc=2)")],
        "subst": [("trace op not exercised: touch", "trace op not exercised: chmod")],
        "wording": [("PASS caught: tmp-write (tmp)", "PASS caught tmp-write"),
                    ("FAIL rt-diff self-tests", "FAIL rt-diff self-tests (see above)")],
    },
    "test-runtime2-scanner-parity": {
        "count_source": 'none: the closing line counts compared sites and scripts, not failures',
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

# F1 (review of 3161f52d): a diagnostic printed outside every case block is part of the
# fingerprint: each insertion, before the first case line and after the last block (before make's
# error), must change it on every gate
INSERTIONS = {
    "traceback": "Traceback (most recent call last):\n  File \"x.py\", line 1, in <module>\nRuntimeError: boom",
    "new refusal": "error: /tmp/x/tmp.AB/z.f:1:1: not compiled for the VM (new-refusal): this form is not supported",
    "internal failure": "error: z.f: NIFT_VM internal failure: unexpected operands",
    "crash": "Segmentation fault (core dumped)",
    "C++ assertion": "nift: src/rt/vm.cpp:10: void f(): Assertion `x' failed.",
    "sanitizer": "==123==ERROR: AddressSanitizer: heap-use-after-free on address 0x602000000010",
    "timeout": "subprocess.TimeoutExpired: Command '['nift']' timed out after 30 seconds",
    # C2 (confirmation of C20)
    "uncaught C++ exception": "terminate called after throwing an instance of 'std::bad_alloc'\n  what():  std::bad_alloc",
    "ThreadSanitizer": "WARNING: ThreadSanitizer: data race (pid=4242)",
    "ASan warning": "==7==WARNING: AddressSanitizer: failed to allocate 0x1000 bytes",
    "UBSan": "src/rt/vm.cpp:12:5: runtime error: signed integer overflow: 2147483647 + 1",
    "MemoryError": "MemoryError",
    "dotted exception": "subprocess.CalledProcessError: Command '['nift', 'x.f']' returned non-zero exit status 3.",
    "dotted decoder exception": "json.decoder.JSONDecodeError: Expecting value: line 1 column 1 (char 0)",
}


def positions(gate, text):
    lines = text.splitlines(keepends=True)
    spec = F.SPECS[gate]
    first = next((i for i, l in enumerate(lines) if any(c.match(l.rstrip("\n")) for c in spec.cases)), None)
    if first is None:  # a gate whose failure is a step abort: before its first diagnostic
        first = next(i for i, l in enumerate(lines) if F.matches(l))
    last = max((i for i, l in enumerate(lines) if l.startswith("make: ***")), default=len(lines))
    return {"before the first case": first, "after the last block": last}


for gate in sorted(TABLE):
    text = open(os.path.join(LOGS, gate + ".txt"), errors="replace").read()
    base = F.fingerprint(gate, text, [ROOT])
    lines = text.splitlines(keepends=True)
    for where, at in positions(gate, text).items():
        for kind, ins in INSERTIONS.items():
            got = F.fingerprint(gate, "".join(lines[:at] + [ins + "\n"] + lines[at:]), [ROOT])
            check(f"{gate} {kind} {where}", not F.same(base, got), "a diagnostic outside the cases is invisible")
            n += 1

# category moves the review named: a crash before an existing FAIL line, a traceback before another
for gate, old, new in [
        ("test-v44-shell-restricted", "FAIL run-captures-not-tty", "Segmentation fault\nFAIL run-captures-not-tty"),
        ("test-v44-shell-restricted", "FAIL run-captures-not-tty", "error: x: NIFT_VM internal failure: y\nFAIL run-captures-not-tty"),
        ("test-cp19-s1-dispatch", "FAIL failed user function status non-zero",
         "Traceback (most recent call last):\nRuntimeError: x\nFAIL failed user function status non-zero")]:
    text = open(os.path.join(LOGS, gate + ".txt"), errors="replace").read()
    check(f"{gate} category move", not F.same(F.fingerprint(gate, text, [ROOT]),
                                             F.fingerprint(gate, mutate(text, [(old, new)], gate, "move"), [ROOT])))
    n += 1

# F5: the failure count is the gate's own where it prints one; the table states which gates do not
for gate, row in sorted(TABLE.items()):
    has = F.SPECS[gate].count is not None
    check(f"{gate} count source stated", isinstance(row.get("count_source"), str) and row["count_source"].startswith(
        "own: " if has else "none: "), "count_source must say `own: <line>` or `none: <why>` and match the spec")
    if has:
        text = open(os.path.join(LOGS, gate + ".txt"), errors="replace").read()
        check(f"{gate} own count found", F.fingerprint(gate, text, [ROOT])["count"] is not None)
        n += 1

# owner 03:27: a read_limit.py failure that maps to no table entry (or to several) blocks: its
# fingerprint never equals anything, not even itself
text = open(os.path.join(LOGS, "test-runtime2-host-deps.txt"), errors="replace").read()
unmapped = text.replace("FAIL default limit: open_bytes(/dev/zero) stops at 1073741824 bytes",
                        "FAIL a check nobody listed: it failed", 1)
fp = F.fingerprint("test-runtime2-host-deps", unmapped, [ROOT])
check("host-deps unmapped failure blocks", fp is not None and not F.same(fp, fp)
      and any("unmapped failure (blocks)" in d for d in F.differences(fp, fp)))
check("host-deps every real failure maps", not any(c[1] == "UNMAPPED" for c in
      F.fingerprint("test-runtime2-host-deps", text, [ROOT])["cases"]))
n += 2

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
