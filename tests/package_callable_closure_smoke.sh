#!/usr/bin/env bash
set -euo pipefail
NIFT="${NIFT:-./nift}"
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/site/.nift/packages/demo/src"
cat > "$tmp/site/.nift/packages/demo/manifest.json" <<'JSON'
{"name":"demo","version":"0.1.0","entry":"src/main.f"}
JSON
cat > "$tmp/site/.nift/packages/demo/src/main.f" <<'F'
@fn(helper(x)) { return x * 2 }
@fn(other(x)) { return x + 1 }
@fn(public_add(a, b)) { return helper(a) + other(b) }
export(public_add)
F
cat > "$tmp/site/test.f" <<'F'
@import("demo")
print(public_add(3, 4))
F
out=$(cd "$tmp/site" && "$NIFT_ABS" run test.f)
[ "$out" = 11 ] || { printf '%s\n' "$out" >&2; exit 1; }
echo 'PASS package exported callables use private module helpers'