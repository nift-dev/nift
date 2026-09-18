#!/usr/bin/env bash
# v43 object methods on a JSON object value: keys/values/entries/has/size,
# and expression-valued object literals. Rewritten for the current surface
# (json_parse was removed; object literals + methods are the path).
set -euo pipefail
NIFT=${NIFT_BIN:-./nift}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cat > "$T/t.nift" <<'NIFT'
x := {"b": 2, "a": 1}
print(x.keys().join(","))
print(x.values().size())
print(x.entries().size())
print(x.has("a"))
print(x.has("z"))
print(x.size())
NIFT
out="$($NIFT run "$T/t.nift")"
[ "$(printf '%s\n' "$out" | sed -n '1p')" = 'b,a' ]
[ "$(printf '%s\n' "$out" | sed -n '2p')" = '2' ]
[ "$(printf '%s\n' "$out" | sed -n '3p')" = '2' ]
[ "$(printf '%s\n' "$out" | sed -n '4p')" = 'true' ]
[ "$(printf '%s\n' "$out" | sed -n '5p')" = 'false' ]
[ "$(printf '%s\n' "$out" | sed -n '6p')" = '2' ]
# expression-valued object literal (composition surface)
cat > "$T/e.nift" <<'NIFT'
y := 5
o := {"expr": y + 1, "nested": {"v": y}}
print(o.expr)
print(o.nested.v)
NIFT
[ "$("$NIFT" run "$T/e.nift")" = $'6\n5' ]
echo 'object methods smoke passed'