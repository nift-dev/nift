#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-path-security.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
P="$TMP/project"
mkdir -p "$P/.nift" "$P/content" "$P/templates" "$P/public/assets"
cat >"$P/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
cat >"$P/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}
JSON
printf '\n' >"$P/content/index.html"
printf 'outside\n' >"$TMP/outside.txt"

printf '<a href="@pathto('"'"'../outside.txt'"'"')">escape</a>\n@content\n' >"$P/templates/template.html"
if (cd "$P" && "$NIFT_BIN" build-all >log 2>&1); then
  echo "@pathto traversal unexpectedly succeeded" >&2; exit 1
fi
grep -Fq '@pathto path must stay inside the Nift project' "$P/log"

printf '@dep("../outside.txt")\n@content\n' >"$P/templates/template.html"
if (cd "$P" && "$NIFT_BIN" build-all >log 2>&1); then
  echo "@dep traversal unexpectedly succeeded" >&2; exit 1
fi
grep -Fq 'dep: path must stay inside the Nift project' "$P/log"

printf 'ok\n' >"$P/public/assets/a.txt"
cat >"$P/templates/template.html" <<'EOF'
<a href="@pathto('public/assets/a.txt')">ok</a>
@dep("public/assets/a.txt")
@content
EOF
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq 'assets/a.txt' "$P/public/index.html"
grep -Fq '"public/assets/a.txt"' "$P/.nift/public/index.info.json"

echo "Path security smoke test passed"
