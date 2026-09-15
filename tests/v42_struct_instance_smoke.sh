#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT; cd "$t"; "$OLDPWD/$NIFT" init >/dev/null
cat > content/index.html <<'EOT'
@struct(point) { x := 0; y := 0 }
$[p := point()]$[q := p]ok
EOT
"$OLDPWD/$NIFT" build >/dev/null
grep -q 'ok' public/index.html
