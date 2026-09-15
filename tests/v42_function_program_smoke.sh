#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"; T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T"; "$NIFT_BIN" init >/dev/null
cat > content/index.html <<'EOT'
@fn(foo(x)) {
  y := x + 1;
  y = y + 1
  @return(y)
}
$[foo(3)]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q '5' public/index.html
