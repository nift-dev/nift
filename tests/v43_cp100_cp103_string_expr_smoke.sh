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
print("é🙂".length())
print("é🙂".split("").size())
fn(label()) { return "  hello  " }
print(label().trim().to_upper())
print((1 + 2).to_string())
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
expected=$'["a","b","","c",""]\n5\n2\n3\ntrue\ntrue\ntrue\nx\nbar bar\n43\n4.5\n42\n3.5\na-b-c\n2\n2\nHELLO\n3\n0\n[2,3,0,1,6]\n2\nA,B\ntrue'
[[ "$out" == "$expected" ]] || { printf 'unexpected output:\n%s\n' "$out"; exit 1; }
for expr in '"x".split()' '"x".replace("", "y")' '" 42 ".to_int()' '"3.2x".to_double()' '"3.2".to_int()'; do
  printf 'print(%s)\n' "$expr" > bad.nift
  if $NIFT_BIN run bad.nift >/dev/null 2>&1; then echo "expected failure: $expr"; exit 1; fi
done
# Strict numeric conversion: hex float forms are rejected (strtod permissiveness),
# a leading '+' is accepted consistently by both to_int and to_double, and the
# complete-input contract holds at the int64 boundaries.
for expr in '"0x10".to_double()' '"0x1p3".to_double()' '"+9223372036854775808".to_int()' '"-9223372036854775809".to_int()' '"1e999".to_double()' '"nan".to_double()'; do
  printf 'print(%s)\n' "$expr" > bad.nift
  if $NIFT_BIN run bad.nift >/dev/null 2>&1; then echo "expected failure: $expr"; exit 1; fi
done
cat > num.nift <<'NIFT'
print("+42".to_int())
print("+42".to_double())
print("9223372036854775807".to_int())
print("-9223372036854775808".to_int())
print("3.14".to_double())
print("1e6".to_double())
NIFT
[[ "$($NIFT_BIN run num.nift)" == $'42\n42\n9223372036854775807\n-9223372036854775808\n3.14\n1000000' ]]
# int -> double widening assignment: a double binding accepts int values (the
# arithmetic model already computes int + double as double), including through
# a map retrieval and a struct double field; double -> int stays lossy/error.
cat > widen.nift <<'NIFT'
old := 0.0
m := map()
m.set("a", 0.0 + 45.0)
old = m.get("a")
print(old + 0.5)
x := 0.0
x = 5
print(x + 0.5)
struct(box) { v := 0.0 }
b := box()
b.v = 7
print(b.v + 0.5)
NIFT
[[ "$($NIFT_BIN run widen.nift)" == $'45.5\n5.5\n7.5' ]]
printf 'print(x := 5; x = 5.5)\n' > wbad.nift
cat > wbad.nift <<'NIFT'
x := 5
x = 5.5
NIFT
if $NIFT_BIN run wbad.nift >/dev/null 2>&1; then echo "double->int widened unexpectedly" >&2; exit 1; fi
printf 'CP100-CP103 string/expression ergonomics smoke: PASS\n'
