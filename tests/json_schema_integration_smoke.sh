#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-json-schema-int.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

mkproj() {
  local d="$1"
  mkdir -p "$d/.nift" "$d/content" "$d/templates" "$d/public" "$d/data" "$d/schemas"
  cat >"$d/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
  cat >"$d/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}
JSON
}

# Successful validation and automatic dependency recording for data + schema.
P="$TMP/basic"; mkproj "$P"
cat >"$P/templates/template.html" <<'EOF'
@json(data, "schemas/items.schema.json", "data/items.json")
@for(item : data.items by item.rank asc){$[loop.index]:$[item.name]
}
@content
EOF
cat >"$P/data/items.json" <<'EOF'
{"items":[{"name":"b","rank":2},{"name":"a","rank":1}]}
EOF
cat >"$P/schemas/items.schema.json" <<'EOF'
{"type":"object","required":["items"],"properties":{"items":{"type":"array","items":{"type":"object","required":["name","rank"],"properties":{"name":{"type":"string"},"rank":{"type":"integer"}},"additionalProperties":false}}},"additionalProperties":false}
EOF
printf '\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
grep -Fq '1:a' "$P/public/index.html"
grep -Fq '2:b' "$P/public/index.html"
grep -Fq '"data/items.json"' "$P/.nift/public/index.info.json"
grep -Fq '"schemas/items.schema.json"' "$P/.nift/public/index.info.json"

# Data change invalidates and bad data fails through the schema.
cat >"$P/data/items.json" <<'EOF'
{"items":[{"name":"oops","rank":"2"}]}
EOF
if (cd "$P" && "$NIFT_BIN" build >bad-data.log 2>&1); then
  echo "schema-invalid data unexpectedly built" >&2
  exit 1
fi
(cd "$P" && rm -f .nift/.unfinished)
grep -Fq 'does not satisfy schema' "$P/bad-data.log"
grep -Fq '$.items[0].rank' "$P/bad-data.log"

# Repair data, then tighten schema: schema change itself must trigger rebuild.
cat >"$P/data/items.json" <<'EOF'
{"items":[{"name":"ok","rank":2}]}
EOF
(cd "$P" && "$NIFT_BIN" build >/dev/null)
cat >"$P/schemas/items.schema.json" <<'EOF'
{"type":"object","properties":{"items":{"type":"array","items":{"type":"object","required":["name","rank"],"properties":{"name":{"type":"string"},"rank":{"type":"integer","maximum":1}}}}}}
EOF
if (cd "$P" && "$NIFT_BIN" build >bad-schema-change.log 2>&1); then
  echo "schema change failed to invalidate dependent page" >&2
  exit 1
fi
grep -Fq 'greater than maximum' "$P/bad-schema-change.log"

# False branch containing an invalid schema load must stay lazy.
P="$TMP/lazy"; mkproj "$P"
cat >"$P/templates/template.html" <<'EOF'
@if(false){
  @json(never, "schemas/missing.json", "data/missing.json")
  $[never.x]
}
OK
@content
EOF
printf '\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
grep -Fq 'OK' "$P/public/index.html"

# Same schema path loaded in nested input should remain a dependency and binding
# scope must not leak after the nested parse.
P="$TMP/input-scope"; mkproj "$P"
mkdir -p "$P/templates/partials"
cat >"$P/templates/template.html" <<'EOF'
@input("templates/partials/part.html")
AFTER=$[nested.name]
@content
EOF
cat >"$P/templates/partials/part.html" <<'EOF'
@json(nested, "schemas/item.schema.json", "data/item.json")
IN=$[nested.name]
EOF
cat >"$P/data/item.json" <<'EOF'
{"name":"Ada"}
EOF
cat >"$P/schemas/item.schema.json" <<'EOF'
{"type":"object","required":["name"],"properties":{"name":{"type":"string"}}}
EOF
printf '\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
grep -Fq 'IN=Ada' "$P/public/index.html"
grep -Fq 'AFTER=Ada' "$P/public/index.html"
grep -Fq '"schemas/item.schema.json"' "$P/.nift/public/index.info.json"

# Path traversal for both data and schema must be rejected.
P="$TMP/traversal"; mkproj "$P"
printf '{}\n' >"$TMP/outside.json"
printf '{}\n' >"$P/data/ok.json"
printf '{}\n' >"$P/schemas/ok.json"
printf '@json(data, "../outside.json")\n@content\n' >"$P/templates/template.html"
printf '\n' >"$P/content/index.html"
if (cd "$P" && "$NIFT_BIN" build --all >trav-data.log 2>&1); then
  echo "json traversal unexpectedly succeeded" >&2
  exit 1
fi
(cd "$P" && rm -f .nift/.unfinished)
grep -Fq 'path must stay inside the Nift project' "$P/trav-data.log"

printf '@json(data, "../outside.json", "data/ok.json")\n@content\n' >"$P/templates/template.html"
if (cd "$P" && "$NIFT_BIN" build --all >trav-schema.log 2>&1); then
  echo "schema traversal unexpectedly succeeded" >&2
  exit 1
fi
grep -Fq 'schema path must stay inside the Nift project' "$P/trav-schema.log"

echo "JSON Schema integration smoke test passed"
