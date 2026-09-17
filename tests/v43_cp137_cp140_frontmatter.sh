#!/usr/bin/env bash
set -euo pipefail
BIN=$(pwd)/nift; R=$(mktemp -d); trap 'rm -rf "$R"' EXIT
mkdir -p "$R/.nift" "$R/content" "$R/public" "$R/templates" "$R/meta"
cat > "$R/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified"}}
JSON
cat > "$R/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"a","title":"A"}]}
JSON
cat > "$R/content/a.md" <<'EOF2'
---
type: post
title: Hello
draft: false
tags:
  - nift
  - release
---
$[frontmatter.title]
EOF2
(cd "$R" && "$BIN" build --all >/dev/null)
grep -qx 'Hello' "$R/public/a.html"
[[ $(cd "$R" && "$BIN" eval --json 'project.files[0].metadata.title') == '"Hello"' ]]
cat > "$R/meta/a.yaml" <<'EOF2'
title: External
type: post
EOF2
python3 - "$R/.nift/tracked.json" <<'PY'
import json,sys
p=sys.argv[1];d=json.load(open(p));d['tracked'][0]['frontmatter']='meta/a.yaml';json.dump(d,open(p,'w'))
PY
if (cd "$R" && "$BIN" build --all >/tmp/fm.out 2>/tmp/fm.err); then echo 'expected conflict failure' >&2; exit 1; fi
grep -q 'multiple front matter sources' /tmp/fm.err
rm "$R/content/a.md"; echo 'body' > "$R/content/a.md"
[[ $(cd "$R" && "$BIN" eval --json 'project.files[0].metadata.title') == '"External"' ]]
