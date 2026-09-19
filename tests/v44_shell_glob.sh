#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
touch "$t/a.txt" "$t/b.txt"
out=$(cd "$t" && printf "printf '%%s' *.txt\nexit\n" | "$NIFT_ABS" sh 2>/dev/null)
[[ "$out" == *"a.txtb.txt"* ]]
echo 'PASS v4.4 shell glob'