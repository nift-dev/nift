#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift};root=$PWD;t=$(mktemp -d);trap 'rm -rf "$t"' EXIT;cd "$t";"$root/$NIFT" init >/dev/null
cat > content/index.html <<'EOT'
@struct(inner) { x := 1 }
@struct(outer) { child := inner() }
$[a := outer()]$[b := deepcopy(a)]$[b.child.x = 9]$[a.child.x],$[b.child.x]
EOT
"$root/$NIFT" build >/dev/null;grep -q '1,9' public/index.html
