#!/usr/bin/env bash
# v4.4 package ecosystem hardening: manifest path traversal rejected, add
# conflict detection, and import isolation (package-private bindings must not
# leak into the importer while exported functions keep their private context).
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/site/.nift"
echo "HARD-STEP 1"
# 1) manifest entry must stay inside the package directory
mkdir -p "$t/evil"
printf '{"name":"evil","entry":"../../escape.f"}\n' > "$t/evil/manifest.json"
printf 'print("pwned")\n' > "$t/escape.f"
evil=$(cd "$t/site" && "$NIFT_ABS" add ../evil 2>&1 || true)
grep -q 'package entry escapes the package directory' <<<"$evil" || { echo "HARD-DIAG evil add output: $evil" >&2; exit 1; }
# 2) adding a dependency name that already exists is refused
mkdir -p "$t/pkg/src"
printf '{"name":"demo","entry":"src/main.f"}\n' > "$t/pkg/manifest.json"
printf 'v := 1\nexport(v)\n' > "$t/pkg/src/main.f"
echo "HARD-STEP 2"
pkg_add=$(cd "$t/site" && "$NIFT_ABS" add ../pkg 2>&1)
echo "HARD-DIAG first add ../pkg output: $pkg_add"
dup=$(cd "$t/site" && "$NIFT_ABS" add ../pkg 2>&1 || true)
grep -q "already a dependency" <<<"$dup" || { echo "HARD-DIAG dup add output: $dup" >&2; exit 1; }
echo "HARD-STEP 3"
# 3) import isolation: exported functions keep private context; private
#    bindings never leak into the importer.
mkdir -p "$t/site/.nift/packages/iso/src"
printf '{"name":"iso","entry":"src/main.f"}\n' > "$t/site/.nift/packages/iso/manifest.json"
printf 'secret_helper := "hidden"\n@fn(public_fn(x)){ return x + 1 }\nexport(public_fn)\n' > "$t/site/.nift/packages/iso/src/main.f"
printf '@import("iso")\nprint(public_fn(1))\n' > "$t/site/t.f"
[ "$(cd "$t/site" && "$NIFT_ABS" run t.f)" = "2" ] || { echo "HARD-DIAG import run output: $(cd "$t/site" && "$NIFT_ABS" run t.f 2>&1)" >&2; exit 1; }
printf '@import("iso")\nprint(secret_helper)\n' > "$t/site/t2.f"
if (cd "$t/site" && "$NIFT_ABS" run t2.f >/dev/null 2>&1); then echo "private binding leaked into importer" >&2; exit 1; fi
echo 'PASS v4.4 package hardening'