#!/usr/bin/env bash
# Prepared-execution method-argument parity (v4.7.2 regression).
# The prepared (loop-body) AST method-call path previously parsed no argument
# expressions, so arg-taking native methods dispatched by prepared execution
# (string encode, bytes decode, bytes slice, atomic/timer setters) received zero
# arguments inside for/while bodies and failed while ordinary execution
# succeeded. Every context below must agree with top-level behavior; invalid
# encodings must remain rejected; multibyte UTF-8 must round-trip through
# prepared loops too.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
run(){ printf '%s\n' "$1" > "$t/p.f"; "$NIFT_ABS" "$t/p.f"; }
assert_out(){ [ "$2" = "$3" ] || { echo "FAIL $1: expected [$3] got [$2]" >&2; exit 1; }; }

# --- string encode parity across contexts ---
assert_out "encode top level" "$(run 'x := "a".encode("utf-8")
print(x.length())')" "1"
assert_out "encode function" "$(run 'fn(enc_f()) { return "a".encode("utf-8").length() }
print(enc_f())')" "1"
assert_out "encode for loop" "$(run 'for(i : [1,2,3]) { print("a".encode("utf-8").length()) }')" "1
1
1"
assert_out "encode while loop" "$(run 'i := 1
while(i <= 3) { print("a".encode("utf-8").length()); i = i + 1 }')" "1
1
1"
assert_out "encode nested loop" "$(run 'for(i : [1,2]) { for(j : [1,2]) { print("a".encode("utf-8").length()) } }')" "1
1
1
1"
assert_out "encode loop in function" "$(run 'fn(loop_in_fn()) { for(i : [1,2,3]) { x := "a".encode("utf-8"); print(x.length()) } }
loop_in_fn()')" "1
1
1"

# --- bytes decode parity across contexts ---
assert_out "decode top level" "$(run 'print(bytes([65]).decode("utf-8"))')" "A"
assert_out "decode function" "$(run 'fn(dec_f()) { return bytes([65]).decode("utf-8") }
print(dec_f())')" "A"
assert_out "decode for loop" "$(run 'for(i : [1,2,3]) { print(bytes([65]).decode("utf-8")) }')" "A
A
A"
assert_out "decode while loop" "$(run 'i := 1
while(i <= 2) { print(bytes([65]).decode("utf-8")); i = i + 1 }')" "A
A"
assert_out "decode nested loop" "$(run 'for(i : [1,2]) { for(j : [1,2]) { print(bytes([66]).decode("utf-8")) } }')" "B
B
B
B"
assert_out "decode loop in function" "$(run 'fn(dec_loop_in_fn()) { for(i : [1,2]) { print(bytes([65]).decode("utf-8")) } }
dec_loop_in_fn()')" "A
A"

# --- same native dispatch, separate arg path (bytes slice) ---
assert_out "bytes slice in loop" "$(run 'for(i : [1]) { print(bytes([1,2,3]).slice(0,1).length()) }')" "1"

# --- invalid encodings remain rejected (exact encoding contract preserved) ---
for code in \
    '"a".encode("utf8")' \
    'for(i : [1]) { "a".encode("utf8") }' \
    '"a".encode("UTF-8")' \
    'bytes([65]).decode("utf8")' \
    'for(i : [1]) { bytes([65]).decode("utf8") }' \
    'bytes([65]).decode("UTF-8")' \
    'for(i : [1]) { bytes([65]).decode("UTF-8") }'; do
    printf '%s\n' "$code" > "$t/p.f"
    if "$NIFT_ABS" "$t/p.f" >"$t/o" 2>"$t/e"; then
        echo "FAIL invalid encoding accepted: $code" >&2; exit 1
    fi
    grep -q "encoding must be exactly 'utf-8'" "$t/e" || {
        echo "FAIL invalid encoding diagnostic missing for: $code -> $(head -1 "$t/e")" >&2; exit 1
    }
done
echo "invalid encodings rejected: PASS"

# --- multibyte UTF-8 round trip, top level and through prepared loops ---
assert_out "multibyte top level" "$(run 'print("é".encode("utf-8").decode("utf-8") == "é")
print("中".encode("utf-8").decode("utf-8") == "中")
print("😀".encode("utf-8").decode("utf-8") == "😀")')" "true
true
true"
assert_out "multibyte in loop" "$(run 'for(i : [1,2]) {
    print("é".encode("utf-8").decode("utf-8") == "é")
    print("中".encode("utf-8").decode("utf-8") == "中")
    print("😀".encode("utf-8").decode("utf-8") == "😀")
}')" "true
true
true
true
true
true"

echo "PASS v4.7 prepared method-argument parity"