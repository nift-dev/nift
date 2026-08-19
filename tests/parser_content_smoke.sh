#!/usr/bin/env bash
set -euo pipefail

NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-content-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

cd "$TMP"
mkdir -p .nift content templates public/assets data
cat > .nift/config.json <<'JSON'
{
  "config": {
    "content-dir": "content/",
    "content-ext": ".html",
    "output-dir": "public/",
    "output-ext": ".html",
    "default-template": "templates/template.html",
    "build-threads": -1,
    "incremental-mode": "modified"
  }
}
JSON

cat > .nift/tracked.json <<'JSON'
{
  "tracked": [
    {
      "name": "/",
      "title": "Content parser test",
      "template": "templates/template.html"
    }
  ]
}
JSON

cat > templates/template.html <<'EOF'
<body>
  @content
</body>
EOF

cat > content/fragment.html <<'EOF'
<strong>$[title]</strong>
@input('nested.html')
EOF

cat > content/nested.html <<'EOF'
<img src="@pathto('public/assets/logo.txt')" alt="$[name]">
@dep('data/nested-state.json')
EOF

cat > content/index.html <<'EOF'
<a href="@pathto('public/assets/logo.txt')">asset</a>
@input('fragment.html')
@getenv('NIFT_CONTENT_TEST')
@ent('&')
@dep('data/state.json')
EOF

printf 'logo\n' > public/assets/logo.txt
printf '{}\n' > data/state.json
printf '{}\n' > data/nested-state.json

NIFT_CONTENT_TEST='from-env' "$NIFT_BIN" build-all >/dev/null

grep -F '<a href="assets/logo.txt">asset</a>' public/index.html >/dev/null
grep -F '<strong>Content parser test</strong>' public/index.html >/dev/null
grep -F '<img src="assets/logo.txt" alt="/">' public/index.html >/dev/null
grep -F 'from-env' public/index.html >/dev/null
grep -F '&amp;' public/index.html >/dev/null

if grep -F '@pathto(' public/index.html >/dev/null ||
   grep -F '@input(' public/index.html >/dev/null ||
   grep -F '$[title]' public/index.html >/dev/null; then
  echo "tracked content was not fully parsed" >&2
  exit 1
fi

grep -F '"content/index.html"' .nift/public/index.info.json >/dev/null
grep -F '"content/fragment.html"' .nift/public/index.info.json >/dev/null
grep -F '"data/state.json"' .nift/public/index.info.json >/dev/null

# Content is part of the parser input stack, so self-input should fail cleanly.
cat > content/index.html <<'EOF'
@input('index.html')
EOF

if "$NIFT_BIN" build-all >/dev/null 2>&1; then
  echo "content/input recursion unexpectedly succeeded" >&2
  exit 1
fi


# Verify template indentation does not leak into <pre*> contents from @content or @input.
cd "$TMP"
rm -rf .nift content templates public data
mkdir -p .nift content templates public

cat > .nift/config.json <<'JSON'
{
  "config": {
    "content-dir": "content/",
    "content-ext": ".html",
    "output-dir": "public/",
    "output-ext": ".html",
    "default-template": "templates/template.html",
    "build-threads": -1,
    "incremental-mode": "modified"
  }
}
JSON

cat > .nift/tracked.json <<'JSON'
{
  "tracked": [
    {
      "name": "/",
      "title": "Pre indentation test",
      "template": "templates/template.html"
    }
  ]
}
JSON

cat > templates/template.html <<'EOF'
<body>
	<main>
		@content
	</main>
</body>
EOF

cat > content/index.html <<'EOF'
<pre class="example"><code>first
  second
third</code></pre>
@input('fragment.html')
EOF

cat > content/fragment.html <<'EOF'
<pre><code>alpha
    beta
gamma</code></pre>
EOF

"$NIFT_BIN" build-all >/dev/null

grep -F $'<pre class="example"><code>first\n  second\nthird</code></pre>' public/index.html >/dev/null || {
  echo "template indentation leaked into @content <pre> block" >&2
  exit 1
}
grep -F $'<pre><code>alpha\n    beta\ngamma</code></pre>' public/index.html >/dev/null || {
  echo "template/content indentation leaked into @input <pre> block" >&2
  exit 1
}


# @pathto(...) records resolved targets as requirements. A requirement only
# needs to continue existing; modification alone must not invalidate the page.
cd "$TMP"
rm -rf .nift content templates public data
mkdir -p .nift content templates public/assets

