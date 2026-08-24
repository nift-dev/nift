#!/usr/bin/env bash
# @pathto on the tracked page named `404` must emit root-absolute web paths,
# because a deployed 404 document is served at arbitrary request depth and has
# no meaningful relative location. Every existence/dependency-checking property
# is unchanged: only the path representation differs. Ordinary pages keep the
# usual relative-path behaviour.
set -u
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-pathto-404.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
P="$TMP/project"
mkdir -p "$P/.nift" "$P/content" "$P/content/assets/css" "$P/content/docs" "$P/content/guides" "$P/templates" "$P/public"
cat >"$P/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
cat >"$P/.nift/tracked.json" <<'JSON'
{"tracked":[
  {"name":"/","title":"Home","template":"templates/template.html"},
  {"name":"about","title":"About","template":"templates/template.html"},
  {"name":"docs","title":"Docs","template":"templates/template.html"},
  {"name":"docs/getting-started","title":"Start","template":"templates/template.html"},
  {"name":"guides/","title":"Guides","template":"templates/template.html"},
  {"name":"404","title":"Not found","template":"templates/template.html"},
  {"name":"assets/css/style","title":"style","content-ext":".css","output-ext":".css"}
]}
JSON
printf '@content\n' >"$P/templates/template.html"
printf '<p>home</p>\n' >"$P/content/index.html"
for n in about docs docs/getting-started guides/index; do printf '<p>%s</p>\n' "$n" >"$P/content/$n.html"; done
printf 'css\n' >"$P/content/assets/css/style.css"

fail() { echo "pathto-404 FAIL: $*" >&2; exit 1; }

# The 404 page: every @pathto must be root-absolute.
cat >"$P/content/404.html" <<'EOF'
<a href="@pathto('/')">Home</a>
<a href="@pathto('about')">About</a>
<a href="@pathto('docs')">Docs</a>
<a href="@pathto('docs/getting-started')">Start</a>
<a href="@pathto('guides/')">Guides</a>
<link rel="stylesheet" href="@pathto('assets/css/style')">
EOF
(cd "$P" && "$NIFT_BIN" build --all >log 2>&1) || { echo "log:"; cat "$P/log"; fail "404 project did not build"; }
for want in 'href="/"' 'href="/about.html"' 'href="/docs.html"' \
            'href="/docs/getting-started.html"' 'href="/guides/"' \
            'href="/assets/css/style.css"'; do
  grep -Fq "$want" "$P/public/404.html" || fail "404 output missing $want (got: $(cat "$P/public/404.html"))"
done

# A normal page must keep relative paths.
cat >"$P/content/index.html" <<'EOF'
<a href="@pathto('/')">Home</a>
<a href="@pathto('docs/getting-started')">Start</a>
<link rel="stylesheet" href="@pathto('assets/css/style')">
EOF
(cd "$P" && "$NIFT_BIN" build --all >/dev/null 2>&1) || fail "normal page did not build"
grep -Fq 'href="./"' "$P/public/index.html" || grep -Fq 'href="./index.html"' "$P/public/index.html" \
  || fail "normal page root link is not relative (got: $(cat "$P/public/index.html"))"
grep -Fq 'href="docs/getting-started.html"' "$P/public/index.html" \
  || fail "normal page link is not relative"
grep -Fq 'href="assets/css/style.css"' "$P/public/index.html" \
  || fail "normal page asset link is not relative"

# Existence/dependency checking must be intact on the 404 page too.
printf '<a href="@pathto('"'"'missing'"'"')">x</a>\n' >"$P/content/404.html"
(cd "$P" && "$NIFT_BIN" build --all >log 2>&1) && fail "404 @pathto to a missing target was accepted"
grep -Fq "neither a tracked name nor a file that exists" "$P/log" \
  || fail "404 @pathto missing-target did not report the usual existence error (got: $(cat "$P/log" 2>/dev/null))"

echo "pathto-404 smoke test passed: 404 paths are root-absolute; ordinary pages stay relative; checking intact"
