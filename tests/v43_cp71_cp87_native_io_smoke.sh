#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="$ROOT/nift"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/run.nift" <<'NIFT'
make_dir("work")
touch("work/a.txt")
print(exists("work/a.txt"))
copy("work/a.txt", "work/b.txt")
move("work/b.txt", "work/c.txt")
out := ofs("work/data.txt")
out.write_line("hello")
out.write("world")
out.flush()
close(out)
in := ifs("work/data.txt")
print(in.read_line())
print(in.read_all())
close(in)
whole := open("work/data.txt")
print(whole.substr(0, 5))
remove("work/c.txt")
return pwd()
NIFT
out=$(cd "$TMP" && "$NIFT" run run.nift)
grep -qx 'true' <<<"$out"
grep -qx 'hello' <<<"$out"
grep -qx 'world' <<<"$out"
grep -qx "$TMP" <<<"$out"
cat > "$TMP/vals" <<'EOFV'
true 42 3.5 "hello" [1,2,3]
EOFV
cat > "$TMP/vals.nift" <<'NIFT'
s := ifs("vals")
print(s.read_val())
print(s.read_val())
print(s.read_val())
print(s.read_val())
a := s.read_val()
print(a.join(","))
close(s)
NIFT
expected=$'true\n42\n3.5\nhello\n1,2,3'
[[ "$(cd "$TMP" && "$NIFT" run vals.nift)" == "$expected" ]]
cat > "$TMP/read.nift" <<'NIFT'
x := read()
print("got: " + x)
NIFT
[[ "$(cd "$TMP"; printf 'abc\n' | "$NIFT" run read.nift)" == 'got: abc' ]]
# REPL keeps bindings and recovers from an ordinary error.
repl=$(cd "$TMP" && printf 'x := 4\nprint(x)\nprint(missing)\nprint(x + 1)\nquit\n' | "$NIFT" sh 2>&1 || true)
grep -q '4' <<<"$repl"
grep -q '5' <<<"$repl"
grep -Fq "$TMP:~? " <<<"$repl"
# Filesystem remove is deliberately non-recursive.
cat > "$TMP/remove-dir.nift" <<'NIFT'
make_dir("dir")
remove("dir")
NIFT
if (cd "$TMP" && "$NIFT" run remove-dir.nift >/dev/null 2>&1); then
  echo 'remove(directory) unexpectedly succeeded' >&2; exit 1
fi
echo 'v4.3 CP71-CP87 native I/O smoke: PASS'
