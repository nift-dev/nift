#!/usr/bin/env bash
# D-010: `make certify` runs certify-targets (scripts/certify.py). This guard
# keeps that set complete, without running anything:
#  1. certify-targets reaches the required comprehensive suites;
#  2. every `make <target>` the CI certification workflows run is reached;
#  3. every test/script the workflows launch directly (bash tests/..., python
#     scripts/..., ./tests/...) is invoked by the recipe of some reached target,
#     or is listed in CI_ONLY below with the reason it cannot run locally.
set -u -o pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
REQUIRED=(test-all test-conformance test-host-seam test-c-abi test-c-abi-c-smoke test-embed test-bindings
          test-runtime2-vm-corpus test-runtime2-rt-diff test-runtime2-rt-diff-selftest test-runtime2-modified-race test-runtime2-dynamic-reads test-runtime2-fs-freedom test-rt-value test-rt-lint
          test-rt-parser test-sha256 test-site-dx
          test-rt-vm test-runtime2-cp16-n1 test-build-recovery test-runtime2-cp13b-mutants test-runtime2-cp14b-mutants test-runtime2-cp18 test-runtime2-cp18-switch test-runtime2-cp18-sanitize test-runtime2-cp18-cow
          test-runtime2-cp22-mutants test-cp19-certify test-cp23-certify)
WORKFLOWS=(.github/workflows/test-integrity.yml .github/workflows/checkpoint-10-cross-platform.yml .github/workflows/v44-cross-platform.yml)
python3 - "${WORKFLOWS[@]}" -- "${REQUIRED[@]}" <<'PY'
import re, sys
sys.path.insert(0, 'scripts')
import certify

# Scripts CI runs that have no local equivalent, with the reason.
CI_ONLY = {
    'scripts/compare_checkpoint10.py': 'compares the Linux, macOS and Windows corpus outputs produced by three CI runners',
}

args = sys.argv[1:]; sep = args.index('--')
workflows, required = args[:sep], args[sep + 1:]
ci_targets, ci_scripts = set(), set()
for wf in workflows:
    text = open(wf).read()
    # Join shell (\) and PowerShell (`) line continuations.
    text = re.sub(r'[\\`]\n\s*', ' ', text)
    for line in text.split('\n'):
        for m in re.finditer(r'\bmake(?:\s+-\S+)*((?:\s+[a-z0-9][a-z0-9_.-]*)+)', line):
            ci_targets.update(t for t in m.group(1).split() if re.match(r'^(test|bh)', t))
        for m in re.finditer(r'(?:\b(?:bash|sh|python3?|node|pwsh)\s+|(?<![\w/.])\./)((?:tests|scripts)/[\w./-]+\.(?:sh|py|js|ps1|f))', line):
            ci_scripts.add(m.group(1))
deps, recipe = certify.make_db(certify.ROOT, 'certify-targets')
seen, stack = set(), ['certify-targets']
while stack:
    t = stack.pop()
    if t in seen: continue
    seen.add(t); stack.extend(deps.get(t, ()))
reached_recipes = '\n'.join(recipe.get(t, '') for t in seen)
failures = []
for t in sorted(set(required) | ci_targets):
    if t not in seen:
        failures.append(f'make certify-targets does not reach {t}')
for s in sorted(ci_scripts):
    if s in CI_ONLY:
        continue
    if s not in reached_recipes:
        failures.append(f'CI runs {s} directly, but no certify-targets recipe invokes it (add a target or list it in CI_ONLY with a reason)')
if not ci_targets:
    failures.append('no make targets found in CI workflows')
for f in failures:
    print('FAIL certification guard: ' + f)
if failures:
    sys.exit(1)
print(f'PASS certification guard: certify-targets reaches {len(required)} required and {len(ci_targets)} CI-run targets; '
      f'{len(ci_scripts) - len([s for s in ci_scripts if s in CI_ONLY])} directly-run CI scripts covered, '
      f'{len([s for s in ci_scripts if s in CI_ONLY])} CI-only: ' + ', '.join(f'{k} ({v})' for k, v in CI_ONLY.items() if k in ci_scripts))
PY
# Without set -e the script's status was the later self-test's, so an
# incomplete certify-targets printed FAIL and still exited 0 (found 2026-10-23:
# test-cp19-s1-dispatch-unit was not reached).
inventory_status=$?
[ "$inventory_status" -eq 0 ] || exit "$inventory_status"

# Removal controls for required edges: each child must be reached through its
# named parent, and removing that specific edge must make the guard fail even if
# another path keeps the child reachable. N1: test-rt-vm -> test-runtime2-cp16-n1.
# CP20 F6: the cleanup controls, mutants and sanitizer cases hang off the Host
# functional, mutation and sanitizer targets respectively.
python3 - <<'PYCONTROL'
import sys
sys.path.insert(0, 'scripts')
import certify
deps, _ = certify.make_db(certify.ROOT, 'certify-targets')
EDGES = [('test-rt-vm', 'test-runtime2-cp16-n1'),
         ('test-rt-host', 'test-runtime2-f6-cleanup'),
         ('test-rt-host-mutants', 'test-runtime2-f6-mutants'),
         ('test-rt-host-sanitize', 'test-runtime2-f6-sanitize'),
         ('test-rt-vm', 'test-runtime2-cp13b'),
         ('test-embed', 'test-runtime2-cp18'),
         ('test-runtime2-cp18', 'test-runtime2-cp18-switch'),
         ('test-runtime2-cp18', 'test-runtime2-cp18-cow'),
         ('test-embed', 'test-runtime2-cp18-sanitize'),
         ('test-rt-vm', 'test-rt-source-map'),
         ('test-rt-vm', 'test-rt-error-oom'),
         ('test-rt-vm', 'test-cp25r'),
         ('test-rt-vm-sanitize', 'test-rt-source-map-sanitize'),
         ('test-rt-vm-sanitize', 'test-rt-source-map-tsan'),
         ('test-rt-vm-sanitize', 'test-rt-error-oom-sanitize'),
         ('test-rt-vm', 'test-runtime2-cp14b'),
         ('test-rt-vm', 'test-runtime2-cp14b-sort'),
         ('test-rt-vm', 'test-runtime2-cp14b-witnesses'),
         ('test-rt-vm-sanitize', 'test-runtime2-cp14b-sanitize'),
         ('test-rt-vm', 'test-runtime2-d040-witnesses'),
         ('test-rt-vm', 'test-runtime2-cp14b-profile'),
         # CP17 + N2 (2026-10-27): each CP17 suite through one tier edge.
         ('test-rt-vm', 'test-runtime2-cp17-tripwire'),
         ('test-rt-host', 'test-rt-handles'),
         ('test-rt-host', 'test-rt-handles-guards'),
         ('test-rt-host', 'test-runtime2-cp17-handles'),
         ('test-rt-host', 'test-runtime2-cp17-n2-rebuild'),
         ('test-rt-host-sanitize', 'test-rt-handles-sanitize'),
         ('test-rt-host-sanitize', 'test-rt-handles-tsan'),
         ('test-rt-host-sanitize', 'test-runtime2-cp17-sanitize'),
         ('test-rt-host-mutants', 'test-runtime2-cp17-routing-mutants'),
         ('test-rt-host-mutants', 'test-runtime2-cp17-n2-rebuild-mutant'),
         ('test-rt-host-mutants', 'test-runtime2-cp17-handle-mutants'),
         ('test-rt-host-mutants', 'test-runtime2-cp17-unwind-mutants'),
         ('test-rt-host', 'test-runtime2-cp17-handle-allocation'),
         ('test-rt-vm', 'test-runtime2-cp22'),
         ('test-rt-vm-sanitize', 'test-runtime2-cp22-sanitize'),
         ('test-rt-vm-sanitize', 'test-runtime2-cp22-tsan'),
         ('test-v45-job-control', 'test-cp21-interactive-host'),
         ('test-v45-job-control', 'test-cp21-interactive-host-mutants'),
         ('test-v45-job-control', 'test-cp21-make-failure-control')]
if sys.platform.startswith('linux'):
    EDGES += [('test-v45-job-control', 'test-cp21-terminal-recovery'),
              ('test-v45-job-control', 'test-cp21-terminal-recovery-mutants'),
              ('test-v45-job-control', 'test-cp21-d3-mandatory'),
              ('test-cp21-fd-inheritance', 'test-cp21-fd-protocol-unit'),
              ('test-cp21-d3-mandatory', 'test-cp21-d3-mutant'),
              ('test-cp21-d3-mandatory', 'test-cp21-parser-target-mutant')]

