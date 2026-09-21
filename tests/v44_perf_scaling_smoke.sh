#!/usr/bin/env bash
# Performance regression guards for the catastrophic-complexity defects found by
# the scripting benchmark campaign:
#   1. array push: deep-copied the whole array per call (O(n^2)) -> now linear
#   2. set add/membership: linear scan per call (O(n^2)) -> now indexed
# The guard detects SCALING regression, not brittle absolute times: doubling the
# input must not quadruple the runtime. The n=40k case finishes in ~0.7s when
# linear but took ~11s when quadratic; the ratio check uses a generous bound so
# slow CI machines do not flake.
set -euo pipefail
NIFT="${NIFT:-./nift}"
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT

measure() { # $1 = .f source path
  local start end
  start=$(date +%s%N)
  "$NIFT_ABS" run "$1" >/dev/null
  end=$(( $(date +%s%N) - start ))
  echo "$(( end / 1000000 ))"
}

cat >"$t/push40.f" <<'F'
a := []
i := 0
while(i < 40000) { a.push("x"); i += 1 }
print(a.size())
F
cat >"$t/push80.f" <<'F'
a := []
i := 0
while(i < 80000) { a.push("x"); i += 1 }
print(a.size())
F
cat >"$t/set40.f" <<'F'
s := set()
i := 0
while(i < 40000) { s.add(i); i += 1 }
print(s.size())
F
cat >"$t/set80.f" <<'F'
s := set()
i := 0
while(i < 80000) { s.add(i); i += 1 }
print(s.size())
F
cat >"$t/map40.f" <<'F'
m := map()
i := 0
while(i < 40000) { m.set(i, i); i += 1 }
print(m.size())
F
cat >"$t/map80.f" <<'F'
m := map()
i := 0
while(i < 80000) { m.set(i, i); i += 1 }
print(m.size())
F

p40=$("$NIFT_ABS" run "$t/push40.f"); [ "$p40" = "40000" ] || { echo "push40 wrong: $p40" >&2; exit 1; }
p80=$("$NIFT_ABS" run "$t/push80.f"); [ "$p80" = "80000" ] || { echo "push80 wrong: $p80" >&2; exit 1; }
s40=$("$NIFT_ABS" run "$t/set40.f"); [ "$s40" = "40000" ] || { echo "set40 wrong: $s40" >&2; exit 1; }
s80=$("$NIFT_ABS" run "$t/set80.f"); [ "$s80" = "80000" ] || { echo "set80 wrong: $s80" >&2; exit 1; }
m40=$("$NIFT_ABS" run "$t/map40.f"); [ "$m40" = "40000" ] || { echo "map40 wrong: $m40" >&2; exit 1; }
m80=$("$NIFT_ABS" run "$t/map80.f"); [ "$m80" = "80000" ] || { echo "map80 wrong: $m80" >&2; exit 1; }

push_lo=$(measure "$t/push40.f")
push_hi=$(measure "$t/push80.f")
set_lo=$(measure "$t/set40.f")
set_hi=$(measure "$t/set80.f")
map_lo=$(measure "$t/map40.f")
map_hi=$(measure "$t/map80.f")
cat > "$t/str160.f" <<'F'
s := ""
i := 1
while(i <= 160000) { s += "abc"; i += 1 }
print(s.length())
F
cat > "$t/str320.f" <<'F'
s := ""
i := 1
while(i <= 320000) { s += "abc"; i += 1 }
print(s.length())
F
str_lo=$(measure "$t/str160.f")
str_hi=$(measure "$t/str320.f")

echo "push 40k=${push_lo}ms 80k=${push_hi}ms | set 40k=${set_lo}ms 80k=${set_hi}ms | map 40k=${map_lo}ms 80k=${map_hi}ms | str += 160k=${str_lo}ms 320k=${str_hi}ms"

ratio_ok() { # $1 lo, $2 hi; doubling must be under 3.5x (linear ~2x, O(n^2) ~4x)
  local lo=$1 hi=$2
  [ "$lo" -lt 1 ] && lo=1000
  [ "$hi" -le $(( (lo * 35) / 10 )) ]
}

if ! ratio_ok "$push_lo" "$push_hi"; then echo "FAIL: array push scaling regression (40k=${push_lo}ms 80k=${push_hi}ms)" >&2; exit 1; fi
if ! ratio_ok "$set_lo" "$set_hi"; then echo "FAIL: set add scaling regression (40k=${set_lo}ms 80k=${set_hi}ms)" >&2; exit 1; fi
if ! ratio_ok "$map_lo" "$map_hi"; then echo "FAIL: map set scaling regression (40k=${map_lo}ms 80k=${map_hi}ms)" >&2; exit 1; fi
if ! ratio_ok "$str_lo" "$str_hi"; then echo "FAIL: string += scaling regression (160k=${str_lo}ms 320k=${str_hi}ms)" >&2; exit 1; fi

echo "PASS performance scaling guards (array push + set add + map set + string += remain linear)"