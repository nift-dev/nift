#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT=${NIFT:-$ROOT/nift}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

printf '\000A\n\r\200\377Z' >"$TMP/input.bin"
: >"$TMP/empty.bin"
dd if=/dev/zero of="$TMP/large.bin" bs=65536 count=2 2>/dev/null
printf '\377' >>"$TMP/large.bin"
: >"$TMP/all-octets.bin"
for i in $(seq 0 255); do printf "\\$(printf '%03o' "$i")" >>"$TMP/all-octets.bin"; done

actual=$("$NIFT" -e "b := open_bytes(\"$TMP/input.bin\"); print(type(b)); print(b.length()); print(b[0]); print(b[1]); print(b[4]); print(b[5]); print(b[6])")
[[ "$actual" == $'bytes\n7\n0\n65\n128\n255\n90' ]]

cat >"$TMP/stream.nift" <<'EOF'
i := ifstream("input.bin")
a := i.read_bytes(3)
b := i.read_all_bytes()
print(type(a))
print(a.length())
print(a[0])
print(a[2])
print(b.length())
print(b[0])
print(b[1])
print(b[2])
print(b[3])
close(i)
o := ofstream("stream.bin")
o.write(a)
o.write(b)
close(o)
EOF
actual=$(cd "$TMP" && "$NIFT" stream.nift)
[[ "$actual" == $'bytes\n3\n0\n10\n4\n13\n128\n255\n90' ]]
cmp "$TMP/input.bin" "$TMP/stream.bin"

cat >"$TMP/managed.nift" <<'EOF'
f := file("input.bin")
f.open("r")
a := f.read_bytes(2)
b := f.read_all_bytes()
print(a.length())
print(a[0])
print(a[1])
print(b.length())
print(b[0])
print(b[4])
f.close()
o := file("managed.bin")
o.open("w")
o.write(a)
o.write(b)
print(o.tell())
print(o.modified())
o.save()
o.close()
EOF
actual=$(cd "$TMP" && "$NIFT" managed.nift)
[[ "$actual" == $'2\n0\n65\n5\n10\n90\n7\ntrue' ]]
cmp "$TMP/input.bin" "$TMP/managed.bin"

actual=$("$NIFT" -e "e := open_bytes(\"$TMP/empty.bin\"); print(e.length()); s := ifstream(\"$TMP/input.bin\"); print(s.read_bytes(0).length()); print(s.read_bytes(9007199254740993).length()); print(s.read_bytes(1).length()); print(s.read_all_bytes().length()); close(s); l := open_bytes(\"$TMP/large.bin\"); print(l.length()); print(l[131072]); a := open_bytes(\"$TMP/all-octets.bin\"); print(a.length()); print(a[0]); print(a[127]); print(a[128]); print(a[255])")
[[ "$actual" == $'0\n0\n7\n0\n0\n131073\n255\n256\n0\n127\n128\n255' ]]

# Managed writes preserve suffixes in rw mode, extend at EOF, revert in-memory
# changes, and atomically replace both existing rw and w destinations.
cp "$TMP/input.bin" "$TMP/existing-rw.bin"
printf 'long-existing-data' >"$TMP/existing-w.bin"
cat >"$TMP/managed-existing.nift" <<'EOF'
f := file("existing-rw.bin")
f.open("rw")
f.seek(1)
f.write(bytes([9,8]))
f.seek(7)
f.write(bytes([7,6]))
f.write(bytes())
print(f.tell())
f.save()
print(f.modified())
f.close()
g := file("existing-rw.bin")
g.open("rw")
g.write(bytes([1]))
g.revert()
print(g.read_all_bytes() == bytes([0,9,8,13,128,255,90,7,6]))
g.close()
w := file("existing-w.bin")
w.open("w")
w.write(bytes([255,0]))
w.save()
w.close()
EOF
actual=$(cd "$TMP" && "$NIFT" managed-existing.nift)
[[ "$actual" == $'9\nfalse\ntrue' ]]
printf '\000\011\010\r\200\377Z\007\006' >"$TMP/expected-rw.bin"
printf '\377\000' >"$TMP/expected-w.bin"
cmp "$TMP/expected-rw.bin" "$TMP/existing-rw.bin"
cmp "$TMP/expected-w.bin" "$TMP/existing-w.bin"

cat >"$TMP/parity.nift" <<'EOF'
fn(prepared_write(f, b)) { f.write(b); return f.tell() }
fn(legacy_write(f, b)) { 9007199254740993; f.write(b); return f.tell() }
a := file("prepared.bin")
a.open("w")
pa := prepared_write(a, bytes([0,128,255]))
a.save()
a.close()
b := file("legacy.bin")
b.open("w")
pb := legacy_write(b, bytes([0,128,255]))
b.save()
b.close()
print(pa == pb)
print(open_bytes("prepared.bin") == open_bytes("legacy.bin"))
EOF
actual=$(cd "$TMP" && "$NIFT" parity.nift)
[[ "$actual" == $'true\ntrue' ]]

# Existing text reads and writes retain their string behavior.
actual=$(cd "$TMP" && "$NIFT" -e 's := ifstream("input.bin"); x := s.read(2); print(type(x)); close(s); o := ofstream("text.txt"); o.write("A"); o.write_line("B"); close(o); print(open("text.txt") == "AB\n")')
[[ "$actual" == $'string\ntrue' ]]

reject() {
    local name=$1 script=$2 pattern=$3
    if (cd "$TMP" && "$NIFT" -e "$script") >"$TMP/$name.out" 2>"$TMP/$name.err"; then
        echo "CP19 expected rejection: $name" >&2
        exit 1
    fi
    grep -q "$pattern" "$TMP/$name.err"
}

reject open-missing 'open_bytes("missing.bin")' 'open_bytes: cannot open path'
reject stream-count 's := ifstream("input.bin"); s.read_bytes(-1)' 'read_bytes: invalid byte count'
reject stream-count-overflow 's := ifstream("input.bin"); s.read_bytes(18446744073709551616)' 'read_bytes: invalid byte count'
reject stream-direction 's := ofstream("x.bin"); s.read_all_bytes()' 'read_all_bytes: expected input stream'
reject managed-direction 'f := file("input.bin"); f.open("w"); f.read_bytes(1)' 'read_bytes: file is not open for reading'
reject stream-line-bytes 's := ofstream("x.bin"); s.write_line(bytes([65]))' 'write_line: value is not directly renderable'
reject managed-line-bytes 'f := file("x.bin"); f.open("w"); f.write_line(bytes([65]))' 'write_line: value is not directly renderable'
reject stream-closed 's := ifstream("input.bin"); close(s); s.read_bytes(1)' 'stream is closed or invalid'

mkdir "$TMP/root"
if NIFT_FS_ROOT="$TMP/root" "$NIFT" -e "open_bytes(\"$TMP/input.bin\")" >"$TMP/root.out" 2>"$TMP/root.err"; then
    echo 'CP19 expected NIFT_FS_ROOT rejection' >&2
    exit 1
fi
grep -q 'path escapes configured filesystem root' "$TMP/root.err"

echo 'CP19 bytes I/O: PASS'
