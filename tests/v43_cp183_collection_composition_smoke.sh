#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"
td="$(mktemp -d)"; trap 'rm -rf "$td"' EXIT
cat > "$td/t.nift" <<'NIFT'
posts := [
 {"title":"Alpha","published":true,"score":8,"tags":["nift","cpp"]},
 {"title":"Beta","published":false,"score":3,"tags":["cpp"]},
 {"title":"Gamma","published":true,"score":9,"tags":["nift","web"]},
 {"title":"Alpha copy","published":true,"score":8,"tags":["nift","nift"]}
]
p := posts.partition(x => x.published)
print(p.matched.take(2).map(x => x.title).join(","))
print(p.unmatched.first().title)
print(posts.unique_by(x => x.score).map(x => x.title).join(","))
print(posts.max_by(x => x.score).title)
print(posts.count_by(x => x.published).stringify())
print(posts.group_by_each(x => x.tags).get("nift").map(x => x.title).join(","))
print(posts.drop(1).take(2).chunk(1).size())
NIFT
out="$($NIFT_BIN run "$td/t.nift")"
[[ "$out" == $'Alpha,Gamma\nBeta\nAlpha,Beta,Gamma\nGamma\n{"true":3,"false":1}\nAlpha,Gamma,Alpha copy\n2' ]]
echo 'CP183 collection composition smoke passed'
