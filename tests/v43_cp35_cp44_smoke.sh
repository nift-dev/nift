#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"; T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T"; "$NIFT_BIN" init >/dev/null
cat > content/index.html <<'EOT'
@fn(pair(start)) {
 n := start
 xs := []
 xs.push(() => ++n)
 xs.push(() => n)
 return xs
}
$[p := pair(5)]$[inc := p[0]]$[get := p[1]]$[inc()],$[get()]
$[a := [1,2,3,4]]
$[d := a.map(x => x * 2)]@for(x : d){$[x]}
$[e := a.filter(x => x % 2 == 0)]@for(x : e){$[x]}
$[sum := a.reduce((acc,x) => acc + x, 0)]$[sum]
$[a.any(x => x == 3)],$[a.all(x => x > 0)],$[a.find(x => x > 2)],$[a.count(x => x % 2 == 0)]
$[u := [3,1,2,2]]$[u.sort()]@for(x : u){$[x]}
$[v := [3,1,2]]$[v.sort((x,y) => x > y)]@for(x : v){$[x]}
$[m := map()]$[m.set("a",1)]$[m.set("b",2)]$[mv := m.map((k,v) => v * 10)]@for(x : mv){$[x]}
$[s := set()]$[s.add(1)]$[s.add(2)]$[sf := s.filter(x => x > 1)]@for(x : sf){$[x]}
EOT
"$NIFT_BIN" build >/dev/null
body=$(sed -n '/<body>/,/<\/body>/p' public/index.html | sed '1d;$d' | tr -d '\t\r\n ')
[[ "$body" == *"6,6"* ]]
[[ "$body" == *"2468"* ]]
[[ "$body" == *"24"* ]]
[[ "$body" == *"10"* ]]
[[ "$body" == *"true,true,3,2"* ]]
[[ "$body" == *"1223"* ]]
[[ "$body" == *"321"* ]]
[[ "$body" == *"1020"* ]]
[[ "$body" == *"2"* ]]
