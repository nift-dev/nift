#!/usr/bin/env bash
# Deep-expression parser hardening (DEEP-EXPRESSION-PARSER-HARDENING).
# Large flat binary chains and paren nesting must parse and evaluate; genuinely
# pathological syntactic nesting must fail with a controlled parser diagnostic,
# never a segfault / stack overflow.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
run(){ printf '%s\n' "$1" > "$t/p.f"; "$NIFT_ABS" "$t/p.f"; }
gen(){ python3 - "$1" "$2" "$3" <<'PY'
import sys
op, term, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
print(f" {op} ".join([term] * n))
PY
}
assert_eq(){ [ "$2" = "$3" ] || { echo "$1: expected [$3] got [$2]" >&2; exit 1; }; }
fails_cleanly(){ # <desc> <program>: must exit non-zero with a controlled diagnostic
  printf '%s\n' "$2" > "$t/p.f"
  if "$NIFT_ABS" "$t/p.f" >/dev/null 2>"$t/e"; then echo "$1: unexpectedly succeeded" >&2; exit 1; fi
  local rc=$?
  [ "$rc" != 139 ] && [ "$rc" != 134 ] || { echo "$1: crashed (rc=$rc, segfault/abort)" >&2; exit 1; }
  grep -qiE 'parser limit|nesting|too large' "$t/e" || { echo "$1: no controlled diagnostic: $(head -1 "$t/e")" >&2; exit 1; }
}

# Large flat binary chains (previously ~2000 terms exhausted the stack).
assert_eq "plus 10000"  "$(run "x := $(gen + 1 10000)
print(x)")" 10000
assert_eq "mul 10000"   "$(run "x := $(gen '*' 1 10000)
print(x)")" 1
assert_eq "and 10000"   "$(run "x := $(gen '&&' true 10000)
print(x)")" true
assert_eq "or 10000"    "$(run "x := $(gen '||' true 10000)
print(x)")" true
assert_eq "minus 10000" "$(run "x := $(gen - 1 10000)
print(x)")" $((1 - 9999))

# Deep paren nesting.
p=$(python3 -c 'print("("*8000+"7"+")"*8000)')
assert_eq "paren 8000" "$(run "x := $p
print(x)")" 7

# Operator semantics preserved: precedence, associativity, unary signs, strings.
assert_eq "precedence" "$(run 'print(1 + 2 * 3 - 4)')" 3
assert_eq "assoc"      "$(run 'print(20 - 5 - 3)')" 12
assert_eq "unary+bin"  "$(run 'print("a" + -1)
print(1 - -2)
print(2 * -3)
print(-1 + 2)')" $'a-1\n3\n-6\n1'
assert_eq "shortcircuit" "$(run 'print(false && (1/0 == 1))
print(true || (1/0 == 1))')" $'false\ntrue'

# Controlled failure for pathological nesting (never a crash).
fails_cleanly "unary nesting 5000" "$(python3 -c 'print("x := "+"!"*5000+"true")')"
fails_cleanly "call nesting 5000"  "$(python3 -c 'print("fn(f(x)) { return x }")
print("x := "+"f("*5000+"1"+")"*5000)')"

echo "PASS v4.7 deep expression"
