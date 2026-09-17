#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cat >"$tmp/wall.nft" <<'NIFT'
$o := json_parse("{\"z\":[],\"u\":\"λ\",\"a\":{\"x\":1}}")
$o.keys().stringify()
$o.get("a").stringify()
$o.merge(json_parse("{\"n\":null}")).stringify()
$a := [3,1,3,2]
$a.unique().sum()
NIFT
"$NIFT" run "$tmp/wall.nft" >/dev/null
