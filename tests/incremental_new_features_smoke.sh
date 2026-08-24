#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-inc-new.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

mkproj() {
  local d="$1" mode="$2"
  mkdir -p "$d/.nift" "$d/content" "$d/templates" "$d/public/assets" "$d/data" "$d/schemas"
  cat >"$d/.nift/config.json" <<JSON
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"$mode"}}
JSON
  cat >"$d/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}
JSON
  printf '\n' >"$d/content/index.html"
}

for MODE in modified hash hybrid; do
  P="$TMP/$MODE"; mkproj "$P" "$MODE"
  printf 'asset-v1\n' >"$P/public/assets/a.txt"
  printf '{"value":1}\n' >"$P/data/site.json"
  printf '%s\n' '{"type":"object","properties":{"value":{"type":"integer","maximum":10}}}' >"$P/schemas/site.json"
  cat >"$P/templates/template.html" <<'EOF'
@json("data/site.json", site, "schemas/site.json")
<a href="@pathto('public/assets/a.txt')">$[site.value]</a>
@content
EOF
  (cd "$P" && "$NIFT_BIN" build --all >/dev/null)

  # Requirement content change alone must never trigger rebuild.
  sleep 1
  printf 'asset-v2\n' >"$P/public/assets/a.txt"
  (cd "$P" && "$NIFT_BIN" status >status-req-change.log)
  if grep -Fq 'needs rebuilding' "$P/status-req-change.log"; then
    echo "$MODE: modified req incorrectly invalidated page" >&2
    cat "$P/status-req-change.log" >&2
    exit 1
  fi

  # JSON dependency change must trigger rebuild in every mode.
  sleep 1
  printf '{"value":2}\n' >"$P/data/site.json"
  (cd "$P" && "$NIFT_BIN" status >status-json.log)
  grep -Fq 'needs rebuilding' "$P/status-json.log"
  (cd "$P" && "$NIFT_BIN" build >/dev/null)
  grep -Fq '>2</a>' "$P/public/index.html"

  # Schema change must also trigger rebuild and can invalidate unchanged data.
  sleep 1
  printf '%s\n' '{"type":"object","properties":{"value":{"type":"integer","maximum":1}}}' >"$P/schemas/site.json"
  if (cd "$P" && "$NIFT_BIN" build >schema-fail.log 2>&1); then
    echo "$MODE: schema invalidation did not fail" >&2
    exit 1
  fi
  grep -Fq 'greater than maximum' "$P/schema-fail.log"

  # Repair by changing schema again.
  printf '%s\n' '{"type":"object","properties":{"value":{"type":"integer","maximum":5}}}' >"$P/schemas/site.json"
  (cd "$P" && "$NIFT_BIN" build >/dev/null)

  # Deleting a schema marks rebuild; changing template to stop using it must repair.
  rm "$P/schemas/site.json"
  cat >"$P/templates/template.html" <<'EOF'
@json("data/site.json", site)
<span>$[site.value]</span>
<a href="@pathto('public/assets/a.txt')">asset</a>
@content
EOF
  (cd "$P" && "$NIFT_BIN" build >/dev/null)
  ! grep -Fq '"schemas/site.json"' "$P/.nift/public/index.info.json"

  # Deleting a req marks rebuild; recreating it before build should make page clean again.
  rm "$P/public/assets/a.txt"
  (cd "$P" && "$NIFT_BIN" status >status-missing-req.log)
  grep -Fq 'required path missing: public/assets/a.txt' "$P/status-missing-req.log"
  printf 'restored\n' >"$P/public/assets/a.txt"
  (cd "$P" && "$NIFT_BIN" status >status-restored-req.log)
  if grep -Fq 'needs rebuilding' "$P/status-restored-req.log"; then
    echo "$MODE: restored req remained stale without source/dependency change" >&2
    cat "$P/status-restored-req.log" >&2
    exit 1
  fi
done

echo "New-feature incremental modes smoke test passed"
