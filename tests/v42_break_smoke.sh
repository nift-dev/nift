#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}";T=$(mktemp -d);trap 'rm -rf "$T"' EXIT;cd "$T";"$NIFT_BIN" init >/dev/null
cat > content/index.html <<'EOT'
@for(x : [1,2,3]){[$[x]]@if(x == 2){break}}
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q '\[1\].*\[2\]' public/index.html
! grep -q '\[3\]' public/index.html
