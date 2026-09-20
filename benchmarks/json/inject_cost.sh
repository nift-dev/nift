#!/usr/bin/env bash
set -euo pipefail
BIN=${1:-./nift}
FIXTURE=${2:-../scripting-benchmark/benchmarks/fixtures/structured-data/records-large.json}
TMP=$(mktemp --suffix=.f)
trap 'rm -f "$TMP"' EXIT
printf 'arr := inject("%s")\nprint(arr.size())\n' "$FIXTURE" > "$TMP"
/usr/bin/time -f 'wall_s=%e peak_rss_kb=%M' "$BIN" run "$TMP"
