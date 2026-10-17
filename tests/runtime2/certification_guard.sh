#!/usr/bin/env bash
# D-010: `make certify` (certification target) must reach every suite the CI
# workflows run, plus the comprehensive suites, so a checkpoint cannot be
# certified locally while skipping something CI will run. Checked by asking
# make for the prerequisite closure, without running anything.
set -u -o pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
REQUIRED=(test-all test-conformance test-host-seam test-c-abi test-c-abi-c-smoke test-embed test-bindings
          test-runtime2-vm-corpus test-runtime2-modified-race test-runtime2-clock-lifecycle test-runtime2-dynamic-reads test-runtime2-fs-freedom)
          test-runtime2-vm-corpus test-runtime2-rt-diff test-runtime2-rt-diff-selftest test-runtime2-modified-race test-runtime2-dynamic-reads test-runtime2-fs-freedom)
WORKFLOWS=(.github/workflows/test-integrity.yml .github/workflows/checkpoint-10-cross-platform.yml .github/workflows/v44-cross-platform.yml)
python3 - "${WORKFLOWS[@]}" -- "${REQUIRED[@]}" <<'PY'
import re, subprocess, sys
args = sys.argv[1:]; sep = args.index('--')
workflows, required = args[:sep], args[sep + 1:]
ci = set()
for wf in workflows:
    for line in open(wf):
        for m in re.finditer(r'\bmake(?:\s+-\S+)*((?:\s+[a-z0-9][a-z0-9_.-]*)+)', line):
            ci.update(t for t in m.group(1).split() if re.match(r'^(test|bh)', t))
db = subprocess.run(['make', '-pqn', 'certify'], capture_output=True, text=True).stdout.split('\n')
deps = {}
for l in db:
    m = re.match(r'^([A-Za-z0-9_.-]+):(?!=)\s*(.*)$', l)
    if m: deps.setdefault(m.group(1), set()).update(m.group(2).split())
seen, stack = set(), ['certify']
while stack:
    t = stack.pop()
    if t in seen: continue
    seen.add(t); stack.extend(deps.get(t, ()))
missing = sorted(t for t in set(required) | ci if t not in seen)
if not ci:
    print('FAIL certification guard: no make targets found in CI workflows'); sys.exit(1)
for t in missing: print(f'FAIL certification guard: make certify does not reach {t}')
if missing: sys.exit(1)
print(f'PASS certification guard: make certify reaches {len(required)} required and {len(ci)} CI-run targets')
PY
