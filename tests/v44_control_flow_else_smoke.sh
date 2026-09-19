#!/usr/bin/env bash
# v4.4 break/continue inside an else branch must propagate to the enclosing
# loop. Package dogfood (conservative SQL binders) exposed that a
# break/continue raised in an else/else-if branch was consumed and lost.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat > "$t/a.f" <<'F'
i := 0
while(i < 5) {
    i += 1
    if(i == 3) { print("hit") }
    else { print("else-" + i.to_string()); break }
}
print("done")
F
out=$("$NIFT_ABS" run "$t/a.f")
[ "$out" = $'else-1\ndone' ] || { echo "$out" >&2; exit 1; }
cat > "$t/b.f" <<'F'
j := 0
while(j < 5) {
    j += 1
    if(j == 2) { continue }
    else { print("v" + j.to_string()) }
}
print("done")
F
out=$("$NIFT_ABS" run "$t/b.f")
[ "$out" = $'v1\nv3\nv4\nv5\ndone' ] || { echo "$out" >&2; exit 1; }
cat > "$t/c.f" <<'F'
k := 0
while(k < 4) {
    k += 1
    if(k == 1) { print("one") }
    else if(k == 2) { print("two") }
    else { break }
}
print("done")
F
out=$("$NIFT_ABS" run "$t/c.f")
[ "$out" = $'one\ntwo\ndone' ] || { echo "$out" >&2; exit 1; }
echo 'PASS v4.4 break/continue propagation from else branches'
