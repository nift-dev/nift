#!/usr/bin/env bash
# CORE-SERVER-BLOCKERS regression wall: pinned language/runtime correctness
# cases surfaced by the SERVER2 investigation. These must remain green; they
# guard the intended contracts (expression evaluation, empty-string codecs,
# return semantics) against regressions.
set -euo pipefail
NIFT="${NIFT:-./nift}"
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
fails=0
check() { local name="$1"; shift; if "$@"; then echo "PASS $name"; else echo "FAIL $name"; fails=1; fi }

# 1. Object-literal values are expressions, not just quoted strings.
#    {"a": "pre " + x + " post"} must evaluate the concatenation.
cat >"$t/b4.f" <<'EOF'
v := "x"
print({"a": "pre " + v + " post"}.a)
print({"a": "x" + "y"}.a)
print({"a": "hello"}.a)
print({"a": 'single'}.a)
print({"a": "esc \" inside"}.a)
print({"a": 1 + 2}.a)
print({"a": ""}.a)
print({"nested": {"k": "v"}}.nested.k)
EOF
out=$("$NIFT" "$t/b4.f")
check "object-literal expression values" \
  [ "$out" == $'pre x post\nxy\nhello\nsingle\nesc " inside\n3\n\nv' ]

# 2. Empty-string UTF-8 encode returns an empty bytes value; decode of an empty
#    bytes value returns an empty string.
cat >"$t/b5.f" <<'EOF'
print("".encode("utf-8").length())
print(bytes([]).decode("utf-8").length())
EOF
out=$("$NIFT" "$t/b5.f")
check "empty string/bytes UTF-8 codecs" [ "$out" == $'0\n0' ]

# 3. return inside a for exits the callable in every callable context.
cat >"$t/b3.f" <<'EOF'
fn(toplevel(xs)) { for(x : xs) { if(x == 2) { return 99 } } return 0 }
lam := (xs) => { for(x : xs) { if(x == 2) { return 99 } } return 0 }
struct(s) {
    fn(m(xs)) { for(x : xs) { if(x == 2) { return 99 } } return 0 }
}
s := s()
print(toplevel([1,2,3]))
print(lam([1,2,3]))
print(s.m([1,2,3]))
EOF
out=$("$NIFT" "$t/b3.f")
check "return exits callable (for loops)" [ "$out" == $'99\n99\n99' ]

if [ "$fails" -ne 0 ]; then echo "FAILED"; exit 1; fi
echo "PASS core language correctness"