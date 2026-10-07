#!/usr/bin/env bash
# A method receiver's persistent location must refresh before module detection.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat > "$t/p.f" <<'F'
a := [[1]]
fn(id(x)) { return x }
b := id(a[0])
a.push([2])
b.push(3)
print(a[0][1])
F
[ "$("$B" "$t/p.f")" = 3 ]
cat > "$t/missing.f" <<'F'
a := [[1]]
b := a[0]
a.clear()
b.push(2)
F
if "$B" "$t/missing.f" >"$t/o" 2>"$t/e"; then echo 'FAIL missing receiver succeeded' >&2; exit 1; fi
grep -Fq 'reference target no longer exists: b' "$t/e"
echo 'PASS location method receiver synchronization'
