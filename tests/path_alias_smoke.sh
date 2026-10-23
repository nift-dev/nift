#!/usr/bin/env bash
set -euo pipefail

NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-path-alias.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
P="$TMP/project"
mkdir -p "$P/.nift" "$P/content" "$P/templates" "$P/public/assets"

cat >"$P/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
cat >"$P/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"canonical","title":"Canonical"},{"name":"legacy","title":"Legacy"}]}
JSON
printf '@content\n' >"$P/templates/template.html"
printf 'asset\n' >"$P/public/assets/a file.txt"
printf 'unused\n' >"$P/public/assets/skipped.txt"
printf '{"asset":"a file"}\n' >"$P/data.json"

cat >"$P/content/canonical.html" <<'EOF'
@json(data, "data.json")
<a href="@path('public/assets/$[data.asset].txt')">asset</a>
@if(false){@path('public/assets/skipped.txt')}
<code>\@path('public/assets/a file.txt')</code>
EOF
cat >"$P/content/legacy.html" <<'EOF'
@json(data, "data.json")
<a href="@pathto('public/assets/$[data.asset].txt')">asset</a>
@if(false){@pathto('public/assets/skipped.txt')}
<code>\@path('public/assets/a file.txt')</code>
EOF

(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
cmp "$P/public/canonical.html" "$P/public/legacy.html"
grep -Fq '<a href="assets/a file.txt">asset</a>' "$P/public/canonical.html"
grep -Fq "@path('public/assets/a file.txt')" "$P/public/canonical.html"

python3 -S - "$P/.nift/public/canonical.info.json" "$P/.nift/public/legacy.info.json" <<'PY'
import json, sys
canonical = json.load(open(sys.argv[1], encoding="utf-8"))["reqs"]
legacy = json.load(open(sys.argv[2], encoding="utf-8"))["reqs"]
expected = ["public/assets/a file.txt"]
assert canonical == expected, canonical
assert legacy == expected, legacy
PY

rm "$P/public/assets/a file.txt"
(cd "$P" && "$NIFT_BIN" status >status.log)
test "$(grep -Fc 'required path missing: public/assets/a file.txt' "$P/status.log")" -eq 2

# CP03: each spelling resolves an existing outside target the same way; a
# missing target still fails with the ordinary existence diagnostic.
printf 'outside\n' >"$TMP/outside"
printf "@path('../outside')\n" >"$P/content/canonical.html"
printf "@pathto('../outside')\n" >"$P/content/legacy.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null 2>&1)
grep -Fq '../../outside' "$P/public/canonical.html"
grep -Fq '../../outside' "$P/public/legacy.html"
printf "@path('../missing-outside')\n" >"$P/content/canonical.html"
if (cd "$P" && "$NIFT_BIN" build --all >missing.log 2>&1); then
  echo "missing outside @path unexpectedly succeeded" >&2
  exit 1
fi
grep -Fq "is neither a tracked name nor a file that exists" "$P/missing.log"

echo "Path alias smoke test passed"
