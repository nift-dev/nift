#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT=${NIFT:-$ROOT/nift}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

actual=$("$NIFT" -e 'b := bytes([0,65,127,128,255]); print(type(b)); print(is_bytes(b)); print(b.length()); print(b.size()); print(b[1]); print(b?[1]); print(b.slice(1,4)[2]); print((b + bytes([1])).length()); print(b == bytes([0,65,127,128,255])); print(b != bytes([0])); print(b != [0,65,127,128,255]); print(b != ""); print(!bytes()); print(!b); u := bytes([195,169,240,159,152,128]); print(u.decode("utf-8").encode("utf-8") == u)')
expected=$'bytes\ntrue\n5\n5\n65\n65\n128\n6\ntrue\ntrue\ntrue\ntrue\ntrue\nfalse\ntrue'
[[ "$actual" == "$expected" ]]

# The large-integer marker makes the second callable ineligible for prepared
# execution, so both evaluator paths must produce the same byte semantics.
parity=$("$NIFT" -e 'fn(prepared_bytes()) { b := bytes([0,65,127,128,255]); u := bytes([195,169,240,159,152,128]); return [type(b), is_bytes(b), b.length(), b[1], b?[1], b.slice(1,4)[2], (b + bytes([1])).length(), b == bytes([0,65,127,128,255]), b != bytes([0]), !bytes(), !b, u.decode("utf-8").encode("utf-8") == u] }; fn(legacy_bytes()) { 9007199254740993; b := bytes([0,65,127,128,255]); u := bytes([195,169,240,159,152,128]); return [type(b), is_bytes(b), b.length(), b[1], b?[1], b.slice(1,4)[2], (b + bytes([1])).length(), b == bytes([0,65,127,128,255]), b != bytes([0]), !bytes(), !b, u.decode("utf-8").encode("utf-8") == u] }; print(prepared_bytes() == legacy_bytes())')
[[ "$parity" == "true" ]]

reject_expr() {
    local name=$1 expression=$2 pattern=$3
    if "$NIFT" -e "$expression" >"$TMP/$name.out" 2>"$TMP/$name.err"; then
        echo "CP17 expected rejection: $name" >&2
        exit 1
    fi
    grep -q "$pattern" "$TMP/$name.err"
}

reject_expr byte-negative 'bytes([-1])' '0 to 255'
reject_expr byte-large 'bytes([256])' '0 to 255'
reject_expr byte-fraction 'bytes([1.5])' '0 to 255'
reject_expr byte-non-number 'bytes(["1"])' '0 to 255'
reject_expr byte-source 'bytes("x")' 'array of integers'
reject_expr byte-arity 'bytes([], [])' 'zero arguments or one byte array'
reject_expr byte-index 'bytes([1])[1]' 'bytes index.*out of range\|invalid index'
reject_expr immutable-index 'b := bytes([1]); b[0] = 2' 'does not address an array or object\|invalid index target'
reject_expr byte-order 'bytes([1]) < bytes([2])' 'bytes ordering is not supported'
reject_expr byte-mixed-plus '"x" + bytes([1])' 'bytes concatenation requires two bytes values\|string concatenation requires renderable'
reject_expr codec-case '"x".encode("UTF-8")' "exactly 'utf-8'"
reject_expr malformed-continuation 'bytes([226,40,161]).decode("utf-8")' 'invalid UTF-8'
reject_expr overlong-two 'bytes([192,128]).decode("utf-8")' 'invalid UTF-8'
reject_expr overlong-three 'bytes([224,128,128]).decode("utf-8")' 'invalid UTF-8'
reject_expr overlong-four 'bytes([240,128,128,128]).decode("utf-8")' 'invalid UTF-8'
reject_expr surrogate 'bytes([237,160,128]).decode("utf-8")' 'invalid UTF-8'
reject_expr too-large 'bytes([244,144,128,128]).decode("utf-8")' 'invalid UTF-8'
reject_expr print-bytes 'print(bytes([65]))' 'cannot be rendered as text'
reject_expr parameter-interpolation 'print("x$[bytes([65])]")' 'scalar value'
reject_expr stringify-bytes '[1,{"x":bytes([2])}].stringify()' 'bytes values are not serializable'
reject_expr prettify-bytes 'bytes([1]).prettify()' 'bytes values are not serializable'
reject_expr highlight-bytes 'bytes([1]).highlight()' 'bytes values are not serializable'
reject_expr ordered-bytes 'p := prique(); p.push({"nested":bytes([1])})' 'prique: bytes values are not supported'
reject_expr cmd-bytes 'cmd("printf", bytes([65]))' 'cmd: arguments must be scalar'
reject_expr run-bytes 'run("printf", bytes([65]))' 'run: arguments must be scalar'

