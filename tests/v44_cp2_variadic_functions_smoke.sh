#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat >"$t/ok.f" <<'F'
@fn(pack(head, ...rest)){
return {"head": head, "rest": rest}
}
$[a := pack("x")]
print(a.head + ":" + a.rest.size().to_string())
$[b := pack("x", 1, "two", true)]
print("B=" + b.rest.size().to_string())
@fn(recur(n, ...xs)){
if(n <= 0) { return xs.size() }
return recur(n - 1, n, ...xs)
}
print("R=" + recur(2).to_string())
print("R2=" + recur(2).to_string())
F
out=$($NIFT run "$t/ok.f")
[ "$out" = "x:0
B=3
R=2
R2=2" ] || { printf 'unexpected output:\n%s\n' "$out" >&2; exit 1; }
for sig in 'f(...a, b)' 'f(...a, ...b)' 'f(...)'; do
  printf '@fn(%s){ return null }\n' "$sig" >"$t/bad.f"
  if $NIFT run "$t/bad.f" >/dev/null 2>&1; then echo "accepted bad variadic: $sig" >&2; exit 1; fi
done
echo 'PASS v4.4 CP2 variadic functions'
