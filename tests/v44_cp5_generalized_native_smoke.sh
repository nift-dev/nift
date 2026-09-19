#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat >"$t/test.f" <<F
print(min(4, 2, 9))
print(max([4, 2, 9]))
print(min(...[8, 3, 5]))
make_dir("$t/src")
make_dir("$t/copy")
make_dir("$t/move")
touch("$t/src/a")
touch("$t/src/b")
copy(["$t/src/a", "$t/src/b"], "$t/copy")
move("$t/copy/a", "$t/copy/b", "$t/move")
remove(["$t/move/a", "$t/move/b"])
print(exists("$t/move/a"))
F
out=$($NIFT run "$t/test.f")
[ "$out" = "2
9
3
false" ] || { printf '%s\n' "$out" >&2; exit 1; }
echo 'PASS v4.4 CP5 generalized native operations'
