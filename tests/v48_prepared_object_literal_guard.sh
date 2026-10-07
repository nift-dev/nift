#!/usr/bin/env bash
# Prepared object-literal scaling guard (v4.8). Object literals inside prepared
# loop bodies must stay on the prepared path; the regression this guards against
# sent every such statement through the legacy string evaluator per iteration
# (~57x slower than an equivalent scalar push). Uses a ratio between the
# object-literal loop and the scalar push loop, with a generous additive floor,
# so host variance does not flake but a fallback-to-legacy regression does.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
run(){ printf '%s\n' "$2" > "$t/p.f"; : > "$t/t"; local r; for r in 1 2 3 4 5; do /usr/bin/time -f "%e" "$B" "$t/p.f" >/dev/null 2>>"$t/t"; done; python3 -c "import statistics;print(int(statistics.median([float(x) for x in open('$t/t')])*1000))"; }
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