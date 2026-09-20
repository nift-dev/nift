#!/usr/bin/env bash
set -euo pipefail
N=${NIFT_BIN:-./nift}; N=$(cd "$(dirname "$N")" && pwd)/$(basename "$N")
R=$(mktemp -d); trap 'rm -rf "$R"' EXIT
cd "$R"
printf '[{"v":1},{"v":2}]\n' > data.json
printf '40 + 2\n' > expr.f
cat > test.f <<'F'
a := inject("data.json")
print(a.size())
print(a[1].v)
print(inject("expr.f"))
F
out=$($N run test.f)
[ "$out" = $'2\n2\n42' ] || { printf 'unexpected output:\n%s\n' "$out" >&2; exit 1; }
echo 'JSON inject fast-path smoke passed'
