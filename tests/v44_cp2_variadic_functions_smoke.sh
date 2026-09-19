#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat >"$t/ok.f" <<'F'
@fn(pack(head, ...rest)){
return {"head": head, "rest": rest}
}
$[a := pack("x")]$[a.head]:$[a.rest.size()]
$[b := pack("x", 1, "two", true)]$[b.rest.size()]:$[b.rest[1]]
@fn(recur(n, ...xs)){
if(n <= 0) { return xs.size() }
return recur(n - 1, n, xs)
}
$[recur(2)]
F
out=$($NIFT run "$t/ok.f")
printf '%s' "$out" | grep -q 'x:0' || exit 1
printf '%s' "$out" | grep -q '3:two' || exit 1
# malformed contracts
for sig in 'f(...a, b)' 'f(...a, ...b)' 'f(...)'; do
  printf '@fn(%s){ return null }\n' "$sig" >"$t/bad.f"
  if $NIFT run "$t/bad.f" >/dev/null 2>&1; then echo "accepted bad variadic: $sig" >&2; exit 1; fi
done
echo 'PASS v4.4 CP2 variadic functions'
