#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
touch "$t/a.txt" "$t/b.txt"
out=$(cd "$t" && printf 'printf %s *.txt\nexit\n' | "$OLDPWD/$NIFT" 2>/dev/null)
[[ "$out" == *"a.txtb.txt"* ]]
