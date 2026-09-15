#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-./nift}
ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT
cd "$ROOT"
"$NIFT" init >/dev/null
printf '@content\n' > templates/template.html
cat > content/index.html <<'SRC'
$[x := 10]x=$[x]
$[x = 20]x2=$[x]
@if(true){$[x = 30]$[local := "yes"]inside=$[x]/$[local]}
after=$[x]
$[shadow := 1]@if(true){$[shadow := 2]$[shadow = 3]inner-shadow=$[shadow]}outer-shadow=$[shadow]
$[a := c := 10]chain=$[a]/$[c]
$[b := 0]$[d := b = 10]mixed=$[b]/$[d]
$[const fixed := 7]fixed=$[fixed]
SRC
"$NIFT" build --all >/dev/null
grep -q '^x=10$' public/index.html
grep -q '^x2=20$' public/index.html
grep -q 'inside=30/yes' public/index.html
grep -q '^after=30$' public/index.html
grep -q 'inner-shadow=3' public/index.html
grep -q 'outer-shadow=1' public/index.html
grep -q '^chain=10/10$' public/index.html
grep -q '^mixed=10/10$' public/index.html
grep -q '^fixed=7$' public/index.html
cat > content/index.html <<'SRC'
$[const x := 1]$[x = 2]
SRC
if "$NIFT" build --all >/dev/null 2>err; then echo 'const reassignment unexpectedly succeeded' >&2; exit 1; fi
grep -q 'cannot assign to const binding' err
cat > content/index.html <<'SRC'
$[x := 1]$[x = "wrong"]
SRC
if "$NIFT" build --all >/dev/null 2>err; then echo 'type-changing assignment unexpectedly succeeded' >&2; exit 1; fi
grep -q 'cannot assign string to int binding' err
cat > content/index.html <<'SRC'
$[x = 1]
SRC
if "$NIFT" build --all >/dev/null 2>err; then echo 'undefined assignment unexpectedly succeeded' >&2; exit 1; fi
grep -q 'assignment to undefined binding' err
echo 'Nift v4.1 template variable smoke passed'
# v4.1 immut binding is non-rebindable (deep mutation operations are not yet exposed).
