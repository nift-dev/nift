#!/usr/bin/env bash
# D-010: the certification target must keep the comprehensive suites. Fails if
# `make test-all` stops reaching any required target (checked by asking make
# for the prerequisite closure, without running anything).
set -u -o pipefail
REQUIRED=(test-conformance test-host-seam test-c-abi test-c-abi-c-smoke test-embed test-bindings
          test-runtime2-vm-corpus test-runtime2-modified-race test-runtime2-dynamic-reads test-runtime2-fs-freedom)
DB="$(make -pqn test-all 2>/dev/null)"
missing=0
reach() { # breadth-first walk of prerequisites from test-all
  local -A seen=(); local queue=(test-all) t deps
  while [ ${#queue[@]} -gt 0 ]; do
    t="${queue[0]}"; queue=("${queue[@]:1}")
    [ -n "${seen[$t]:-}" ] && continue; seen[$t]=1
    deps="$(printf '%s\n' "$DB" | awk -v t="$t" '$0 ~ "^"t":" && $0 !~ /:=/ {sub("^"t":[ \t]*",""); print}' | tr ' ' '\n' | grep -E '^test(-|$)' || true)"
    for d in $deps; do queue+=("$d"); done
  done
  for t in "${!seen[@]}"; do echo "$t"; done
}
REACHED="$(reach)"
for t in "${REQUIRED[@]}"; do
  if ! printf '%s\n' "$REACHED" | grep -Fxq "$t"; then echo "FAIL certification guard: test-all no longer reaches $t"; missing=1; fi
done
[ "$missing" -eq 0 ] && echo "PASS certification guard: test-all reaches all ${#REQUIRED[@]} required suites"
exit $missing
