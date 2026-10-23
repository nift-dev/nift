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
          test-rt-vm test-runtime2-cp16-n1 test-build-recovery)
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

# Removal control for the N1 positive target: it must be reached through
# test-rt-vm; removing that edge must make the guard's reachability check fail.
python3 - <<'PYCONTROL'
import sys
sys.path.insert(0, 'scripts')
import certify
deps, _ = certify.make_db(certify.ROOT, 'certify-targets')
def reachable(graph):
    seen, stack = set(), ['certify-targets']
    while stack:
        t = stack.pop()
        if t in seen: continue
        seen.add(t); stack.extend(graph.get(t, ()))
    return seen
if 'test-runtime2-cp16-n1' not in reachable(deps):
    print('FAIL certification guard removal control: test-runtime2-cp16-n1 is not reached')
    sys.exit(1)
reduced = {k: [d for d in v if not (k == 'test-rt-vm' and d == 'test-runtime2-cp16-n1')]
           for k, v in deps.items()}
if 'test-runtime2-cp16-n1' in reachable(reduced):
    print('FAIL certification guard removal control: target is still reached without its edge')
    sys.exit(1)
print('PASS certification guard removal control: removing the test-rt-vm edge loses test-runtime2-cp16-n1')
PYCONTROL

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
    ('genuine pre-existing (same step, same user)', case(unpriv_rc=1, base_rc=1, base_has_target=True,
        cand_step=sig(LATER), base_step=sig(LATER)), 'BASELINE-FAILURE'),
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
