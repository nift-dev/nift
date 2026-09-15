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
@fn(step_advance(x, times)) { x.advance(times); return x.value() }
$[a := counter(2)]$[b := pass(a)]$[step_advance(b, 3)]$[a.value()]
$[c := copy(a)]$[step_advance(c, 1)]/$[a.value()]/$[c.value()]
$[d := deepcopy(a)]$[step_advance(d, 2)]/$[a.value()]/$[d.value()]
EOT
"$root/$NIFT" build >/dev/null
grep -q '5' public/index.html
grep -q '6/5/6' public/index.html
grep -q '7/5/7' public/index.html

# Rendering a struct reference directly must fail: the internal representation
# must never leak into output (structs have no implicit JSON serialization).
cat > content/index.html <<'EOT'
@struct(counter) {
  n := 0
  fn(value()) { return n }
}
$[a := counter()]x=$[a]
EOT
if "$root/$NIFT" build >/dev/null 2>err; then echo 'struct instance rendering unexpectedly succeeded' >&2; exit 1; fi
grep -q 'cannot render a struct instance' err

# A method may return the receiver, but the receiver reference must be usable
# through a function boundary without leaking a private representation.
cat > content/index.html <<'EOT'
@struct(counter) {
  n := 0
  fn(counter(s)) { n = s }
  fn(advance(times)) { n = n + times; return this }
  fn(value()) { return n }
}
@fn(get(x)) { return x.value() }
$[a := counter(1)]$[get(a.advance(4))]
EOT
"$root/$NIFT" build >/dev/null
grep -q '5' public/index.html