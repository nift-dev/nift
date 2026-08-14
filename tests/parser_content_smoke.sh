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

python3 - <<'PY'
from pathlib import Path
text = Path("public/index.html").read_text()
expected_content = '<pre class="example"><code>first\n  second\nthird</code></pre>'
expected_input = '<pre><code>alpha\n    beta\ngamma</code></pre>'
if expected_content not in text:
    raise SystemExit("template indentation leaked into @content <pre> block")
if expected_input not in text:
    raise SystemExit("template/content indentation leaked into @input <pre> block")
PY

echo "Tracked content/input parser smoke test passed"
