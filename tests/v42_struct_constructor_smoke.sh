#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT; cd "$t"; "$OLDPWD/$NIFT" init >/dev/null
cat > content/index.html <<'EOT'
@struct(point) {
 x := 0
 y := 0
 fn(point(x_, y_)) { x = x_; y = y_ }
}
$[p := point(3, 4)]$[p.x],$[p.y]
EOT
"$OLDPWD/$NIFT" build >/dev/null
grep -q '3,4' public/index.html
