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
          test-runtime2-vm-corpus test-runtime2-rt-diff test-runtime2-rt-diff-selftest test-runtime2-modified-race test-runtime2-clock-lifecycle test-runtime2-dynamic-reads test-runtime2-fs-freedom)
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
