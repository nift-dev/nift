#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}";T=$(mktemp -d);trap 'rm -rf "$T"' EXIT;cd "$T";"$NIFT_BIN" init >/dev/null
cat > content/index.html <<'EOT'
@fragment(card(x)){A@if(x){return}B}
X=$[card(true)] Y=$[card(false)]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'X=A Y=AB' public/index.html
cat > content/index.html <<'EOT'
@fragment(bad()){return 5}
$[bad()]
EOT
if "$NIFT_BIN" build --all >/dev/null 2>err; then exit 1; fi
grep -q 'fragment return cannot have a value' err