cat > .nift/config.json <<'JSON'
{
  "config": {
    "content-dir": "content/",
    "content-ext": ".html",
    "output-dir": "public/",
    "output-ext": ".html",
    "default-template": "templates/template.html",
    "build-threads": -1,
    "incremental-mode": "modified"
  }
}
JSON

cat > .nift/tracked.json <<'JSON'
{
  "tracked": [
    {"name":"/","title":"requirements test","template":"templates/template.html"},
    {"name":"about","title":"About","template":"templates/template.html"}
  ]
}
JSON

cat > templates/template.html <<'EOF'
@content
EOF
cat > content/index.html <<'EOF'
<link href="@pathto('public/assets/generated.css')" rel="stylesheet">
<a href="@pathto('about')">About</a>
EOF
cat > content/about.html <<'EOF'
about
EOF
printf 'body{}\n' > public/assets/generated.css

"$NIFT_BIN" build-all >/dev/null
grep -F 'href="assets/generated.css"' public/index.html >/dev/null
grep -F 'href="./about.html"' public/index.html >/dev/null
grep -F '"public/assets/generated.css"' .nift/public/index.info.json >/dev/null
grep -F '"public/about.html"' .nift/public/index.info.json >/dev/null

grep -F '"reqs"' .nift/public/index.info.json >/dev/null
printf 'body{color:red}\n' > public/assets/generated.css
"$NIFT_BIN" status > status-modified.log
if grep -F 'generated.css' status-modified.log >/dev/null; then
  echo "modified requirement incorrectly invalidated page" >&2
  exit 1
fi
rm public/assets/generated.css
"$NIFT_BIN" status > status-missing.log
grep -F 'required path missing: public/assets/generated.css' status-missing.log >/dev/null

# A missing requirement is only a rebuild reason. If the source changed so the
# reference is no longer needed, the normal rebuild must be allowed to succeed.
cat > content/index.html <<'EOF'
<p>No generated stylesheet is required any more.</p>
<a href="@pathto('about')">About</a>
EOF
"$NIFT_BIN" build-updated >/dev/null
grep -F 'No generated stylesheet is required any more.' public/index.html >/dev/null
if grep -F 'public/assets/generated.css' .nift/public/index.info.json >/dev/null; then
  echo "successful rebuild retained a removed requirement" >&2
  exit 1
fi

# Conversely, when source still contains the reference, reqs merely select the
# page for rebuilding and the ordinary @pathto error is what ultimately fails.
printf 'body{}\n' > public/assets/generated.css
cat > content/index.html <<'EOF'
<link href="@pathto('public/assets/generated.css')" rel="stylesheet">
EOF
"$NIFT_BIN" build-all >/dev/null
rm public/assets/generated.css
if "$NIFT_BIN" build-updated >missing-rebuild.log 2>&1; then
  echo "missing requirement unexpectedly rebuilt successfully" >&2
  exit 1
fi
grep -F "'public/assets/generated.css' is neither a tracked name nor a file that exists" missing-rebuild.log >/dev/null


echo "Tracked content/input parser smoke test passed"

# Templated tracked items must execute precisely one @content across the actual
# template/@input graph, even when content is empty. Skipped/commented uses do
# not count; nested @input uses do count.
cd "$TMP"
rm -rf .nift content templates public
mkdir -p .nift content templates public
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"/","title":"content-count","template":"templates/template.html"}]}
JSON
: > content/index.html
cat > templates/template.html <<'EOF2'
@input('slot.html')
EOF2
cat > templates/slot.html <<'EOF2'
@content
EOF2
"$NIFT_BIN" build-all >/dev/null

cat > templates/template.html <<'EOF2'
<p>no insertion</p>
EOF2
if "$NIFT_BIN" build-all >/dev/null 2>&1; then
  echo "empty templated content unexpectedly succeeded without @content" >&2; exit 1
fi

cat > templates/template.html <<'EOF2'
@content
@input('slot.html')
EOF2
if "$NIFT_BIN" build-all >/dev/null 2>&1; then
  echo "duplicate @content across template/input graph unexpectedly succeeded" >&2; exit 1
fi

cat > templates/template.html <<'EOF2'
<#-- @content --#>
@if(false){@content}
@input('slot.html')
EOF2
"$NIFT_BIN" build-all >/dev/null
