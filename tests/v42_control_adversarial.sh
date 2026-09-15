#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cd "$T"; "$NIFT_BIN" init >/dev/null
cat > content/index.html <<'CASE1'
@fn(scan(items)) {
 total := 0
 for(x : items) {
   if(x == 2) { continue }
   if(x == 4) { break }
   total = total + x
 }
 return total
}
SCAN=$[scan([1,2,3,4,5])]
@fn(loopret()) {
 i := 0
 while(i < 10) { i = i + 1; if(i == 3) { return i } }
 return 99
}
LOOPRET=$[loopret()]
@fragment(f(x)){A@if(x){return}B}
F1=$[f(true)] F2=$[f(false)]
CASE1
"$NIFT_BIN" build --all >/dev/null
grep -q 'SCAN=4' public/index.html
grep -q 'LOOPRET=3' public/index.html
grep -q 'F1=A F2=AB' public/index.html
cat > content/index.html <<'CASE2'
@fn(bad()){ break }
@for(x : [1]){$[bad()]}
CASE2
if "$NIFT_BIN" build --all >/dev/null 2>err; then echo expected-boundary-error >&2; exit 1; fi
grep -q 'break is only valid inside a loop' err
cat > content/index.html <<'CASE3'
Please return home. Click continue to proceed. Do not break this text.
CASE3
"$NIFT_BIN" build --all >/dev/null
grep -q 'Please return home. Click continue to proceed. Do not break this text.' public/index.html
