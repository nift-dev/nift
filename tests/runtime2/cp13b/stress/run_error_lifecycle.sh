#!/usr/bin/env bash
# CP13B Phase A: build and run the Error ownership/release unit under the
# 8 MB budget. The normal run must pass; the leak probe must fail, proving the
# check detects retained exception values. Test-only; no production sources
# are modified.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"
out="${TMPDIR:-/tmp}/cp13b-error-lifecycle-$$"
trap 'rm -f "$out"' EXIT
"${CXX:-g++}" -I"$root/src" -std=c++17 -O2 -Wall -Wextra -pedantic \
  "$here/error_lifecycle_unit.cpp" \
  "$root/src/rt/error.cpp" "$root/src/rt/heap_account.cpp" \
  "$root/src/rt/value.cpp" "$root/src/rt/string.cpp" -o "$out"
echo "== normal (must pass) =="
"$out"
echo "== leak probe (must fail) =="
if "$out" leak; then
  echo "FAIL: the leak probe passed; the accumulation check is blind" >&2
  exit 1
fi
echo "PASS error lifecycle: normal run released every Error; leak probe detected"