# CP19 certification reachability (2026-10-29): every accepted CP19 gate.
EDGES += [('test-cp19-certify', 'test-cp19-functional'),
          ('test-cp19-certify', 'test-cp19-mutants'),
          ('test-cp19-certify', 'test-cp19-sanitize'),
          ('test-cp19-functional', 'test-cp19-jobs'),
          ('test-cp19-functional', 'test-cp19-session'),
          ('test-cp19-functional', 'test-cp19-resources'),
          ('test-cp19-functional', 'test-cp19-proof'),
          ('test-cp19-functional', 'test-cp19-operand-windows'),
          ('test-cp19-functional', 'test-cp19-job-cli'),
          ('test-cp19-functional', 'test-cp19-resource-cli'),
          ('test-cp19-functional', 'test-cp19-session-cli'),
          ('test-cp19-mutants', 'test-cp19-job-mutants'),
          ('test-cp19-mutants', 'test-cp19-proof-mutants'),
          ('test-cp19-mutants', 'test-cp19-resource-mutants'),
          ('test-cp19-mutants', 'test-cp19-session-mutants'),
          ('test-cp19-mutants', 'test-cp19-session-mutants-switch'),
          ('test-cp19-mutants', 'test-cp19-admission-mutants'),
          ('test-cp19-mutants', 'test-cp19-admission-mutants-switch'),
          ('test-cp19-mutants', 'test-cp19-resource-alloc'),
          ('test-cp19-mutants', 'test-cp19-proof-alloc'),
          ('test-cp19-sanitize', 'test-cp19-session-sanitize'),
          ('test-cp19-sanitize', 'test-cp19-proof-sanitize'),
          ('test-cp19-sanitize', 'test-cp19-admission-sanitize')]
if sys.platform.startswith('linux'):
    EDGES += [('test-cp19-certify', 'test-cp19-host'), ('test-cp19-host', 'test-cp19-cleanup-cli'),
              ('test-cp19-sanitize', 'test-cp19-host-sanitize')]
if not sys.platform.startswith('win'):
    EDGES += [('test', 'test-cp19-shell-exception')]
# CP23 certification reachability (CP19 x CP23 integration, 2026-10-29): Linux local gates.
if sys.platform.startswith('linux'):
    EDGES += [('test-cp23-certify', 'test-runtime2-cp23'), ('test-cp23-certify', 'test-cp23-cancellation'),
              ('test-cp23-certify', 'test-cp23-cp19-combined'), ('test-cp23-certify', 'test-cp23-mutants'),
              ('test-cp23-certify', 'test-cp23-allocation'), ('test-cp23-certify', 'test-runtime2-cp23-sanitize'),
              ('test-cp23-certify', 'test-runtime2-cp23-tsan')]
# test-cp19-proof-cli is planned by scripts/certify.py's inventory (C1) and runs against the
# explicit v4.12.0 legacy binary (--legacy-oracle; owner 2026-11-07 18:00 D3); without one the
# campaign preflight fails and the gate is BLOCKED, never passed.

def reachable(graph):
    seen, stack = set(), ['certify-targets']
    while stack:
        t = stack.pop()
        if t in seen: continue
        seen.add(t); stack.extend(graph.get(t, ()))
    return seen

def remove_edge(graph, parent, child):
    return {k: [d for d in v if not (k == parent and d == child)] for k, v in graph.items()}

def removal_ok(graph, parent, child):
    if child not in graph.get(parent, []):
        return False
    return child not in reachable(remove_edge(graph, parent, child))

for parent, child in EDGES:
    if child not in reachable(deps):
        print(f'FAIL certification guard removal control: {child} is not reached')
        sys.exit(1)
    if child not in deps.get(parent, []):
        print(f'FAIL certification guard removal control: the {parent} -> {child} edge is absent')
        sys.exit(1)
    if not removal_ok(deps, parent, child):
        print(f'FAIL certification guard removal control: {child} is still reached without its {parent} edge')
        sys.exit(1)
    # Alternate-edge control: a direct certify-targets edge must not satisfy
    # the removal predicate merely because the child stays reachable.
    alt = {k: list(v) for k, v in deps.items()}
    alt.setdefault('certify-targets', []).append(child)
    alt_reduced = remove_edge(alt, parent, child)
    if child not in reachable(alt_reduced):
        print(f'FAIL certification guard alternate-edge control: the synthetic edge did not reach {child}')
        sys.exit(1)
    if removal_ok(alt_reduced, parent, child):
        print(f'FAIL certification guard alternate-edge control: an alternate edge satisfied {child}')
        sys.exit(1)
    print(f'PASS certification guard removal control: the {parent} -> {child} edge is required')
print('PASS certification guard alternate-edge control: direct certify-targets edges are rejected')
PYCONTROL
removal_status=$?
[ "$removal_status" -eq 0 ] || exit "$removal_status"

# Combined-candidate harness (C1-C3, docs/runtime-2/checkpoints/combined-b3-b5.md). C1: the plan
# (certify-targets plus the Runtime 2.0 inventory derived from the Make database) covers every
# inventory and mandatory gate; negative controls: dropping one mandatory gate, or one inventory
# gate, from the plan makes certification preparation fail.
python3 scripts/certify.py --check-plan || exit 1
for dropped in test-runtime2-b5 test-runtime2-b4b-fixpoint test-cp19-proof-cli test-runtime2-diagnostics; do
    out=$(python3 scripts/certify.py --check-plan --drop "$dropped" 2>&1); rc=$?
    if [ "$rc" -ne 1 ] || ! grep -q "$dropped is not in the certification plan" <<<"$out"; then
        echo "FAIL certification guard negative control: dropping $dropped from the plan was not detected (exit $rc)"; exit 1
    fi
    echo "PASS certification guard negative control: dropping $dropped from the plan fails preparation"
done
python3 - <<'PYC'
import sys
sys.path.insert(0, 'scripts')
import certify
ip, rc, pp, fl, br = (certify.inventory_problems, certify.report_class, certify.preflight_problems,
                      certify.fail_lines, certify.blocked_reason)
INV = ['test-runtime2-a', 'test-runtime2-b', 'test-runtime2-prep']
DEF = set(INV) | {'test-x'}
GOOD = dict(cxx='g++ (GCC) 15.2.1 20260321', glibcxx=20260321, comparator='ok', require_env='1',
            oracle='/o/nift', oracle_version='v4.12.0', oracle_commit=certify.V4120_COMMIT, oracle_clean=True,
            oracle_current=True, campaign_start=1000, oracle_mtime=2000, oracle_sha256='aa', candidate_sha256='bb',
            oracle_realpath='/o/nift', candidate_realpath='/c/nift')
import json
PROFILE = json.load(open('tests/runtime2/cp14b/toolchain-profile.json'))
PROF_FILES = {k: v['sha256'] for k, v in PROFILE['files'].items()}
PROF_OK = {**GOOD, 'cxx': PROFILE['compiler'], 'profile': PROFILE, 'profile_files': PROF_FILES}
import os
BUILD_OK = {'build_rc': 0, 'cxx_resolved': '/usr/bin/x86_64-linux-gnu-g++-15', 'cxx_version_sha256': 'aa' * 32,
            'build_sig': {'resolved': '/usr/bin/x86_64-linux-gnu-g++-15', 'version-sha256': 'aa' * 32,
                          'args': ['-include', 'src/build/production_tu.h'] + PROFILE['flags'].split()}}
EXP = BUILD_OK['build_sig']['args']
CC_OK = {'cxx_identity': {'path': '/usr/bin/x86_64-linux-gnu-g++-15', 'elf': True, 'sha256': 'aa' * 32,
                          'package': 'g++-15-x86-64-linux-gnu', 'version': PROFILE['compiler_version_contains']},
         'cc_identity': {'path': '/usr/bin/x86_64-linux-gnu-gcc-15', 'elf': True, 'sha256': 'bb' * 32,
                         'package': 'gcc-15-x86-64-linux-gnu', 'version': PROFILE['compiler_version_contains']}}
C_ARGS = ['-Imarkuppp/vendor/cmark', '-std=c99', '-O2', '-Wall', '-Wextra', '-pedantic']
C_OK = {'build_sig_c': {'resolved': '/usr/bin/gcc-15', 'args': C_ARGS}, 'expected_args_c': C_ARGS,
        'cc_resolved': '/usr/bin/gcc-15'}
