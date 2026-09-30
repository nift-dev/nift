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
print(b.rest.size())
@fn(recur(n, ...xs)){
if(n <= 0) { return xs.size() }
return recur(n - 1, n, ...xs)
}
print(recur(2))
@fn(_leading(value9)){
return value9
}
print(_leading(42))
F
out=$($NIFT "$t/ok.f")
[ "$out" = "x:0
3
2
42" ] || { printf 'unexpected output:\n%s\n' "$out" >&2; exit 1; }
for sig in 'f(...a, b)' 'f(...a, ...b)' 'f(...)'; do
  printf '@fn(%s){ return null }\n' "$sig" >"$t/bad.f"
  if $NIFT "$t/bad.f" >/dev/null 2>&1; then echo "accepted bad variadic: $sig" >&2; exit 1; fi
done
for sig in 'f(9value)' 'f(value-name)' 'f(value,)' 'f(,value)'; do
  printf '@fn(%s){ return null }\n' "$sig" >"$t/bad.f"
  if err=$($NIFT "$t/bad.f" 2>&1); then echo "accepted invalid callable signature: $sig" >&2; exit 1; fi
  message=${err##*:1:1: }
  [ "$message" = "invalid callable signature" ] || {
    printf 'unexpected diagnostic for %s:\n%s\n' "$sig" "$err" >&2
    exit 1
  }
done
echo 'PASS v4.4 CP2 variadic functions'
