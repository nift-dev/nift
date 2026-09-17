#!/usr/bin/env bash
# read_val()/write_val() semantics: parse/serialize Nift JSON values through
# ifstream/ofstream and managed files. Covers object, array, nested, string,
# integer, floating-point, true/false, null, whitespace, malformed input, EOF,
# sequential reads, invalid use on write-only/read-only streams, and round-trip.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

check() { # $1=name $2=expected $3=script $4...=files
  local name="$1" expected="$2" script="$3"; shift 3
  local i f
  for f in "$@"; do :; done
  printf '%s' "$script" > "t_$name.nift"
  # write any extra fixture files (name:content pairs)
  i=0; while [ $# -gt 0 ]; do
    printf '%b' "$2" > "$1"
    shift 2
  done
  local out err rc
  out=$("$NIFT" run "t_$name.nift" 2>"err_$name") && rc=0 || rc=$?
  err=$(head -1 "err_$name")
  out=$(printf '%s' "$out" | tr '\n' ' ')
  if [ "$out" = "$expected" ]; then
    echo "PASS  $name"
  else
    echo "FAIL  $name: expected [$expected] got [$out] err[$err]" >&2
    exit 1
  fi
}

# --- read_val: scalar types ---
check string 'hello world true' 's := ifstream("f.txt")
print(s.read_val())
print(s.eof())' 'f.txt' '"hello world"'
check int '42' 's := ifstream("f.txt")
print(s.read_val())' 'f.txt' '42'
check double '3.14' 's := ifstream("f.txt")
print(s.read_val())' 'f.txt' '3.14'
check boolean 'true false null' 's := ifstream("f.txt")
print(s.read_val())
print(s.read_val())
print(s.read_val())' 'f.txt' 'true false null'
# --- read_val: structured ---
check array '3 1 y' 's := ifstream("f.txt")
a := s.read_val()
print(a.size())
print(a[0])
print(a[2].x)' 'f.txt' '[1,2,{"x":"y"}]'
check nested '3 hi' 's := ifstream("f.txt")
a := s.read_val()
print(a.outer.inner.size())
print(a.s)' 'f.txt' '{"outer":{"inner":[1,2,3]},"s":"hi"}'
# --- whitespace / sequential / EOF ---
check whitespace '1' 's := ifstream("f.txt")
a := s.read_val()
print(a.a)' 'f.txt' '  \n\t {"a":1}  '
check sequential '1 two 1 4 true null' 's := ifstream("f.txt")
print(s.read_val())
print(s.read_val())
c := s.read_val()
print(c.size())
d := s.read_val()
print(d.k)
print(s.read_val())
print(s.read_val())' 'f.txt' '1 "two" [3] {"k":4} true null'
check eof-null 'null' 's := ifstream("f.txt")
print(s.read_val())' 'f.txt' '   '
# --- malformed is an error ---
set +e
printf '%s' 's := ifstream("f.txt")
print(s.read_val())' > bad.nift
printf '%s' '{invalid' > f.txt
"$NIFT" run bad.nift >/dev/null 2>bad.err
rc=$?
set -e
[ $rc -ne 0 ] && grep -q 'read_val:' bad.err && echo "PASS  malformed-errors" || { echo "FAIL  malformed-errors" >&2; exit 1; }
# --- read_val on write-only stream is an error ---
set +e
printf '%s' 's := ofstream("f.txt")
print(s.read_val())' > wo.nift
"$NIFT" run wo.nift >/dev/null 2>wo.err
rc=$?
set -e
[ $rc -ne 0 ] && grep -q 'expected input stream' wo.err && echo "PASS  write-only-rejected" || { echo "FAIL  write-only-rejected" >&2; exit 1; }
# --- write_val round-trip (stream) ---
check writeval-obj '1 3 true x' 's := ofstream("o.json")
s.write_val({"a":1,"b":[true,null,"x"]})
close(s)
i := ifstream("o.json")
v := i.read_val()
print(v.a)
print(v.b.size())
print(v.b[0])
print(v.b[2])
close(i)'
check writeval-seq '42 hi 2 true' 's := ofstream("o.txt")
s.write_val(42)
s.write_val("hi")
s.write_val([1,2])
s.write_val(true)
close(s)
i := ifstream("o.txt")
print(i.read_val())
print(i.read_val())
print(i.read_val().size())
print(i.read_val())
close(i)'
# --- write_val round-trip (managed file) ---
check writeval-file '3 true' 'f := file("m.json")
f.open("rw")
f.write_val([1,2,3])
f.write_val({"k":true})
f.seek(0)
a := f.read_val()
b := f.read_val()
print(a.size())
print(b.k)
f.save()
f.close()' 'm.json' '{}'
# --- write_val on read-only stream/file is an error ---
set +e
printf '%s' 'i := ifstream("f.txt")
i.write_val(1)' > wr.nift
printf '%s' 'x' > f.txt
"$NIFT" run wr.nift >/dev/null 2>wr.err
rc=$?
set -e
[ $rc -ne 0 ] && grep -q 'expected one value on output stream' wr.err && echo "PASS  writeval-readonly-stream" || { echo "FAIL  writeval-readonly-stream" >&2; exit 1; }

echo "read_val/write_val smoke passed"