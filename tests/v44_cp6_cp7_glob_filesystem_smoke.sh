#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/tree/a/deep" "$t/tree/b" "$t/tree/.hidden" "$t/out"
touch "$t/tree/a/one.o" "$t/tree/a/two.txt" "$t/tree/a/deep/three.o" "$t/tree/b/four.o" "$t/tree/.hidden/secret.o"
i=0; while [ $i -lt 200 ]; do touch "$t/tree/b/item-$i.tmp"; i=$((i+1)); done
# Run inside $t with relative paths so the script is portable across POSIX and
# Windows: an MSYS2 POSIX absolute path (e.g. /d/a/...) is not a path the
# native Windows binary can resolve, and the absolute form broke the glob walk
# there (everything globbed to zero matches).
cd "$t"
cat >"$t/test.f" <<F
print(ls("tree/**/*.o").size())
copy("tree/a/*.o", "out")
copy("tree/b/*.o", "out")
print(ls("out/*.o").size())
remove("out/*.o")
print(ls("out/*.o").size())
F
out=$($NIFT run "$t/test.f")
[ "$out" = "3
2
0" ] || { printf '%s\n' "$out" >&2; exit 1; }
echo 'PASS v4.4 CP6-CP7 filesystem globs'
