#!/usr/bin/env python3
"""Runtime 2.0 checkpoint certification runner (D-010, docs/runtime-2/certification.md).

Expands the `certify-targets` aggregate into its leaf make targets, runs the
parallel-safe ones concurrently and the exclusive ones serially, re-runs every
failure alone, and classifies each target:

  PASS                  passed on the first run
  FLAKY                 failed in the concurrent run, passed alone (load-sensitive);
                        reported, never counted as a clean pass
  SKIPPED-ENVIRONMENTAL failed only because of the execution environment, shown by
                        re-running: e.g. fails as root but passes as an
                        unprivileged user
  UNVERIFIED            a dependency or platform is unavailable here (detected from
                        the environment AND the log: missing tool, blocked toolchain
                        download, missing sibling checkout); CI covers these
  BASELINE-FAILURE      fails on the v4.12.0 baseline tree at the same failing step,
                        run as the same user as the candidate result it is
                        compared with (pre-existing, not a Runtime 2.0 regression)
  FAIL                  anything else, including a baseline failure at a different
                        or unidentified step

The policy is the pure function decide(), table-tested in
tests/runtime2/certification_guard.sh.

Combined-candidate harness (docs/runtime-2/checkpoints/combined-b3-b5.md, owner 2026-11-07
18:00; C1-C3):

  C1 the plan is certify-targets PLUS the Runtime 2.0 inventory derived from the Make database
     (inventory(): every defined target named like a Runtime 2.0 gate), minus NON_GATE (each with
     its reason); MANDATORY lists the owner-named B3-B5 gates, which must be planned. --check-plan
     fails (exit 1) when an inventory target is neither planned nor in NON_GATE, a NON_GATE entry
     is not a defined target, or a MANDATORY gate is not planned; --drop T removes T from the plan
     first (the guard's negative control).
  C2 an umbrella (a target with a recipe AND gate prerequisites) is expanded: each gate
     prerequisite is its own entry, and the umbrella's own recipe is an entry run with its gate
     prerequisites marked old (`make -k T -o P ...`), after the others. Every entry runs with
     `make -k`, so a known failure stops nothing else. Each required gate is reported in one class:
       PASSED           passed (PASS)
       FAILED-NEW       failed, and not shown identical on a named baseline (FAIL, FLAKY: the
                        zero-flakiness policy counts a flaky gate as failed)
       FAILED-BASELINE  the identical failure (same step, same FAIL lines) on a named baseline:
                        v4.12.0 (--baseline, legacy compatibility) or a regression baseline
                        (--regression-baseline NAME=TREE: the accepted B4b 17f7964c and weekend/r2),
                        reported per baseline and never merged
       BLOCKED          a dependency or the harness is missing (UNVERIFIED, SKIPPED-ENVIRONMENTAL, a
                        harness-blocked log: no CP19 legacy oracle, comparator coverage skipped)
       NOT-RUN          planned and not run
     Only PASSED is a pass. Approved behavioural differences are not a runner baseline: they are
     the B5 expected-output table (docs/runtime-2/evidence/b5/expected-changes.tsv), checked by the
     gates themselves.
  C3 preflight (preflight_problems(), table-tested): GCC 15.2, __GLIBCXX__ == 20260321, comparator
     sort compiled into the production binary (a comparator sort runs on the VM),
     NIFT_CP14B_REQUIRE_COMPARATOR=1, and an explicit v4.12.0 legacy binary (--legacy-oracle or
     CP19_LEGACY_ORACLE; it must report v4.12.0) for test-cp19-proof-cli. A full campaign with a
     preflight problem exits 2 before running anything; any comparator gate that reports SKIP
     fails the campaign.

Campaign verdict (owner 2026-11-08 06:46; campaign_verdict()), one exit status each:
  FAILED     exit 1: a FAILED-NEW gate, comparator coverage skipped, a gate NOT-RUN, SKIP or
             BLOCKED by anything other than required checks that did not execute, or (--strict)
             FAILED-BASELINE;
  PARTIAL    exit 4: nothing failed, every executed assertion passed or is FAILED-BASELINE, and the
             only gaps are REQUIRED checks that did not execute (a gate that passed with a required
             skip, checks an early abort hid). Never a pass; each unexecuted check is listed with
             its category (scripts/certify_hidden_checks.tsv: A release-critical, B unrelated
             historical; no row reads as A), and any A makes the report "NOT RELEASE-QUALIFYING";
  CERTIFIED  exit 0: every gate PASSED or FAILED-BASELINE (--strict: only PASSED), nothing hidden.
Exit 2: preflight failed (full campaign), nothing planned, or the category table is unusable.
Logs: .build/certify/<target>.log; summary: .build/certify/summary.json and summary.txt.
"""
import argparse, concurrent.futures as cf, json, os, pwd, re, shutil, signal, subprocess, sys, time
from pathlib import Path

import certify_fingerprints as FP  # owner 2026-11-08 01:06: per-gate failure fingerprints

ROOT = Path(__file__).resolve().parent.parent
AGGREGATE = "certify-targets"

# Targets that must not run concurrently with others. A target is also treated
# as exclusive automatically when its recipe runs a sub-make (shared build
# outputs) — see recipe_runs_make().
EXCLUSIVE = {
    "test-runtime2-cp18-sanitize": "compiles both VM dispatches and runs allocation-failure controls",
    "test-build-boundary-nondestructive": "snapshots the whole checkout before/after; any concurrent writer is reported as a modification",
    "test-build-boundary": "builds from a clean copy of the checkout; reads the tree while others write it",
    # Isolation audit (docs/runtime-2/certification.md): these recipes write
    # shared outputs outside .build/ (embedding objects, libnift_c.a, the
    # staged dist/embed-prefix, binding build products) that other targets
    # also build or read.
    "test-cp18-bytes": "rebuilds src/embed/*.o in the source tree",
    "test-cp21-bytes": "rebuilds libnift_c.a in the source tree",
    "test-v45-embed-staged-consumer": "restages dist/embed-prefix (shared with the binding targets)",
    "test-go-binding": "restages dist/embed-prefix; builds bindings/go products",
    "test-csharp-binding": "restages dist/embed-prefix; builds bindings/csharp products",
    "test-node-binding": "builds bindings/node/build (shared native module)",
    "test-python-binding": "builds the bindings/python extension module",
    "test-minify": "writes minifypp/.build via a sub-make",
    # Tooling checkpoint (docs/runtime-2/checkpoints/tooling.md 1, 3): these
    # rebuild the shared in-tree objects under other configurations, or plant
    # reproducers into production sources, through a script (no make on the
    # recipe line, so recipe_runs_make() does not see it).
    "test-build-reuse": "rebuilds the shared in-tree objects under other compilers, flags and defines (A->B->A)",
    "test-rt-isolation": "plants reproducers into production sources and rebuilds their objects",
    "test-rt-sort-forgery": "plants the forged-header reproducer into src/Hooks.cpp and relinks nift",
}

# Load-sensitive measurements (owner review of D-012): targets whose pass/fail
# depends on wall-clock upper bounds. Their thresholds are unchanged; they run
# serially, with no competing target, so that a failure reflects the product and
# not the scheduler. A pass under exclusive execution is the evidence; an
# intermittent failure is never recorded as resolved by a later passing run.
LOAD_SENSITIVE = {
    "test-v44-language-foundation": "tests/v44_perf_scaling_smoke.sh: doubling n must not quadruple wall time (map/set/push)",
    "test-v48-map-set-scaling": "tests/v48_map_set_scaling_guard.sh: wall-clock scaling ratio",
    "test-scripting-perf": "tests/scripting_perf_guard.sh: wall-clock budgets",
    "test-runtime2-process-capture": "process_capture.sh: background descendant must not block (<280 ms)",
    "test-load-sensitive": "the four wall-clock scripts above, serially (CI's exclusive step)",
    "test-v43-language": "benchmarks/*_scaling.py: wall-clock scaling ratios (hierarchy, typed content, project dependencies) after the v4.3 smokes",
}
for _t, _why in LOAD_SENSITIVE.items():
    EXCLUSIVE.setdefault(_t, "load-sensitive: " + _why)

# Implementation-time tier (--quick): fast, broad smoke of the runtime and the
# Runtime 2.0 gates. Checkpoint certification always runs the full set.
QUICK = [
    "test-runtime2-certification-guard", "test-runtime2-vm-corpus", "test-runtime2-modified-race",
    "test-runtime2-dynamic-reads", "test-runtime2-fs-freedom", "test-conformance", "test-contracts",
    "test-requirements", "test-dependency-status", "test-call-semantics", "test-control-flow",
]

TEST_NAME = re.compile(r"^(test|bh|checkpoint-[0-9])[A-Za-z0-9_.-]*$")

# C1: the Runtime 2.0 gate inventory is derived from the Make database, not listed by hand: every
# defined target whose name starts with test-, cp25r or vm-, contains runtime2, -rt, cp or vm, and
# is not a 4.x-line test-v4* target (the B5 coverage reconciliation's rule, b5-refsem.md 11.10;
# 244 targets at fc987ec1). A new target that matches is required automatically.
INVENTORY_NAME = re.compile(r"^(test-|cp25r|vm-)")
INVENTORY_PART = re.compile(r"runtime2|-rt|cp|vm")
# Inventory targets that are not gates, each with its reason. Kept short; the guard fails on a
# stale entry.
NON_GATE = {
    "test-runtime2-closures-s1-prepare": "maintenance mode (--prepare): regenerates the CP12C S1 suite's inputs",
    "test-runtime2-closures-s1-stress-prepare": "maintenance mode (--prepare): regenerates the S1 stress inputs",
}
# The owner-named B3-B5 gates (REVIEW-COMBINED.md C1, LINODE-GATES.tsv, owner 2026-11-07 18:00)
# and the combined-candidate gates; each must be planned (checked by --check-plan).
MANDATORY = [
    "test-runtime2-b3", "test-runtime2-b3-mutants", "test-runtime2-b4", "test-runtime2-b4-mutants",
    "test-runtime2-b4b", "test-runtime2-b4b-mutants", "test-runtime2-b4b-fixpoint", "test-runtime2-b4b-work",
    "test-rt-b4b-types-unit", "test-runtime2-b4b-gen-alias",
    "test-runtime2-b5", "test-runtime2-b5-mutants", "test-runtime2-b5-generators", "test-runtime2-b5-evalorder",
    "test-runtime2-b5-evalorder-sanitize", "test-runtime2-b5-snapshot-memory",
    "test-runtime2-guardoff", "test-runtime2-guardoff-sanitize", "test-cp19-proof-cli",
    "test-runtime2-cp14b-design-mutants", "test-runtime2-diagnostics", "test-runtime2-scanner-parity",
    "test-runtime2-prevalidate-gate", "test-runtime2-capture-origins", "test-runtime2-cp12c-probes",
    "test-runtime2-refsem", "test-runtime2-refsem-mutants", "test-runtime2-cp22", "test-runtime2-cp23",
    "test-rt-vm", "test-rt-vm-sanitize", "test-runtime2-certification-guard",
]
# Wall-clock targets of the inventory: run alone, like LOAD_SENSITIVE (Linode performance lane).
PERF_LANE = {
    "test-cp20-launch-bench": "wall-clock launch benchmark",
    "test-cp21-prompt-latency": "wall-clock prompt latency",
    "test-rt-builtins-guards": "wall-clock builtin guard budgets",
    "test-runtime2-for-s2-bench": "wall-clock snapshot-loop benchmark",
    "test-runtime2-literals-scaling": "wall-clock literal scaling",
}
for _t, _why in PERF_LANE.items():
    EXCLUSIVE.setdefault(_t, "performance lane: " + _why)

# C2: log evidence that a gate was blocked by its harness or a missing dependency (BLOCKED, never a
# pass and never a candidate failure). COMPARATOR_SKIP also fails the campaign (C3).
COMPARATOR_SKIP = "SKIP UNVERIFIED COMPARATOR COVERAGE"
HARNESS_BLOCKED = [
    (re.compile(r"CP19_LEGACY_ORACLE is unset|CP19_LEGACY_ORACLE=\S* is not an executable"),
     "no CP19 legacy oracle (v4.12.0 binary): pass --legacy-oracle"),
    (re.compile(re.escape(COMPARATOR_SKIP)), "comparator coverage skipped: not GCC 15.2 with __GLIBCXX__ 20260321"),
    (re.compile(r"NIFT_REF must name a reference nift binary"),
     "no scanner-parity reference binary (NIFT_REF; the campaign passes the v4.12.0 oracle)"),
]


def make_db(cwd, target):
    out = subprocess.run(["make", "-pqn", target], cwd=cwd, capture_output=True, text=True,
                         env=clean_env()).stdout.split("\n")
    deps, recipe = {}, {}
    cur, in_recipe = None, False
    for line in out:
        m = re.match(r"^([A-Za-z0-9_./+-]+):(?![=:])\s*(.*)$", line)
        if m and not line.startswith("#"):
            cur, in_recipe = m.group(1), False
            deps.setdefault(cur, []).extend(m.group(2).split())
            recipe.setdefault(cur, "")
            continue
        if cur and line.startswith("#  recipe to execute"):
            in_recipe = True
            continue
        if cur and in_recipe and line.startswith("\t"):
            recipe[cur] += line[1:] + "\n"
            continue
        if line == "":
            cur, in_recipe = None, False
    return deps, recipe


