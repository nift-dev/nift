#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
cd "$t"
cat > ok.nift <<'NIFT'
print(" a,b,,c, ".trim().split(",").stringify())
print("hello".length())
print("hello".index_of("ll"))
print("hello".last_index_of("l"))
print("abc".contains("b"))
print("abc".starts_with("a"))
print("abc".ends_with("c"))
print("  X  ".trim_start().trim_end().to_lower())
print("foo foo".replace("foo", "bar"))
print("42".to_int() + 1)
print("3.5".to_double() + 1)
print(42.to_string())
print(3.5.to_string())
print("abc".split("").join("-"))
print("".split("").size())
x := 2
i := 0
a := [x, x + 1, i++, i++, "6".to_int()]
print(a.stringify())
print(i)
print(["a", "b"].join(",").to_upper())
print(ls().size() > 0)
NIFT
out="$($NIFT_BIN run ok.nift)"
expected=$'["a","b","","c",""]\n5\n2\n3\ntrue\ntrue\ntrue\nx\nbar bar\n43\n4.5\n42\n3.5\na-b-c\n0\n[2,3,0,1,6]\n2\nA,B\ntrue'
[[ "$out" == "$expected" ]] || { printf 'unexpected output:\n%s\n' "$out"; exit 1; }
for expr in '"x".split()' '"x".replace("", "y")' '" 42 ".to_int()' '"3.2x".to_double()' '"3.2".to_int()'; do
  printf 'print(%s)\n' "$expr" > bad.nift
  if $NIFT_BIN run bad.nift >/dev/null 2>&1; then echo "expected failure: $expr"; exit 1; fi
done
printf 'CP100-CP103 string/expression ergonomics smoke: PASS\n'
