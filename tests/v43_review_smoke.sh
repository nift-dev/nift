#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"; T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T"; "$NIFT_BIN" init >/dev/null
run() { printf '%s\n' "$1" > content/index.html; "$NIFT_BIN" build --all >/dev/null 2>err.txt || true; }
body() { tr -d '\n ' < public/index.html | sed 's/.*<body>//;s#</body>.*##'; }

# --- Numeric boundaries: signed 64-bit exact arithmetic, no silent rounding. ---
run '$[a := 9223372036854775806]$[a + 1]$[b := -9223372036854775808]$[b]'
o=$(body); [[ "$o" == *"9223372036854775807"* ]] && [[ "$o" == *"-9223372036854775808"* ]]

# int64 overflow is a deterministic build error.
run '$[x := 9223372036854775807]$[x + 1]'; ! "$NIFT_BIN" build --all >/dev/null 2>&1
run '$[x := 9223372036854775807]$[++x]'; ! "$NIFT_BIN" build --all >/dev/null 2>&1
run '$[x := 9223372036854775806]$[x *= 2]'; ! "$NIFT_BIN" build --all >/dev/null 2>&1

# A double at 2^63 must NOT be cast to int64 (was UB): it computes in double.
run '$[x := 9223372036854775808.0]$[x + 1]'; o=$(body); [[ "$o" == *"9.223372036854776e+18"* ]]

# 2^53 boundary keeps exact int64 spelling through compound assignment.
run '$[b := 9007199254740992]$[b += 1]$[b]'; o=$(body); [[ "$o" == *"9007199254740993"* ]]

# --- Operators: prefix returns new, postfix returns old. ---
run '$[i := 5]$[a := ++i]$[b := i--]$[a],$[b],$[i]'; o=$(body); [[ "$o" == *"6,6,5"* ]]
# Prefix/postfix at top level are mutations and render no text; the value changes.
run '$[i := 5]$[++i]$[i]$[i--]$[i]'; o=$(body); [[ "$o" == *"65"* ]]

# --- Arrays: structural equality, alias vs copy identity, helpers. ---
run '$[a := [1,[2,3]]]$[c := copy(a)]$[d := deepcopy(a)]$[a == c],$[same(a,c)],$[same(a,d)]'
o=$(body); [[ "$o" == *"true,false,false"* ]]
run '$[a := []]$[a.pop()]'; ! "$NIFT_BIN" build --all >/dev/null 2>&1
run '$[a := [1,2,3]]$[a.indexOf(9)],$[a.contains(9)]'; o=$(body); [[ "$o" == *"-1,false"* ]]

# --- Collections: ordering and logical vs identity equality. ---
# stack @for is top-first (LIFO); queue is FIFO; prique is ascending; map is insertion order.
run '$[s := stack()]$[s.push(1)]$[s.push(2)]$[s.push(3)]@for(x : s){$[x]}|'
[[ "$(body)" == *"321|"* ]]
run '$[q := queue()]$[q.push(1)]$[q.push(2)]$[q.push(3)]@for(x : q){$[x]}|'
[[ "$(body)" == *"123|"* ]]
run '$[p := prique()]$[p.push(3)]$[p.push(1)]$[p.push(2)]@for(x : p){$[x]}|'
[[ "$(body)" == *"123|"* ]]
# map/set logical equality ignores insertion history; struct/callable/prique are identity.
run '$[m1 := map()]$[m1.set("a",1)]$[m1.set("b",2)]$[m2 := map()]$[m2.set("b",2)]$[m2.set("a",1)]$[m1 == m2]'
o=$(body); [[ "$o" == *"true"* ]]
run '$[s1 := set()]$[s1.add(1)]$[s1.add(2)]$[s2 := set()]$[s2.add(2)]$[s2.add(1)]$[s1 == s2]'
o=$(body); [[ "$o" == *"true"* ]]
# map int/float key equivalence (1 == 1.0).
run '$[m := map()]$[m.set(1,"a")]$[m.contains(1.0)],$[m.get(1.0)]'; o=$(body); [[ "$o" == *"true,a"* ]]

