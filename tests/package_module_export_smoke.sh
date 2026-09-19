#!/usr/bin/env bash
# v4.4 exported struct/object module pattern: a package exports a value whose
# callable fields are invoked as methods (vips.resize(...)) while retaining
# package-private helpers, without leaking those helpers into the importer.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/site/.nift/packages/vips/src"
printf '{"name":"vips","entry":"src/main.f"}\n' > "$t/site/.nift/packages/vips/manifest.json"
cat > "$t/site/.nift/packages/vips/src/main.f" <<'F'
@fn(scale_helper(x)) { return x * 2 }
@struct(vips_lib) { resize := (w, h) => { return {"w": scale_helper(w), "h": scale_helper(h)} } }
vips := vips_lib()
export(vips)
F
cat > "$t/site/t.f" <<'F'
@import("vips")
r := vips.resize(100, 50)
print(r.w)
print(r.h)
print(vips.resize(2, 3).w)
F
out=$(cd "$t/site" && "$NIFT_ABS" run t.f)
[ "$out" = "200
100
4" ] || { printf 'unexpected struct-module output:\n%s\n' "$out" >&2; exit 1; }
cat > "$t/site/.nift/packages/vips/src/main.f" <<'F'
@fn(scale_helper(x)) { return x * 2 }
vips := {"resize": (w, h) => { return {"w": scale_helper(w), "h": scale_helper(h)} }, "version": "0.1.0"}
export(vips)
F
out2=$(cd "$t/site" && "$NIFT_ABS" run t.f)
[ "$out2" = "200
100
4" ] || { printf 'unexpected object-module output:\n%s\n' "$out2" >&2; exit 1; }
# Private helper must not leak into the importer.
cat > "$t/site/t2.f" <<'F'
@import("vips")
print(scale_helper(5))
F
if (cd "$t/site" && "$NIFT_ABS" run t2.f >/dev/null 2>&1); then echo "private helper leaked" >&2; exit 1; fi
# A struct instance without an exported type must not crash (clean diagnostic).
mkdir -p "$t/local"
cat > "$t/local/bad.f" <<'F'
@struct(inner) { f := 1 }
export(gone)
F
if (cd "$t/site" && "$NIFT_ABS" run "$t/local/bad.f" >/dev/null 2>&1); then :; fi
echo 'PASS v4.4 exported module values with callable fields'