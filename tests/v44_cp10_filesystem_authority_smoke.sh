#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/a/b"; printf outside >"$t/a/outside.txt"
cd "$t/a/b"
cat >"$t/a/b/test.f" <<F
print(cat("../outside.txt"))
print(exists("../outside.txt"))
print(type(ls("~")))
F
out=$($NIFT run "$t/a/b/test.f")
printf '%s' "$out" | grep -q 'outside' || exit 1
printf '%s' "$out" | grep -q 'true' || exit 1
printf '%s' "$out" | grep -q 'array' || exit 1
echo 'PASS v4.4 CP10 standalone filesystem authority'
