#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; root=$PWD;t=$(mktemp -d);trap 'rm -rf "$t"' EXIT;cd "$t";"$root/$NIFT" init >/dev/null
cat > content/index.html <<'EOT'
@struct(point) { x := 1 }
$[a := point()]$[b := copy(a)]$[b.x = 8]$[a.x],$[b.x]
EOT
"$root/$NIFT" build >/dev/null;grep -q '1,8' public/index.html
