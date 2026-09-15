#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT; cd "$t"; "$OLDPWD/$NIFT" init >/dev/null
cat > content/index.html <<'EOT'
@struct(box) {
 value := 1
 fn(set(value)) { this.value = value }
 fn(get()) { return this.value }
}
$[b := box()]$[b.set(9)]$[b.get()]
EOT
"$OLDPWD/$NIFT" build >/dev/null
grep -q '9' public/index.html
