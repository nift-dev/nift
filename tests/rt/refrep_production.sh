#!/usr/bin/env bash
# B2 (b2-refrep.md, owner ruling 2): the reference probe is a compile-time
# test configuration only.
#   refrep_production.sh <nift> <shared-lib> <probe-test-binary>
# 1. Only the refrep test rules of the Makefile define NIFT_RT_REFERENCE_PROBE
#    (no production CPPFLAGS/CXXFLAGS, no other rule).
# 2. Neither the CLI nor the embedded library contains the probe build's
#    marker symbol: probe code is not compiled into any production artifact,
#    so no script, environment variable, argument or embedded-API call can
#    switch it on (there is no runtime switch at all: rt/reference_probe.h).
# 3. Control: the probe test binary does contain the marker.
# 4. Behaviour: the production CLI keeps the comparator sort refusal the probe
#    lifts (method:sort, before anything runs) on a toolchain outside the
#    admitted libstdc++ profile, and the Phase A guard refusal the probe skips.
set -u
nift=$1; lib=$2; probe=$3
fail=0
rules=$(grep -n -- '-DNIFT_RT_REFERENCE_PROBE' Makefile | grep -v '^[0-9]*:#')
count=$(printf '%s\n' "$rules" | grep -c .)
if [ "$count" -ne 2 ] || printf '%s\n' "$rules" | grep -qv 'RT_REFREP_DIR\|\$(CXX) \$(CPPFLAGS)'; then
  echo "FAIL: unexpected NIFT_RT_REFERENCE_PROBE build rules:"; printf '%s\n' "$rules"; fail=1; fi
if grep -nE '^(CPPFLAGS|CXXFLAGS|CFLAGS)\b.*NIFT_RT_REFERENCE_PROBE' Makefile; then
  echo "FAIL: production flags define the probe"; fail=1; fi
for artifact in "$nift" "$lib"; do
  if nm -D "$artifact" 2>/dev/null | grep -q nift_rt_reference_probe_build ||
     nm "$artifact" 2>/dev/null | grep -q nift_rt_reference_probe_build ||
     grep -q 'B2 reference probe (test configuration)' "$artifact"; then
    echo "FAIL: $artifact contains the reference probe"; fail=1; fi
done
if ! nm "$probe" | grep -q nift_rt_reference_probe_build; then
  echo "FAIL: control: the probe test binary lacks its marker"; fail=1; fi
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
printf 'n := [3,1,2]\nn.sort((x,y)=>x<y)\nprint(n)\n' > "$tmp/sort.f"
printf 'fn(g(a)) { a.push(1); return a.length() }\nb := [5]\nprint(g(b))\n' > "$tmp/guard.f"
profile=$(printf '#include <cstddef>\n__GLIBCXX__\n' | ${CXX:-g++} -E -x c++ - | tail -1)
out=$(cd "$tmp" && NIFT_VM=require "$nift" sort.f 2>&1); rc=$?
if [ "$profile" != 20260321 ]; then
  if [ $rc -eq 0 ] || ! printf '%s' "$out" | grep -q '(method:sort)' || printf '%s' "$out" | grep -q '^\['; then
    echo "FAIL: production comparator sort not refused (method:sort): rc=$rc $out"; fail=1
  else echo "PASS production keeps the comparator sort refusal (method:sort; libstdc++ $profile)"; fi
else echo "SKIP comparator refusal: admitted libstdc++ profile $profile (the production VM runs it)"; fi
out=$(cd "$tmp" && NIFT_VM=require "$nift" guard.f 2>&1); rc=$?
if [ $rc -eq 0 ] || ! printf '%s' "$out" | grep -q 'reference-guard'; then
  echo "FAIL: production reference guard not applied: rc=$rc $out"; fail=1
else echo "PASS production keeps the reference guard ($(printf '%s' "$out" | grep -o '(reference-guard[^)]*)' | head -1))"; fi
# Review L5: the probe also lifts the legacy location-alias refusal (compiler_rules.cpp
# no_alias), which the marker symbol (compiler.cpp) does not witness: production must
# still refuse an object alias with exactly `aliasing`.
printf 'o := {"k": 1}\nc := o\nprint(c.k)\n' > "$tmp/alias.f"
out=$(cd "$tmp" && NIFT_VM=require "$nift" alias.f 2>&1); rc=$?
if [ $rc -eq 0 ] || ! printf '%s' "$out" | grep -q '(aliasing)'; then
  echo "FAIL: production location-alias refusal not applied: rc=$rc $out"; fail=1
else echo "PASS production keeps the location-alias refusal (aliasing)"; fi
[ $fail -eq 0 ] && echo "PASS reference probe: test configuration only (absent from $(basename "$nift") and $(basename "$lib"))"
exit $fail
