#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-comments.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"

mkdir -p .nift content templates public data
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"/","title":"Comments","template":"templates/template.html"}]}
JSON
cat > templates/template.html <<'EOF'
@content
EOF
printf '{}\n' > data/ignored.json

cat > content/index.html <<'EOF'
before
<#--
@dep('data/ignored.json')
$[title]
--#>
@# ignored line $[title]
@// also ignored $[title]
<!-- html comment: $[title] -->
@#-- old parsed-comment opener is now just a single-line @# comment
after
EOF

"$NIFT_BIN" build --all >/dev/null

grep -F 'before' public/index.html >/dev/null
grep -F 'after' public/index.html >/dev/null
grep -F '<!-- html comment: Comments -->' public/index.html >/dev/null
! grep -F 'ignored line' public/index.html >/dev/null
! grep -F 'also ignored' public/index.html >/dev/null
! grep -F 'old parsed-comment opener' public/index.html >/dev/null
! grep -F '"data/ignored.json"' .nift/public/index.info.json >/dev/null

echo "Comments smoke test passed"
