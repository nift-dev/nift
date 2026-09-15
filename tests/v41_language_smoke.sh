#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-./nift}; R=$(mktemp -d); trap 'rm -rf "$R"' EXIT; cd "$R"; "$NIFT" init >/dev/null; printf '@content\n' > templates/template.html
cat > data.expr <<'E'
{"n": 42, "ok": true}
E
cat > content/index.html <<'E'
$[immut frozen := {"x":1}]
@:=(items){[1,2,3]}
$[schema := {"type":"object","required":["n"],"properties":{"n":{"type":"number"}}}]
$[j := validate(schema, inject("data.expr"))]
@fn(add(a,b)){@return(a+b)}
@fragment(card(x)){<b>$[x]</b>}
n=$[j.n] sum=$[add(2,3)] frag=$[card("ok")]
E
"$NIFT" build --all >/dev/null
grep -q 'n=42 sum=5 frag=<b>ok</b>' public/index.html
cat > content/index.html <<'E'
$[immut x := {"a":1}]$[x = {"a":2}]
E
if "$NIFT" build --all >/dev/null 2>err; then exit 1; fi
grep -q 'cannot assign to const binding' err
echo 'Nift v4.1 language smoke passed'
