#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift};root=$PWD;t=$(mktemp -d);trap 'rm -rf "$t"' EXIT;cd "$t";"$root/$NIFT" init >/dev/null
cat > content/index.html <<'EOT'
@struct(counter) {
 private n := 0
 private fn(step()) { n = n + 1 }
 fn(counter(start)) { n = start }
 fn(advance(times)) { i := 0; while(i < times) { step(); i = i + 1 }; return this }
 fn(value()) { return n }
}
@fn(pass(x)) { return x }
$[a := counter(2)]$[b := pass(a)]$[b.advance(3)]$[a.value()]
$[c := copy(a)]$[c.advance(1)]/$[a.value()]/$[c.value()]
$[d := deepcopy(a)]$[d.advance(2)]/$[a.value()]/$[d.value()]
EOT
"$root/$NIFT" build >/dev/null
grep -q '5' public/index.html
grep -q '6/5/6' public/index.html
grep -q '7/5/7' public/index.html