def leaves(deps, recipe, root):
    out, seen = [], set()

    def walk(t):
        if t in seen:
            return
        seen.add(t)
        if not recipe.get(t) and deps.get(t):
            for d in deps[t]:
                walk(d)
        elif TEST_NAME.match(t) or t.endswith("-binding"):
            out.append(t)
    walk(root)
    return out


def inventory(deps):
    """C1: the Runtime 2.0 gates defined in the Make database (the derivation rule above)."""
    return sorted(t for t in deps if INVENTORY_NAME.match(t) and INVENTORY_PART.search(t)
                  and not t.startswith("test-v4"))


def plan(deps, recipe, roots):
    """C2: the entries of `roots` in Make's prerequisite order: (target, kind, old). kind "leaf" is a
    gate with a recipe and no gate prerequisites; kind "own" is an umbrella's own recipe, run with
    its gate prerequisites `old` (make -o), which are entries of their own. Also returns every
    target visited (aggregates included), for the inventory check."""
    entries, seen, rootset = [], set(), set(roots)

    def gate(d):
        return TEST_NAME.match(d) or d.endswith("-binding")

    def walk(t):
        if t in seen:
            return
        seen.add(t)
        sub = [d for d in deps.get(t, []) if gate(d)]
        for d in (sub if recipe.get(t) else deps.get(t, [])):  # an aggregate: every prerequisite
            walk(d)
        if recipe.get(t) and (gate(t) or t in rootset):
            entries.append((t, "own" if sub else "leaf", tuple(dict.fromkeys(sub))))
    for r in roots:
        walk(r)
    return entries, seen


def inventory_problems(covered, inv, non_gate, mandatory, defined):
    """C1, pure (table-tested by the guard): every inventory target is planned (covered) or a
    named non-gate; every non-gate is a defined target; every mandatory gate is planned."""
    out = []
    for t in inv:
        if t not in covered and t not in non_gate:
            out.append(f"inventory target {t} is not in the certification plan (plan it, or list it in NON_GATE with a reason)")
    for t in sorted(non_gate):
        if t not in defined:
            out.append(f"NON_GATE entry {t} is not a defined Make target (stale)")
        elif t in covered:
            out.append(f"NON_GATE entry {t} is planned anyway (remove it from NON_GATE)")
    for t in mandatory:
        if t not in defined:
            out.append(f"mandatory gate {t} is not a defined Make target")
        elif t not in covered:
            out.append(f"mandatory gate {t} is not in the certification plan")
    return out


# D3 (owner 2026-11-07 22:13): each gate's assertions are PASS, PARTIAL (some required assertion
# skipped), SKIP (every assertion skipped), FAIL or BLOCKED. Skips are found mechanically in the
# log; KNOWN_CONDITIONAL names the conditional checks the tree prints, with each one's requirement
# and whether it is required on a Linux certification host (platform checks of other systems are
# not; they belong to their CI lanes). An unknown skip is required (conservative).
# R3-1 (confirmation round 3): case-insensitive skip / not-run / unverified wording, TAP `# SKIP`
# and pytest progress; explicit zero counts are not skips. tests/runtime2/skip_phrases.tsv lists
# every such message the tree prints; the guard fails on an unlisted one.
SKIP_MARKERS = [re.compile(r"\bskip(?:ped|ping|s)?\b(?![-_\w])", re.I), re.compile(r"\bNOT[ -]RUN\b"),
                re.compile(r"\bunverified\b", re.I), re.compile(r"\bnot verified\b", re.I),
                re.compile(r"^\s*[.sxFE]*s[.sxFE]*\s*(\[\s*\d+%\])?\s*$")]
ZERO_SKIPS = re.compile(r"\b0\s+(?:[\w-]+\s+)?skip(?:ped|s)?\b|\bskip(?:ped|s)?\s*[:=]?\s*0\b(?!\.\d)", re.I)
KNOWN_CONDITIONAL = [  # (pattern, requirement, required on the Linux campaign host)
    (r"default 1 GiB limit \(MemAvailable", "MemAvailable >= 4096 MiB while tests/runtime2/read_limit.py runs", True),
    # R4-1 (round 4): printed on every run; the entries are the rt-diff ignore list's known
    # nondeterministic outputs (tools/rt-diff/ignore.txt), excluded by design, not a skipped check
    (r"^paren oracle: skipped \d+ nondeterministic entries \(tools/rt-diff/ignore\.txt\)$", "none: tools/rt-diff/ignore.txt lists them by design", False),
    # R4-1 local classification pass (combined-b3-b5.md 15.1): lines of passing gates whose wording
    # is not a skip, and the conditional checks they revealed
    (r"^ok: B1 \w+: status --strict lists unverified pages$", "none: product wording (status --strict's unverified pages), an assertion that ran", False),
    (r"^PASS (skipped (&&|\|\||pipeline) interpolation|syntax validates skipped segment)$", "none: a test case named after the shell branch it skips, which ran", False),
    (r"^PASS mutant \d+ \((?:[^()]|\([^()]*\))*\): \d+ failing checks?, witness \S+:?$", "none: a mutant's description (the mutant was caught)", False),
    (r"^SKIPPED - IPv6 ::1 not available on this host$|^serve_smoke: \d+ passed, [1-9]\d* skipped", "IPv6 on loopback (::1) for test-serve", True),
    (r"^UNVERIFIED (\d+ )?[\w-]+$|^UNVERIFIED \d+ D4 cases refused with method:sort", "GCC 15.2 comparator profile (cp14b cases and mutants unverified elsewhere)", True),
    # R4-1: the unreadable-file check needs a non-root run: required when not root; as root the
    # runner re-runs the gate as the unprivileged user (ROOT_ONLY_SKIPS)
    (r"^note: running as root; unreadable-file check skipped \(root bypasses permissions\)$", "a non-root run (the runner re-runs as the unprivileged user)", "when-not-root"),
    (r"UNVERIFIED COMPARATOR COVERAGE", "GCC 15.2 with __GLIBCXX__ 20260321 (comparator sort compiled in)", True),
    (r"^SKIP comparator refusal: admitted libstdc\+\+ profile \S+ \(the production VM runs it\)$", "a toolchain that refuses comparator sort (checked on the other-toolchain lane; n/a on GCC 15.2)", False),
    (r"root without setpriv|needs Linux setpriv", "setpriv (util-linux) when running as root", True),
    (r"tracer unavailable|strace unavailable|syscalls: tracer", "strace", True),
    (r"/usr/bin/time (is )?unavailable|requires /usr/bin/time", "GNU time at /usr/bin/time", True),
    (r"git unavailable", "git", True),
    (r"python3 (not available|unavailable)", "python3", True),
    (r"/dev/shm missing or on the same filesystem", "/dev/shm on a separate (tmpfs) filesystem", True),
    (r"cannot create symlinks|symlinks unavailable", "a filesystem that supports symlinks", True),
    (r"/proc/self/mem|read-failure (fixture|cases) (is |are )?unavailable|needs /proc", "/proc with /proc/self/mem", True),
    (r"cc unavailable|peak tracker: build failed", "a C compiler (cc)", True),
    (r"no head\)|needs /proc, yes, head, sh", "coreutils (head, yes, sh)", True),
    (r"^SKIP \[modified\] edit during streaming is outside the v4\.12 modified-mode contract$", "none: a documented design boundary (modified mode), not an assertion", False),
    # R3-5: the other-platform messages, anchored to the exact texts the tree prints
    (r"^SKIP CP20 Windows pipeline \(POSIX host\)$", "Windows (its CI lane)", False),
    (r"^SKIP v4\.4 interactive completion PTY \(Windows uses line-based input\)$", "Windows (its CI lane)", False),
    (r"^SKIP v4\.4 shell foreground TTY \(POSIX PTY test; macOS foreground execution hangs\)$", "macOS/Windows (their CI lanes)", False),
    (r"^SKIP v4\.4 shell glob \(Windows passthrough pending glob backend\)$", "Windows (its CI lane)", False),
    (r"^SKIP CP19 completion \(POSIX PTY test\)$", "Windows (its CI lane)", False),
    (r"^SKIP shell dispatch \(POSIX pty/coreutils regression\)$", "Windows (its CI lane)", False),
    (r"^SKIP unreadable file \(POSIX permission bits do not apply on Windows\)$", "Windows (its CI lane)", False),
    (r"^SKIP \[\w+\] trace descriptor inheritance under MSYS$", "MSYS (its CI lane)", False),
    (r"^SKIP: serve_smoke\.sh runs on Linux \(full\) and macOS \(lifecycle core\); Windows uses tests/serve_windows_lifecycle\.ps1$", "Windows (its CI lane)", False),
]


def skipped_assertions(text, echoed=()):
    """D3: [(line, requirement, required)] for every skip marker in a log. `echoed`: make's echo of
    the recipe (R4-1 local pass: a recipe that contains `echo "... skipped ..."` is not a skip
    until the gate prints it)."""
    out = []
    echoed = {e.strip() for e in echoed}
    for line in text.splitlines():
        if line.strip() in echoed:
            continue
        if not any(m.search(line) for m in SKIP_MARKERS):
            continue
        if ZERO_SKIPS.search(line) and not any(m.search(ZERO_SKIPS.sub("", line)) for m in SKIP_MARKERS):
            continue  # "0 skipped", "skipped: 0"
        req, required = "unknown condition (not in KNOWN_CONDITIONAL)", True
        for pat, why, needed in KNOWN_CONDITIONAL:
            if re.search(pat, line.strip()):
                req, required = why, needed
                # R5-3: an entry that excuses (not required) covers only its own exact text
                rest = re.sub(pat, "", line.strip())
                if needed is not True and any(m.search(rest) for m in SKIP_MARKERS):
                    req, required = "unknown condition (text beyond a KNOWN_CONDITIONAL entry)", True
                break
        if required == "when-not-root":
            required = os.geteuid() != 0
        out.append((line.strip()[:200], req, required))
    return out


# R3-4: gates that are silent by design (they print nothing of their own when they pass), each with
# its reason. Any other gate that exits 0 without output of its own is SKIP (no assertion shown).
SILENT_BY_DESIGN = {
}
_MAKE_NOISE = re.compile(r"^(\$ make |make(\[\d+\])?: (Entering|Leaving|Nothing to be done|'.*' is up to date))")


def recipe_echo(cwd, target, old=()):
    """R4-5: the lines make echoes for a recipe, as printed (a continued command spans lines)."""
    out = subprocess.run(["make", "-n", target] + [x for p in old for x in ("-o", p)], cwd=cwd,
                         capture_output=True, text=True, env=clean_env()).stdout
    return [l.strip() for l in out.splitlines() if l.strip()]


def own_output(text, commands=()):
    """R3-4: the lines a gate printed itself: not the runner's header, make's own messages, or the
    recipe commands make echoes (R4-5: each echoed line, continuations included)."""
    echoed = {c.strip() for c in commands}
    return [l for l in text.splitlines() if l.strip() and not _MAKE_NOISE.match(l) and l.strip() not in echoed]


def assertion_class(rc, text, blocked=None, silent=False, skipped=None):
    """D3, pure (table-tested): PASS / PARTIAL / SKIP / FAIL / BLOCKED of one gate's assertions.
    PARTIAL: it passed, but a required assertion was skipped; SKIP: every result line is a skip, or
    (R3-4) it printed nothing of its own (`silent`) and is not silent by design."""
    if blocked:
        return "BLOCKED"
    if rc != 0:
        return "FAIL"
    if silent:
        return "SKIP"
    req = [x for x in (skipped if skipped is not None else skipped_assertions(text)) if x[2]]
    if not req:
        return "PASS"
    ran = any(re.search(r"^\s*(PASS|ok)\b|\bpassed\b", l) for l in text.splitlines())
    return "PARTIAL" if ran else "SKIP"


def skip_outcome(skipped, silent=False):
    """Owner 23:14: one of four distinct outcomes. `zero-skipped`; `inapplicable-only` (every skipped
    line is an explicitly inapplicable check: another platform, or root-only and run as the
    unprivileged user); `mandatory-skipped` (at least one required check skipped; a
    KNOWN_CONDITIONAL entry excuses only its own line); `not-executed` (no output of its own)."""
    if silent:
        return "not-executed"
    if not skipped:
        return "zero-skipped"
    return "mandatory-skipped" if any(req for _, _, req in skipped) else "inapplicable-only"


def report_class(status, regression=None, blocked=None, assertion=None):
    """C2, pure (table-tested): the owner's report class of one required gate. `status` is
    decide()'s verdict (or None: not run); `regression` maps each regression baseline to whether it
    shows the identical failure; `blocked` is a harness/dependency reason from the log."""
    if status is None or assertion == "SKIP":
        return "NOT-RUN"  # D3: SKIP maps to NOT-RUN
    if status == "PASS" and assertion == "PARTIAL":
        return "BLOCKED"  # D3: PARTIAL maps to BLOCKED (a required assertion did not execute)
    if status == "PASS" and not blocked:
        return "PASSED"
    if blocked or status in ("UNVERIFIED", "SKIPPED-ENVIRONMENTAL"):
        return "BLOCKED"
    if status in ("FAIL", "BASELINE-FAILURE"):
        # F2 (decision 2026-11-08): pre-existing for B3-B5 means equal on EVERY applicable regression
        # baseline (None = not applicable there); a passing or differing one makes it new. Only when
        # no regression baseline applies does an equal v4.12.0 fingerprint count (legacy-only).
        applicable = {n: v for n, v in (regression or {}).items() if v is not None}
        if applicable:
            return "FAILED-BASELINE" if all(applicable.values()) else "FAILED-NEW"
        if status == "BASELINE-FAILURE":
            return "FAILED-BASELINE"
    return "FAILED-NEW"  # FAIL, and FLAKY under the zero-flakiness policy


