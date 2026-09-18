#!/usr/bin/env bash
set -euo pipefail
BIN=${NIFT_BIN:-./nift}
ROOT=$(mktemp -d); trap 'rm -rf "$ROOT"' EXIT
mkdir -p "$ROOT/.nift" "$ROOT/content" "$ROOT/public" "$ROOT/templates"
cat > "$ROOT/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified"}}
JSON
cat > "$ROOT/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"a","title":"A"},{"name":"b","title":"B"}]}
JSON
: > "$ROOT/content/a.md"; : > "$ROOT/content/b.md"
cd "$ROOT"
if [[ "$BIN" == /* ]]; then B="$BIN"; else B="$OLDPWD/$BIN"; fi
OUT=$($B eval --json 'project')
python3 - "$OUT" "$ROOT" <<'PY'
import json,sys
p=json.loads(sys.argv[1]); root=sys.argv[2]
assert p['root']==root and p['mode']=='modified' and len(p['files'])==2
assert [x['name'] for x in p['files']]==['a','b']
assert all(set(('name','path','output_path','url','extension','metadata')) <= set(x) for x in p['files'])
PY
[[ $($B eval --json 'project.files.size()') == '2' ]]
