#!/usr/bin/env bash
# CP09 value-model benchmark: instruction counts (callgrind) of rt::Value vs
# legacy RuntimeValue per workload. Usage: tools/rt-bench.sh <value_bench binary>
# Prints a markdown table; the ratio is rt / legacy (lower is better).
set -euo pipefail
BIN="$1"
command -v valgrind >/dev/null || { echo "valgrind is required" >&2; exit 2; }
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
count() {
    valgrind --tool=callgrind --callgrind-out-file="$OUT/cg" "$BIN" "$1" "$2" 2>"$OUT/log" >/dev/null
    sed -n 's/.*Collected : \([0-9]*\).*/\1/p' "$OUT/log"
}
"$BIN" sizes
base_rt=$(count rt none)
base_legacy=$(count legacy none)
echo "| workload | rt Ir | legacy Ir | rt/legacy |"
echo "|---|---:|---:|---:|"
row() { awk -v w="$1" -v r="$2" -v l="$3" 'BEGIN { printf "| %s | %d | %d | %.3f |\n", w, r, l, r / l }'; }
declare -A R L
for w in copy push index objset objget; do
    R[$w]=$(( $(count rt "$w") - base_rt ))
    L[$w]=$(( $(count legacy "$w") - base_legacy ))
done
# index and objget include building the container; the "only" rows subtract it.
row "copy (1000 copies of a 10k-string array)" "${R[copy]}" "${L[copy]}"
row "push (100k numbers)" "${R[push]}" "${L[push]}"
row "index only (1M reads)" $(( R[index] - R[push] )) $(( L[index] - L[push] ))
row "object set (128k sets, 64 keys)" "${R[objset]}" "${L[objset]}"
row "object get only (128k gets, 64 keys)" $(( R[objget] - R[objset] / 2000 )) $(( L[objget] - L[objset] / 2000 ))