_FAIL_LINE = re.compile(r"^\s*(FAIL|not ok)\b")


def fail_lines(text, roots=()):
    """The failure lines of a log, with checkout and temporary paths normalised: with the failing
    step, the identity of a failure on two trees."""
    out = set()
    for line in text.splitlines():
        if _FAIL_LINE.match(line):
            for r in roots:
                line = line.replace(str(r), "<ROOT>")
            out.add(re.sub(r"/tmp/\S+", "<TMP>", line.strip()))
    return out


_MAKE_ERR = re.compile(r"^make(?:\[\d+\])?: \*\*\* \[[^\]]*\] (.*)$")
_SIGNAL = re.compile(r"Segmentation fault|Aborted|Killed|Bus error|core dumped|Floating point exception")
_TIMING = re.compile(r"^\s*(real|user|sys)\s")


_VARYING = [  # F1 (confirmation of c73a7692): values that vary between two runs of the same failure
    (re.compile(r"/tmp/\S+"), "<TMP>"),
    (re.compile(r"\b\d{4}-\d\d-\d\d[T ]\d\d:\d\d(:\d\d(\.\d+)?)?\S*"), "<TIMESTAMP>"),
    (re.compile(r"\b\d\d:\d\d:\d\d(\.\d+)?\b"), "<CLOCK>"),
    (re.compile(r"\b\d+(\.\d+)?\s?(ns|us|\u00b5s|ms|sec|secs|seconds)\b|\b\d+\.\d+\s?s\b"), "<DURATION>"),
    (re.compile(r'("[A-Za-z_]*_(us|ms|ns|seconds)"\s*:\s*)[\d.]+'), r"\1<DURATION>"),
    (re.compile(r"\bpid\s*[=: ]\s*\d+", re.I), "pid <PID>"),
    # R3-3: addresses only in pointer contexts (sanitizer frames, "at"/"address"/"ptr=", or a
    # pointer-width 12+ digit value); data such as `expected 0xdeadbeef` is kept
    (re.compile(r"(?i)(\bat |\baddress |\bptr=|^\s*#\d+\s+)0x[0-9a-f]{8,}\b"), r"\1<ADDR>"),
    (re.compile(r"\b0x[0-9a-fA-F]{12,}\b"), "<ADDR>"),
]


def normalise(line, roots=()):
    """F1: one normalisation for FAIL, detail and summary lines: checkout roots, temporary paths,
    timestamps, durations, process ids and addresses. Counts are kept (`30 differing` vs `31`)."""
    for r in roots:
        if str(r):
            line = line.replace(str(r), "<ROOT>")
    for pat, rep in _VARYING:
        line = pat.sub(rep, line)
    return line.strip()


def failure_identity(text, roots=(), rc=None):
    """H2 (review of 0d853db4), F1 (confirmation of c73a7692): what makes two failures the same
    failure: the failing step (or None), the exit statuses and signals (make's `Error N`, a signal
    text, or the command's own rc), the normalised FAIL lines, AND the normalised last lines of the
    log (the detail and summary lines under a FAIL line: `bad case: ...`, `30 differing`), which
    always count. None when there is nothing to compare: never identical."""
    lines = text.splitlines()
    codes = tuple(m.group(1).strip() for m in map(_MAKE_ERR.match, lines) if m)
    sigs = tuple(sorted(set(_SIGNAL.findall(text))))
    fails = frozenset(normalise(l, roots) for l in lines if _FAIL_LINE.match(l))
    # R3-2: the whole normalised output of the failing step (from the line that starts it, the last
    # mention of the step before make's error, to the end), not a fixed tail; without an
    # identified step, the whole log.
    # R4-2 (round 4): the whole log, not from a mention of the step (a FAIL line naming a test
    # path moved that start past earlier differing lines); earlier passing steps are deterministic
    # output too, and a varying value there shows as FAILED-NEW (the conservative side).
    body = [normalise(l, roots) for l in lines
            if l.strip() and not l.startswith(("make", "$ ")) and not _TIMING.match(l)]
    tail = tuple(body)
    if not fails and not tail:
        return None
    return (failure_signature(text), codes or (rc,), sigs, fails, tail)


def lacks_target(text, target):
    """C1 (confirmation of C20): make's own message that the Makefile has no rule for the gate itself
    (`No rule to make target 'T'.`, with `  Stop.` without -k), not for a prerequisite
    (`No rule to make target 'P', needed by 'T'.`)."""
    return re.search(r"^make: \*\*\* No rule to make target '" + re.escape(target) + r"'\.(?:  Stop\.)?$", text, re.M) is not None


def same_failure(a, b):
    return a is not None and b is not None and a == b


def recipe_lines(cwd, target, old=()):
    """C2: the commands `make -k target -o old...` would run, one per recipe line (continuations
    joined), from `make -n`."""
    out = subprocess.run(["make", "-n", target] + [x for p in old for x in ("-o", p)], cwd=cwd,
                         capture_output=True, text=True, env=clean_env()).stdout
    lines, cur = [], ""
    for line in out.splitlines():
        cur += line[:-1] + " " if line.endswith("\\") else line
        if not line.endswith("\\"):
            if cur.strip():
                lines.append(cur)
            cur = ""
    return lines


