#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"; T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T"; "$NIFT_BIN" init >/dev/null
cat > content/index.html <<'EOT'
$[big := 9223372036854775806]$[big2 := big + 1]$[big2]
$[ii := 5]$[post := ii++]$[post],$[ii]
$[prex := ++ii]$[prex],$[ii]
$[ii += 3]$[ii -= 1]$[ii *= 2]$[ii /= 3]$[ii]
$[arr := [1,2,[3]]]$[alias := arr]$[cloned := copy(arr)]$[same(arr,alias)],$[same(arr,cloned)],$[arr == cloned]
$[arr.push(4)]$[arr.insert(1,9)]$[arr.size()],$[arr.first()],$[arr.last()],$[arr.indexOf([3])],$[arr.contains([3])]
$[removed := arr.remove(1)]$[removed],$[arr.size()]$[popped := arr.pop()]$[popped],$[arr.size()]
EOT
"$NIFT_BIN" build >/dev/null
body=$(sed -n '/<body>/,/<\/body>/p' public/index.html | sed '1d;$d' | sed 's/^[[:space:]]*//')
[[ "$body" == *"9223372036854775807"* ]]
[[ "$body" == *"5,6"* ]]
[[ "$body" == *"7,7"* ]]
[[ "$body" == *"6"* ]]
[[ "$body" == *"true,false,true"* ]]
[[ "$body" == *"5,1,4,3,true"* ]]
[[ "$body" == *"9,4"* ]]
[[ "$body" == *"4,3"* ]]

# Overflow is deterministic.
cat > content/index.html <<'EOT'
$[xv := 9223372036854775807]$[yv := xv + 1]
EOT
if "$NIFT_BIN" build >/dev/null 2>err; then echo 'expected int64 overflow failure' >&2; exit 1; fi
grep -q 'signed 64-bit integer overflow' err
