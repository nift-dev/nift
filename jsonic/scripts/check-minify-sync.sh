#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then echo "usage: $0 /path/to/minify" >&2; exit 2; fi
root=$(cd "$(dirname "$0")/.." && pwd)
minify=$(cd "$1" && pwd)
cmp -s "$root/include/json.h" "$minify/src/Json.h" || { echo "Jsonic++ / Minify++ JSON copy mismatch" >&2; exit 1; }
echo "Jsonic++ / Minify++ sync OK"
