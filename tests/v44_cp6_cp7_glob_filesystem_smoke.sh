#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/tree/a/deep" "$t/tree/b" "$t/tree/.hidden" "$t/out"
touch "$t/tree/a/one.o" "$t/tree/a/two.txt" "$t/tree/a/deep/three.o" "$t/tree/b/four.o" "$t/tree/.hidden/secret.o"
i=0; while [ $i -lt 200 ]; do touch "$t/tree/b/item-$i.tmp"; i=$((i+1)); done
cat >"$t/test.f" <<F
print("ALL=" + ls("$t/tree/**/*.o").size().to_string())
print("A=" + ls("$t/tree/a/*.o").size().to_string())
print("B=" + ls("$t/tree/b/*.o").size().to_string())
print("ADEEP=" + ls("$t/tree/a/deep/*.o").size().to_string())
print("ANAME=" + ls("$t/tree/a/one.o").size().to_string())
F
out=$($NIFT run "$t/test.f")
[ "$out" = "ALL=3
A=1
B=1
ADEEP=1
ANAME=1" ] || { printf '%s\n' "$out" >&2; exit 1; }
echo 'PASS v4.4 CP6-CP7 filesystem globs'
