#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/tree/a/deep" "$t/tree/b" "$t/tree/.hidden" "$t/out"
touch "$t/tree/a/one.o" "$t/tree/a/two.txt" "$t/tree/a/deep/three.o" "$t/tree/b/four.o" "$t/tree/.hidden/secret.o"
cat >"$t/test.f" <<F
print(ls("$t/tree/**/*.o").size())
copy("$t/tree/a/*.o", "$t/out")
copy("$t/tree/b/*.o", "$t/out")
print(ls("$t/out/*.o").size())
remove("$t/out/*.o")
print(ls("$t/out/*.o").size())
F
out=$($NIFT run "$t/test.f")
[ "$out" = "3
2
0" ] || { printf '%s\n' "$out" >&2; exit 1; }
echo 'PASS v4.4 CP6-CP7 filesystem globs'
