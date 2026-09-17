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
printf 's := ifstream("file.txt")\nprint(s.stringify())\n' > "$td/bad.nift"
if (cd "$td" && "$NIFT" run bad.nift >"$td/bad.out" 2>"$td/bad.err"); then
  echo 'expected stream stringify failure' >&2; exit 1
fi
! grep -q 'nift:stream:' "$td/bad.out" "$td/bad.err"

# Presentation modifiers compose over the ORIGINAL value: the two orders are
# equal, repeated modifiers are idempotent, and a compound expression ending in
# a modifier (equality/concat operand) is not hijacked by the presentation
# matcher.
cat > "$td/comp.nift" <<'NIFT'
x := [1, 2]
print(x.prettify() == x.prettify())
print(x.highlight() == x.stringify())
print(x.prettify() == x.stringify())
print(x.prettify().prettify() == x.prettify())
print(x.highlight().highlight() == x.highlight())
print(x.prettify().highlight() == x.highlight().prettify())
print(x.stringify().prettify() == x.prettify())
print("got=" + x.prettify())
NIFT
[[ "$(cd "$td" && "$NIFT" run comp.nift)" == $'true\ntrue\nfalse\ntrue\ntrue\ntrue\ntrue\ngot=[\n  1,\n  2\n]' ]]

# Method calls participate in ordinary compound expressions: array/lambda/struct
# method results compare and combine like any other value.
cat > "$td/methods.nift" <<'NIFT'
x := [1, 2]
print(x.size() == x.size())
print(x.size() + x.size())
f := (a) => a + 1
print(f(1) == f(1))
struct(counter) {
  count := 0
  fn(value()) { return count }
}
c := counter()
print(c.value() == c.value())
print(c.value() + 1)
NIFT
[[ "$(cd "$td" && "$NIFT" run methods.nift)" == $'true\n4\ntrue\ntrue\n1' ]]

# cat is byte-exact (no implicit newline) and rejects directories; stringify of
# a quoted string escapes correctly; callables are opaque and rejected.
mkdir -p "$td/adir"
printf 'cat("adir")\n' > "$td/dir.nift"
if (cd "$td" && "$NIFT" run dir.nift >/dev/null 2>&1); then
  echo 'cat(directory) unexpectedly succeeded' >&2; exit 1
fi
printf 'print("q\\"w\\nc".stringify())\n' > "$td/esc.nift"
[[ "$(cd "$td" && "$NIFT" run esc.nift)" == '"q\"w\nc"' ]]

# A filesystem primitive whose call is not terminal (embedded in a larger
# expression) falls through to the ordinary machinery instead of reporting a
# misleading "malformed arguments" error, and presentation composition over
# ls() still works.
printf 'x := ls()\nprint(x.prettify() == ls().prettify())\nprint(x.size())\n' > "$td/prim.nift"
prim="$(cd "$td" && "$NIFT" run prim.nift)"
[[ "$(echo "$prim" | sed -n 1p)" == 'true' ]]
[[ "$(echo "$prim" | sed -n 2p)" -ge 1 ]]
printf 'print(exists(".") == true)\nprint(ls().highlight() == ls().stringify())\n' > "$td/prim2.nift"
[[ "$(cd "$td" && "$NIFT" run prim2.nift)" == $'true\ntrue' ]]

echo 'CP94-CP97 inspection smoke: PASS'
