#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-path-safety.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

mkproj(){
  local d="$1"
  mkdir -p "$d/.nift" "$d/content" "$d/templates" "$d/public" "$d/data" "$d/schemas"
  cat >"$d/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
  cat >"$d/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}
JSON
  printf '\n' >"$d/content/index.html"
}

OUT="$TMP/outside"
mkdir -p "$OUT"
printf '{"secret":"outside"}\n' >"$OUT/data.json"
printf '{"type":"object"}\n' >"$OUT/schema.json"
printf 'outside\n' >"$OUT/file.txt"
printf 'OUTSIDE INPUT\n' >"$OUT/input.html"

P="$TMP/project"; mkproj "$P"
ln -s "$OUT/data.json" "$P/data/link.json"
ln -s "$OUT/schema.json" "$P/schemas/link.json"
ln -s "$OUT/file.txt" "$P/public/link.txt"
ln -s "$OUT/file.txt" "$P/data/dep-link.txt"
ln -s "$OUT/input.html" "$P/templates/input-link.html"

fail_build(){
  local name="$1" expected="$2" template="$3"
  printf '%s\n@content\n' "$template" >"$P/templates/template.html"
  rm -f "$P/.nift/.unfinished"
  if (cd "$P" && "$NIFT_BIN" build --all >"$name.log" 2>&1); then
    echo "$name unexpectedly succeeded" >&2
    exit 1
  fi
  grep -F "$expected" "$P/$name.log" >/dev/null || {
    echo "$name missing expected error: $expected" >&2
    cat "$P/$name.log" >&2
    exit 1
  }
}

fail_build json-symlink 'path must stay inside the Nift project' '@json(x, "data/link.json")'
printf '{}\n' >"$P/data/local.json"
fail_build schema-symlink 'schema path must stay inside the Nift project' '@json(x, "schemas/link.json", "data/local.json")'

fail_build dep-symlink 'path must stay inside the Nift project' '@dep("data/dep-link.txt")'
fail_build pathto-symlink 'path must stay inside the Nift project' '@pathto("public/link.txt")'

echo "Path safety smoke test passed"
