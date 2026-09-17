#!/usr/bin/env bash
set -euo pipefail
BIN=$(pwd)/nift; j(){ "$BIN" eval --json "$1" | python3 -c 'import json,sys;print(json.dumps(json.load(sys.stdin),separators=(",",":")))'; }; R=$(mktemp -d); trap 'rm -rf "$R"' EXIT
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
sed -i 's/type: post/type: page/' "$R/content/a.md"
if (cd "$R" && "$BIN" build --all >/tmp/cm.out 2>/tmp/cm.err); then echo expected type conflict >&2; exit 1; fi
grep -q 'tracked type conflicts' /tmp/cm.err
sed -i 's/type: page/type: post/' "$R/content/a.md"
(cd "$R" && "$BIN" build --repair >/dev/null)
# schema diagnostics
sed -i 's/title: Alpha/title: 42/' "$R/content/a.md"
if (cd "$R" && "$BIN" build --all >/tmp/cm.out 2>/tmp/cm.err); then echo expected schema failure >&2; exit 1; fi
grep -q "schema post field 'title': expected string" /tmp/cm.err
