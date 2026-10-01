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
@/*
@dep('data/ignored.json')
$[title]
*/
@// ignored line $[title]
<!-- html comment: $[title] -->
<pre>@# is ordinary text now</pre>
<p>@# inline text remains</p>
after
EOF

"$NIFT_BIN" build --all >/dev/null

grep -F 'before' public/index.html >/dev/null
grep -F 'after' public/index.html >/dev/null
grep -F '<!-- html comment: Comments -->' public/index.html >/dev/null
! grep -F 'ignored line' public/index.html >/dev/null
grep -F '<pre>@# is ordinary text now</pre>' public/index.html >/dev/null
grep -F '<p>@# inline text remains</p>' public/index.html >/dev/null
! grep -F '"data/ignored.json"' .nift/public/index.info.json >/dev/null

# A comment in an included template fragment must close before @content is
# parsed. Otherwise comment state leaks into the page and desynchronizes the
# <pre> tracker when literal arrow syntax contains angle brackets.
cat > templates/head.html <<'EOF'
<!-- included expression: $[title] -->
EOF
cat > templates/template.html <<'EOF'
<head>@input('head.html')</head><body>@content</body>
EOF
cat > content/index.html <<'EOF'
<pre class="mermaid">a <--> b</pre>
EOF
"$NIFT_BIN" build --all >/dev/null
grep -F '<head><!-- included expression: Comments -->' public/index.html >/dev/null
grep -F '<pre class="mermaid">a &lt;--> b</pre>' public/index.html >/dev/null

cat > content/index.html <<'EOF'
before
@/* never closes
EOF
if "$NIFT_BIN" build --all >build.out 2>build.err; then
    echo "expected unclosed multiline comment to fail" >&2
    exit 1
fi
grep -F "open comment '@/*' has no close '*/'" build.err >/dev/null

echo "Comments smoke test passed"
