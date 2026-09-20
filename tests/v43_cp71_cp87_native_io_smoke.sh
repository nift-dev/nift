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
out := ofstream("work/data.txt")
out.write_line("hello")
out.write("world")
out.flush()
close(out)
in := ifstream("work/data.txt")
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
s := ifstream("vals")
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
# print() interpolates quoted-string arguments (CP70/CP79) without re-parsing
# non-literal values, and write() shares the same value semantics.
cat > "$TMP/interp.nift" <<'NIFT'
who := "Nift"
print("hello, $[who]")
print("hello, " + who)
out := ofstream("interp.txt")
out.write("v=$[who]")
close(out)
NIFT
expected=$'hello, Nift\nhello, Nift'
[[ "$(cd "$TMP" && "$NIFT" run interp.nift)" == "$expected" ]]
[[ "$(cd "$TMP" && cat interp.txt)" == 'v=Nift' ]]
# REPL keeps bindings and recovers from an ordinary error.
repl=$(cd "$TMP" && printf 'x := 4\nprint(x)\nprint(missing)\nprint(x + 1)\nquit\n' | "$NIFT" sh 2>&1 || true)
grep -q '4' <<<"$repl"
grep -q '5' <<<"$repl"
# Piped (non-interactive) stdin must not emit a prompt; the REPL still keeps
# bindings and recovers from errors (verified by the 4 and 5 greps above).
# Filesystem remove is deliberately non-recursive.
cat > "$TMP/remove-dir.nift" <<'NIFT'
make_dir("dir")
remove("dir")
NIFT
if (cd "$TMP" && "$NIFT" run remove-dir.nift >/dev/null 2>&1); then
  echo 'remove(directory) unexpectedly succeeded' >&2; exit 1
fi
# CLI contract regressions: unknown script statements exit non-zero with a
# diagnostic, and export() is validated-but-ignored under `run` (CP83) rather
# than failing the runnable-and-importable file case.
cat > "$TMP/undefined.nift" <<'NIFT'
this_is_not_defined
NIFT
if (cd "$TMP" && "$NIFT" run undefined.nift >/dev/null 2>&1); then
  echo 'undefined script statement exited zero' >&2; exit 1
fi
cat > "$TMP/exports.nift" <<'NIFT'
value := 3
export(value)
print(value)
NIFT
[[ "$(cd "$TMP" && "$NIFT" run exports.nift)" == '3' ]]
cat > "$TMP/badval.nift" <<'NIFT'
s := ifstream("vals")
print(s.read_val())
NIFT
# 'vals' ends in "[1,2,3]" so read_val then reaches EOF: null is fine; use a
# malformed trailing token to require a non-zero exit.
cat > "$TMP/malformed" <<'EOFV'
true broken
EOFV
cat > "$TMP/badval.nift" <<'NIFT'
s := ifstream("malformed")
print(s.read_val())
print(s.read_val())
NIFT
if (cd "$TMP" && "$NIFT" run badval.nift >/dev/null 2>&1); then
  echo 'malformed read_val succeeded' >&2; exit 1
fi
echo 'v4.3 CP71-CP87 native I/O smoke: PASS'
