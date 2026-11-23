#!/usr/bin/env bash
# Run in the same unprivileged FreeBSD guest as the established regression job.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "$(uname -s)" != FreeBSD ] || [ "$(id -u)" = 0 ]; then
  echo "ci_freebsd_runtime2.sh requires an unprivileged FreeBSD guest" >&2
  exit 2
fi
export LC_ALL=C.UTF-8 LANG=C.UTF-8 PYTHONDONTWRITEBYTECODE=1
out="$PWD/.build/freebsd-ci/runtime2"
mkdir -p "$out/bin"
for pair in make:gmake stat:gstat timeout:gtimeout; do
  program=$(command -v "${pair#*:}")
  ln -sf "$program" "$out/bin/${pair%%:*}"
done
export PATH="$out/bin:$PATH"
summary="$out/summary.txt"
printf '\n### FreeBSD Runtime 2.0 subset\n\n' | tee "$summary"
git show -s --format='commit %H tree %T' | tee -a "$summary"
python3 tests/runtime2/platform_subset.py --check | tee "$out/table.txt"
units=(.build/rt-parse-dump .build/rt-parser-unit .build/rt-legacy-oracle
       .build/rt/vm-unit .build/rt/vm-globals-unit .build/rt/vm-modules-unit .build/rt/vm-prepared-parse)
result=0
if ! gmake -k -j"${NIFT_CI_JOBS:-2}" nift "${units[@]}" >"$out/build.log" 2>&1; then
  tail -50 "$out/build.log"
  result=1
fi
if [ -x ./nift ]; then
  python3 tests/runtime2/platform_subset.py --platform freebsd --nift "$PWD/nift" --build .build \
    2>&1 | tee "$out/subset.txt" || result=1
else
  python3 tests/runtime2/platform_subset.py --platform freebsd --list |
    sed 's/^RUN /BLOCKED /' | tee "$out/subset.txt"
  result=1
fi
grep -E '^FreeBSD outcomes:|^BLOCKED |^NOT RUN ' "$out/subset.txt" | tee -a "$summary" || true
for gate in test-v45-embed-contracts test-rt-value test-runtime2-kind-bits test-rt-source-map test-runtime2-certification-guard; do
  if gmake -k "$gate" >"$out/$gate.log" 2>&1; then
    printf 'PASS %s\n' "$gate" | tee -a "$summary"
  else
    tail -40 "$out/$gate.log"
    printf 'FAIL %s\n' "$gate" | tee -a "$summary"
    result=1
  fi
done
exit "$result"
