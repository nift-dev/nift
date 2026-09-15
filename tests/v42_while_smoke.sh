#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cd "$T"; "$NIFT_BIN" init >/dev/null
cat > content/index.html <<'EOT'
$[i := 0]@while(i < 3){[$[i]]$[i = i + 1]}
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q '\[0\].*\[1\].*\[2\]' public/index.html
