#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat >"$t/test.f" <<'F'
$[prefix := "P"]
$[posts := [{"title":"A","show":true},{"title":"B","show":false}]]
$[html := posts.map(p => {
<article>@if(p.show){<b>$[prefix]-$[p.title]</b>}</article>
}).join("")]
print(html.contains("P-A"))
print(html.contains("P-B"))
$[empty := [].map(x => {<i>$[x]</i>}).join("")]
print(empty.empty())
$[normal := (x) => { return x + 1 }]
print(normal(2))
F
out=$($NIFT run "$t/test.f")
[ "$out" = "true
false
true
3" ] || { printf '%s\n' "$out" >&2; exit 1; }
echo 'PASS v4.4 CP9 map-to-markup'
