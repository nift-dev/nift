#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"; T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T"; "$NIFT_BIN" init >/dev/null
cat > content/index.html <<'EOT'
$[a := []]$[a.push(1)]$[a.push([2,3])]$[a.contains([2,3])],$[a.indexOf([2,3])]
$[s := stack()]$[s.push(1)]$[s.push(2)]$[s.top()],$[s.size()]
$[q := queue()]$[q.push("a")]$[q.push("b")]$[q.front()],$[q.size()]
$[p := prique()]$[p.push(3)]$[p.push(1)]$[p.push(2)]$[pc := copy(p)]$[p.front()],$[same(p,p)],$[same(p,pc)]
$[m := map()]$[m.set("b",2)]$[m.set("a",1)]$[m.set("b",3)]$[m.get("b")],$[m.size()]
$[sm := sorted_map()]$[sm.set("b",2)]$[sm.set("a",1)]
$[st := set()]$[st.add(2)]$[st.add(2)]$[st.add(1)]$[st.size()],$[st.contains(2)]
$[ss := sorted_set()]$[ss.add(3)]$[ss.add(1)]$[ss.add(2)]
@for(x : s){$[x]}|@for(x : q){$[x]}|@for(x : ss){$[x]}
@fn(double(x)) { return x * 2 }
$[named := double]$[named(6)]
$[lam := x => x * 3]$[lam(4)]
$[block := x => { return x + 5 }]$[block(7)]
$[factor := 10]$[mul := x => x * factor]$[factor = 20]$[mul(2)]
$[count := 0]$[inc := () => ++count]$[inc()],$[inc()],$[count]
@fn(counter(start)) {
    n := start
    return () => n++
}
$[next := counter(10)]$[next()],$[next()],$[next()]
EOT
"$NIFT_BIN" build >/dev/null
body=$(sed -n '/<body>/,/<\/body>/p' public/index.html | sed '1d;$d' | tr -d '\t\r')
[[ "$body" == *"true,1"* ]]
[[ "$body" == *"2,2"* ]]
[[ "$body" == *"a,2"* ]]
[[ "$body" == *"1,true,false"* ]]
[[ "$body" == *"3,2"* ]]
[[ "$body" == *"2,true"* ]]
[[ "$body" == *"21|ab|123"* ]]
[[ "$body" == *"12"* ]]
[[ "$body" == *"40"* ]]
[[ "$body" == *"1,2,2"* ]]
[[ "$body" == *"10,11,12"* ]]

# Large-array/collection sanity gate.
{
  echo '$[a := []]'
  for i in $(seq 1 1000); do echo '$[a.push('"$i"')]'; done
  echo '$[a.size()],$[a.first()],$[a.last()]'
} > content/index.html
"$NIFT_BIN" build >/dev/null
grep -q '1000,1,1000' public/index.html