os.environ['GNUMAKEFLAGS'] = 'CXXFLAGS=-O0'
assert 'GNUMAKEFLAGS' not in certify.clean_env(), 'clean_env keeps GNUMAKEFLAGS'
del os.environ['GNUMAKEFLAGS']
checks = [
    ('inventory complete', ip({'test-runtime2-a', 'test-runtime2-b'}, INV, {'test-runtime2-prep': 'r'}, ['test-runtime2-a'], DEF), []),
    ('inventory gate missing', len(ip({'test-runtime2-a'}, INV, {'test-runtime2-prep': 'r'}, [], DEF)), 1),
    ('mandatory gate missing', len(ip({'test-runtime2-b'}, INV, {'test-runtime2-prep': 'r', 'test-runtime2-a': 'r'},
                                     ['test-runtime2-a'], DEF)), 1),
    ('stale non-gate', len(ip({'test-runtime2-a', 'test-runtime2-b'}, INV, {'test-runtime2-prep': 'r', 'gone': 'r'}, [], DEF)), 1),
    ('mandatory undefined', len(ip(set(INV), INV, {}, ['test-gone'], DEF)), 1),
    ('class pass', rc('PASS'), 'PASSED'),
    ('class not run', rc(None), 'NOT-RUN'),
    ('class fail', rc('FAIL'), 'FAILED-NEW'),
    ('class flaky is a failure', rc('FLAKY'), 'FAILED-NEW'),
    ('class v4.12.0 baseline', rc('BASELINE-FAILURE'), 'FAILED-BASELINE'),
    # F2 (decision 2026-11-08): equal on EVERY applicable regression baseline (None: not applicable)
    ('class regression: equal on one, differs on the other (any vs all)', rc('FAIL', {'17f7964c': True, 'weekend/r2': False}), 'FAILED-NEW'),
    ('class regression: equal on every baseline', rc('FAIL', {'17f7964c': True, 'weekend/r2': True}), 'FAILED-BASELINE'),
    ('class regression: equal where applicable', rc('FAIL', {'17f7964c': True, 'weekend/r2': None}), 'FAILED-BASELINE'),
    ('class regression: none applicable, no v4.12.0 match', rc('FAIL', {'17f7964c': None, 'weekend/r2': None}), 'FAILED-NEW'),
    ('class v4.12.0 match but a regression baseline passes or differs', rc('BASELINE-FAILURE', {'17f7964c': False}), 'FAILED-NEW'),
    ('class v4.12.0 match, regression equal', rc('BASELINE-FAILURE', {'17f7964c': True}), 'FAILED-BASELINE'),
    ('class v4.12.0 match, no regression applicable (legacy-only)', rc('BASELINE-FAILURE', {'17f7964c': None}), 'FAILED-BASELINE'),
    ('class regression baseline differs', rc('FAIL', {'17f7964c': False}), 'FAILED-NEW'),
    ('class unverified', rc('UNVERIFIED'), 'BLOCKED'),
    ('class root-only', rc('SKIPPED-ENVIRONMENTAL'), 'BLOCKED'),
    ('class harness-blocked fail', rc('FAIL', None, 'no oracle'), 'BLOCKED'),
    ('class pass with a comparator skip is not a pass', rc('PASS', None, 'skipped'), 'BLOCKED'),
    ('blocked: no oracle', br('FAIL test-cp19-proof-cli: CP19_LEGACY_ORACLE is unset. This gate') is not None, True),
    ('blocked: comparator skip', br('SKIP UNVERIFIED COMPARATOR COVERAGE: x: 3 comparator case(s)') is not None, True),
    ('not blocked', br('FAIL b5 fixtures: 1 of 9'), None),
    ('blocked: no scanner reference', br('ERROR scanner parity: NIFT_REF must name a reference nift binary') is not None, True),
    ('fail lines normalised', fl('FAIL /a/t/x.f: bad\nok\nFAIL /tmp/q1/y\n', ['/a']) == fl('FAIL /b/t/x.f: bad\nFAIL /tmp/z9/y\n', ['/b']), True),
    ('fail lines differ', fl('FAIL x: 1\n') == fl('FAIL x: 2\n'), False),
    # H2b: no FAIL lines on either side is not an identical failure by itself
    ('identity: crash vs assertion, no FAIL lines', certify.same_failure(
        certify.failure_identity('running tests/c.sh\nSegmentation fault\nmake: *** [M:3: t] Error 139\n'),
        certify.failure_identity('running tests/c.sh\nassertion x failed\nmake: *** [M:3: t] Error 2\n')), False),
    ('identity: empty logs never match', certify.same_failure(certify.failure_identity(''), certify.failure_identity('')), False),
    ('identity: same FAIL lines and status match', certify.same_failure(
        certify.failure_identity('running tests/k.sh\nFAIL k: 1\nmake: *** [M:3: t] Error 1\n', ['/a']),
        certify.failure_identity('running tests/k.sh\nFAIL k: 1\nmake: *** [M:9: t] Error 1\n', ['/b'])), True),
    ('identity: same FAIL lines, other status', certify.same_failure(
        certify.failure_identity('running tests/k.sh\nFAIL k: 1\nmake: *** [M:3: t] Error 1\n'),
        certify.failure_identity('running tests/k.sh\nFAIL k: 1\nmake: *** [M:3: t] Error 2\n')), False),
    ('preflight ok', pp(GOOD), []),
    ('preflight GCC 13', len(pp({**GOOD, 'cxx': 'g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0'})), 1),
    ('preflight clang', len(pp({**GOOD, 'cxx': 'clang version 15.2.0'})), 1),
    ('preflight libstdc++ datestamp', len(pp({**GOOD, 'glibcxx': 20250808})), 1),
    ('preflight comparator refused', len(pp({**GOOD, 'comparator': 'a comparator sort exits 1'})), 1),
    ('preflight switch unset', len(pp({**GOOD, 'require_env': None})), 1),
    ('preflight no oracle', len(pp({**GOOD, 'oracle': None})), 1),
    ('preflight wrong oracle', len(pp({**GOOD, 'oracle_version': 'v4.11.0'})), 1),
    ('preflight oracle from another commit (a Runtime 2.0 build reports v4.12.0 too)',
        len(pp({**GOOD, 'oracle_commit': '7a89b77a'})), 1),
    ('preflight oracle checkout modified', len(pp({**GOOD, 'oracle_clean': False})), 1),
    ('preflight oracle not built from its checkout', len(pp({**GOOD, 'oracle_current': False})), 1),
    # D4 (owner 2026-11-07 22:13): built during the campaign; distinct from the candidate
    ('preflight no campaign start', len(pp({**GOOD, 'campaign_start': None})), 1),
    ('preflight oracle built before the campaign', len(pp({**GOOD, 'oracle_mtime': 999})), 1),
    ('preflight oracle is the candidate (sha256)', len(pp({**GOOD, 'oracle_sha256': 'bb'})), 1),
    ('preflight oracle is the candidate (path)', len(pp({**GOOD, 'oracle_realpath': '/c/nift'})), 1),
    # D3: assertion classes
    ('assertions pass', certify.assertion_class(0, 'PASS a: 3 checks\n'), 'PASS'),
    ('assertions partial (known RAM condition)', certify.assertion_class(0,
        'PASS read limits\nSKIP default 1 GiB limit (MemAvailable 900 MiB < 4096)\n'), 'PARTIAL'),
    ('assertions skip (all skipped)', certify.assertion_class(0, 'SKIP package refs/local lock (git unavailable)\n'), 'SKIP'),
    ('assertions unknown skip is required', certify.assertion_class(0, 'PASS x\nskipping the y check\n'), 'PARTIAL'),
    ('assertions other-platform skip is not required', certify.assertion_class(0,
        'PASS x\nSKIP CP20 Windows pipeline (POSIX host)\n'), 'PASS'),
    ('assertions zero skipped is not a skip', certify.assertion_class(0, 'PASS N1: 16 checks, 0 failure(s), 0 skipped\n'), 'PASS'),
    ('assertions fail', certify.assertion_class(1, 'FAIL x\n'), 'FAIL'),
    ('assertions blocked', certify.assertion_class(1, 'x', 'no oracle'), 'BLOCKED'),
    ('class partial', rc('PASS', None, None, 'PARTIAL'), 'BLOCKED'),
    # R3-1: the in-tree skip wordings (case-insensitive; zero counts are not skips)
    *[('marker: ' + t, bool(certify.skipped_assertions(t)), want) for t, want in [
        ('test-serve: SKIPPED (unsupported platform or python3 unavailable; nothing was tested)', True),
        ('test-progress-pty: skipped (PTY tooling unavailable)', True), ('  (skipped: python3 unavailable)', True),
        ('  (fuzz skipped: no compiler)', True), ('escapees_subreaper: SKIPPED (no PR_SET_CHILD_SUBREAPER)', True),
        ('skip: x', True), ('[SKIP] y', True), ('NOT RUN z', True), ('UNVERIFIED: q', True), ('ok 2 # SKIP', True),
        ('skipped: 3', True), ('..s..', True), ('progress PTY smoke SKIPPED: NOT verified', True),
        ('PASS N1: 16 checks, 0 failure(s), 0 skipped', False), ('skipped: 0', False),
        ('ok   consumer control skipped-float: rejected', False), ('python3 x.py --skip-million', False),
        ('PASS tty REPL: statement not run and left no definition', False)]],
    # R3-5: other-platform entries are anchored
    ('anchored platform skip', certify.assertion_class(0, 'PASS x\nSKIP TSan lane (Windows-style runners differ)\n'), 'PARTIAL'),
    # R3-4: no output of its own
    ('silent gate', certify.assertion_class(0, '', None, True), 'SKIP'),
    ('own output excludes echoed commands', certify.own_output('$ make -k t\npython3 x.py\nmake[1]: Leaving directory\n', ['python3 x.py']), []),
    # R3-3: addresses only in pointer contexts
    ('hex data kept', certify.normalise('FAIL bytes: expected 0xdeadbeef got 0xdeadbeee'), 'FAIL bytes: expected 0xdeadbeef got 0xdeadbeee'),
    ('sanitizer frame address', certify.normalise('    #3 0x55d0a1b2c3d4 in f'), '#3 <ADDR> in f'),
    ('address context', certify.normalise('READ of size 8 at 0x6020000000f0 thread T0'), 'READ of size 8 at <ADDR> thread T0'),
    # R3-6: host requirements
    ('preflight tool missing', len(pp({**GOOD, 'tools': {'go': '', 'git': 'git version 2.43'}})), 1),
    ('preflight memory', len(pp({**GOOD, 'mem_mib': 3900})), 1),
    ('preflight disk', len(pp({**GOOD, 'disk_gib': 10})), 1),
    ('preflight TSan mmap bits', len(pp({**GOOD, 'mmap_rnd_bits': 32})), 1),
    ('preflight loopback', len(pp({**GOOD, 'loopback': False})), 1),
    ('preflight pty', len(pp({**GOOD, 'pty': False})), 1),
    # C14: the D4 toolchain profile (tests/runtime2/cp14b/toolchain-profile.json)
    *[(n, len(pp({**PROF_OK, **kw})), want) for n, kw, want in [
        ('profile matches', {}, 0),
        ('profile: compiler build differs', {'cxx': 'g++ (Ubuntu 15.2.0-4ubuntu4) 15.2.0'}, 1),
        ('profile: libstdc++.so differs', {'profile_files': {**PROF_FILES, 'libstdc++.so': '00'}}, 1),
        ('profile: stl_algo.h differs', {'profile_files': {**PROF_FILES, 'bits/stl_algo.h': '00'}}, 1),
        ('profile: stl_tempbuf.h differs', {'profile_files': {**PROF_FILES, 'bits/stl_tempbuf.h': None}}, 1),
        # R4-3: the certified binary is bound to the profile: built by $CXX with the profile flags
        ('build: same compiler and flags', {**BUILD_OK}, 0),
        ('build: other compiler, same __GLIBCXX__ (e.g. the 15-20260321 snapshot)',
            {**BUILD_OK, 'build_sig': {**BUILD_OK['build_sig'], 'resolved': '/opt/gcc-15-20260321/bin/g++',
                                       'version-sha256': 'ee' * 32}}, 1),
        ('build: profile flag missing', {**BUILD_OK, 'build_sig': {**BUILD_OK['build_sig'], 'args': ['-std=c++17', '-O3']}}, 2),  # also the direct profile-flag check (F3)
        ('build: make nift failed', {**BUILD_OK, 'build_rc': 2}, 1),
        ('build: CXXFLAGS overridden', {**BUILD_OK, 'cxxflags_env': '-O3 -march=native'}, 1),
        ('build: CXXFLAGS equal to the profile', {**BUILD_OK, 'cxxflags_env': PROFILE['flags']}, 0),
        # R5-2: exact arguments, CPPFLAGS/LDFLAGS, the source tree, the signature itself
        ('build: exact arguments', {**BUILD_OK, 'expected_args': EXP}, 0),
        ('build: extra -O0', {**BUILD_OK, 'expected_args': EXP, 'build_sig': {**BUILD_OK['build_sig'], 'args': EXP + ['-O0']}}, 2),  # also the direct profile-flag check (F3)
        ('build: extra -D_GLIBCXX_DEBUG', {**BUILD_OK, 'expected_args': EXP,
            'build_sig': {**BUILD_OK['build_sig'], 'args': ['-D_GLIBCXX_DEBUG'] + EXP}}, 2),  # also the direct profile-flag check (F3)
        ('build: CPPFLAGS set', {**BUILD_OK, 'flags_env': {'CPPFLAGS': '-D_GLIBCXX_DEBUG'}}, 1),
        ('build: LDFLAGS set', {**BUILD_OK, 'flags_env': {'LDFLAGS': '-static'}}, 1),
        ('build: dirty tracked tree', {**BUILD_OK, 'source': {'clean': False, 'untracked_src': []}}, 1),
        ('build: untracked source under src/', {**BUILD_OK, 'source': {'clean': True, 'untracked_src': ['src/rt/extra.cpp']}}, 1),
        ('build: clean tree', {**BUILD_OK, 'source': {'clean': True, 'untracked_src': []}}, 0),
        ('build: missing signature', {**BUILD_OK, 'build_sig': None}, 1),
        # F3/F4 (review of 3161f52d)
        ('build: GNUMAKEFLAGS set', {**BUILD_OK, 'make_env': {'GNUMAKEFLAGS': 'CXXFLAGS=-O0'}}, 1),
        ('build: MAKEFLAGS set', {**BUILD_OK, 'make_env': {'MAKEFLAGS': 'CXXFLAGS=-O0'}}, 1),
        ('build: a ?= make variable from the environment', {**BUILD_OK, 'makefile_env': {'LIBFFI_CFLAGS': '-O0'}}, 1),
        ('build: spoofed derivation agrees with a spoofed build', {**BUILD_OK, 'expected_args': EXP + ['-O0'],
            'build_sig': {**BUILD_OK['build_sig'], 'args': EXP + ['-O0']}}, 1),
        ('build: derivation failed', {**BUILD_OK, 'derivation_error': 'make --eval exited 2'}, 1),
        ('build: C half equal', {**BUILD_OK, **C_OK}, 0),
        ('build: C half extra flag', {**BUILD_OK, **C_OK, 'build_sig_c': {**C_OK['build_sig_c'], 'args': C_ARGS + ['-O0']}}, 1),
        ('build: C half other compiler', {**BUILD_OK, **C_OK, 'cc_resolved': '/usr/bin/clang'}, 1),
        ('build: C block missing', {**BUILD_OK, **C_OK, 'build_sig_c': None}, 1),
        ('build: untracked header under include/', {**BUILD_OK, 'source': {'clean': True, 'untracked_src': ['include/bits/stl_algo.h']}}, 1),
        ('build: git ls-files failed', {**BUILD_OK, 'source': {'clean': True, 'untracked_src': None}}, 1),
        # C21 (confirmation of C20, release audit): the compilers themselves, branch variables, link line
        ('build: compilers pinned', {**BUILD_OK, **CC_OK}, 0),
        ('build: $CXX is a wrapper script', {**BUILD_OK, **CC_OK, 'cxx_identity': {**CC_OK['cxx_identity'], 'elf': False}}, 1),
        ('build: $CC from another package', {**BUILD_OK, **CC_OK, 'cc_identity': {**CC_OK['cc_identity'],
            'package': 'gcc-15-x86-64-linux-gnu', 'version': '15.2.0-4ubuntu4'}}, 1),
        ('build: OS=Windows_NT from the environment', {**BUILD_OK, 'makefile_env': {'OS': 'Windows_NT'}}, 1),
        ('build: link line differs', {**BUILD_OK, 'link': {'as_built': 'g++ ;; -static', 'own': 'g++ ;; '}}, 1),
        ('build: link line not derivable', {**BUILD_OK, 'link': {'as_built': None, 'own': None}}, 1),
        ('build: link line equal', {**BUILD_OK, 'link': {'as_built': 'g++ ;; x', 'own': 'g++ ;; x'}}, 0),
        # R4-6: minimum versions
        ('tools: go below 1.26', {'tools': {'go': 'go version go1.24.7 linux/amd64'}}, 1),
        ('tools: go 1.26', {'tools': {'go': 'go version go1.26.0 linux/amd64'}}, 0),
        ('tools: node 20', {'tools': {'node': 'v20.11.0'}}, 1),
        ('tools: dotnet 9', {'tools': {'dotnet': '9.0.100'}}, 1),
        ('tools: dotnet 10', {'tools': {'dotnet': '10.0.100'}}, 0),
        ('tools: python 3.10', {'tools': {'python3': 'Python 3.10.12'}}, 1)]],
    # owner 23:14: four outcomes; a KNOWN_CONDITIONAL entry excuses only its own line
    ('outcome zero', certify.skip_outcome([]), 'zero-skipped'),
    ('outcome inapplicable', certify.skip_outcome([('SKIP CP20 Windows pipeline (POSIX host)', 'Windows', False)]), 'inapplicable-only'),
    ('outcome mandatory', certify.skip_outcome([('x', 'y', False), ('z', 'w', True)]), 'mandatory-skipped'),
    ('outcome not executed', certify.skip_outcome([], True), 'not-executed'),
    ('root line plus another skip is PARTIAL', certify.assertion_class(0,
        'PASS sha256\nnote: running as root; unreadable-file check skipped (root bypasses permissions)\n'
        'skipping the symlink case\n', None, False,
        [(l, w, r if 'running as root' not in l else False) for l, w, r in certify.skipped_assertions(
            'note: running as root; unreadable-file check skipped (root bypasses permissions)\nskipping the symlink case')]),
        'PARTIAL'),
    # R5-3: an excusing entry covers its own exact text, nothing appended
    ('mutant line with a build skip is not excused', certify.assertion_class(0,
        'PASS mutant 3 (x): build skipped (no compiler), not run\n'), 'PARTIAL'),
    ('real mutant line is excused', certify.assertion_class(0,
        'PASS mutant 13 (module top-level lambdas skipped and init callbacks not seen (review init-lam evasion)): '
        '23 failing checks, witness review/init-lam-ok:\n'), 'PASS'),
    ('interpolation line with an appended skip', certify.assertion_class(0,
        'PASS skipped && interpolation; SKIP remaining cases (no tty)\n'), 'PARTIAL'),
    ('paren line with an appended skip', certify.assertion_class(0,
        'PASS corpus\nSKIP cross-device check (needs tmpfs); paren oracle: skipped 1 nondeterministic entries (tools/rt-diff/ignore.txt)\n'), 'PARTIAL'),
    ('stale binary', len(pp({**GOOD, 'build_current': False})), 1),
    ('binary predates the campaign', len(pp({**GOOD, 'binary_mtime': 500})), 1),
    # R4-1: real in-tree lines of planned gates
    ('rt-parser paren oracle (by design)', certify.assertion_class(0,
        'PASS corpus\nparen oracle: skipped 7 nondeterministic entries (tools/rt-diff/ignore.txt)\n'), 'PASS'),
    ('zero count before the noun', certify.skipped_assertions('paren oracle: skipped 0 nondeterministic entries'), []),
    ('sha256 root-only skip', certify.skipped_assertions(
        'note: running as root; unreadable-file check skipped (root bypasses permissions)')[0][2], os.geteuid() != 0),
    ('class skip', rc('PASS', None, None, 'SKIP'), 'NOT-RUN'),
]
bad = [(n, got, want) for n, got, want in checks if got != want]
if bad:
    print('FAIL certify harness self-test:', bad); sys.exit(1)
