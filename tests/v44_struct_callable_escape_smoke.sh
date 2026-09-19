#!/usr/bin/env bash
# v4.4 struct-callable (module-style package method) string arguments must
# preserve inner quotes, backslashes and control characters. Package dogfood
# exposed that quoted args were re-wrapped without escaping inner quotes.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat > "$t/a.f" <<'F'
fn(echo_it(s)) { return s }
@struct(api) { q := (db, s, p) => echo_it(s) }
a := api()
db := {"x": 1}
print(a.q(db, "SELECT $1, \"id $1\" FROM t", "A"))
print(a.q(db, "a\\b", "x"))
print(a.q(db, 'single "quote"', "x"))
print(a.q(db, "tab\there", "x"))
F
out=$("$NIFT_ABS" run "$t/a.f")
[ "$(sed -n '1p' <<<"$out")" = 'SELECT $1, "id $1" FROM t' ] || { echo "$out" >&2; exit 1; }
[ "$(sed -n '2p' <<<"$out")" = 'a\b' ] || exit 1
[ "$(sed -n '3p' <<<"$out")" = 'single "quote"' ] || exit 1
[ "$(sed -n '4p' <<<"$out")" = $'tab\there' ] || exit 1
echo 'PASS v4.4 struct-callable string argument escaping'
