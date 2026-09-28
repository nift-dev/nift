#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="$ROOT/nift"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# Large-file streaming: many partial reads reassemble the exact file bytes.
python3 - "$TMP" <<'PY'
import sys
with open(sys.argv[1] + "/big.dat", "wb") as f:
    f.write(b"0123456789abcdef" * 65536)  # 1 MiB
PY
cat > "$TMP/stream.nift" <<'NIFT'
big := ifstream("big.dat")
total := ""
part := ""
part = big.read(4096)
while(part != "") { total += part; part = big.read(4096) }
print(total == open("big.dat"))
print(big.eof())
close(big)
NIFT
[[ "$(cd "$TMP" && "$NIFT" stream.nift)" == $'true\ntrue' ]]

# A huge requested count reads only available bytes instead of allocating the request size.
cat > "$TMP/huge-read.nift" <<'NIFT'
@json(n, "count.json")
s := ifstream("small.dat")
print(s.read(n.count))
print(s.eof())
close(s)
NIFT
printf 'small' >"$TMP/small.dat"
printf '{"count":18446744073709551615}\n' >"$TMP/count.json"
[[ "$(cd "$TMP" && "$NIFT" huge-read.nift)" == $'small\ntrue' ]]

# Repeated writes, flush, then read_line/read_all/eof/read_val and null at EOF.
cat > "$TMP/io.nift" <<'NIFT'
o := ofstream("d.txt")
for(i : [1,2,3]) { o.write_line("row-" + i) }
o.write("tail")
o.flush()
close(o)
i := ifstream("d.txt")
print(i.read_line())
print(i.read(2))
print(i.read_line())
print(i.read_all())
print(i.eof())
print(i.read_line() == null)
close(i)
o2 := ofstream("v.txt")
o2.write_line("true 42 3.5 \"s\" [1,2,3] null")
close(o2)
v := ifstream("v.txt")
print(v.read_val())
print(v.read_val())
print(v.read_val())
print(v.read_val())
a := v.read_val()
print(a.join(","))
print(v.read_val() == null)
close(v)
NIFT
[[ "$(cd "$TMP" && "$NIFT" io.nift)" == $'row-1\nro\nw-2\nrow-3\ntail\ntrue\ntrue\ntrue\n42\n3.5\ns\n1,2,3\ntrue' ]]

# Malformed read_val is an error, not silently a string.
cat > "$TMP/bad.nift" <<'NIFT'
o := ofstream("bad.txt")
o.write_line("zzz broken")
close(o)
v := ifstream("bad.txt")
v.read_val()
NIFT
if (cd "$TMP" && "$NIFT" bad.nift >/dev/null 2>&1); then
  echo 'malformed read_val unexpectedly succeeded' >&2; exit 1
fi

# Double close and operation-after-close are errors.
cat > "$TMP/dc.nift" <<'NIFT'
x := ifstream("d.txt")
close(x)
close(x)
NIFT
if (cd "$TMP" && "$NIFT" dc.nift >/dev/null 2>&1); then
  echo 'double close unexpectedly succeeded' >&2; exit 1
fi
cat > "$TMP/oac.nift" <<'NIFT'
x := ifstream("d.txt")
close(x)
print(x.read_all())
NIFT
if (cd "$TMP" && "$NIFT" oac.nift >/dev/null 2>&1); then
  echo 'operation after close unexpectedly succeeded' >&2; exit 1
fi

# Destruction without an explicit close still flushes the ofstream.
cat > "$TMP/auto.nift" <<'NIFT'
o := ofstream("auto.txt")
o.write_line("persisted")
NIFT
(cd "$TMP" && "$NIFT" auto.nift >/dev/null)
[[ "$(cat "$TMP/auto.txt")" == 'persisted' ]]

# Escaping stream values: internal references cannot be written.
cat > "$TMP/esc.nift" <<'NIFT'
o := ofstream("esc.txt")
fn(f()) { return 1 }
o.write(f)
NIFT
if (cd "$TMP" && "$NIFT" esc.nift >/dev/null 2>&1); then
  echo 'callable write unexpectedly succeeded' >&2; exit 1
fi
! grep -q 'nift:' "$TMP/esc.txt" 2>/dev/null || { echo 'internal token leaked to file' >&2; exit 1; }

echo 'v4.3 CP78 stream ownership smoke: PASS'
