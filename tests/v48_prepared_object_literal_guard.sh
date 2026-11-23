#!/usr/bin/env bash
# Prepared object-literal scaling guard (v4.8). Object literals inside prepared
# loop bodies must stay on the prepared path; the regression this guards against
# sent every such statement through the legacy string evaluator per iteration
# (~57x slower than an equivalent scalar push). Uses a ratio between the
# object-literal loop and the scalar push loop, with a generous additive floor,
# so host variance does not flake but a fallback-to-legacy regression does.
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
scalar=$(run scalar 'arr := []
i := 1
while(i <= 100000) { arr.push(i); i += 1 }
print(arr.size())')
obj=$(run objlit 'arr := []
i := 1
while(i <= 100000) { arr.push({"k": i, "v": i + 1}); i += 1 }
print(arr.size())')
bound=$(( 12 * scalar + 100 ))
if [ "$obj" -gt "$bound" ]; then
  echo "FAIL prepared object-literal scaling: scalar=${scalar}ms object=${obj}ms bound=${bound}ms" >&2; exit 1
fi
echo "PASS v4.8 prepared object-literal scaling guard (scalar ${scalar}ms, object ${obj}ms, bound ${bound}ms)"
