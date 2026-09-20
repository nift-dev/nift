#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
# External-command glob expansion is POSIX (glob.h); Windows passes tokens
# through until a native glob backend lands, so skip the assertion there.
if [ "$(uname -s 2>/dev/null)" = "MINGW"* ] || [ "${OS:-}" = "Windows_NT" ]; then
  echo 'SKIP v4.4 shell glob (Windows passthrough pending glob backend)'
  exit 77
fi
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
touch "$t/a.txt" "$t/b.txt"
out=$(cd "$t" && printf "printf '%%s' *.txt\nexit\n" | "$NIFT_ABS" sh 2>/dev/null)
[[ "$out" == *"a.txtb.txt"* ]]
echo 'PASS v4.4 shell glob'