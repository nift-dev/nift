#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}";T=$(mktemp -d);trap 'rm -rf "$T"' EXIT;cd "$T";"$NIFT_BIN" init >/dev/null
cat > content/index.html <<'EOT'
@fn(classify(x)) {
 if(x < 0) { return "neg" }
 if(x == 0) { return "zero" }
 return "pos"
}
RESULT=$[classify(0)]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'RESULT=zero' public/index.html
