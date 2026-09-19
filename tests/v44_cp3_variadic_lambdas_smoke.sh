#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat >"$t/ok.f" <<'F'
$[offset := 7]
$[f := (head, ...rest) => rest.size() + offset]
print(f("x"))
print(f("x", 1, "two", true))
$[block := (...xs) => { return xs.size() }]
print(block())
print(block(1, 2))
$[mapped := [1,2,3].map((...xs) => xs.size())]
print(mapped.join(","))
F
out=$($NIFT run "$t/ok.f")
[ "$out" = "7
10
0
2
1,1,1" ] || { printf 'unexpected output:\n%s\n' "$out" >&2; exit 1; }
for expr in '(...a, b) => 1' '(...a, ...b) => 1' '... => 1'; do
  printf '$[x := %s]\n' "$expr" >"$t/bad.f"
  if $NIFT run "$t/bad.f" >/dev/null 2>&1; then echo "accepted bad lambda: $expr" >&2; exit 1; fi
done
echo 'PASS v4.4 CP3 variadic lambdas'
