#!/usr/bin/env bash
# v4.4 package-authoring language fixes: JSON-array push accepts quoted strings,
# and bare script-land call statements returning objects/arrays are
# fire-and-forget rather than render errors.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat > "$t/a.f" <<'F'
args := []
args.push("hello")
args.push("-w")
args.push("%{http_code}")
print(args.size())
print(args[1])
F
[ "$("$NIFT_ABS" run "$t/a.f")" = "3
-w" ] || exit 1
cat > "$t/b.f" <<'F'
@fn(mk()) { return [1, 2] }
@fn(mko()) { return {"a": 1} }
mk()
mko()
print("ok")
F
[ "$("$NIFT_ABS" run "$t/b.f")" = "ok" ] || exit 1
# A bare object VALUE still requires member selection.
cat > "$t/c.f" <<'F'
o := {"a": 1}
o
print("never")
F
if "$NIFT_ABS" run "$t/c.f" >/dev/null 2>&1; then echo "bare object value should error" >&2; exit 1; fi
echo 'PASS v4.4 package-authoring language fixes'