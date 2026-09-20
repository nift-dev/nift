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
@fn(recur(n, ...xs)){
if(n <= 0) { return xs.size() }
return recur(n - 1, n, ...xs)
}
x := 7
print("C1=" + (b.rest.size()).to_string())
print("C2=" + recur(2).to_string())
print("C3=" + x.to_string())
print("D1")
print(3)
print("D2")
print(b.rest.size())
print("D3")
print(recur(2))
print("D4")
print(x)
F
out=$(cd "$(dirname "$t/ok.f")" && "$NIFT" run ok.f 2>&1 | tr -d '\r')
[ "$out" = "x:0
C1=3
C2=2
C3=7
D1
3
D2
3
D3
2
D4
7" ] || { printf 'unexpected output:\n%s\n' "$out" >&2; exit 1; }
for sig in 'f(...a, b)' 'f(...a, ...b)' 'f(...)'; do
  printf '@fn(%s){ return null }\n' "$sig" >"$t/bad.f"
  if $NIFT run "$t/bad.f" >/dev/null 2>&1; then echo "accepted bad variadic: $sig" >&2; exit 1; fi
done
echo 'PASS v4.4 CP2 variadic functions'
