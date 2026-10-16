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

ok_build(){
  local name="$1" expected="$2" template="$3"
  printf '%s\n@content\n' "$template" >"$P/templates/template.html"
  rm -f "$P/.nift/.unfinished"
  (cd "$P" && "$NIFT_BIN" build --all >"$name.log" 2>&1) || {
    echo "$name failed (Runtime 2.0 D-002: reads outside the project are ordinary)" >&2
    cat "$P/$name.log" >&2; exit 1; }
  [ -z "$expected" ] || grep -F "$expected" "$P/public/index.html" >/dev/null || {
    echo "$name output missing: $expected" >&2; exit 1; }
}

# Runtime 2.0 decision D-002: symlinked/external inputs are ordinary reads.
ok_build json-symlink 'outside' '@json(x, "data/link.json")$[x.secret]'
printf '{}\n' >"$P/data/local.json"
ok_build schema-symlink '' '@json(x, "schemas/link.json", "data/local.json")'

ok_build dep-symlink '' '@dep("data/dep-link.txt")'
# Generated-URL containment is retained: @path must not point outside output.
fail_build pathto-symlink 'path must stay inside the Nift project' '@path("public/link.txt")'

echo "Path safety smoke test passed"
