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
for mode in 0 require; do
    [ "$(NIFT_VM=$mode "$B" "$t/p.f")" = 3 ]
done
cat > "$t/missing.f" <<'F'
a := [[1]]
b := a[0]
a.clear()
b.push(2)
F
for mode in 0 require; do
    status=0
    NIFT_VM=$mode "$B" "$t/missing.f" >"$t/o" 2>"$t/e" || status=$?
    if [[ $status != 1 || -s "$t/o" ]] ||
       ! grep -Fq 'missing.f:4:1: reference target no longer exists: b' "$t/e"; then
        echo "FAIL missing receiver ($mode): $(cat "$t/e")" >&2
        exit 1
    fi
done
echo 'PASS location method receiver synchronization'
