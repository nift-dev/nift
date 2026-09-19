#!/usr/bin/env bash
# v4.4 scalar to_string generalization and array concatenation: package dogfood
# exposed numeric-only to_string and missing array concatenation as recurring
# friction. Scalars stringify; composites still require stringify().
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat > "$t/s.f" <<'F'
print("hello".to_string())
print(true.to_string())
print(false.to_string())
print(42.to_string())
print(4.5.to_string())
print(null.to_string())
F
out=$("$NIFT_ABS" run "$t/s.f")
[ "$out" = $'hello\ntrue\nfalse\n42\n4.5\nnull' ] || { echo "$out" >&2; exit 1; }
cat > "$t/a.f" <<'F'
a := [1, 2]
b := [3, 4]
c := a + b
print(c.size().to_string())
print(c[0].to_string())
print(c[1].to_string())
print(c[2].to_string())
print(c[3].to_string())
print((a.size().to_string() + "," + b.size().to_string()))
print(([] + []).size().to_string())
print(([] + [1]).size().to_string())
print(([1] + []).size().to_string())
m := ["x", "y"] + [true, null]
print(m.size().to_string())
print(m[0])
print(m[2].to_string())
print(m[3].to_string())
k := [1]
k += [2, 3]
print(k.size().to_string())
NIFT
out=$("$NIFT_ABS" run "$t/a.f")
[ "$out" = $'4\n1\n2\n3\n4\n2,2\n0\n1\n1\n4\nx\ntrue\nnull\n3' ] || { echo "$out" >&2; exit 1; }
# composites still require stringify()
cat > "$t/c.f" <<'F'
print([1, 2].to_string())
F
if "$NIFT_ABS" run "$t/c.f" >/dev/null 2>&1; then echo "array.to_string should error" >&2; exit 1; fi
cat > "$t/o.f" <<'F'
print({"a": 1}.to_string())
F
if "$NIFT_ABS" run "$t/o.f" >/dev/null 2>&1; then echo "object.to_string should error" >&2; exit 1; fi
echo 'PASS v4.4 scalar to_string + array concatenation'
