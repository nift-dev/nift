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
big := ifs("big.dat")
total := ""
part := ""
part = big.read(4096)
while(part != "") { total += part; part = big.read(4096) }
print(total == open("big.dat"))
print(big.eof())
close(big)
NIFT
[[ "$(cd "$TMP" && "$NIFT" run stream.nift)" == $'true\ntrue' ]]

# Repeated writes, flush, then read_line/read_all/eof/read_val and null at EOF.
cat > "$TMP/io.nift" <<'NIFT'
o := ofs("d.txt")
for(i : [1,2,3]) { o.write_line("row-" + i) }
o.write("tail")
o.flush()
close(o)
i := ifs("d.txt")
print(i.read_line())
print(i.read(2))
print(i.read_line())
print(i.read_all())
print(i.eof())
print(i.read_line() == null)
close(i)
o2 := ofs("v.txt")
o2.write_line("true 42 3.5 \"s\" [1,2,3] null")
close(o2)
v := ifs("v.txt")
print(v.read_val())
print(v.read_val())
print(v.read_val())
print(v.read_val())
a := v.read_val()
print(a.join(","))
print(v.read_val() == null)
close(v)
NIFT
[[ "$(cd "$TMP" && "$NIFT" run io.nift)" == $'row-1\nro\nw-2\nrow-3\ntail\ntrue\ntrue\ntrue\n42\n3.5\ns\n1,2,3\ntrue' ]]

# Malformed read_val is an error, not silently a string.
cat > "$TMP/bad.nift" <<'NIFT'
o := ofs("bad.txt")
o.write_line("zzz broken")
close(o)
v := ifs("bad.txt")
v.read_val()
NIFT
if (cd "$TMP" && "$NIFT" run bad.nift >/dev/null 2>&1); then
  echo 'malformed read_val unexpectedly succeeded' >&2; exit 1
fi

# Double close and operation-after-close are errors.
cat > "$TMP/dc.nift" <<'NIFT'
x := ifs("d.txt")
close(x)
close(x)
NIFT
if (cd "$TMP" && "$NIFT" run dc.nift >/dev/null 2>&1); then
  echo 'double close unexpectedly succeeded' >&2; exit 1
fi
cat > "$TMP/oac.nift" <<'NIFT'
x := ifs("d.txt")
close(x)
print(x.read_all())
NIFT
if (cd "$TMP" && "$NIFT" run oac.nift >/dev/null 2>&1); then
  echo 'operation after close unexpectedly succeeded' >&2; exit 1
fi

# Destruction without an explicit close still flushes the ofstream.
cat > "$TMP/auto.nift" <<'NIFT'
o := ofs("auto.txt")
o.write_line("persisted")
NIFT
(cd "$TMP" && "$NIFT" run auto.nift >/dev/null)
[[ "$(cat "$TMP/auto.txt")" == 'persisted' ]]

# Escaping stream values: internal references cannot be written.
cat > "$TMP/esc.nift" <<'NIFT'
o := ofs("esc.txt")
fn(f()) { return 1 }
o.write(f)
NIFT
if (cd "$TMP" && "$NIFT" run esc.nift >/dev/null 2>&1); then
  echo 'callable write unexpectedly succeeded' >&2; exit 1
fi
! grep -q 'nift:' "$TMP/esc.txt" 2>/dev/null || { echo 'internal token leaked to file' >&2; exit 1; }

echo 'v4.3 CP78 stream ownership smoke: PASS'