def run_lines(cwd, target, old, logdir, timeout, extra_env=None, tag=""):
    """C2: a failing recipe stops at its first failing line, hiding the rest: run every line on its
    own (the target's prerequisites are built) and return [(command, rc, failure fingerprint)]."""
    results = []
    env = clean_env()
    env.update(extra_env or {})
    env.update(runner_env(target, cwd, logdir / f"{target}{tag}.lines"))
    for i, cmd in enumerate(recipe_lines(cwd, target, old), 1):
        log = logdir / f"{target}{tag}.line{i}.log"
        with open(log, "w") as f:
            f.write(f"$ {cmd}\n")
            f.flush()
            try:
                rc = subprocess.run(["bash", "-c", cmd], cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=env,
                                    timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                rc = 124
        # owner 01:06: each failing line's own fingerprint (same extractor on both trees)
        results.append((cmd, rc, FP.fingerprint(target, log.read_text(errors="replace"), [cwd], rc) if rc else None))
    return results


def lines_match(cand, base, croot, broot):
    """H2a: every recipe line that fails on its own on the candidate fails identically on the
    baseline (same command, equal failure fingerprints; owner 01:06). Returns the candidate lines
    that do not (a line without an extractable fingerprint never matches)."""
    norm = lambda c, r: c.replace(str(r), "<ROOT>")
    have = {norm(c, broot): fp for c, rc, fp in base if rc}
    return [i for i, (c, rc, fp) in enumerate(cand, 1)
            if rc and not FP.same(fp, have.get(norm(c, croot)))]


def blocked_reason(text):
    for pat, why in HARNESS_BLOCKED:
        if pat.search(text):
            return why
    return None


def preflight_problems(f):
    """C3, pure (table-tested): the campaign's toolchain and oracle requirements."""
    out = []
    if "15.2" not in (f.get("cxx") or "") or "clang" in (f.get("cxx") or "").lower():
        out.append(f"GCC 15.2 required for comparator-sort certification (found: {f.get('cxx') or 'no compiler'})")
    if f.get("glibcxx") != 20260321:
        out.append(f"__GLIBCXX__ must be 20260321, the value the comparator-sort gate compiles for (found: {f.get('glibcxx')})")
    # R4-3 (round 4): the certified binary is the one this campaign built, with the profiled
    # compiler and flags (.build/obj-config/default.flags is the build's own signature)
    if f.get("build_rc") not in (None, 0):
        out.append(f"make nift failed (exit {f['build_rc']}): the binary would not be this campaign's build")
    b = f.get("build_sig")
    if b is not None:
        if b.get("resolved") != f.get("cxx_resolved") or b.get("version-sha256") != f.get("cxx_version_sha256"):
            out.append(f"./nift was built by another compiler ({b.get('resolved')}, version {str(b.get('version-sha256'))[:12]}) "
                       f"than $CXX ({f.get('cxx_resolved')}, version {str(f.get('cxx_version_sha256'))[:12]})")
        want_flags = ((f.get("profile") or {}).get("flags") or "").split()
        missing = [x for x in want_flags if x not in b.get("args", [])]
        if missing:
            out.append(f"./nift's build lacks the profile flags {missing}")
        # F3 (review of 3161f52d): the C++ flag portion (all but -include FILE and -I...) is the
        # profile's flags exactly, checked directly, not only through make's own derivation
        a = b.get("args", [])
        flagpart = [x for i, x in enumerate(a) if x != "-include" and not (i and a[i - 1] == "-include")
                    and not x.startswith("-I")]
        if want_flags and flagpart != want_flags:
            out.append(f"./nift's C++ flags {flagpart} are not exactly the profile's {want_flags}")
        # R5-2 (round 5): exactly the Makefile's own arguments with the profile flags, nothing more
        exp = f.get("expected_args")
        if exp is not None and b.get("args") != exp:
            extra = [x for x in b.get("args", []) if x not in exp]
            out.append(f"./nift's build arguments differ from the profiled build's (extra {extra[:6]}, "
                       f"missing {[x for x in exp if x not in b.get('args', [])][:6]})")
    elif f.get("build_rc") is not None or f.get("expected_args") is not None:
        out.append("no build signature (.build/obj-config/default.flags): the binary's build cannot be bound")
    # F4: make's derivation must work (no silent fallback to the subset check), and the C half of
    # the signature (the cmark objects, CFLAGS) is the Makefile's own too, built by $CC
    if f.get("derivation_error"):
        out.append(f"cannot derive the Makefile's own build arguments ({f['derivation_error']}): the build cannot be bound")
    if "build_sig_c" in f:
        c = f.get("build_sig_c")
        if c is None:
            out.append("the build signature has no C block (cmark objects): the C half cannot be bound")
        else:
            if f.get("expected_args_c") is not None and c.get("args") != f["expected_args_c"]:
                out.append(f"the C objects' arguments {c.get('args')} differ from the Makefile's {f['expected_args_c']}")
            if f.get("cc_resolved") and c.get("resolved") != f["cc_resolved"]:
                out.append(f"the C objects were built by {c.get('resolved')}, not $CC ({f['cc_resolved']})")
    # C21: the link line is the Makefile's own (no environment or branch variable changes it)
    if "link" in f:
        lk = f["link"]
        if lk.get("as_built") is None or lk.get("own") is None:
            out.append("cannot derive the link line from the Makefile (make --eval failed)")
        elif lk["as_built"] != lk["own"]:
            out.append(f"the link line differs from the Makefile's own ({lk['as_built']!r} vs {lk['own']!r})")
    # C21: the compilers themselves: ELF binaries owned by the profile's g++-15/gcc-15 packages
    want_ver = (f.get("profile") or {}).get("compiler_version_contains")
    for key, pkg in (("cxx_identity", "g++-15"), ("cc_identity", "gcc-15")):
        ident = f.get(key)
        if ident is None:
            continue
        if ident.get("elf") is not True:
            out.append(f"{ident.get('path')} is not an ELF compiler (a wrapper script?): the compiler must be pinned itself")
        if not (ident.get("package") or "").startswith(pkg) or (want_ver and ident.get("version") != want_ver):
            out.append(f"{ident.get('path')} is owned by {ident.get('package')} {ident.get('version')}, "
                       f"not {pkg} {want_ver}")
    if f.get("driver_env"):
        out.append(f"compiler or loader redirection variables are set in the environment ({sorted(f['driver_env'])}): "
                   f"the profiled compiler, headers and libstdc++ must be the ones the profile hashes")
    if f.get("make_env"):
        out.append(f"make flag variables are set in the environment ({sorted(f['make_env'])}): they can override "
                   f"the build's variables (GNUMAKEFLAGS='CXXFLAGS=-O0' would reach both the build and the derivation)")
    if f.get("makefile_env"):
        out.append(f"Makefile variables are set from the environment ({sorted(f['makefile_env'])}): the profiled "
                   f"build uses the Makefile's own values")
    for var in ("CPPFLAGS", "LDFLAGS"):
        if (f.get("flags_env") or {}).get(var):
            out.append(f"{var} is set in the environment ({f['flags_env'][var]!r}): the profiled build uses the Makefile's")
    src = f.get("source") or {}
    if src.get("clean") is False:
        out.append("the source tree has tracked changes: the binary would not be the commit's")
    if src.get("untracked_src"):
        out.append(f"untracked or ignored files under src/ or an -I directory would be built in or included "
                   f"(the Makefile globs src/rt/*.cpp; -Iinclude precedes the system headers): {src['untracked_src'][:5]}")
    elif "untracked_src" in src and src["untracked_src"] is None:
        out.append("cannot list untracked files in the source tree (git ls-files failed)")
    # owner 23:14: a stale executable never passes: it must be current for the tree (make -q nift)
    # and, with a campaign start, built after it
    if f.get("build_current") is False:
        out.append("./nift is not current for the source tree (make -q nift)")
    if f.get("campaign_start") is not None and f.get("binary_mtime") is not None and \
            f["binary_mtime"] < f["campaign_start"]:
        out.append(f"./nift predates the campaign start ({f['binary_mtime']} < {f['campaign_start']})")
    if f.get("cxxflags_env") is not None and f["cxxflags_env"].split() != \
            ((f.get("profile") or {}).get("flags") or "").split():
        out.append(f"CXXFLAGS is overridden in the environment ({f['cxxflags_env']!r}), not the profile flags")
    # C14: the D4 profile's identity (tests/runtime2/cp14b/toolchain-profile.json), not only the macro
    prof = f.get("profile") or {}
    want = prof.get("compiler_version_contains")
    if want and want not in (f.get("cxx") or ""):
        out.append(f"the compiler is not the D4 profile's ({want}; found: {f.get('cxx')})")
    for name, spec in (prof.get("files") or {}).items():
        got = (f.get("profile_files") or {}).get(name)
        if got != spec.get("sha256"):
            out.append(f"{name} is not the D4 profile's (sha256 {spec.get('sha256', '')[:12]}..., found: "
                       f"{(got or 'missing')[:12]})")
    if f.get("comparator") != "ok":
        out.append(f"comparator sort is not compiled into the production binary ({f.get('comparator')})")
    if f.get("require_env") != "1":
        out.append("NIFT_CP14B_REQUIRE_COMPARATOR=1 is not set (comparator tests would SKIP instead of failing)")
    for name, have in (f.get("tools") or {}).items():
        if not have:
            out.append(f"host tool missing: {name} ({dict((n, w) for n, _, w in HOST_TOOLS).get(name, '')})")
            continue
        m = re.search(r"(\d+)\.(\d+)", have)
        ver = (int(m.group(1)), int(m.group(2))) if m else None
        if name in TOOL_MIN and (ver is None or ver < TOOL_MIN[name] or
                                 (name in TOOL_MAX_MAJOR and ver[0] != TOOL_MAX_MAJOR[name])):
            out.append(f"{name} {have!r} is below the minimum {'.'.join(map(str, TOOL_MIN[name]))}"
                       + (f" (major {TOOL_MAX_MAJOR[name]})" if name in TOOL_MAX_MAJOR else ""))
    if f.get("mem_mib") is not None and f["mem_mib"] < MIN_MEM_MIB:
        out.append(f"MemTotal {f['mem_mib']} MiB < {MIN_MEM_MIB} MiB (an 8 GB plan; read_limit.py needs 4 GiB available)")
    if f.get("disk_gib") is not None and f["disk_gib"] < MIN_DISK_GIB:
        out.append(f"free disk {f['disk_gib']} GiB < {MIN_DISK_GIB} GiB")
    if f.get("mmap_rnd_bits") is not None and f["mmap_rnd_bits"] > 28:
        out.append(f"vm.mmap_rnd_bits={f['mmap_rnd_bits']} > 28: TSan aborts (sysctl -w vm.mmap_rnd_bits=28)")
    if f.get("loopback") is False:
        out.append("cannot bind a loopback TCP port (test-serve)")
    if f.get("loopback6") is False:
        out.append("cannot bind ::1 (IPv6 loopback; test-serve's IPv6 case)")
    if f.get("pty") is False:
        out.append("no PTY available (/dev/ptmx; test-progress-pty, the v4.4 PTY smokes)")
    if f.get("shm_separate") is False:
        out.append("/dev/shm is not a separate filesystem (cross-device move cases)")
    if not f.get("oracle"):
        out.append("no CP19 legacy oracle: pass --legacy-oracle (a v4.12.0 binary built from tag v4.12.0)")
    else:
        # The version string alone does not identify the oracle (Runtime 2.0 builds still report
        # v4.12.0): it must be the `nift` of a checkout of the frozen tag's commit, clean and
        # built from it (make -q nift).
        if f.get("oracle_version") != "v4.12.0":
            out.append(f"the CP19 legacy oracle {f.get('oracle')} is not v4.12.0 (reports: {f.get('oracle_version')})")
        if f.get("oracle_commit") != V4120_COMMIT:
            out.append(f"the CP19 legacy oracle is not built from v4.12.0 = {V4120_COMMIT[:12]} "
                       f"(its checkout is at: {f.get('oracle_commit')})")
        if f.get("oracle_clean") is not True or f.get("oracle_current") is not True:
            out.append("the CP19 legacy oracle's checkout has tracked changes or its nift is not built from it "
                       f"(clean: {f.get('oracle_clean')}, make -q nift: {f.get('oracle_current')})")
        # D4 (owner 2026-11-07 22:13): built during this campaign, and a different binary from the
        # candidate. Deliberate substitution is an accepted threat-model limitation (record 12).
        if f.get("campaign_start") is None:
            out.append("no campaign start time (--campaign-start EPOCH or NIFT_CAMPAIGN_START): the oracle "
                       "cannot be shown to be built during the campaign")
        elif (f.get("oracle_mtime") or 0) < f["campaign_start"]:
            out.append(f"the CP19 legacy oracle was not built during this campaign (mtime {f.get('oracle_mtime')} "
                       f"< campaign start {f['campaign_start']})")
        if f.get("oracle_sha256") and f.get("oracle_sha256") == f.get("candidate_sha256"):
            out.append("the CP19 legacy oracle and the candidate are the same binary (sha256)")
        if f.get("oracle_realpath") and f.get("oracle_realpath") == f.get("candidate_realpath"):
            out.append("the CP19 legacy oracle and the candidate are the same file (real path)")
    return out


V4120_COMMIT = "ce8cd36a92ecfdc8b15b00df41a6c3bb9a31c0eb"  # tag v4.12.0
# R3-6: the host requirements of the planned gates, checked before any gate runs (a missing one
# would make its gates BLOCKED mid-campaign). (name, shell probe that prints a version or
# "ok", minimum / why)
# R4-6 (round 4): minimum versions (major, minor), compared with the first version number the
# probe prints; tools without an entry need only be present.
TOOL_MIN = {"python3": (3, 12), "go": (1, 26), "node": (22, 0), "dotnet": (10, 0)}
TOOL_MAX_MAJOR = {"dotnet": 10}  # the .NET SDK 10 line (net10.0)
HOST_TOOLS = [
    ("python3", "python3 --version", "3.12+ (bindings, harnesses)"),
    ("python3-dev", "python3 -c 'import sysconfig,os;p=sysconfig.get_paths()[\"include\"];print(\"ok\" if os.path.exists(os.path.join(p,\"Python.h\")) else \"\")'",
     "Python.h for test-python-binding (Ubuntu: python3-dev)"),
    ("go", "go version", "1.26+ (bindings/go/go.mod; test-go-binding, go test -race)"),
    ("node", "node --version", "22 (>= 20: bindings/node/package.json; test-node-binding)"),
    ("npm", "npm --version", "with node (bindings/node/build.sh)"),
    ("dotnet", "dotnet --version", ".NET SDK 10 (net10.0; test-csharp-binding)"),
    ("pkg-config", "pkg-config --version", "test-go-binding (dist/embed-prefix .pc)"),
    ("git", "git --version", "package, regression and baseline gates"),
    ("strace", "strace -V", "cp01/cp03 syscall measurements"),
    ("time", "test -x /usr/bin/time && /usr/bin/time --version 2>&1 | head -1", "GNU time at /usr/bin/time (memory gates)"),
    ("setpriv", "setpriv --version", "util-linux setpriv (privilege-drop cases as root)"),
    ("script", "script --version", "util-linux script (test-progress-pty)"),
    ("cc", "cc --version | head -1", "a C compiler"),
    ("make", "make --version | head -1", "GNU make"),
]
MIN_MEM_MIB = 7600     # 8 GB plan: read_limit.py needs MemAvailable >= 4096 MiB while it runs
MIN_DISK_GIB = 40      # combined-b3-b5.md 14 (the packaged GCC 15.2.0-16ubuntu1; no toolchain build)
COMPARATOR_PROGRAM = "xs := [3, 1, 2]\nxs.sort((p, q) => p < q)\nprint(xs.stringify())\n"


def sha256_of(path):
    import hashlib
    try:
        with open(path, "rb") as fh:
            return hashlib.sha256(fh.read()).hexdigest()
    except OSError:
        return None


ORACLE_BUILD = ("git worktree add --detach <tree> " + "ce8cd36a92ecfdc8b15b00df41a6c3bb9a31c0eb"
                + " && make -C <tree> -j\"$(nproc)\" nift")


def build_signature(root):
    """R4-3: the C++ block of .build/obj-config/default.flags (compiler, resolved path, version
    hash, args), or None."""
    try:
        text = (Path(root) / ".build/obj-config/default.flags").read_text()
    except OSError:
        return None
    blocks, cur = [], None
    for line in text.splitlines():
        if line.startswith("nift-object-config"):
            cur = {"args": []}
            blocks.append(cur)
        elif cur is not None:
            k, _, v = line.partition(" ")
            if k == "arg":
                cur["args"].append(v)
            else:
                cur[k] = v
    for b in blocks:
        if "-std=c++17" in b["args"]:
            return b
    return blocks[0] if blocks else None


def build_signature_c(root):
    """F4 (review of 3161f52d): the C block of the build signature (the cmark objects), or None."""
    try:
        text = (Path(root) / ".build/obj-config/default.flags").read_text()
    except OSError:
        return None
    blocks, cur = [], None
    for line in text.splitlines():
        if line.startswith("nift-object-config"):
            cur = {"args": []}
            blocks.append(cur)
        elif cur is not None:
            k, _, v = line.partition(" ")
            if k == "arg":
                cur["args"].append(v)
            else:
                cur[k] = v
    return next((b for b in blocks if "-std=c99" in b["args"]), None)


# F3/F4 (review of 3161f52d): make variables the environment can set (`?=` in the Makefile) and
# make's own flag variables. CXX and CC are the campaign's compilers (bound to the signature's
# resolved compiler); CXXFLAGS may only equal the profile flags.
MAKE_FLAG_VARS = ("MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKEOVERRIDES")
# Review of a977376e: environment variables that redirect the GCC driver (its programs, headers,
# libraries) or the dynamic loader (which libstdc++ a test binary runs with)
DRIVER_VARS = ("COMPILER_PATH", "GCC_EXEC_PREFIX", "LIBRARY_PATH", "CPATH", "C_INCLUDE_PATH",
               "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH", "OBJCPLUS_INCLUDE_PATH", "GCC_COMPARE_DEBUG",
               "DEPENDENCIES_OUTPUT", "SUNPRO_DEPENDENCIES", "LD_LIBRARY_PATH", "LD_PRELOAD", "LD_AUDIT")
ENV_ALLOWED = ("CXX", "CC", "CXXFLAGS")


def makefile_env_vars(root):
    """`?=` variables, and (C21) every variable a Makefile conditional tests (OS=Windows_NT selects
    the Windows branch: static libstdc++, .exe, other LDFLAGS)."""
    try:
        text = (Path(root) / "Makefile").read_text()
    except OSError:
        return []
    names = set(re.findall(r"^\s*([A-Za-z_]\w*)\s*\?=", text, re.M))
    for line in re.findall(r"^\s*(?:ifn?eq|ifn?def)\b.*$", text, re.M):
        names |= set(re.findall(r"\$\(([A-Za-z_]\w*)\)", line))
        m = re.match(r"\s*ifn?def\s+([A-Za-z_]\w*)", line)
        if m:
            names.add(m.group(1))
    return sorted(names)


def make_print(root, expr, env):
    """The expansion of `expr` in the tree's Makefile, read under `make -n`: reading the Makefile
    otherwise rewrites the libffi and object signatures when the environment changes them, which
    would leave the tree out of date (`make -q nift`) after the preflight."""
    res = subprocess.run(["make", "-n", "-s", "--no-print-directory", "--eval",
                          f"certify-print: ; @echo '{expr}'", "certify-print"], cwd=root,
                         capture_output=True, text=True, env=env)
    out = res.stdout.strip()
    if res.returncode == 0 and out.startswith("echo '") and out.endswith("'") and "\n" not in out:
        res.stdout = out[len("echo '"):-1]
    else:
        res.returncode = res.returncode or 1
    return res


def compiler_identity(cmd):
    """C21 (release audit): the compiler itself, not a wrapper: real path, ELF or not, sha256, and
    the Debian package and version that own it."""
    real = os.path.realpath(shutil.which(cmd) or cmd)
    out = {"path": real, "elf": None, "sha256": None, "package": None, "version": None}
    try:
        with open(real, "rb") as fh:
            out["elf"] = fh.read(4) == b"\x7fELF"
        out["sha256"] = sha256_of(real)
    except OSError:
        return out
    owner = subprocess.run(["dpkg", "-S", real], capture_output=True, text=True)
    if owner.returncode == 0 and ":" in owner.stdout:
        pkg = owner.stdout.split(":")[0].strip()
        out["package"] = pkg
        ver = subprocess.run(["dpkg-query", "-W", "-f", "${Version}", pkg], capture_output=True, text=True)
        out["version"] = ver.stdout.strip() if ver.returncode == 0 else None
    return out


def environment_record(root):
    """Owner 2026-11-07 23:10: where a result was produced."""
    import platform
    env = {"hostname": platform.node(), "kernel": platform.release()}
    try:
        osr = dict(l.rstrip("\n").split("=", 1) for l in open("/etc/os-release") if "=" in l)
        env["distro"] = osr.get("PRETTY_NAME", "").strip('"')
    except OSError:
        env["distro"] = None
    cgroup = ""
    try:
        cgroup = open("/proc/1/cgroup").read()
    except OSError:
        pass
    env["container"] = bool(os.path.exists("/.dockerenv") or os.path.exists("/run/.containerenv")
                            or re.search(r"docker|lxc|containerd|kubepods|podman", cgroup))
    return env


def preflight_facts(root, oracle, campaign_start=None, build_rc=None):
    cxx = os.environ.get("CXX", "g++")
    facts = {"require_env": os.environ.get("NIFT_CP14B_REQUIRE_COMPARATOR"), "oracle": oracle, "build_rc": build_rc}
    path = shutil.which(cxx) or cxx
    facts["cxx_resolved"] = os.path.realpath(path)
    try:
        import hashlib
        version_out = subprocess.run([cxx, "--version"], capture_output=True).stdout
        facts["cxx_version_sha256"] = hashlib.sha256(version_out).hexdigest()
    except OSError:
        facts["cxx_version_sha256"] = None
    facts["build_sig"] = build_signature(root)
    # owner 23:14: the source tree the binary was built from, and whether the binary is current
    def git(*a):
        res = subprocess.run(["git", "-C", str(root)] + list(a), capture_output=True, text=True)
        return res.stdout.strip() if res.returncode == 0 else None
    # F4: untracked or ignored files under src/ and every -I directory of the signature (a stray
    # include/bits/stl_algo.h would shadow libstdc++); the build's own outputs (.o .d .cfg) aside
    sig_b, sig_c = build_signature(root), build_signature_c(root)
    idirs = sorted({"src"} | {a[2:] for b in (sig_b, sig_c) if b for a in b.get("args", [])
                              if a.startswith("-I") and not a.startswith("-I$TEST_DIR")})
    others = git("ls-files", "--others", "--", *idirs)
    facts["source"] = {"head": git("rev-parse", "HEAD"), "tree": git("rev-parse", "HEAD^{tree}"),
                       "clean": git("status", "--porcelain", "--untracked-files=no") == "",
                       "include_dirs": idirs,
                       "untracked_src": None if others is None else [
                           x for x in others.splitlines() if not re.search(r"\.(o|d|cfg)$", x)]}
    facts["build_sig_c"] = sig_c
    facts["flags_env"] = {v: os.environ.get(v) for v in ("CXXFLAGS", "CPPFLAGS", "LDFLAGS") if os.environ.get(v)}
    # F3: make's flag variables (GNUMAKEFLAGS too) and every `?=` make variable set in the environment
    facts["make_env"] = {v: os.environ[v] for v in MAKE_FLAG_VARS if os.environ.get(v)}
    facts["driver_env"] = {v: os.environ[v] for v in DRIVER_VARS if os.environ.get(v) is not None}
    facts["makefile_env"] = {v: os.environ[v] for v in makefile_env_vars(root)
                             if v not in ENV_ALLOWED and os.environ.get(v) is not None}
    cc = os.environ.get("CC", "cc")
    facts["cc_resolved"] = os.path.realpath(shutil.which(cc) or cc)
    # R5-2: the arguments the Makefile itself gives the objects (no environment overrides, no
    # make flag variables), written the way the build signature writes them ($TEST_DIR)
    sig = make_print(root, "$(OBJ_SIGNATURE_ARGS)", {k: v for k, v in clean_env().items()
                                                      if k not in ("CXXFLAGS", "CPPFLAGS", "LDFLAGS", "CFLAGS")})
    ok = sig.returncode == 0 and sig.stdout.count("---") == 1
    fix = lambda toks: [t.replace("-I.build/", "-I$TEST_DIR/") if t.startswith("-I.build/") else t for t in toks]
    facts["expected_args"] = fix(sig.stdout.split("---")[0].split()[1:]) if ok else None
    facts["expected_args_c"] = fix(sig.stdout.split("---")[1].split()[1:]) if ok else None
    facts["derivation_error"] = None if ok else (f"make --eval exited {sig.returncode}: "
                                                 f"{(sig.stderr or sig.stdout).strip()[:200]}")
    # C21: the link line's variables as the campaign's make sees them, against make's own (every
    # environment-settable or branch-selecting variable removed, CXX kept)
    link = "$(CXX) ;; $(CXXFLAGS) ;; $(LDFLAGS) ;; $(LDLIBS) ;; $(TARGET)"
    scrub = set(makefile_env_vars(root)) - {"CXX", "CC"}
    as_built = make_print(root, link, clean_env())
    own = make_print(root, link, {k: v for k, v in clean_env().items() if k not in scrub})
    facts["link"] = {"as_built": as_built.stdout.strip() if as_built.returncode == 0 else None,
                     "own": own.stdout.strip() if own.returncode == 0 else None}
    facts["cxx_identity"] = compiler_identity(cxx)
    facts["cc_identity"] = compiler_identity(cc)
    facts["build_invocation"] = "make nift (CXX=%s, CXXFLAGS=%s)" % (cxx, os.environ.get("CXXFLAGS", "<Makefile default>"))
    current = subprocess.run(["make", "-q", "nift"], cwd=root, capture_output=True, env=clean_env())
    facts["build_current"] = current.returncode == 0
    facts["campaign_start"] = campaign_start
    try:
        facts["binary_mtime"] = int(os.stat(Path(root) / "nift").st_mtime)
    except OSError:
        facts["binary_mtime"] = None
    facts["cxxflags_env"] = os.environ.get("CXXFLAGS")
    facts["environment"] = environment_record(root)
    try:
        facts["cxx"] = subprocess.run([cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    except (OSError, IndexError):
        facts["cxx"] = None
    try:
        out = subprocess.run([cxx, "-x", "c++", "-E", "-P", "-"], input="#include <cstddef>\n__GLIBCXX__\n",
                             capture_output=True, text=True).stdout.split()
        facts["glibcxx"] = int(out[-1]) if out and out[-1].isdigit() else None
    except (OSError, ValueError):
        facts["glibcxx"] = None
    # C14: the D4 toolchain profile's files, located through the compiler itself
    try:
        prof = json.loads((Path(root) / "tests/runtime2/cp14b/toolchain-profile.json").read_text())
    except (OSError, ValueError):
        prof = {"compiler_version_contains": "(toolchain-profile.json missing)", "files": {}}
    facts["profile"] = prof
    files = {}
    try:
        lib = subprocess.run([cxx, "-print-file-name=libstdc++.so"], capture_output=True, text=True).stdout.strip()
        files["libstdc++.so"] = sha256_of(os.path.realpath(lib)) if lib and os.path.isabs(lib) else None
        trace = subprocess.run([cxx, "-x", "c++", "-std=c++17", "-E", "-H", "-o", os.devnull, "-"],
                               input="#include <algorithm>\n", capture_output=True, text=True).stderr
        for name in ("bits/stl_algo.h", "bits/stl_tempbuf.h"):
            hit = [l.lstrip(". ").strip() for l in trace.splitlines() if l.strip().endswith("/" + name)]
            files[name] = sha256_of(hit[0]) if hit else None
    except OSError:
        pass
    facts["profile_files"] = files
    tools = {}
    for name, probe, _ in HOST_TOOLS:
        try:
            out = subprocess.run(["bash", "-c", probe], capture_output=True, text=True, timeout=60)
            tools[name] = (out.stdout or out.stderr).strip().splitlines()[0][:80] if out.returncode == 0 and (out.stdout or out.stderr).strip() else ""
        except (OSError, subprocess.TimeoutExpired, IndexError):
            tools[name] = ""
    facts["tools"] = tools
    try:
        facts["mem_mib"] = int([l for l in open("/proc/meminfo") if l.startswith("MemTotal:")][0].split()[1]) // 1024
    except (OSError, IndexError, ValueError):
        facts["mem_mib"] = None
    st = os.statvfs(str(root))
    facts["disk_gib"] = st.f_bavail * st.f_frsize // (1 << 30)
    try:
        facts["mmap_rnd_bits"] = int(open("/proc/sys/vm/mmap_rnd_bits").read())
    except (OSError, ValueError):
        facts["mmap_rnd_bits"] = None
    import socket
    try:
        sk = socket.socket(); sk.bind(("127.0.0.1", 0)); sk.close(); facts["loopback"] = True
    except OSError:
        facts["loopback"] = False
    try:
        sk6 = socket.socket(socket.AF_INET6); sk6.bind(("::1", 0)); sk6.close(); facts["loopback6"] = True
    except OSError:
        facts["loopback6"] = False
    facts["pty"] = os.path.exists("/dev/ptmx")
    try:
        facts["shm_separate"] = os.stat("/dev/shm").st_dev != os.stat(str(root)).st_dev
    except OSError:
        facts["shm_separate"] = False
    nift = Path(root) / "nift"
    if not nift.exists():
        facts["comparator"] = "nift not built"
    else:
        work = Path(os.environ.get("TMPDIR", "/tmp")) / f"nift-certify-preflight-{os.getpid()}"
        work.mkdir(parents=True, exist_ok=True)
        (work / "c.f").write_text(COMPARATOR_PROGRAM)
        p = subprocess.run([str(nift), "c.f"], cwd=work, capture_output=True, text=True,
                           env=dict(clean_env(), NIFT_VM="require"))
        shutil.rmtree(work, ignore_errors=True)
        facts["comparator"] = "ok" if p.returncode == 0 and p.stdout.strip() == "[1,2,3]" else \
            f"a comparator sort exits {p.returncode}: {(p.stderr.strip().splitlines() or [''])[0][:120]}"
    if oracle:
        try:
            v = subprocess.run([oracle, "--version"], capture_output=True, text=True, env=dict(clean_env(), NIFT_VM="0"))
            m = re.search(r"v\d+\.\d+\.\d+", v.stdout + v.stderr)
            facts["oracle_version"] = m.group(0) if m else (v.stdout + v.stderr).strip()[:60]
        except OSError as e:
            facts["oracle_version"] = f"not runnable: {e}"
        tree = Path(oracle).resolve().parent
        def git(*a):
            res = subprocess.run(["git", "-C", str(tree)] + list(a), capture_output=True, text=True)
            return res
        head = git("rev-parse", "HEAD")
        facts["oracle_commit"] = head.stdout.strip() if head.returncode == 0 else "not a git checkout"
        st = git("status", "--porcelain", "--untracked-files=no")
        facts["oracle_clean"] = st.returncode == 0 and not st.stdout.strip()
        oracle_q = subprocess.run(["make", "-q", "nift"], cwd=tree, capture_output=True, env=clean_env())
        facts["oracle_current"] = (Path(oracle).resolve() == tree / "nift" and (tree / "Makefile").is_file()
                                   and oracle_q.returncode == 0)
        facts["campaign_start"] = campaign_start
        try:
            facts["oracle_mtime"] = int(os.stat(oracle).st_mtime)
        except OSError:
            facts["oracle_mtime"] = None
        facts["oracle_sha256"] = sha256_of(oracle)
        facts["oracle_realpath"] = os.path.realpath(oracle)
        facts["oracle_build_command"] = ORACLE_BUILD.replace("<tree>", str(tree))
    facts["candidate_sha256"] = sha256_of(nift)
    facts["candidate_realpath"] = os.path.realpath(nift)
    return facts


def recipe_runs_make(text):
    return bool(re.search(r"\$\(MAKE\)|\$\{MAKE\}|(^|[;&|(\s])make\s", text))


def clean_env():
    env = dict(os.environ)
    # NIFT_DEFER_LOAD_SENSITIVE (CI only) would skip the load-sensitive
    # guards; certification always runs them (exclusively).
    for k in ("MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKELEVEL", "MAKEOVERRIDES", "NIFT_DEFER_LOAD_SENSITIVE"):
        env.pop(k, None)
    return env


def runner_env(target, cwd, log=None):
    """Owner 03:27: a gate's runner-level diagnostics, the same for the candidate and every
    baseline tree (the runner's own files, from this checkout; the trees' scripts unchanged):
    errtrace (certify_errtrace.bash through BASH_ENV) and keep_rtdiff (certify_site/
    sitecustomize.py through PYTHONPATH, keeping rt-diff's report beside the log)."""
    spec = FP.SPECS.get(target)
    env = {}
    if spec is not None and spec.errtrace:
        env["BASH_ENV"] = str(ROOT / "scripts/certify_errtrace.bash")
    if spec is not None and spec.keep_rtdiff:
        env["PYTHONPATH"] = os.pathsep.join(filter(None, [str(ROOT / "scripts/certify_site"),
                                                          os.environ.get("PYTHONPATH")]))
        env["PYTHONDONTWRITEBYTECODE"] = "1"  # no __pycache__ in the runner's directory
        env["CERTIFY_KEEP_DIR"] = str(Path(log).resolve()) + ".kept" if log else str(Path(cwd) / ".certify-kept")
    return env


def run_target(cwd, target, log, timeout, user=None, old=(), extra_env=None):
    """Run one make target with a private TMPDIR (so tests that use fixed names
    under the temp directory, e.g. temp_directory_path()/"nift-host-seam-pg",
    cannot collide with concurrent targets, other checkouts or other users),
    in its own process group (killed on timeout)."""
    log.parent.mkdir(parents=True, exist_ok=True)
    # Review of 4ec1aa80 (F2): the same path LENGTH for every run of every tree (candidate, rerun,
    # baselines, unprivileged copy): harnesses that print a fixed-size tail of their output
    # (read_limit.py) then cut it at the same offset everywhere, so the tail can be compared.
    import hashlib
    tmp = (Path(os.environ.get("TMPDIR", "/tmp")) / f"nift-certify-{os.getpid():07d}"
           / hashlib.sha256(str(log).encode()).hexdigest()[:16])
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    env = clean_env()
    env["TMPDIR"] = str(tmp)
    # The target's own log may sit inside the checkout (.build/certify). Tests
    # that snapshot the checkout exclude exactly this one file (and nothing else).
    env["NIFT_CERTIFY_TARGET_LOG"] = str(Path(log).resolve())
    extra_env = {**(extra_env or {}), **runner_env(target, cwd, log)}
    env.update(extra_env)
    # C2: -k (a failing prerequisite stops nothing else); an umbrella's own recipe runs with its
    # gate prerequisites marked old (they are entries of their own).
    args = ["-k", target] + [x for p in old for x in ("-o", p)]
    cmd = ["make"] + args
    if user:
        shutil.chown(tmp, user=user)
        os.chmod(tmp.parent, 0o755)
        exports = " ".join(f"{k}='{v}'" for k, v in (extra_env or {}).items())
        cmd = ["su", user, "-s", "/bin/sh", "-c", f"cd '{cwd}' && TMPDIR='{tmp}' NIFT_CERTIFY_TARGET_LOG='{Path(log).resolve()}' {exports} make {' '.join(args)}"]
    t0 = time.time()
    with open(log, "w") as f:
        f.write(f"$ {' '.join(cmd)}  (cwd={cwd}, TMPDIR={tmp})\n")
        f.flush()
        p = subprocess.Popen(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=env,
                             start_new_session=True)
        try:
            rc = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.wait()
            f.write(f"\n[certify] TIMEOUT after {timeout}s\n")
            rc = 124
    shutil.rmtree(tmp, ignore_errors=True)
    return rc, round(time.time() - t0, 1)


# ----- environment-condition detectors (environment fact AND log evidence) -----

def detect_unverified(text):
    reasons = []
    for m in re.finditer(r"(?:^|\s|/)([A-Za-z0-9_.+-]+): (?:command )?not found", text, re.M):
        tool = m.group(1)
        if tool not in ("sh", "bash") and shutil.which(tool) is None:
            reasons.append(f"tool '{tool}' not installed")
    if re.search(r"go: download go\S*: .*(no such host|dial tcp|connection refused|403)", text):
        reasons.append("Go toolchain download blocked (no network access to proxy.golang.org)")
    for m in re.finditer(r"(/[\w./-]+): No such file or directory", text):
        path = m.group(1)
        if path.startswith(("/usr/bin/", "/bin/", "/usr/local/bin/")) and not os.path.exists(path):
            reasons.append(f"system tool {path} missing")
    for m in re.finditer(r"local package source is not a directory: (\S+)", text):
        if not os.path.isdir(m.group(1)):
            reasons.append(f"sibling checkout {m.group(1)} missing (CI checks out nift-packages)")
    return sorted(set(reasons))


def unprivileged_user(name):
    if os.geteuid() != 0 or not name:
        return None
    try:
        pwd.getpwnam(name)
        return name
    except KeyError:
        return None


def prepare_copy(src, dest, user):
    if dest.exists():
        shutil.rmtree(dest)
    subprocess.run(["cp", "-a", str(src), str(dest)], check=True)
    subprocess.run(["chown", "-R", f"{user}:", str(dest)], check=True)


SNAP_SKIP_DIRS = {".git", "__pycache__", "certify"}


def snapshot(root):
    state = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SNAP_SKIP_DIRS]
        for name in filenames:
            path = os.path.join(dirpath, name)
            try:
                st = os.lstat(path)
            except OSError:
                continue
            state[os.path.relpath(path, root)] = (st.st_size, st.st_mtime_ns)
    return state


def tmp_names():
    base = Path(os.environ.get("TMPDIR", "/tmp"))
    try:
        return {p.name for p in base.iterdir()}
    except OSError:
        return set()


def audit(planned, timeout, out_path):
    """Serial run recording, per target, every file it created/changed/removed
    in the checkout (including .build/) and every new top-level TMPDIR name it
    left behind. Shared outputs between targets and writes outside .build/
    identify targets that are not safe to run concurrently."""
    report = {}
    before = snapshot(ROOT)
    for t in planned:
        tmp0 = tmp_names()
        rc, dur = run_target(ROOT, t, ROOT / ".build" / "certify" / "audit" / f"{t}.log", timeout)
        after = snapshot(ROOT)
        changed = sorted(k for k in after if before.get(k) != after[k])
        removed = sorted(k for k in before if k not in after)
        report[t] = {"rc": rc, "seconds": dur, "changed": changed, "removed": removed,
                     "tmp_left": sorted(tmp_names() - tmp0)}
        print(f"[audit] {t}: rc={rc} changed={len(changed)} removed={len(removed)} tmp_left={len(report[t]['tmp_left'])}", flush=True)
        before = after
    writers = {}
    for t, r in report.items():
        for k in r["changed"] + r["removed"]:
            writers.setdefault(k, []).append(t)
    shared = {k: v for k, v in writers.items() if len(v) > 1}
    outside = {t: [k for k in r["changed"] + r["removed"] if not k.startswith(".build/")] for t, r in report.items()}
    out = {"targets": report, "shared_outputs": shared,
           "writes_outside_build": {t: v for t, v in outside.items() if v},
           "tmp_left": {t: r["tmp_left"] for t, r in report.items() if r["tmp_left"]}}
    Path(out_path).write_text(json.dumps(out, indent=2) + "\n")
    return out


_STEP = re.compile(r"(?:tests|scripts|tools)/[\w./-]+")


def failure_signature(text):
    """The step a target failed at: the last test/script path mentioned before
    make's final error line, or None if it cannot be identified. Used so that
    BASELINE-FAILURE is only claimed when the baseline fails at the same step
    (a baseline failure at an earlier step must not mask a later regression)."""
    lines = text.splitlines()
    end = max((i for i, l in enumerate(lines) if l.startswith("make: ***")), default=len(lines))
    for line in reversed(lines[:end]):
        m = _STEP.findall(line)
        if m:
            return m[-1]
    return None


# Wall-clock performance components inside a recipe (owner, 2026-10-23: the same
# patterns precert uses). make echoes each recipe line before running it, so a
# log that contains one of these reached a timing step.
PERF_COMPONENT = re.compile(r"benchmarks/[A-Za-z0-9_]+\.py|_perf_scaling_smoke\.sh|_scaling_guard\.sh|scripting_perf_guard\.sh")


def reached_timing(text):
    """True if the log shows any performance component was started."""
    return any(PERF_COMPONENT.search(l) for l in text.splitlines())


def decide(f):
    """The whole classification policy, as a pure function of observed facts.

    Order of evidence: pass; re-run alone; environment detected from the log;
    unprivileged re-run; baseline under the same user at the same failing step.
    Anything not positively shown to be acceptable is FAIL: a baseline failure
    never excuses a candidate failure at a different or unidentified step.
    """
    t = f["target"]
    if f["rc"] == 0:
        return "PASS", ""
    if f["load_sensitive"]:
        # Already ran with no competing target: a later pass does not resolve
        # the failure (owner review, D-012). Thresholds unchanged.
        # Root-only exception (owner, 2026-10-23, CP16): only when the failure is
        # shown to be privilege-related and before any timing step: both root runs
        # failed at the same identified step, neither root log reached a
        # performance component, and the complete target (every timing step
        # included) then passed as the unprivileged user. Anything else, including
        # missing or incomplete unprivileged evidence, stays FAIL.
        steps = f.get("root_steps") or (None, None)
        if (f["rerun_rc"] != 0 and steps[0] is not None and steps[0] == steps[1]
                and f.get("root_reached_timing") is False and f.get("unpriv_rc") == 0):
            return "SKIPPED-ENVIRONMENTAL", (f"fails only as root at functional step {steps[0]} (before any timing "
                                             f"step, both root runs); complete target incl. timing passes as "
                                             f"unprivileged user '{f['user']}'")
        return "FAIL", (f"load-sensitive guard failed under exclusive execution "
                        f"(re-run alone: {'passed' if f['rerun_rc'] == 0 else 'failed'}); see .build/certify/{t}.log")
    if f["rerun_rc"] == 0:
        return "FLAKY", "failed in the concurrent run, passed when re-run alone"
    if f.get("unverified"):
        return "UNVERIFIED", "; ".join(f["unverified"])
    if f.get("unpriv_rc") == 0:
        return "SKIPPED-ENVIRONMENTAL", f"fails only as root; passes as unprivileged user '{f['user']}'"
    if f.get("base_rc") not in (None, 0) and f.get("base_has_target"):
        cs, bs = f.get("cand_step"), f.get("base_step")
        # owner 2026-11-08 01:06: FAILED-BASELINE means equal failure fingerprints (the same
        # extractor on both raw logs), nothing else; no fingerprint is never a match
        if f.get("base_identical") is True:
            return "BASELINE-FAILURE", f"equal failure fingerprint on the v4.12.0 baseline, same user (steps {cs} / {bs})"
        return "FAIL", (f"the v4.12.0 baseline also fails, but its failure fingerprint differs or none could be "
                        f"extracted ({'; '.join(f.get('base_fp_diff') or ['no comparison'])[:300]}); see {f.get('cand_log')}")
    return "FAIL", f"see .build/certify/{t}.log"


def ensure_baseline(path, jobs):
    if not (path / "Makefile").exists():
        return False
    if not (path / "nift").exists():
        built = subprocess.run(["make", f"-j{jobs}", "nift"], cwd=path, capture_output=True, env=clean_env())
        if built.returncode != 0:
            print(f"certify: baseline build failed in {path}; BASELINE-FAILURE cannot be classified", file=sys.stderr)
            return False
    return (path / "nift").exists()


def merge_summaries(paths):
    """Owner 2026-11-07 23:10: merged results list each environment and which gates ran in it."""
    envs, gates = [], {}
    for p in paths:
        data = json.loads(Path(p).read_text())
        for t, r in data.get("targets", {}).items():
            env = r.get("environment") or data.get("environment") or {}
            key = json.dumps(env, sort_keys=True)
            if key not in envs:
                envs.append(key)
            gates.setdefault(t, []).append((envs.index(key), r.get("class")))
    print(f"merged {len(paths)} summaries: {len(envs)} environment(s)")
    for i, key in enumerate(envs):
        env = json.loads(key)
        ran = sorted(t for t, v in gates.items() if any(e == i for e, _ in v))
        print(f"  environment {i + 1}: {env.get('hostname')} {env.get('distro')} kernel {env.get('kernel')} "
              f"container={env.get('container')} compiler={env.get('compiler')} "
              f"stdlib={str(env.get('stdlib_sha256'))[:12]} binary={str(env.get('binary_sha256'))[:12]}: {len(ran)} gate(s)")
        for t in ran:
            print(f"    {t}: " + ", ".join(c for e, c in gates[t] if e == i))
    # R5-5: one certification is one binary built against one libstdc++; differing ones are refused
    rc = 0
    for field in ("binary_sha256", "stdlib_sha256"):
        seen = sorted({str(json.loads(k).get(field)) for k in envs})
        if len(seen) > 1:
            print(f"MERGE REFUSED: {field} differs between environments: " + ", ".join(x[:12] for x in seen))
            rc = 1
    return rc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jobs", "-j", type=int, default=max(1, (os.cpu_count() or 2)))
    ap.add_argument("--targets", nargs="+", help="focused run (tier 1/2): only these targets (umbrellas expanded)")
    ap.add_argument("--quick", action="store_true", help="implementation-time tier: the QUICK smoke set")
    ap.add_argument("--timeout", type=int, default=3600, help="per-target timeout in seconds")
    ap.add_argument("--unprivileged-user", default=os.environ.get("NIFT_CERTIFY_USER", "tester"),
                    help="when running as root, re-run failures as this user to detect root-only failures")
    ap.add_argument("--baseline", default=os.environ.get("NIFT_CERTIFY_BASELINE",
                                                         str(ROOT.parent / "baseline-src")),
                    help="v4.12.0 source tree used to classify BASELINE-FAILURE ('' disables)")
    ap.add_argument("--regression-baseline", action="append", default=[], metavar="NAME=TREE",
                    help="a regression baseline tree (repeatable): the accepted B4b 17f7964c, weekend/r2")
    ap.add_argument("--legacy-oracle", default=os.environ.get("CP19_LEGACY_ORACLE", ""),
                    help="the v4.12.0 legacy binary test-cp19-proof-cli compares with (CP19_LEGACY_ORACLE)")
    ap.add_argument("--strict", action="store_true", help="exit 1 unless every required gate is PASSED")
    ap.add_argument("--campaign-start", type=int, default=int(os.environ["NIFT_CAMPAIGN_START"])
                    if os.environ.get("NIFT_CAMPAIGN_START", "").isdigit() else None,
                    help="the campaign's start (epoch seconds); the CP19 oracle must be built after it")
    ap.add_argument("--certify", action="store_true",
                    help="certification mode (implied by a full campaign): a mandatory gate that is PARTIAL or SKIP fails the campaign")
    ap.add_argument("--list", action="store_true", help="dry run: print the plan, the inventory check and the preflight, then exit")
    ap.add_argument("--check-plan", action="store_true", help="exit 1 when the plan misses an inventory or mandatory gate")
    ap.add_argument("--drop", action="append", default=[], metavar="TARGET",
                    help="remove TARGET from the plan before --check-plan (negative control)")
    ap.add_argument("--audit", metavar="JSON", help="isolation audit: run targets serially and record writes (use a disposable copy)")
    ap.add_argument("--merge", nargs="+", metavar="SUMMARY_JSON",
                    help="merge certification summaries: list each environment and the gates that ran in it")
    a = ap.parse_args()
    if a.merge:
        return merge_summaries(a.merge)
    # owner 2026-11-08 06:46: the reviewed category table of abort-hidden checks (the runner's own)
    try:
        hidden_table = FP.hidden_categories((Path(__file__).resolve().parent / "certify_hidden_checks.tsv").read_text())
    except (OSError, ValueError) as e:
        print(f"certify: the hidden-check category table is unusable: {e}", file=sys.stderr)
        return 2

    deps, recipe = make_db(ROOT, AGGREGATE)
    inv = inventory(deps)
    roots = a.targets or [AGGREGATE] + [t for t in inv if t not in NON_GATE]
    entries, covered = plan(deps, recipe, roots)
    if a.quick:
        entries = [e for e in entries if e[0] in QUICK]
    for t in a.drop:
        entries = [e for e in entries if e[0] != t]
        covered.discard(t)
    problems = inventory_problems({e[0] for e in entries} | {t for t in covered if not recipe.get(t)},
                                  inv, NON_GATE, MANDATORY, set(deps))
    if a.check_plan or (a.list and not a.targets):
        for pr in problems:
            print("FAIL certify plan: " + pr)
        if a.check_plan:
            if not problems:
                print(f"PASS certify plan: {len(entries)} entries; every one of the {len(inv)} inventory targets is "
                      f"planned or one of {len(NON_GATE)} named non-gates; {len(MANDATORY)} mandatory gates planned")
            return 1 if problems else 0
    planned = [e[0] for e in entries]
    olds = {e[0]: e[2] if e[1] == "own" else () for e in entries}
    # Entries that are not certify-targets leaves of their own (umbrella prerequisites, inventory
    # additions, own recipes) run serially, in plan order: before C2 they ran serially inside
    # their umbrella or not at all, and their isolation is not audited.
    old_leaves = set(leaves(deps, recipe, AGGREGATE))
    exclusive = {}
    for t, kind, _ in entries:
        if t in EXCLUSIVE or recipe_runs_make(recipe.get(t, "")):
            exclusive[t] = EXCLUSIVE.get(t) or "recipe runs a sub-make (shared build outputs)"
        elif kind == "own":
            exclusive[t] = "an umbrella's own recipe (its gate prerequisites are entries of their own)"
        elif t not in old_leaves:
            exclusive[t] = "planned by the C1 inventory or expanded from an umbrella (isolation not audited)"
    # own recipes last, in plan order
    exclusive = dict(sorted(exclusive.items(), key=lambda kv: (olds.get(kv[0]) != (), planned.index(kv[0]))))
    parallel = [t for t in planned if t not in exclusive]
    oracle = a.legacy_oracle or None
    full = not a.targets and not a.quick
    if a.list:
        for t in parallel:
            print(f"parallel   {t}")
        for t, why in exclusive.items():
            print(f"exclusive  {t}  ({why})" + (f"  [make -k {t} " + " ".join("-o " + o for o in olds[t]) + "]" if olds[t] else ""))
        pf = preflight_problems(preflight_facts(ROOT, oracle, a.campaign_start))
        print(f"plan: {len(planned)} required gates ({len(parallel)} parallel, {len(exclusive)} serial), "
              f"{len(inv)} inventory targets, {len(NON_GATE)} non-gates: " + ", ".join(sorted(NON_GATE)))
        for pr in pf:
            print("PREFLIGHT BLOCKED: " + pr)
        print("preflight: " + ("ok" if not pf else f"{len(pf)} problem(s): a full campaign would exit 2 here"))
        return 1 if problems and not a.targets else 0
    if not planned:
        print("certify: no targets planned", file=sys.stderr)
        return 2
    if problems and full:
        for pr in problems:
            print("certify: plan incomplete: " + pr, file=sys.stderr)
        return 2

    logdir = ROOT / ".build" / "certify"
    logdir.mkdir(parents=True, exist_ok=True)
    if a.audit:
        built = subprocess.run(["make", f"-j{a.jobs}", "nift"], cwd=ROOT, capture_output=True, env=clean_env())
        if built.returncode != 0:
            print("certify: could not build nift for the audit", file=sys.stderr)
            return 1
        audit(planned, a.timeout, a.audit)
        return 0
    # C3 preflight, before anything else is built: the production binary (for the comparator
    # check), then the toolchain, switch and oracle facts. A full campaign does not start with a
    # preflight problem.
    built = subprocess.run(["make", f"-j{a.jobs}", "nift"], cwd=ROOT, capture_output=True, text=True, env=clean_env())
    (logdir / "_build-nift.log").write_text(built.stdout + built.stderr)
    facts = preflight_facts(ROOT, oracle, a.campaign_start, built.returncode)
    pf = preflight_problems(facts)
    (logdir / "_preflight.json").write_text(json.dumps({"facts": facts, "problems": pf}, indent=2) + "\n")
    for pr in pf:
        print("PREFLIGHT BLOCKED: " + pr, file=sys.stderr)
    if pf and full:
        print("certify: preflight failed; the campaign did not start (.build/certify/_preflight.json)", file=sys.stderr)
        return 2
    if built.returncode != 0:  # R4-3: never certify a stale binary
        print("certify: make nift failed; see .build/certify/_build-nift.log", file=sys.stderr)
        return 2
    # Phase 0: build every non-test prerequisite once (binaries, objects,
    # staged prefixes, bindings), so concurrent make processes do not race to
    # build shared outputs. -k: a prerequisite that cannot be built here (e.g. a
    # missing toolchain) is left for its targets to fail and be classified.
    prereqs = sorted({d for t in planned for d in deps.get(t, []) if not TEST_NAME.match(d)} | {"nift"})
    pre = subprocess.run(["make", "-k", f"-j{a.jobs}"] + prereqs, cwd=ROOT, capture_output=True, text=True, env=clean_env())
    (logdir / "_prebuild.log").write_text(pre.stdout + pre.stderr)
    if not (ROOT / "nift").exists():
        print("certify: could not build nift, see .build/certify/_prebuild.log", file=sys.stderr)
        return 1
    # The v4.12.0 oracle is also test-runtime2-scanner-parity's reference binary (NIFT_REF).
    extra = {"CP19_LEGACY_ORACLE": oracle, "NIFT_REF": os.environ.get("NIFT_REF") or oracle} if oracle else {}

    results = {}
    started = time.time()
    with cf.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        futs = {pool.submit(run_target, ROOT, t, logdir / f"{t}.log", a.timeout, None, olds[t], extra): t for t in parallel}
        for fut in cf.as_completed(futs):
            t = futs[fut]
            rc, dur = fut.result()
            results[t] = {"rc": rc, "seconds": dur, "mode": "parallel"}
            print(f"[{'ok' if rc == 0 else 'FAILED'}] {t} ({dur}s)", flush=True)
    for t in exclusive:
        rc, dur = run_target(ROOT, t, logdir / f"{t}.log", a.timeout, None, olds[t], extra)
        results[t] = {"rc": rc, "seconds": dur, "mode": "exclusive"}
        print(f"[{'ok' if rc == 0 else 'FAILED'}] {t} ({dur}s, exclusive)", flush=True)

    # Classification of failures: rerun alone, then environment, then baseline.
    user = unprivileged_user(a.unprivileged_user)
    user_copy = None
    baseline = Path(a.baseline) if a.baseline else None
    baseline_ready = None
    baseline_user_copy = None
    regressions = {}
    for spec in a.regression_baseline:
        name, _, tree = spec.partition("=")
        if name and tree and ensure_baseline(Path(tree), a.jobs):
            regressions[name] = Path(tree)
        else:
            # F2: a named baseline that cannot be built is never "not applicable": it differs
            regressions[name or spec] = None
            print(f"certify: regression baseline {spec!r} unavailable; it counts as differing", file=sys.stderr)
    comparator_skipped = []
    # owner 2026-11-07 23:10: every result states the environment it ran in
    env_record = {**facts.get("environment", {}), "compiler": facts.get("cxx"),
                  "compiler_resolved": facts.get("cxx_resolved"),
                  "stdlib_sha256": (facts.get("profile_files") or {}).get("libstdc++.so"),
                  "binary_sha256": facts.get("candidate_sha256")}
    env_record["compiler_sha256"] = (facts.get("cxx_identity") or {}).get("sha256")  # C21
    env_record["cc_sha256"] = (facts.get("cc_identity") or {}).get("sha256")
    for t, r in sorted(results.items()):
        # Gather facts; the verdict comes only from decide(), which is pure and
        # table-tested (tests/runtime2/certification_guard.sh).
        f = {"target": t, "rc": r["rc"], "load_sensitive": t in LOAD_SENSITIVE or t in PERF_LANE, "user": user}
        if r["rc"] != 0:
            f["rerun_rc"], _ = run_target(ROOT, t, logdir / f"{t}.rerun.log", a.timeout, None, olds[t], extra)
        if r["rc"] != 0 and f["rerun_rc"] != 0 and f["load_sensitive"] and os.geteuid() == 0 and user:
            # Root evidence is kept as is ({t}.log, {t}.rerun.log); the unprivileged
            # run is the complete target, timing steps included.
            texts = [(logdir / f"{t}.log").read_text(errors="replace"), (logdir / f"{t}.rerun.log").read_text(errors="replace")]
            f["root_steps"] = tuple(failure_signature(x) for x in texts)
            f["root_reached_timing"] = any(reached_timing(x) for x in texts)
            if f["root_steps"][0] is not None and f["root_steps"][0] == f["root_steps"][1] and not f["root_reached_timing"]:
                if user_copy is None:
                    user_copy = Path(pwd.getpwnam(user).pw_dir) / "nift-certify-copy"
                    prepare_copy(ROOT, user_copy, user)
                f["unpriv_rc"], _ = run_target(user_copy, t, logdir / f"{t}.unprivileged.log", a.timeout, user, olds[t], extra)
        if r["rc"] != 0 and f["rerun_rc"] != 0 and not f["load_sensitive"]:
            text = (logdir / f"{t}.log").read_text(errors="replace") + (logdir / f"{t}.rerun.log").read_text(errors="replace")
            f["unverified"] = detect_unverified(text)
            cand_log, base_user = logdir / f"{t}.log", None
            if not f["unverified"] and os.geteuid() == 0 and user:
                if user_copy is None:
                    user_copy = Path(pwd.getpwnam(user).pw_dir) / "nift-certify-copy"
                    prepare_copy(ROOT, user_copy, user)
                f["unpriv_rc"], _ = run_target(user_copy, t, logdir / f"{t}.unprivileged.log", a.timeout, user, olds[t], extra)
                # The unprivileged run gets past root-only steps and may fail later:
                # it is the candidate result the baseline must be compared with.
                cand_log, base_user = logdir / f"{t}.unprivileged.log", user
            if not f["unverified"] and f.get("unpriv_rc") != 0 and baseline:
                if baseline_ready is None:
                    baseline_ready = ensure_baseline(baseline, a.jobs)
                if baseline_ready:
                    base_dir = baseline   # same user as the candidate log it is compared with
                    if base_user:
                        if baseline_user_copy is None:
                            baseline_user_copy = Path(pwd.getpwnam(base_user).pw_dir) / "nift-certify-baseline-copy"
                            prepare_copy(baseline, baseline_user_copy, base_user)
                        base_dir = baseline_user_copy
                    blog = logdir / f"{t}.baseline.log"
                    f["base_rc"], _ = run_target(base_dir, t, blog, a.timeout, base_user, olds[t], extra)
                    btext = blog.read_text(errors="replace")
                    f["base_has_target"] = not lacks_target(btext, t)
                    f["cand_step"] = failure_signature(cand_log.read_text(errors="replace"))
                    f["base_step"] = failure_signature(btext)
                    f["cand_log"] = str(cand_log.relative_to(ROOT))
                    croots = [ROOT, user_copy] if user_copy else [ROOT]
                    ctext0 = cand_log.read_text(errors="replace")
                    diff = FP.differences(FP.fingerprint(t, ctext0, croots), FP.fingerprint(t, btext, [base_dir]))
                    f["base_identical"], f["base_fp_diff"] = not diff, diff
                    # the stricter whole-log identity: reported, does not decide
                    r.setdefault("whole_log_identical", {})["v4.12.0"] = same_failure(
                        failure_identity(ctext0, croots), failure_identity(btext, [base_dir]))
                    base_tree_used = base_dir
            # C2: the regression baselines, each compared on its own (same step AND same FAIL lines).
            if not f["unverified"] and f.get("unpriv_rc") != 0 and regressions:
                ctext = cand_log.read_text(errors="replace")
                r["regression"], r["baselines"] = {}, {}
                for name, tree in regressions.items():
                    if tree is None:
                        r["regression"][name], r["baselines"][name] = False, "applicable, unavailable (counts as differing)"
                        continue
                    rlog = logdir / f"{t}.regression-{re.sub(r'[^\w.-]', '_', name)}.log"
                    rrc, _ = run_target(tree, t, rlog, a.timeout, None, olds[t], extra)
                    rtext = rlog.read_text(errors="replace")
                    # F2 (decision 2026-11-08), C1 (confirmation of C20): not applicable only when the
                    # baseline's own Makefile has no such target; a missing prerequisite, or a log that
                    # is harness-blocked there, is applicable and differing (no evidence either way)
                    if rrc != 0 and lacks_target(rtext, t):
                        r["regression"][name], r["baselines"][name] = None, "not applicable: no such target there"
                        continue
                    if rrc != 0 and blocked_reason(rtext):
                        r["regression"][name] = False
                        r["baselines"][name] = f"applicable, cannot run there ({blocked_reason(rtext)}): counts as differing"
                        r.setdefault("fingerprint_diff", {})[name] = ["the baseline log is harness-blocked"]
                        continue
                    diff = (FP.differences(FP.fingerprint(t, ctext, [ROOT]), FP.fingerprint(t, rtext, [tree]))
                            if rrc != 0 else ["the gate passes on this baseline"])
                    r["regression"][name] = not diff
                    r["baselines"][name] = "applicable, " + ("fingerprint equal" if not diff else
                                                             "passes there" if rrc == 0 else "fingerprint differs")
                    r.setdefault("fingerprint_diff", {})[name] = diff[:20]
                    r.setdefault("whole_log_identical", {})[name] = rrc != 0 and same_failure(
                        failure_identity(ctext, [ROOT]), failure_identity(rtext, [tree]))
        line_note = ""
        if r["rc"] != 0 and recipe.get(t, "").count("\n") > 1:
            # C2: the lines after the failing one would never run: each line on its own. H2a: a
            # baseline matches only if every line failing on the candidate fails identically there.
            cand_lines = run_lines(ROOT, t, olds[t], logdir, a.timeout, extra)
            r["lines"] = [(c[:200], lrc) for c, lrc, _ in cand_lines]
            failing = [i for i, (_, lrc, _) in enumerate(cand_lines, 1) if lrc]
            line_note = "recipe lines failing on their own: " + (", ".join(map(str, failing)) or "none")
            if f.get("base_identical"):
                bl = run_lines(base_tree_used, t, olds[t], logdir, a.timeout, extra, ".baseline")
                only = lines_match(cand_lines, bl, ROOT, base_tree_used)
                if only:
                    f["base_identical"] = False
                    line_note += f"; on the candidate only (not v4.12.0): {', '.join(map(str, only))}"
            for name, tree in regressions.items():
                if r.get("regression", {}).get(name):
                    tag = ".regression-" + re.sub(r"[^\w.-]", "_", name)
                    only = lines_match(cand_lines, run_lines(tree, t, olds[t], logdir, a.timeout, extra, tag), ROOT, tree)
                    if only:
                        r["regression"][name] = False
                        line_note += f"; on the candidate only (not {name}): {', '.join(map(str, only))}"
        r["status"], r["reason"] = decide(f)
        logtext = (logdir / f"{t}.log").read_text(errors="replace") if (logdir / f"{t}.log").exists() else ""
        r["blocked"] = blocked_reason(logtext)
        if COMPARATOR_SKIP in logtext:
            comparator_skipped.append(t)
        silent = False
        echo = recipe_echo(ROOT, t, olds[t])
        r["skipped"] = skipped_assertions(logtext, echo)
        if r["rc"] not in (None, 0) and t in FP.ABORT_CASES:
            # F3 (review of 4ec1aa80), D3: checks a failing script never reached, on every tree
            texts = [logtext] + [(logdir / f"{t}.line{i}.log").read_text(errors="replace")
                                 for i, (_, lrc) in enumerate(r.get("lines") or [], 1)
                                 if lrc and (logdir / f"{t}.line{i}.log").exists()]
            hidden = []
            for text in texts:
                hidden += [x for x in FP.not_executed(t, text, [ROOT]) if x not in hidden]
            r["skipped"] = r["skipped"] + hidden
        if r["rc"] == 0 and t not in SILENT_BY_DESIGN:  # R4-5: no line cap
            silent = not own_output(logtext, echo)
        # R4-1: a check skipped only because the run is root is run as the unprivileged user
        rootonly = [x for x in r["skipped"] if x[1].startswith("a non-root run")]
        if rootonly and r["rc"] == 0 and os.geteuid() == 0 and user:
            if user_copy is None:
                user_copy = Path(pwd.getpwnam(user).pw_dir) / "nift-certify-copy"
                prepare_copy(ROOT, user_copy, user)
            urc, _ = run_target(user_copy, t, logdir / f"{t}.unprivileged.log", a.timeout, user, olds[t], extra)
            r["unprivileged_check"] = urc
            if urc == 0:
                r["skipped"] = [x for x in r["skipped"] if x not in rootonly] + [
                    (l + " [ran as " + user + ": passed]", why, False) for l, why, _ in rootonly]
        elif rootonly:  # never run anywhere: a required check that did not execute
            r["skipped"] = [x for x in r["skipped"] if x not in rootonly] + [
                (l, why + " (no unprivileged run available)", True) for l, why, _ in rootonly]
        r["assertion"] = assertion_class(r["rc"], logtext, r["blocked"], silent, r["skipped"])
        r["skip_outcome"] = skip_outcome(r["skipped"], silent)
        if r.get("unprivileged_check") not in (None, 0):
            r["assertion"] = "FAIL"
        if silent:
            r["skipped"] = r["skipped"] + [("(no output of its own)", "a gate that prints its results "
                                            "(or an entry in SILENT_BY_DESIGN with a reason)", True)]
        r["class"] = report_class(r["status"], r.get("regression"), r["blocked"], r["assertion"])
        if r["assertion"] == "FAIL" and r["rc"] == 0:
            r["class"], r["reason"] = "FAILED-NEW", f"its root-skipped check failed as {user} (.build/certify/{t}.unprivileged.log)"
        r["environment"] = env_record
        if r["assertion"] in ("PARTIAL", "SKIP"):
            r["reason"] = (f"{r['assertion']}: required assertions skipped: "
                           + "; ".join(f"{l} [{why}]" for l, why, req in r["skipped"] if req))
        if r["class"] == "BLOCKED":
            r["reason"] = r["blocked"] or r["reason"]
        elif r["class"] == "FAILED-BASELINE" and not any(v is not None for v in (r.get("regression") or {}).values()):
            r["legacy_only"] = True  # F2: visibly distinct
            r["reason"] = "LEGACY-ONLY (no regression baseline applicable; v4.12.0 fingerprint equal): " + r["reason"]
        elif r["class"] == "FAILED-BASELINE":
            r["reason"] = "equal failure fingerprint on every applicable regression baseline: " + ", ".join(
                n for n, same in r["regression"].items() if same) + "; whole log identical: " + ", ".join(
                f"{n}={v}" for n, v in r.get("whole_log_identical", {}).items()) + "; " + r["reason"]
        if r["class"] == "FAILED-NEW" and r.get("fingerprint_diff"):
            r["reason"] += "; fingerprint differs: " + " / ".join(
                f"{n}: {'; '.join(d[:3])}" for n, d in r["fingerprint_diff"].items() if d)[:600]
        if r["class"].startswith("FAILED") and r.get("baselines"):
            r["reason"] += "; baselines: " + "; ".join(f"{n}: {w}" for n, w in r["baselines"].items())
        if line_note:
            r["reason"] += "; " + line_note  # H2a: never overwritten
    for t in planned:
        if t not in results:
            results[t] = {"rc": None, "status": None, "class": "NOT-RUN", "reason": "planned, not run"}

    classes = ["FAILED-NEW", "FAILED-BASELINE", "BLOCKED", "NOT-RUN", "PASSED"]
    counts = {k: sum(1 for r in results.values() if r["class"] == k) for k in classes}
    order = ["FAIL", "BASELINE-FAILURE", "UNVERIFIED", "SKIPPED-ENVIRONMENTAL", "FLAKY", "PASS"]
    detail = {k: sum(1 for r in results.values() if r.get("status") == k) for k in order}
    summary = {
        "root": str(ROOT), "commit": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip(),
        "euid": os.geteuid(), "jobs": a.jobs, "seconds": round(time.time() - started, 1),
        "counts": counts, "decide_counts": detail, "exclusive": exclusive, "preflight": {"facts": facts, "problems": pf},
        "baselines": {"v4.12.0 (legacy compatibility)": str(baseline) if baseline else None,
                      **{f"{n} (regression)": str(p) for n, p in regressions.items()},
                      "approved differences": "docs/runtime-2/evidence/b5/expected-changes.tsv (checked by the gates)"},
        "comparator_skipped": comparator_skipped,
        "environment": env_record,
        "certified_binary": {"sha256": facts.get("candidate_sha256"),
                             "profile": (facts.get("profile") or {}).get("compiler"),
                             "compiler": facts.get("cxx"), "compiler_resolved": facts.get("cxx_resolved"),
                             "stdlib_sha256": (facts.get("profile_files") or {}).get("libstdc++.so"),
                             "build_invocation": facts.get("build_invocation"),
                             "build_signature": facts.get("build_sig"), "source": facts.get("source")},
        "targets": {t: results[t] for t in sorted(results)},
    }
    (logdir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    lines = [f"certify @ {summary['commit'][:7]}: " + ", ".join(f"{k} {v}" for k, v in counts.items())]
    for k in classes[:-1]:
        for t in sorted(t for t, r in results.items() if r["class"] == k):
            lines.append(f"  {k:<16} {t}: {results[t]['reason']}")
    if comparator_skipped:
        lines.append("CAMPAIGN FAILED: comparator coverage skipped in " + ", ".join(comparator_skipped))
    # D3: every required assertion that did not execute, and in certification mode the campaign fails
    # when a mandatory gate is PARTIAL or SKIP.
    unexecuted = [(t, l, why, req) for t in sorted(results) for l, why, req in results[t].get("skipped", [])]
    summary["unexecuted_assertions"] = [{"gate": t, "line": l, "requirement": why, "required": req}
                                        for t, l, why, req in unexecuted]
    if unexecuted:
        lines.append(f"ASSERTIONS NOT EXECUTED: {len(unexecuted)} "
                     f"({sum(1 for x in unexecuted if x[3])} required, {sum(1 for x in unexecuted if not x[3])} not required here)")
        lines += [f"  {'REQUIRED' if req else 'not-required'} {t}: {l}  [needs: {why}]" for t, l, why, req in unexecuted]
    # every planned gate is a required gate of the certification plan (MANDATORY is the subset the
    # plan check insists on)
    partial_mandatory = sorted(t for t, r in results.items() if r.get("assertion") in ("PARTIAL", "SKIP"))
    summary["assertion_counts"] = {k: sum(1 for r in results.values() if r.get("assertion") == k)
                                   for k in ("PASS", "PARTIAL", "SKIP", "FAIL", "BLOCKED")}
    certify_mode = a.certify or full
    verdict, gaps, why_failed = campaign_verdict(results, counts, comparator_skipped, a.strict, hidden_table)
    summary["verdict"] = verdict
    summary["not_executed_required"] = [{"gate": t, "check": l, "category": c, "category_reason": cr}
                                        for t, l, c, cr in gaps]
    lines.append(f"CAMPAIGN VERDICT: {verdict} (exit {VERDICT_EXIT[verdict]})"
                 + ("" if certify_mode else " [not a --certify campaign]"))
    if verdict == "FAILED":
        lines += [f"  FAILED: {w}" for w in why_failed]
    if gaps:
        lines.append(f"REQUIRED CHECKS NOT EXECUTED: {len(gaps)} (category A: {sum(1 for g in gaps if g[2] == 'A')}, "
                     f"B: {sum(1 for g in gaps if g[2] == 'B')})")
        lines += [f"  [{c}] {t}: {l}  ({cr})" for t, l, c, cr in gaps]
    if verdict == "PARTIAL":
        lines.append("PARTIAL is not a pass: required checks did not execute (owner 2026-11-08 06:46)")
    if gaps and any(g[2] == "A" for g in gaps):
        lines.append("NOT RELEASE-QUALIFYING: a Category A (release-critical) check did not execute")
    (logdir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    not_passed = sum(counts[k] for k in classes[:-1])
    if not_passed:
        lines.append(f"NOT PASSED: {not_passed} required gate(s) are not PASSED (only PASSED counts as a pass)")
    (logdir / "summary.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return VERDICT_EXIT[verdict]


# owner 2026-11-08 06:46: three campaign verdicts, each with its own exit status.
VERDICT_EXIT = {"CERTIFIED": 0, "FAILED": 1, "PARTIAL": 4}


def campaign_verdict(results, counts, comparator_skipped, strict, hidden_table):
    """(verdict, gaps, why_failed). FAILED: a FAILED-NEW gate, comparator coverage skipped, a gate
    that did not run or is blocked for any reason other than required checks that did not execute
    (a SKIP or NOT-RUN gate, an environment block), or FAILED-BASELINE under --strict. PARTIAL:
    otherwise, when a REQUIRED check did not execute (a gate that passed with a required skip, or
    checks an abort hid): every executed assertion passed or is FAILED-BASELINE. CERTIFIED: the
    rest. gaps: [(gate, check, category, reason)] for every required check not executed."""
    gaps = []
    for t in sorted(results):
        for label, _why, req in results[t].get("skipped") or []:
            if req:
                cat, reason = FP.hidden_category(hidden_table, t, label)
                gaps.append((t, label, cat, reason))
    why = []
    if counts["FAILED-NEW"]:
        why.append("FAILED-NEW: " + ", ".join(sorted(t for t, r in results.items() if r["class"] == "FAILED-NEW")))
    if comparator_skipped:
        why.append("comparator coverage skipped in " + ", ".join(comparator_skipped))
    incomplete = sorted(t for t, r in results.items()
                        if r["class"] == "NOT-RUN" or (r["class"] == "BLOCKED" and r.get("assertion") != "PARTIAL"))
    if incomplete:
        why.append("not run or blocked (not only by unexecuted checks): " + ", ".join(incomplete))
    if strict and counts["FAILED-BASELINE"]:
        why.append("FAILED-BASELINE under --strict")
    if why:
        return "FAILED", gaps, why
    return ("PARTIAL" if gaps else "CERTIFIED"), gaps, []


if __name__ == "__main__":
    sys.exit(main())