print(f'PASS certify harness self-test ({len(checks)} cases: inventory, report classes, blocked logs, failure identity, preflight)')
PYC
harness_status=$?
[ "$harness_status" -eq 0 ] || exit "$harness_status"
# owner 2026-11-08 01:06: per-gate failure fingerprints. Every expected-baseline gate has an
# extractor and a table test on its real log (positive, new failure, changed diagnostic,
# substitution with the same count, wording-only change); a gate without one fails here.
python3 -B tests/runtime2/certify_fingerprints_selftest.py || exit 1

# R3-1: the skip-phrase inventory. Every tracked line that prints skip / not-run / unverified
# wording is listed in tests/runtime2/skip_phrases.tsv as detected (certify.py's markers catch it)
# or justified; a new unlisted line, a stale entry, or a detected line the markers miss fails.
python3 -B - <<'PYSKIP' || exit 1
import re, subprocess, sys
sys.path.insert(0, 'scripts')
import certify
PRINT = re.compile(r"\b(print|echo|printf|fprintf|cout|cerr|puts|write|fail|log|say|note|report)\b|<<", re.I)
PHRASE = re.compile(r"skip|not[ -]run|not verified|unverified", re.I)
INV = 'tests/runtime2/skip_phrases.tsv'
# R4-7 (round 4): the Makefile, every script, harness and test source of tests/, scripts/, tools/
# and bindings/. Not scanned, with the reason: docs/ (not printed by a gate); program fixtures
# (*.f, *.in, *.out, *.json: Nift program data compared with goldens, not runner messages); src/
# (the product's own messages: a gate that passes on them asserts them).
SCAN = ["Makefile", "*.mk", "tests/*.py", "tests/*.sh", "tests/*.cpp", "tests/*.cc", "tests/*.c", "tests/*.h",
        "tests/*.js", "tests/*.go", "tests/*.ps1", "scripts/*.py", "scripts/*.sh", "scripts/*.ps1",
        "tools/*.py", "tools/*.sh", "bindings/*.py", "bindings/*.sh", "bindings/*.js", "bindings/*.go",
        "bindings/*.cs", "bindings/*.c", "bindings/*.cc", "bindings/*.cpp", "bindings/*.h", "bindings/*.ps1"]
