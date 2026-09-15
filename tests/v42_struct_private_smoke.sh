#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; root=$PWD; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT; cd "$t"; "$root/$NIFT" init >/dev/null
cat > content/index.html <<'EOT'
@struct(vault) { private secret := 7; fn(read()) { return secret } }
$[v := vault()]$[v.read()]
EOT
"$root/$NIFT" build >/dev/null; grep -q 7 public/index.html
cat > content/index.html <<'EOT'
@struct(vault) { private secret := 7 }
$[v := vault()]$[v.secret]
EOT
! "$root/$NIFT" build >/dev/null 2>&1
