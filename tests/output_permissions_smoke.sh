#!/usr/bin/env bash
# Tracked outputs deterministically preserve the source content file's
# permissions: an executable script output stays executable, and an ordinary
# content output keeps its source mode. Rebuilding does not depend on whatever
# mode the output happened to have before.
set -u
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-output-perms.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
P="$TMP/project"
mkdir -p "$P/.nift" "$P/content" "$P/templates" "$P/public"
cat >"$P/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
cat >"$P/.nift/tracked.json" <<'JSON'
{"tracked":[
  {"name":"/","title":"Home","template":"templates/template.html"},
  {"name":"script","title":"script","content-ext":".sh","output-ext":".sh"},
  {"name":"helper","title":"helper","content-ext":".sh","output-ext":".sh"}
]}
JSON
printf '@content\n' >"$P/templates/template.html"
printf '<p>home</p>\n' >"$P/content/index.html" && chmod 644 "$P/content/index.html"
printf '#!/bin/sh\necho run\n' >"$P/content/script.sh" && chmod 755 "$P/content/script.sh"
printf '#!/bin/sh\necho helper\n' >"$P/content/helper.sh" && chmod 700 "$P/content/helper.sh"

fail() { echo "output-permissions FAIL: $*" >&2; exit 1; }
mode() { stat -c '%a' "$1"; }

(cd "$P" && "$NIFT_BIN" build-all >/dev/null 2>&1) || fail "project did not build"
[ "$(mode "$P/public/script.sh")" = "755" ] || fail "executable script output lost exec bit (got $(mode "$P/public/script.sh"))"
[ "$(mode "$P/public/helper.sh")" = "700" ] || fail "0700 script output wrong mode (got $(mode "$P/public/helper.sh"))"
[ "$(mode "$P/public/index.html")" = "644" ] || fail "0644 content output wrong mode (got $(mode "$P/public/index.html"))"
"$P/public/script.sh" | grep -q run || fail "executable output did not actually run"

# Deterministic across rebuilds regardless of the output's prior mode.
chmod 600 "$P/public/script.sh"
(cd "$P" && "$NIFT_BIN" build-all >/dev/null 2>&1) || fail "rebuild failed"
[ "$(mode "$P/public/script.sh")" = "755" ] || fail "rebuild did not restore source permissions (got $(mode "$P/public/script.sh"))"

echo "output-permissions smoke test passed: outputs preserve source permissions deterministically"
