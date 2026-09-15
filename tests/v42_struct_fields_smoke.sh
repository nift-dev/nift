#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT; cd "$t"; "$OLDPWD/$NIFT" init >/dev/null
cat > content/index.html <<'EOT'
@struct(point) { x := 0; y := 0 }
$[p := point()]$[q := p]$[q.x = 7]$[p.x]
EOT
"$OLDPWD/$NIFT" build >/dev/null
grep -q '7' public/index.html
cat > content/index.html <<'EOT'
@struct(point) { x := 0 }
$[p := point()]$[p.z = 2]
EOT
! "$OLDPWD/$NIFT" build >/dev/null 2>&1

# Unknown field reads must fail too, not render literally.
cat > content/index.html <<'EOT'
@struct(point) { x := 0 }
$[p := point()]$[p.z]
EOT
! "$OLDPWD/$NIFT" build >/dev/null 2>&1