files = subprocess.run(["git", "ls-files"] + SCAN, capture_output=True, text=True).stdout.split()
found = set()
for f in files:
    try:
        lines = open(f, errors="replace").read().splitlines()
    except OSError:
        continue
    for l in lines:
        if PHRASE.search(l) and PRINT.search(l):
            found.add((f, l.strip()))
listed, bad = {}, []
for row in open(INV):
    if row.startswith('#') or not row.strip():
        continue
    p, cls, why, line = row.rstrip('\n').split('\t', 3)
    listed[(p, line)] = cls
    if cls == 'detected' and not certify.skipped_assertions(line):
        bad.append(f'{p}: listed as detected but the markers miss it: {line[:100]}')
    if cls not in ('detected', 'justified') or (cls == 'justified' and why in ('', '-')):
        bad.append(f'{p}: bad class or missing reason: {line[:100]}')
bad += [f'{p}: unlisted skip/not-run/unverified message (add it to {INV}): {l[:100]}' for p, l in sorted(found - set(listed))]
bad += [f'{p}: stale inventory entry: {l[:100]}' for p, l in sorted(set(listed) - found)]
# negative control: a new skip message the inventory does not list is reported
fake = ('tests/runtime2/new_gate.sh', 'echo "SKIP new check (tool missing)"')
if fake in listed or not certify.skipped_assertions(fake[1]):
    bad.append('negative control: the synthetic unlisted skip message was not reported')
else:
    print('PASS certification guard phrase inventory negative control: an unlisted message is reported')
for b in bad:
    print('FAIL certification guard phrase inventory: ' + b)
if bad:
    sys.exit(1)
print(f'PASS certification guard phrase inventory: {len(found)} lines, '
      f'{sum(1 for v in listed.values() if v == "detected")} detected, {sum(1 for v in listed.values() if v == "justified")} justified')
PYSKIP

