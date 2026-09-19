#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat >"$t/ok.f" <<'F'
@fn(pack(a, ...rest)){ return rest }
$[xs := [1, "two", true]]
print(pack("head", ...xs).size())
$[f := (...args) => args.size()]
print(f(...xs))
print(range(...[1,4]).join(","))
F
out=$($NIFT run "$t/ok.f")
[ "$out" = "3
3
1,2,3" ] || { printf '%s\n' "$out" >&2; exit 1; }
printf '@fn(f(...x)){ return x }\n$[bad := 3]\nprint(f(...bad))\n' >"$t/bad.f"
if $NIFT run "$t/bad.f" >/dev/null 2>&1; then echo 'non-array spread accepted' >&2; exit 1; fi
echo 'PASS v4.4 CP4 spread'
