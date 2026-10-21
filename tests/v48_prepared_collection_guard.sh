#!/usr/bin/env bash
# Prepared collection-method scaling guard (v4.8). Scalar-key map.contains/get
# and map.set must reuse the canonical collection runtime in prepared bodies;
# the regression this guards against sent each such call through the legacy
# string evaluator per iteration (~10x). Uses ratios against a prepared array
# push loop (a fast prepared scalar op) with a generous additive floor.
set -euo pipefail
if [ -n "${PYTHON:-}" ]; then
  read -r -a PYTHON_CMD <<< "$PYTHON"
elif command -v python3 >/dev/null 2>&1; then
  PYTHON_CMD=(python3)
elif command -v python >/dev/null 2>&1; then
  PYTHON_CMD=(python)
elif command -v py >/dev/null 2>&1; then
  PYTHON_CMD=(py -3)
else
  PYTHON_CMD=()
  for candidate in /usr/local/bin/python3.[0-9]*; do
    if [ -x "$candidate" ] && [ -f "$candidate" ]; then
      PYTHON_CMD=("$candidate")
      break
    fi
  done
  if [ "${#PYTHON_CMD[@]}" -eq 0 ]; then
    printf '%s\n' "error: Python 3 is required to run this script" >&2
    exit 1
  fi
fi
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
run(){ printf '%s\n' "$2" > "$t/p.f"; : > "$t/t"; local r; for r in 1 2 3 4 5; do /usr/bin/time -p "$B" "$t/p.f" >/dev/null 2>>"$t/t"; done; "${PYTHON_CMD[@]}" -c "import statistics;print(int(statistics.median([float(x.split()[1]) for x in open('$t/t') if x.startswith('real ')])*1000))"; }
push=$(run push 'arr := []
i := 1
while(i <= 100000) { arr.push(i); i += 1 }
print(arr.size())')
contains=$(run contains 'm := map()
m.set("k", 1)
i := 0
while(i < 100000) { if(m.contains("k")) { }; i += 1 }
print(i)')
sete=$(run set 'm := map()
m.set("k", 0)
i := 0
while(i < 100000) { m.set("k", i); i += 1 }
print(m.get("k"))')
bound=$(( 14 * push + 150 ))
for name in "$contains:contains" "$sete:set-existing"; do
  IFS=: read -r v label <<<"$name"
  if [ "$v" -gt "$bound" ]; then echo "FAIL $label scaling: value=${v}ms bound=${bound}ms (push=${push}ms)" >&2; exit 1; fi
done
echo "PASS v4.8 prepared collection-method scaling guard (push ${push}ms, contains ${contains}ms, set ${sete}ms, bound ${bound}ms)"
