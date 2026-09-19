#!/usr/bin/env bash
# v4.4 script-land comments: bare // and /* */ (no template '@' prefix) must
# work in .f package/script source, including inside function bodies, while
# the template forms @// and @/* */ remain valid.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat > "$t/c.f" <<'F'
// top-level line comment
fn(add(a, b)) { // inline comment in body
  return a + b
}
/* block
   comment */
print(add(2, 3))
F
out=$("$NIFT_ABS" run "$t/c.f")
[ "$out" = "5" ] || { printf 'unexpected:\n%s\n' "$out" >&2; exit 1; }
# A comment line with a semicolon must not split statements.
cat > "$t/d.f" <<'F'
// comment with ; inside
x := 1
print(x + 1)
F
[ "$("$NIFT_ABS" run "$t/d.f")" = "2" ] || exit 1
echo 'PASS v4.4 script-land comments'