# --- deepcopy must clone collections (was returning the identical instance). ---
run '$[s := set()]$[s.add(1)]$[d := deepcopy(s)]$[d.add(2)]$[s.size()],$[d.size()],$[same(s,d)]'
o=$(body); [[ "$o" == *"1,2,false"* ]]
run '$[m := map()]$[m.set("a",1)]$[d := deepcopy(m)]$[d.set("b",2)]$[m.size()],$[d.size()]'
o=$(body); [[ "$o" == *"1,2"* ]]

# --- Rendering an internal callable/collection reference must fail, not leak. ---
run '$[m := map()]$[m.set("a",1)]$[m]'; ! "$NIFT_BIN" build --all >/dev/null 2>&1
run '$[f := x => x]$[f]'; ! "$NIFT_BIN" build --all >/dev/null 2>&1
run '@fn(named(x)){ return x }$[named]'; ! "$NIFT_BIN" build --all >/dev/null 2>&1

# --- Closures: binding-not-snapshot capture, shared state, escaping. ---
run '$[n := 0]$[a := () => ++n]$[b := () => ++n]$[a()],$[a()],$[b()],$[n]'
o=$(body); [[ "$o" == *"1,2,3,3"* ]]
cat > content/index.html <<'EOT'
@fn(counter(start)) {
    n := start
    return () => n++
}
$[c := counter(10)]$[c()],$[c()],$[c()]
EOT
"$NIFT_BIN" build --all >/dev/null
o=$(body); [[ "$o" == *"10,11,12"* ]]

# --- Lambda body failure must be a build error, not silent passthrough. ---
run '$[f := () => undefined_var]$[f()]'; ! "$NIFT_BIN" build --all >/dev/null 2>&1

# --- Higher-order operations. ---
run '$[a := [1,2,3,4]]$[a.reduce((acc,x) => acc + x, 0)]$[a.any(x => x == 4)],$[a.all(x => x > 0)],$[a.find(x => x > 2)],$[a.count(x => x % 2 == 0)]'
o=$(body); [[ "$o" == *"10"* ]] && [[ "$o" == *"true,true,3,2"* ]]
# reduce on empty uses the initial accumulator; find no-match is null; count no-match is 0.
run '$[a := []]$[a.reduce((acc,x) => acc + x, 5)],$[a.find(x => x > 0)],$[a.count(x => x > 0)]'
o=$(body); [[ "$o" == *"5,null,0"* ]]
# callback error propagates.
run '$[a := [1,2,3]]$[a.map(x => 1 / 0)]'; ! "$NIFT_BIN" build --all >/dev/null 2>&1
# stable sort default ascending and with a boolean less-than comparator.
run '$[u := [3,1,2,1]]$[u.sort()]@for(x : u){$[x]}|'
[[ "$(body)" == *"1123|"* ]]
run '$[v := [3,1,2]]$[v.sort((x,y) => x > y)]@for(x : v){$[x]}|'
[[ "$(body)" == *"321|"* ]]

# --- @for over a map uses object tuple syntax; numeric map keys are not @for-iterable. ---
run '$[m := map()]$[m.set("a",1)]$[m.set("b",2)]@for((k,v) : m){$[k]=$[v]}|'
[[ "$(body)" == *"a=1b=2|"* ]]
run '$[m := map()]$[m.set(1,"a")]@for((k,v) : m){$[k]}$[m.get(1)]'
! "$NIFT_BIN" build --all >/dev/null 2>&1

# --- Struct self-reference cycles are currently permitted (contract gap, no crash). ---
cat > content/index.html <<'EOT'
@struct(other) { x := 0 }
@struct(node) { next := other() }
$[a := node()]$[a.next = a]$[a.next == a]
EOT
"$NIFT_BIN" build --all >/dev/null
o=$(body); [[ "$o" == *"true"* ]]
