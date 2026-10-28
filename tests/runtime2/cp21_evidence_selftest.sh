#!/usr/bin/env bash
# Negative controls for the mandatory D3 evidence records. Each case preserves
# the target/edge but replaces its execution body with a no-op; the mandatory
# target must then reject the stale evidence instead of reporting success.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/cp21-evidence-selftest-XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"

hollow() {
    local source="$1" target="$2" dest="$3"
    python3 - "$source" "$target" "$dest" <<'PY'
import sys
source, target, dest = sys.argv[1:]
lines = open(source).read().splitlines(keepends=True)
out = []
i = 0
found = False
while i < len(lines):
    line = lines[i]
    if line.startswith(target + ':'):
        found = True
        out.append(line)
        i += 1
        while i < len(lines) and lines[i].startswith('\t'):
            i += 1
        out.append('\t@:\n')
        continue
    out.append(line)
    i += 1
if not found:
    raise SystemExit('target not found: ' + target)
open(dest, 'w').writelines(out)
PY
}

make test-cp21-d3-mutant test-cp21-parser-target-mutant >/dev/null

for target in test-cp21-d3-mutant test-cp21-parser-target-mutant; do
    makefile="$TMP/$target.Makefile"
    log="$TMP/$target.log"
    hollow "$ROOT/Makefile" "$target" "$makefile"
    if make -f "$makefile" test-cp21-d3-mandatory >"$log" 2>&1; then
        echo "FAIL CP21 evidence control: hollow $target still passed mandatory" >&2
        cat "$log" >&2
        exit 1
    fi
    if ! grep -Eq 'mandatory evidence (is stale|missing)' "$log"; then
        echo "FAIL CP21 evidence control: hollow $target failed without evidence rejection" >&2
        cat "$log" >&2
        exit 1
    fi
    echo "PASS CP21 evidence control: hollow $target is rejected"
done
