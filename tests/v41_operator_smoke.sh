#!/usr/bin/env bash
set -euo pipefail
N=${NIFT_BIN:-./nift}; R=$(mktemp -d); trap 'rm -rf "$R"' EXIT; cd "$R"; "$N" init >/dev/null; echo '@content' > templates/template.html
cat > content/index.html <<'E'
$[a := 1]$[b := 2]$[a = b = 3]
@if(a == 3 && b >= 3){ok}
E
"$N" build --all >/dev/null; grep -q ok public/index.html
