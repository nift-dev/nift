#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"; T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; cd "$T"; "$NIFT_BIN" init >/dev/null

# Numeric literal typing is lexical: integer forms (0, 8) infer int while the
# fractional/exponent forms (0.0, 8.0, 8.5, 1e3) infer double even when the
# value happens to be integral. Inferred binding/field types stay stable.

# 1. Ordinary bindings preserve the source literal form.
cat > content/index.html <<'EOT'
$[a := 0]
$[a = 8]
a=$[a]
$[b := 0.0]
$[b = 8.5]
b=$[b]
$[c := 8.0]
$[c = 9.5]
c=$[c]
$[d := 1e3]
$[d = 2.5]
d=$[d]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'a=8' public/index.html
grep -q 'b=8.5' public/index.html
grep -q 'c=9.5' public/index.html
grep -q 'd=2.5' public/index.html

# 2. Stable inferred types reject cross-form reassignment.
cat > content/index.html <<'EOT'
$[e := 8.5]
$[e = 3]
EOT
! "$NIFT_BIN" build --all >/dev/null 2>&1
cat > content/index.html <<'EOT'
$[f := 0.0]
$[f = 3]
EOT
! "$NIFT_BIN" build --all >/dev/null 2>&1
cat > content/index.html <<'EOT'
$[g := 8]
$[g = 8.5]
EOT
! "$NIFT_BIN" build --all >/dev/null 2>&1

# 3. Arithmetic keeps int/double by the operands' inferred forms.
cat > content/index.html <<'EOT'
$[h := 8]
$[h = h + 1]
h=$[h]
$[i := 0.0]
$[i = i + 1.5]
i=$[i]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'h=9' public/index.html
grep -q 'i=1.5' public/index.html

# 4. Comparisons operate on values regardless of inferred literal form.
cat > content/index.html <<'EOT'
$[j := 0.0]
$[j == 0.0]|$[j == 0]|$[j < 1.5]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'true|true|true' public/index.html

# 5. Struct fields infer by source literal form (the documented stats example).
cat > content/index.html <<'EOT'
@struct(stats) {
 private count := 0
 private total := 0.0
 fn(add(value)) { count = count + 1; total = total + value }
 fn(count()) { return count }
 fn(total()) { return total }
 fn(average()) { if(count == 0) { return 0.0 }; return total / count }
}
$[s := stats()]
$[s.add(8.5)]
$[s.add(9.0)]
$[s.add(7.5)]
count=$[s.count()] total=$[s.total()] avg=$[s.average()]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'count=3' public/index.html
grep -q 'total=25' public/index.html
grep -q 'avg=8.333' public/index.html

# 6. Struct int fields stay int and reject double reassignment.
cat > content/index.html <<'EOT'
@struct(point) { x := 0 }
$[p := point()]
$[p.x = 3]
x=$[p.x]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'x=3' public/index.html
cat > content/index.html <<'EOT'
@struct(point) { x := 0 }
$[p := point()]
$[p.x = 3.5]
EOT
! "$NIFT_BIN" build --all >/dev/null 2>&1

# 7. Function parameters infer by the argument source form.
cat > content/index.html <<'EOT'
@fn(adjust(v)) {
  v = v + 0.5
  return v
}
ADJ=$[adjust(1.0)]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'ADJ=1.5' public/index.html

# 8. Object/array literal numeric values keep their JSON representation.
cat > content/index.html <<'EOT'
$[obj := {"a": 0.5, "b": 2, "c": 3.0}]
$[arr := [1, 2.5, 3.0]]
a=$[obj.a] b=$[obj.b] c=$[obj.c] n0=$[arr[0]] n1=$[arr[1]] n2=$[arr[2]]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'a=0.5 b=2 c=3 n0=1 n1=2.5 n2=3' public/index.html

# 9. copy/deepcopy preserve struct field inferred types.
cat > content/index.html <<'EOT'
@struct(metric) { low := 0.0; high := 10 }
$[m := metric()]
$[c := copy(m)]
$[d := deepcopy(m)]
$[c.low = 2.5]
$[d.high = 42]
c=$[c.low] d=$[d.high] m=$[m.low]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'c=2.5 d=42 m=0' public/index.html

# 10. Injected JSON numbers keep their established representation.
mkdir -p data
cat > data/nums.json <<'J'
{"zero": 0.0, "half": 0.5, "big": 1000}
J
cat > content/index.html <<'EOT'
@json(n, "data/nums.json")
zero=$[n.zero] half=$[n.half] big=$[n.big]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'zero=0.0 half=0.5 big=1000' public/index.html