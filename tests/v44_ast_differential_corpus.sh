#!/usr/bin/env bash
# CP42 differential corpus: every program is executed by the reviewed AST build
# and checked against a golden output that was verified equal to the legacy
# (prepared-execution-disabled) evaluator. The corpus spans arithmetic,
# precedence, strings, break/continue, nested while, declarations, arrays,
# indexed mutation, functions, recursion, closures, safe access, aliasing,
# native calls, sort/group, JSON ops, maps, string methods, for-ranges and
# big-integer handling.
set -euo pipefail
# Portable timeout: GNU timeout where present, otherwise perl alarm (macOS has
# no GNU timeout by default).
timed() {
  local secs="$1"; shift
  if command -v timeout >/dev/null 2>&1; then timeout "$secs" "$@";
  else perl -e 'alarm shift; exec @ARGV' "$secs" "$@";
  fi
}
nift=${NIFT:-"$(cd "$(dirname "$0")/.." && pwd)/nift"}
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT

run() { printf '%s' "$1" > "$t/prog.f"; timed 30 "$nift" run "$t/prog.f" 2>&1 | tr '\n' '|'; }

fail=0
check() { # name expected actual
  if [ "$2" != "$3" ]; then
    echo "FAIL $1: expected [$2] got [$3]" >&2
    fail=1
  fi
}

check p01_arith "10100|" "$(run 'total := 0
i := 1
while(i <= 100) { total += i * 2; i += 1 }
print(total)')"
check p02_precedence "1|20|" "$(run 'i := 0
while(2 + 3 * 4 == 14 && i < 1) { i += 1 }
print(i)
print((2 + 3) * 4)')"
check p03_strings "100|xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxy|" "$(run 's := ""
i := 1
while(i <= 100) { s += "x"; i += 1 }
print(s.length())
print(s + "y")')"
check p04_break_continue "37|" "$(run 'total := 0
i := 0
while(i < 20) { i += 1; if(i % 3 == 0) { continue }; if(i > 10) { break }; total += i }
print(total)')"
check p05_nested_while "100|" "$(run 's := 0
r := 0
while(r < 5) { c := 0; while(c < 5) { s += r * c; c += 1 }; r += 1 }
print(s)')"
check p06_declarations "328350|" "$(run 'total := 0
i := 0
while(i < 100) { x := i * i; total += x; i += 1 }
print(total)')"
check p07_arrays "100|101|11|" "$(run 'a := []
i := 1
while(i <= 100) { a.push(i); i += 1 }
print(a.size())
print(a[0] + a[99])
print(a[10])')"
check p08_indexed_mutation "10|30|40|" "$(run 'arr := [[1,2],[3,4]]
total := 0
for(e : arr) { e[0] = e[0] * 10; total += e[0] }
print(arr[0][0])
print(arr[1][0])
print(total)')"
check p09_functions "1000|" "$(run 'fn(inc(x)) { return x + 1 }
total := 0
i := 0
while(i < 1000) { total = inc(total); i += 1 }
print(total)')"
check p10_recursion "6765|" "$(run 'fn(fib(n)) { if(n < 2) { return n }; return fib(n-1) + fib(n-2) }
print(fib(20))')"
check p11_closures "11|" "$(run 'x := 5
f := (a) => a + x
x = 10
print(f(1))')"
check p12_safe_access "5|null|null|" "$(run 'o := {"a": 5}
print(o?.a)
n := null
print(n?.a)
print(n?["a"])')"
check p13_aliasing "4|4|" "$(run 'a := [1, 2, 3]
x := a
x.push(99)
print(a.length())
print(x.length())')"
check p14_native_calls "100|true|false|" "$(run 's := set()
i := 1
while(i <= 100) { s.add(i); i += 1 }
print(s.size())
print(s.contains(50))
print(s.contains(200))')"
check p15_sort_group "6|2|3|" "$(run 'a := [3, 1, 2, 5, 4]
s := a.sort_by(x => x)
print(s[0] + s[4])
g := a.group_by(x => x % 2)
print(g["0"].length())
print(g["1"].length())')"
check p16_json_ops "5|4|3|" "$(run 'd := {"total": 5, "items": [1, 2, 3]}
print(d.total)
print(d.items[0] + d.items[2])
print(d.items.length())')"
check p17_map_loop "50|100|" "$(run 'm := {}
i := 1
while(i <= 100) { k := "k" + i.to_string(); m[k] = i; i += 1 }
print(m["k50"])
print(m["k100"])')"
check p18_string_methods "5|3.5|5|hi|abc|" "$(run 'print((5).to_string())
print((-3.5).abs())
print("hello".length())
print("  hi  ".trim())
print("AbC".to_lower())')"
check p19_for_ranges "10|" "$(run 'total := 0
for(e : [1, 2, 3, 4]) { total += e }
print(total)')"
check p20_bigint "0|9007199254741002|" "$(run 'i := 0
while(9007199254740993 == 9007199254740992 && i < 100000) { i += 1 }
print(i)
x := 9007199254740992
j := 0
while(j < 10) { x += 1; j += 1 }
print(x)')"

if [ "$fail" -ne 0 ]; then
  echo "v4.4 AST differential corpus: FAIL" >&2
  exit 1
fi
echo "v4.4 AST differential corpus: PASS (20 programs, legacy-equivalent)"