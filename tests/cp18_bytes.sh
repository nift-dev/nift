#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT=${NIFT:-$ROOT/nift}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/transfer.nift" <<'NIFT'
fn(prepared_identity(x)) { return x }
fn(legacy_wrap(x)) { marker := 0; marker; return {"payload":[x]} }
fn(make_closure(x)) { local := x; result := () => local; return result }
fn(thread_identity(x)) { return x }
@fn[async](async_identity(x)) { return x }
source := bytes([0,17,127,128,254,255])
assigned := source
copied := copy(source)
aggregate := {"payload":[source], "marker":1}
deep := deepcopy(aggregate)
deep["marker"] = 2
deep["payload"][0] = bytes([9])
print(aggregate.marker)
print(deep.marker)
print(aggregate.payload[0].length())
print(aggregate.payload[0][0])
print(aggregate.payload[0][5])
print(assigned == source)
print(copied == source)
prepared := prepared_identity(source)
legacy := legacy_wrap(source).payload[0]
closure := make_closure(source)
print(prepared == legacy)
print(closure() == source)
m := mutex({"payload":source, "marker":1})
m.lock()
mv := m.get()
mv["marker"] = 8
mv["payload"] = bytes([8])
print(m.get().marker)
print(m.get().payload == source)
m.unlock()
t := thread(thread_identity, source)
tn := thread(thread_identity, {"payload":[source]})
f := async_identity(source)
fnested := async_identity({"payload":[source]})
print(t.join() == source)
print(tn.join().payload[0] == source)
print((await f) == source)
print((await fnested).payload[0] == source)
NIFT

actual=$("$NIFT" "$TMP/transfer.nift")
expected=$'1\n2\n6\n0\n255\ntrue\ntrue\ntrue\ntrue\n1\ntrue\ntrue\ntrue\ntrue\ntrue'
[[ "$actual" == "$expected" ]] || { printf 'CP18 transfer output mismatch:\n%s\n' "$actual" >&2; exit 1; }

reject() {
    local name=$1 source=$2 pattern=$3
    if "$NIFT" -e "$source" >"$TMP/$name.out" 2>"$TMP/$name.err"; then
        echo "CP18 expected rejection: $name" >&2
        exit 1
    fi
    grep -q "$pattern" "$TMP/$name.err"
}
reject transferred-text 'fn(id(x)){return x}; b := id(bytes([65])); print(b)' 'cannot be rendered as text'
reject transferred-json 'fn(wrap(x)){return {"payload":x}}; wrap(bytes([65])).stringify()' 'bytes values are not serializable'
reject threaded-text 'fn(id(x)){return x}; t := thread(id, bytes([65])); print(t.join())' 'cannot be rendered as text'
reject async-json '@fn[async](id(x)){return x}; f := id(bytes([65])); [await f].stringify()' 'bytes values are not serializable'

echo 'CP18 bytes language transfer: PASS'