printf '\377' >"$TMP/invalid-utf8"
reject_expr encode-invalid "open(\"$TMP/invalid-utf8\").encode(\"utf-8\")" 'invalid UTF-8 text'

if "$NIFT" eval '[bytes([1])]' >"$TMP/eval.out" 2>"$TMP/eval.err"; then exit 1; fi
grep -q 'bytes values are not serializable' "$TMP/eval.err"

printf 'b := bytes([65])\nprintf "$[b]"\nexit\n' | "$NIFT" >"$TMP/shell.out" 2>"$TMP/shell.err" || true
grep -q 'bytes values must be decoded as UTF-8' "$TMP/shell.err"
printf 'a := [bytes([65])]\nprintf "$[a]"\nexit\n' | "$NIFT" >"$TMP/shell-nested.out" 2>"$TMP/shell-nested.err" || true
grep -q 'bytes values must be decoded as UTF-8' "$TMP/shell-nested.err"

run_file_reject() {
    local name=$1 pattern=$2
    if (cd "$TMP" && "$NIFT" "$name.nift") >"$TMP/$name.out" 2>"$TMP/$name.err"; then
        echo "CP17 expected file rejection: $name" >&2
        exit 1
    fi
    grep -q "$pattern" "$TMP/$name.err"
}

printf 'b := bytes([65])\n$[b]\n' >"$TMP/direct.nift"
run_file_reject direct 'cannot be rendered as text'
printf '@join([bytes([65])], ",")\n' >"$TMP/template-join.nift"
run_file_reject template-join 'scalar JSON values'
printf '@slice([bytes([65])], 0, 1)\n' >"$TMP/template-collection.nift"
run_file_reject template-collection 'bytes values cannot be rendered as text'
printf '@script { return bytes([65]) }\n' >"$TMP/script-return.nift"
run_file_reject script-return 'not directly renderable'
printf 'f := file("managed-line.txt")\nf.open("w")\nf.write_line(bytes([65]))\n' >"$TMP/managed-line.nift"
run_file_reject managed-line 'not directly renderable'
printf 's := ofstream("stream-line.txt")\ns.write_line(bytes([65]))\n' >"$TMP/stream-line.nift"
run_file_reject stream-line 'not directly renderable'
printf 's := ofstream("value.txt")\ns.write_val({"nested":[bytes([65])]})\n' >"$TMP/write-val.nift"
run_file_reject write-val 'bytes values are not serializable'
printf 'f := file("managed-value.txt")\nf.open("w")\nf.write_val({"nested":[bytes([65])]})\n' >"$TMP/managed-write-val.nift"
run_file_reject managed-write-val 'bytes values are not serializable'
printf '[bytes([65])]' >"$TMP/read-value.txt"
printf 's := ifstream("read-value.txt")\ns.read_val()\n' >"$TMP/read-val.nift"
run_file_reject read-val 'read_val: bytes values are not serializable'
printf 'f := file("read-value.txt")\nf.open("r")\nf.read_val()\n' >"$TMP/managed-read-val.nift"
run_file_reject managed-read-val 'read_val: bytes values are not serializable'

echo 'CP17 bytes language semantics: PASS'