# H2 toy-Makefile negative controls (review of 0d853db4): scripts/certify.py itself, on a candidate
# and a baseline toy tree. (a) a later recipe line failing only on the candidate, behind a first line
# that fails identically on the baseline; (b) a crash against an assertion with no FAIL lines; (c)
# different FAIL lines at the same step on the v4.12.0 baseline: each must be FAILED-NEW (exit 1).
# Positive control: an identical failure is FAILED-BASELINE (exit 0).
TOY=$(mktemp -d)
trap 'rm -rf "$TOY"' EXIT
toy() {  # toy DIR TARGET RECIPE-LINES...  (CERTIFY_PY: the runner copied in, default the tree's)
    local dir=$1 target=$2; shift 2
    mkdir -p "$dir/scripts"; cp "${CERTIFY_PY:-scripts/certify.py}" "$dir/scripts/certify.py"
    cp scripts/certify_fingerprints.py "$dir/scripts/"
    { printf 'nift:\n\tprintf "#!/bin/sh\\necho stub\\n" > nift; chmod +x nift\n'
      printf 'certify-targets: %s\n.PHONY: %s\n%s: nift\n' "$target" "$target" "$target"
      for l in "$@"; do printf '\t@%s\n' "$l"; done; } >> "$dir/Makefile"
}
toy "$TOY/a/cand" test-runtime2-rt-diff-selftest 'echo "running tests/known.sh"; echo "FAIL known: 1 of 10"; exit 1' 'echo ok' 'echo "running tests/later.sh"; exit 1'
toy "$TOY/a/base" test-runtime2-rt-diff-selftest 'echo "running tests/known.sh"; echo "FAIL known: 1 of 10"; exit 1' 'echo ok' 'echo "running tests/later.sh"; exit 0'
toy "$TOY/b/cand" test-rt-crash 'echo "running tests/crashy.sh"; echo "Segmentation fault"; exit 139'
toy "$TOY/b/base" test-rt-crash 'echo "running tests/crashy.sh"; echo "assertion failed: x"; exit 2'
toy "$TOY/c/cand" test-runtime2-rt-diff-selftest 'echo "running tests/cases.sh"; echo "FAIL case-new-1"; echo "FAIL case-new-2"; exit 1'
toy "$TOY/c/base" test-runtime2-rt-diff-selftest 'echo "running tests/cases.sh"; echo "FAIL case-old-9"; exit 1'
toy "$TOY/p/cand" test-runtime2-rt-diff-selftest 'echo "running tests/same.sh"; echo "FAIL same: 3"; exit 1'
toy "$TOY/p/base" test-runtime2-rt-diff-selftest 'echo "running tests/same.sh"; echo "FAIL same: 3"; exit 1'
toyrun() {  # toyrun CASE TARGET BASELINE-FLAG WANT-RC WANT-CLASS
    local out rc
    out=$(cd "$TOY/$1/cand" && python3 scripts/certify.py --jobs 1 --targets "$2" --unprivileged-user '' \
          --baseline "$([ "$3" = v4 ] && echo "$TOY/$1/base")" \
          $([ "$3" = reg ] && echo --regression-baseline "17f7964c=$TOY/$1/base") 2>/dev/null); rc=$?
    if [ "$rc" -ne "$4" ] || ! grep -q "^  $5 *$2:" <<<"$out"; then
        echo "FAIL certification guard toy control $1 ($2): exit $rc, wanted $4 and $5"; echo "$out" | tail -5; exit 1
    fi
    echo "PASS certification guard toy control $1: $2 is $5 (exit $rc)"
}
# F1 (confirmation of c73a7692): detail lines under the same FAIL line count; a vm_parity-style
# summary count change counts; varying values (pid, temporary path, duration, timestamp) do not.
toy "$TOY/d/cand" test-runtime2-rt-diff-selftest 'echo "running tests/suite.py"; echo "FAIL suite"; echo "  bad case: case-NEW"; exit 1'
toy "$TOY/d/base" test-runtime2-rt-diff-selftest 'echo "running tests/suite.py"; echo "FAIL suite"; echo "  bad case: case-old"; exit 1'
toy "$TOY/e/cand" test-rt-parity 'echo "running tests/runtime2/vm_parity.py"; echo "rt-diff: 817 entries, 31 differing, 16 approved"; echo "FAIL vm parity: 1 failure(s)"; exit 1'
toy "$TOY/e/base" test-rt-parity 'echo "running tests/runtime2/vm_parity.py"; echo "rt-diff: 817 entries, 30 differing, 16 approved"; echo "FAIL vm parity: 1 failure(s)"; exit 1'
# R3-2: a changed detail above many identical lines; R3-3: real hex data
toy "$TOY/t/cand" test-runtime2-rt-diff-selftest 'echo "running tests/b5/run.py"; echo "FAIL fixture a.f:"; echo "--- got"; echo "[7]"; for i in $$(seq 1 20); do echo "  diff line $$i"; done; echo "FAIL fixture b.f:"; echo "FAIL b5 fixtures: 2 of 9"; exit 1'
toy "$TOY/t/base" test-runtime2-rt-diff-selftest 'echo "running tests/b5/run.py"; echo "FAIL fixture a.f:"; echo "--- got"; echo "[7,4]"; for i in $$(seq 1 20); do echo "  diff line $$i"; done; echo "FAIL fixture b.f:"; echo "FAIL b5 fixtures: 2 of 9"; exit 1'
toy "$TOY/x/cand" test-runtime2-rt-diff-selftest 'echo "running tests/bytes.sh"; echo "FAIL bytes: expected 0xdeadbeef got 0xdeadbeee"; exit 1'
toy "$TOY/x/base" test-runtime2-rt-diff-selftest 'echo "running tests/bytes.sh"; echo "FAIL bytes: expected 0xdeadbeef got 0xdeadbeef"; exit 1'
# R4-2: a FAIL line naming a test path does not cut the identity short
toy "$TOY/tp/cand" test-runtime2-rt-diff-selftest 'echo "running tests/runtime2/b5/run.py"; echo "FAIL tests/runtime2/b5/evalorder/a.f:"; echo "--- got"; echo "[7]"; echo "FAIL tests/runtime2/b5/evalorder/b.f:"; echo "--- got"; echo "same"; echo "FAIL b5 fixtures: 2 of 9"; exit 1'
toy "$TOY/tp/base" test-runtime2-rt-diff-selftest 'echo "running tests/runtime2/b5/run.py"; echo "FAIL tests/runtime2/b5/evalorder/a.f:"; echo "--- got"; echo "[7,4]"; echo "FAIL tests/runtime2/b5/evalorder/b.f:"; echo "--- got"; echo "same"; echo "FAIL b5 fixtures: 2 of 9"; exit 1'
toy "$TOY/v/cand" test-runtime2-rt-diff-selftest 'echo "running tests/vary.sh"; echo "work /tmp/nift-x-$$$$/a pid $$$$ took 123ms at 2026-11-07T21:04:30"; echo "FAIL vary: 2 of 9 in /tmp/nift-x-$$$$/a"; exit 1'
toy "$TOY/v/base" test-runtime2-rt-diff-selftest 'echo "running tests/vary.sh"; echo "work /tmp/nift-x-7/a pid 7 took 9ms at 2026-11-08T01:00:00"; echo "FAIL vary: 2 of 9 in /tmp/nift-x-7/a"; exit 1'
# owner 2026-11-08 01:06, the three mandatory negative controls (FAILED-NEW, never FAILED-BASELINE):
# n1 an existing failure changes its diagnostic; n2 a passing case fails alongside the existing
# failure; n3 one failing case is replaced by another, same count
toy "$TOY/n1/cand" test-runtime2-rt-diff-selftest 'echo "FAIL case-a: error: a.f:1:1: not compiled for the VM (fn-flags): x"; exit 1'
toy "$TOY/n1/base" test-runtime2-rt-diff-selftest 'echo "FAIL case-a: error: a.f:1:1: not compiled for the VM (import-nested): x"; exit 1'
toy "$TOY/n2/cand" test-runtime2-rt-diff-selftest 'echo "FAIL case-a"; echo "FAIL case-b"; exit 1'
toy "$TOY/n2/base" test-runtime2-rt-diff-selftest 'echo "FAIL case-a"; echo "PASS case-b"; exit 1'
toy "$TOY/n3/cand" test-runtime2-rt-diff-selftest 'echo "FAIL case-a"; echo "FAIL case-c"; echo "PASS case-b"; exit 1'
toy "$TOY/n3/base" test-runtime2-rt-diff-selftest 'echo "FAIL case-a"; echo "FAIL case-b"; echo "PASS case-c"; exit 1'
# F2 (review of 3161f52d, toy9): a v4.12.0 match never outweighs a regression baseline that passes;
# any-vs-all: equal on 17f7964c but different on weekend/r2 is new; legacy-only is labelled
toy3() {  # toy3 CASE WANT-RC WANT-CLASS WANT-TEXT V4-RECIPE REG1-RECIPE [REG2-RECIPE]
    local c=$1 rc out; shift
    out=$(cd "$TOY/$c/cand" && python3 scripts/certify.py --jobs 1 --targets test-sha256 --unprivileged-user '' \
          --baseline "$([ -d "$TOY/$c/v4" ] && echo "$TOY/$c/v4")" --regression-baseline "17f7964c=$TOY/$c/r1" \
          $([ -d "$TOY/$c/r2" ] && echo --regression-baseline "weekend/r2=$TOY/$c/r2") 2>/dev/null); rc=$?
    if [ "$rc" -ne "$1" ] || ! grep -q "^  $2 *test-sha256: .*$3" <<<"$out"; then
        echo "FAIL certification guard baseline control $c: exit $rc, wanted $1, $2 with '$3'"; echo "$out" | tail -4; exit 1
    fi
    echo "PASS certification guard baseline control $c: $2 (exit $rc)"
}
SHA_FAIL='echo "bash tests/sha256_builtins.sh"; echo "FAIL NIST one million a (expected '"'"'x'"'"', got '"'"''"'"')"; exit 1'
SHA_PASS='echo "bash tests/sha256_builtins.sh"; echo "PASS NIST one million a"'
SHA_OTHER='echo "bash tests/sha256_builtins.sh"; echo "FAIL NIST abc (expected '"'"'x'"'"', got '"'"''"'"')"; exit 1'
toy "$TOY/b9/cand" test-sha256 "$SHA_FAIL"; toy "$TOY/b9/v4" test-sha256 "$SHA_FAIL"; toy "$TOY/b9/r1" test-sha256 "$SHA_PASS"
toy3 b9 1 FAILED-NEW "passes there"
toy "$TOY/ba/cand" test-sha256 "$SHA_FAIL"; toy "$TOY/ba/r1" test-sha256 "$SHA_FAIL"; toy "$TOY/ba/r2" test-sha256 "$SHA_OTHER"
toy3 ba 1 FAILED-NEW "weekend/r2: applicable, fingerprint differs"
toy "$TOY/bb/cand" test-sha256 "$SHA_FAIL"; toy "$TOY/bb/r1" test-sha256 "$SHA_FAIL"; toy "$TOY/bb/r2" test-sha256 "$SHA_FAIL"
toy3 bb 0 FAILED-BASELINE "every applicable regression baseline"
mkdir -p "$TOY/bc/r1"; printf 'nift:\n\tprintf "#!/bin/sh\\necho stub\\n" > nift; chmod +x nift\n' > "$TOY/bc/r1/Makefile"
toy "$TOY/bc/cand" test-sha256 "$SHA_FAIL"; toy "$TOY/bc/v4" test-sha256 "$SHA_FAIL"
toy3 bc 0 FAILED-BASELINE "LEGACY-ONLY"
# C1 (confirmation of C20, toy11): a missing prerequisite is not a missing target, and a harness-
# blocked baseline is no evidence: both are applicable and differing, never "not applicable"
toy "$TOY/bd/cand" test-sha256 "$SHA_FAIL"; toy "$TOY/bd/r1" test-sha256 "$SHA_FAIL"; toy "$TOY/bd/r2" test-sha256 "$SHA_FAIL"
sed -i 's/^test-sha256: nift$/test-sha256: nift tests\/missing-input.txt/' "$TOY/bd/r2/Makefile"
toy3 bd 1 FAILED-NEW "weekend/r2: applicable"
SHA_BLOCKED='echo "bash tests/sha256_builtins.sh"; echo "SKIP UNVERIFIED COMPARATOR COVERAGE: x"; exit 1'
toy "$TOY/be/cand" test-sha256 "$SHA_FAIL"; toy "$TOY/be/r1" test-sha256 "$SHA_FAIL"; toy "$TOY/be/r2" test-sha256 "$SHA_BLOCKED"
toy3 be 1 FAILED-NEW "weekend/r2: applicable, cannot run there"
# every regression baseline blocked or unbuildable, v4.12.0 equal: never LEGACY-ONLY
toy "$TOY/bf/cand" test-sha256 "$SHA_FAIL"; toy "$TOY/bf/v4" test-sha256 "$SHA_FAIL"; toy "$TOY/bf/r1" test-sha256 "$SHA_BLOCKED"
toy "$TOY/bf/r2" test-sha256 "$SHA_FAIL"; sed -i 's/^test-sha256: nift$/test-sha256: nift tests\/missing-input.txt/' "$TOY/bf/r2/Makefile"
toy3 bf 1 FAILED-NEW "17f7964c: applicable, cannot run there"
toyrun n1 test-runtime2-rt-diff-selftest reg 1 FAILED-NEW
toyrun n2 test-runtime2-rt-diff-selftest reg 1 FAILED-NEW
toyrun n3 test-runtime2-rt-diff-selftest reg 1 FAILED-NEW
toyrun a test-runtime2-rt-diff-selftest reg 1 FAILED-NEW
toyrun b test-rt-crash reg 1 FAILED-NEW
toyrun c test-runtime2-rt-diff-selftest v4 1 FAILED-NEW
toyrun d test-runtime2-rt-diff-selftest reg 1 FAILED-NEW
toyrun d test-runtime2-rt-diff-selftest v4 1 FAILED-NEW
toyrun e test-rt-parity reg 1 FAILED-NEW
toyrun t test-runtime2-rt-diff-selftest reg 1 FAILED-NEW
toyrun t test-runtime2-rt-diff-selftest v4 1 FAILED-NEW
toyrun x test-runtime2-rt-diff-selftest reg 1 FAILED-NEW
toyrun tp test-runtime2-rt-diff-selftest reg 1 FAILED-NEW
toyrun p test-runtime2-rt-diff-selftest reg 0 FAILED-BASELINE
toyrun v test-runtime2-rt-diff-selftest reg 0 FAILED-BASELINE
# D3 toy controls: a gate that passes with a required assertion skipped is PARTIAL (reported
# BLOCKED) and a gate whose only result is a skip is SKIP (NOT-RUN); in certification mode
# (--certify) either fails the campaign (exit 1); an other-platform skip stays PASSED.
toy "$TOY/s1/cand" test-rt-partial 'echo "running tests/runtime2/read_limit.py"; echo "PASS limits"; echo "SKIP default 1 GiB limit (MemAvailable 900 MiB < 4096)"; exit 0'
toy "$TOY/s2/cand" test-rt-skipall 'echo "running tests/refs.sh"; echo "SKIP package refs/local lock (git unavailable)"; exit 0'
toy "$TOY/s3/cand" test-rt-platform 'echo "running tests/w.py"; echo "PASS w"; echo "SKIP CP20 Windows pipeline (POSIX host)"; exit 0'
skiprun() {  # skiprun CASE TARGET WANT-RC WANT-TEXT
    local out rc
    out=$(cd "$TOY/$1/cand" && python3 scripts/certify.py --jobs 1 --certify --targets "$2" --unprivileged-user '' --baseline '' 2>/dev/null); rc=$?
    if [ "$rc" -ne "$3" ] || ! grep -q -- "$4" <<<"$out"; then
        echo "FAIL certification guard D3 control $1 ($2): exit $rc, wanted $3 and '$4'"; echo "$out" | tail -5; exit 1
    fi
    echo "PASS certification guard D3 control $1: $2 (exit $rc) as expected"
}
skiprun s1 test-rt-partial 1 "BLOCKED          test-rt-partial: PARTIAL"
skiprun s1 test-rt-partial 1 "CAMPAIGN FAILED: mandatory gate(s) PARTIAL or SKIP: test-rt-partial"
skiprun s1 test-rt-partial 1 "needs: MemAvailable >= 4096 MiB"
skiprun s2 test-rt-skipall 1 "NOT-RUN          test-rt-skipall: SKIP"
skiprun s3 test-rt-platform 0 "PASSED 1"
skiprun s3 test-rt-platform 0 "not-required test-rt-platform: SKIP CP20 Windows pipeline"
# R3-4: a gate with no output of its own is SKIP (NOT-RUN) and fails the campaign under --certify
toy "$TOY/s4/cand" test-rt-silent 'true'
skiprun s4 test-rt-silent 1 "NOT-RUN          test-rt-silent: SKIP"
# R4-5: a silent recipe whose command make echoes over two lines (a continuation) is SKIP too
mkdir -p "$TOY/s5/cand/scripts"; cp scripts/certify.py scripts/certify_fingerprints.py "$TOY/s5/cand/scripts/"
printf 'nift:\n\tprintf "#!/bin/sh\\necho stub\\n" > nift; chmod +x nift\ncertify-targets: test-rt-cont\n.PHONY: test-rt-cont\ntest-rt-cont: nift\n\ttrue \\\n\t  \&\& true\n' > "$TOY/s5/cand/Makefile"
skiprun s5 test-rt-cont 1 "NOT-RUN          test-rt-cont: SKIP"
# owner 23:10: each result states its environment
python3 -B -c "
import json,sys; d=json.load(open('$TOY/s5/cand/.build/certify/summary.json'))
env=d['targets']['test-rt-cont'].get('environment') or {}
need=('hostname','kernel','distro','container','compiler','stdlib_sha256','binary_sha256')
miss=[k for k in need if k not in env]
sys.exit('FAIL certification guard environment record: missing %s' % miss if miss or 'environment' not in d else 0)" || exit 1
echo "PASS certification guard environment record: every result states its environment"
# R5-5: --merge refuses summaries whose binaries or libstdc++ differ, and accepts equal ones
python3 -B - "$TOY" <<'MERGE' || exit 1
import json, subprocess, sys
toy = sys.argv[1]
d = json.load(open(f"{toy}/s5/cand/.build/certify/summary.json"))
def write(name, **env):
    e = json.loads(json.dumps(d))
    for r in e["targets"].values():
        r["environment"] = {**r.get("environment", {}), **env}
    open(f"{toy}/{name}.json", "w").write(json.dumps(e))
