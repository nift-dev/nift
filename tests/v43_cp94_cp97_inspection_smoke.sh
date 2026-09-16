#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NIFT="$ROOT/nift"
td="$(mktemp -d)"; trap 'rm -rf "$td"' EXIT
printf 'beta\nalpha' > "$td/file.txt"
printf z > "$td/z.txt"; printf a > "$td/a.txt"
cat > "$td/check.nift" <<'NIFT'
print(ls().prettify())
cat("file.txt")
print("")
a := [1, 2, 3]
print(a.stringify())
print(a.prettify())
NIFT
out="$(cd "$td" && "$NIFT" run check.nift)"
[[ "$out" == *'"a.txt"'* && "$out" == *'"z.txt"'* ]]
[[ "$out" == *$'beta\nalpha'* ]]
[[ "$out" == *'[1,2,3]'* ]]
[[ "$out" == *$'[\n  1,\n  2,\n  3\n]'* ]]
# Deterministic ls ordering.
[[ "$(printf '%s' "$out" | grep -bo '"a.txt"' | head -1 | cut -d: -f1)" -lt "$(printf '%s' "$out" | grep -bo '"z.txt"' | head -1 | cut -d: -f1)" ]]
# REPL bare compound values are inspectable, and prettify remains ANSI-free.
repl="$(cd "$td" && printf 'x := [1, 2]\nx\nx.prettify()\nx.highlight()\nx.prettify().highlight()\nx.highlight().prettify()\nnull\n""\ncd(".")\n' | HOME="$td" NO_COLOR=1 "$NIFT" sh)"
[[ "$repl" == *'[1,2]'* ]]
[[ "$repl" == *$'[\n  1,\n  2\n]'* ]]
[[ "$repl" == *'~$ '* ]]
[[ "$repl" != *$'\033'* ]]
[[ "$repl" == *'[1,2]'* ]]
# Bare null/empty-string results and null-returning commands stay silent.
! printf '%s\n' "$repl" | grep -qx 'null'
[[ "$repl" != *'""'* ]]
# Opaque values must not leak internal reference tokens through formatting.
printf 's := ifs("file.txt")\nprint(s.stringify())\n' > "$td/bad.nift"
if (cd "$td" && "$NIFT" run bad.nift >"$td/bad.out" 2>"$td/bad.err"); then
  echo 'expected stream stringify failure' >&2; exit 1
fi
! grep -q 'nift:stream:' "$td/bad.out" "$td/bad.err"
echo 'CP94-CP97 inspection smoke: PASS'
