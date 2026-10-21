#!/usr/bin/env bash
set -euo pipefail
if [ -n "${PYTHON:-}" ]; then
  read -r -a PYTHON_CMD <<< "$PYTHON"
elif command -v python3 >/dev/null 2>&1; then
  PYTHON_CMD=(python3)
elif command -v python >/dev/null 2>&1; then
  PYTHON_CMD=(python)
elif command -v py >/dev/null 2>&1; then
  PYTHON_CMD=(py -3)
else
  PYTHON_CMD=()
  for candidate in /usr/local/bin/python3.[0-9]*; do
    if [ -x "$candidate" ] && [ -f "$candidate" ]; then
      PYTHON_CMD=("$candidate")
      break
    fi
  done
  if [ "${#PYTHON_CMD[@]}" -eq 0 ]; then
    printf '%s\n' "error: Python 3 is required to run this script" >&2
    exit 1
  fi
fi
BIN=$(pwd)/nift; j(){ "$BIN" eval --json "$1" | "${PYTHON_CMD[@]}" -c 'import json,sys;print(json.dumps(json.load(sys.stdin),separators=(",",":")))'; }; R=$(mktemp -d); trap 'rm -rf "$R"' EXIT
mkdir -p "$R/.nift" "$R/content" "$R/public" "$R/templates" "$R/model" "$R/meta"
cat > "$R/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified","schemas":["model/site.schema"],"taxonomies":["model/site.tax"]}}
JSON
cat > "$R/model/site.tax" <<'EOF2'
@taxonomy(tag)
@taxonomy(section) { hierarchical: true }
EOF2
cat > "$R/model/site.schema" <<'EOF2'
@schema(post) {
title: string
draft: bool = false
tags: taxonomy(tag)[]
section: taxonomy(section)?
}
@schema(page) {
title: string
}
EOF2
cat > "$R/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"a","title":"A","type":"post"},{"name":"b","title":"B","type":"post","frontmatter":"meta/b.yaml"}]}
JSON
cat > "$R/content/a.md" <<'EOF2'
---
type: post
title: Alpha
tags:
  - nift
  - cpp
section: docs/guides
---
Alpha body
EOF2
cat > "$R/content/b.md" <<'EOF2'
Beta body
EOF2
cat > "$R/meta/b.yaml" <<'EOF2'
type: post
title: Beta
tags:
  - nift
EOF2
cat > "$R/templates/template.html" <<'EOF2'
$[frontmatter.title]
EOF2
(cd "$R" && "$BIN" build --all >/dev/null)
[[ $(cd "$R" && j '(project.schemas).keys()') == '["page","post"]' ]]
[[ $(cd "$R" && j '(project.content.post).map(p => p.title)') == '["Alpha","Beta"]' ]]
[[ $(cd "$R" && j '(project.content.all).size()') == '2' ]]
[[ $(cd "$R" && j '(project.content["post"]).size()') == '2' ]]
[[ $(cd "$R" && j 'project.content.post[0].draft') == 'false' ]]
[[ $(cd "$R" && j '(project.taxonomies.tag).map(t => t.name)') == '["cpp","nift"]' ]]
[[ $(cd "$R" && j '(project.taxonomies.tag[1].content).size()') == '2' ]]
[[ $(cd "$R" && j 'project.taxonomies.section[0].parent') == '"docs"' ]]
# tracked/frontmatter type disagreement is a hard error
sed -i.bak 's/type: post/type: page/' "$R/content/a.md"
if (cd "$R" && "$BIN" build --all >/tmp/cm.out 2>/tmp/cm.err); then echo expected type conflict >&2; exit 1; fi
grep -q 'tracked type conflicts' /tmp/cm.err
sed -i.bak 's/type: page/type: post/' "$R/content/a.md"
(cd "$R" && "$BIN" build --repair >/dev/null)
# schema diagnostics
sed -i.bak 's/title: Alpha/title: 42/' "$R/content/a.md"
if (cd "$R" && "$BIN" build --all >/tmp/cm.out 2>/tmp/cm.err); then echo expected schema failure >&2; exit 1; fi
grep -q "schema post field 'title': expected string" /tmp/cm.err
