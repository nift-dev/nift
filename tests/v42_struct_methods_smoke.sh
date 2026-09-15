#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT; cd "$t"; "$OLDPWD/$NIFT" init >/dev/null
cat > content/index.html <<'EOT'
@struct(counter) {
 count := 0
 fn(add(n)) { count = count + n }
 fn(value()) { return count }
}
$[c := counter()]$[c.add(3)]$[c.add(4)]$[c.value()]
EOT
"$OLDPWD/$NIFT" build >/dev/null
grep -q '7' public/index.html