write("m0"); write("m1", hostname="other-host"); write("m2", binary_sha256="ff" * 32); write("m3", stdlib_sha256="ee" * 32)
def merge(*names):
    return subprocess.run([sys.executable, "-B", "scripts/certify.py", "--merge", *[f"{toy}/{n}.json" for n in names]],
                          capture_output=True, text=True)
same, b, l = merge("m0", "m1"), merge("m0", "m2"), merge("m0", "m3")
if same.returncode != 0 or b.returncode == 0 or "binary_sha256 differs" not in b.stdout \
        or l.returncode == 0 or "stdlib_sha256 differs" not in l.stdout:
    sys.exit("FAIL certification guard merge: rc %s/%s/%s\n%s%s" % (same.returncode, b.returncode, l.returncode, b.stdout, l.stdout))
MERGE
echo "PASS certification guard merge: differing binary or libstdc++ between environments refused"
# R3-1 inventory check of the D3 phrases the guard itself prints: the historical control's
# missing-commit branch prints a SKIP line, so that gate cannot be PASSED (see above).
# F2: the 0d853db4 runner (before the H2 repair) launders control (a) into FAILED-BASELINE, exit 0;
# the tree's runner (above) does not. Needs that commit in this repository.
if git cat-file -e 0d853db4:scripts/certify.py 2>/dev/null; then
    git show 0d853db4:scripts/certify.py > "$TOY/old-certify.py"
    CERTIFY_PY="$TOY/old-certify.py" toy "$TOY/o/cand" test-rt-multi 'echo "running tests/known.sh"; echo "FAIL known: 1 of 10"; exit 1' 'echo ok' 'echo "running tests/later.sh"; exit 1'
    CERTIFY_PY="$TOY/old-certify.py" toy "$TOY/o/base" test-rt-multi 'echo "running tests/known.sh"; echo "FAIL known: 1 of 10"; exit 1' 'echo ok' 'echo "running tests/later.sh"; exit 0'
    toyrun o test-rt-multi reg 0 FAILED-BASELINE
    echo "PASS certification guard historical control: the 0d853db4 runner launders control (a); this tree's runner reports it FAILED-NEW"
