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
"${PYTHON_CMD[@]}" - "$OUT" "$ROOT" <<'PY'
import json,sys
p=json.loads(sys.argv[1]); root=sys.argv[2]
assert p['root']==root and p['mode']=='modified' and len(p['files'])==2
assert [x['name'] for x in p['files']]==['a','b']
assert all(set(('name','path','output_path','url','extension','metadata')) <= set(x) for x in p['files'])
PY
[[ $($B eval --json 'project.files.size()') == '2' ]]
