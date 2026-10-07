#!/usr/bin/env bash
# Scalar map/set lookup scaling guard (v4.8). Successful scalar-key operations
# must be approximately independent of N after the index lands; a regression to
# linear location makes the 100k sample ~100x slower than the 1k sample (the
# pre-fix contains-before-set build was ~222s at 100k, sub-second at 1k). Uses
# generous RATIO bounds (not fragile absolute millisecond limits) so normal
# per-host evaluator/JIT noise does not flake.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT

measure(){ # $1 = N  $2 = K  $3 = body(inner statement count)  $4 = build-N  ; prints phase ms
  local n="$1" k="$2" bn="$4"
  cat > "$t/p.f" <<EOF
m := map()
i := 0
while(i < $bn) { m.set("key" + i, i); i += 1 }
t0 := epoch()
j := 0
while(j < $k) { $3 ; j += 1 }
t1 := epoch()
print(t1 - t0)
EOF
  "$B" "$t/p.f"
}

get_1k=$(measure 1000 2000 'v := m.get("key" + 500)' 1000)
get_100k=$(measure 100000 2000 'v := m.get("key" + 50000)' 100000)
cat > "$t/p.f" <<'EOF'
m := map()
i := 0
while(i < 100000) { m.set("key" + i, i); i += 1 }
t0 := epoch()
j := 0
while(j < 2000) { if(m.contains("key" + 50000)) { }; j += 1 }
t1 := epoch()
print(t1 - t0)
EOF
con_100k=$("$B" "$t/p.f")
cat > "$t/p.f" <<'EOF'
m := map()
i := 0
while(i < 1000) { m.set("key" + i, i); i += 1 }
t0 := epoch()
j := 0
while(j < 2000) { if(m.contains("key" + 500)) { }; j += 1 }
t1 := epoch()
print(t1 - t0)
EOF
con_1k=$("$B" "$t/p.f")
cat > "$t/p.f" <<EOF
m := map()
i := 0
while(i < 100000) { m.set("key" + i, i); i += 1 }
t0 := epoch()
j := 0
while(j < 2000) { m.set("key" + 50000, j); j += 1 }
t1 := epoch()
print(t1 - t0)
EOF
rep_100k=$("$B" "$t/p.f")
cat > "$t/p.f" <<EOF
m := map()
i := 0
while(i < 1000) { m.set("key" + i, i); i += 1 }
t0 := epoch()
j := 0
while(j < 2000) { m.set("key" + 500, j); j += 1 }
t1 := epoch()
print(t1 - t0)
EOF
rep_1k=$("$B" "$t/p.f")
cat > "$t/p.f" <<'EOF'
s := set()
i := 0
while(i < 100000) { s.add("key" + i); i += 1 }
t0 := epoch()
j := 0
while(j < 2000) { if(s.contains("key" + 50000)) { }; j += 1 }
t1 := epoch()
print(t1 - t0)
EOF
setcon_100k=$("$B" "$t/p.f")
cat > "$t/p.f" <<'EOF'
s := set()
i := 0
while(i < 1000) { s.add("key" + i); i += 1 }
t0 := epoch()
j := 0
while(j < 2000) { if(s.contains("key" + 500)) { }; j += 1 }
t1 := epoch()
print(t1 - t0)
EOF
setcon_1k=$("$B" "$t/p.f")

build_100k(){ cat > "$t/p.f" <<'EOF'
m := map()
t0 := epoch()
i := 0
while(i < 100000) { if(!m.contains("key" + i)) { m.set("key" + i, i) } ; i += 1 }
t1 := epoch()
print(t1 - t0)
EOF
  "$B" "$t/p.f"; }
build_1k(){ cat > "$t/p.f" <<'EOF'
m := map()
t0 := epoch()
i := 0
while(i < 1000) { if(!m.contains("key" + i)) { m.set("key" + i, i) } ; i += 1 }
t1 := epoch()
print(t1 - t0)
EOF
  "$B" "$t/p.f"; }
cb_100k=$(build_100k); cb_1k=$(build_1k)

failed=0
# Successful lookups/replacement: 100k must not be ~100x the 1k time (linear).
for name in "$get_1k:$get_100k:get" "$con_1k:$con_100k:contains" "$setcon_1k:$setcon_100k:set-contains" "$rep_1k:$rep_100k:replace"; do
  IFS=: read -r t1 t100 label <<<"$name"
  bound=$(( 25 * t1 + 200 ))
  if [ "$t100" -gt "$bound" ]; then
    echo "FAIL $label scaling: 1k=${t1}ms 100k=${t100}ms bound=${bound}ms" >&2; failed=1
  fi
done
# contains-before-set construction: linear (size-ratio bound), not quadratic.
cb_bound=$(( 30 * (100000 / 1000) * cb_1k + 1000 ))
if [ "$cb_100k" -gt "$cb_bound" ]; then
  echo "FAIL contains-before-set scaling: 1k=${cb_1k}ms 100k=${cb_100k}ms bound=${cb_bound}ms" >&2; failed=1
fi
[ "$failed" -eq 0 ] || exit 1
echo "PASS v4.8 map/set scaling guard (get ${get_1k}->${get_100k}ms, contains ${con_1k}->${con_100k}ms, set.contains ${setcon_1k}->${setcon_100k}ms, replace ${rep_1k}->${rep_100k}ms, build ${cb_1k}->${cb_100k}ms)"