else
    # R3-1: never a pass when it cannot run: the SKIP line makes this gate PARTIAL (BLOCKED) under certify.py
    echo "SKIP certification guard historical control: commit 0d853db4 is not in this repository (manual evidence: combined-b3-b5.md 10)"
fi

# Self-test of the classification policy (scripts/certify.py decide()). A failure
# is accepted only when positively shown to be acceptable; in particular an
# earlier root-only failure shared with the baseline cannot mask a later
# candidate regression (CP13 phase A @0b879df, test-v43-language).
python3 - <<'PY'
import sys
sys.path.insert(0, 'scripts')
import certify
sig, decide = certify.failure_signature, certify.decide
ROOT_ONLY = 'NIFT_BIN=x tests/v43_cp112_filevalue_wall_smoke.sh\nboom\nmake: *** [Makefile:1136: test-v43-language] Error 1\n'
LATER = ('NIFT_BIN=x tests/v43_cp112_filevalue_wall_smoke.sh\nPASS\n'
         'NIFT_BIN=x tests/v43_final_language_smoke.sh\nerror: t.nift:1:36: bad\n'
         'make: *** [Makefile:1160: test-v43-language] Error 1\n')
base = dict(target='t', rc=1, load_sensitive=False, rerun_rc=1, user='tester')
def case(**kw): return decide({**base, **kw})[0]
reach = certify.reached_timing
TIMING_FAIL = ('NIFT_BIN=x tests/v43_cp112_filevalue_wall_smoke.sh\nPASS\n'
               'NIFT_BIN=x python3 benchmarks/hierarchy_scaling.py\nratio 4.9 > 4.0\n'
               'make: *** [Makefile:1429: test-v43-language] Error 1\n')
ls_base = dict(load_sensitive=True, root_steps=(sig(ROOT_ONLY), sig(ROOT_ONLY)), root_reached_timing=False)
def ls(**kw): return case(**{**ls_base, **kw})
checks = [
    ('signature root-only step', sig(ROOT_ONLY), 'tests/v43_cp112_filevalue_wall_smoke.sh'),
    ('signature later step', sig(LATER), 'tests/v43_final_language_smoke.sh'),
    ('signature unidentified', sig('nothing\nmake: *** [x] Error 2\n'), None),
    ('pass', case(rc=0), 'PASS'),
    ('flaky', case(rerun_rc=0), 'FLAKY'),
    ('load-sensitive never flaky', case(load_sensitive=True, rerun_rc=0), 'FAIL'),
    ('unverified', case(unverified=['tool x not installed']), 'UNVERIFIED'),
    ('root-only', case(unpriv_rc=0), 'SKIPPED-ENVIRONMENTAL'),
    # The CP13 masking scenario, end to end: root fails at the root-only step on
    # both trees; unprivileged candidate fails later; unprivileged baseline passes.
    ('masking: unprivileged baseline passes', case(unpriv_rc=1, base_rc=0, base_has_target=True,
        cand_step=sig(LATER), base_step=None), 'FAIL'),
    # Same, if the unprivileged baseline failed at the earlier step only.
    ('masking: baseline fails earlier', case(unpriv_rc=1, base_rc=1, base_has_target=True,
        cand_step=sig(LATER), base_step=sig(ROOT_ONLY)), 'FAIL'),
    ('genuine pre-existing (same step, same user, identical)', case(unpriv_rc=1, base_rc=1, base_has_target=True,
        cand_step=sig(LATER), base_step=sig(LATER), base_identical=True), 'BASELINE-FAILURE'),
    # H2c (review of 0d853db4): the same step with different FAIL lines or statuses is not pre-existing.
    ('same step, not identical', case(unpriv_rc=1, base_rc=1, base_has_target=True,
        cand_step=sig(LATER), base_step=sig(LATER), base_identical=False), 'FAIL'),
    ('same step, identity unknown', case(unpriv_rc=1, base_rc=1, base_has_target=True,
        cand_step=sig(LATER), base_step=sig(LATER)), 'FAIL'),
    ('unidentified steps never excuse', case(base_rc=1, base_has_target=True, cand_step=None, base_step=None), 'FAIL'),
    ('target absent from baseline', case(base_rc=2, base_has_target=False), 'FAIL'),
    ('no baseline available', case(), 'FAIL'),
    # Run-alone (load-sensitive) root-only exception (owner, 2026-10-23, CP16 c94bb6dd
    # Linode): privilege-related functional failure before any timing step, same
    # step in both root runs, complete target incl. timing passes unprivileged.
    ('timing reached: benchmark line', reach(TIMING_FAIL), True),
    ('timing not reached: root-only smoke', reach(ROOT_ONLY), False),
    ('run-alone root-only, complete tester pass', ls(unpriv_rc=0), 'SKIPPED-ENVIRONMENTAL'),
    ('run-alone root-only, tester functional fail', ls(unpriv_rc=1), 'FAIL'),
    ('run-alone root-only, tester timing fail', ls(unpriv_rc=2), 'FAIL'),
    ('run-alone tester timeout (incomplete)', ls(unpriv_rc=124), 'FAIL'),
    ('run-alone no unprivileged evidence', ls(), 'FAIL'),
    ('run-alone root failure at a timing step, tester pass', ls(root_reached_timing=True, unpriv_rc=0), 'FAIL'),
    ('run-alone timing-reach unknown, tester pass', ls(root_reached_timing=None, unpriv_rc=0), 'FAIL'),
    ('run-alone unrelated root failure (steps differ), tester pass',
        ls(root_steps=(sig(ROOT_ONLY), sig(LATER)), unpriv_rc=0), 'FAIL'),
    ('run-alone unidentified root step, tester pass', ls(root_steps=(None, None), unpriv_rc=0), 'FAIL'),
    ('run-alone root rerun passed: never resolved', ls(rerun_rc=0, unpriv_rc=0), 'FAIL'),
    ('run-alone passes as root', ls(rc=0), 'PASS'),
    ('ordinary target, same root-only facts', case(root_steps=(sig(ROOT_ONLY),)*2, root_reached_timing=False, unpriv_rc=0),
        'SKIPPED-ENVIRONMENTAL'),
    ('root-only reported distinctly, never PASS', ls(unpriv_rc=0) != 'PASS', True),
]
bad = [(n, got, want) for n, got, want in checks if got != want]
if bad:
    print('FAIL certify classification self-test:', bad); sys.exit(1)
print(f'PASS certify classification self-test ({len(checks)} cases)')
PY
