#!/usr/bin/env bash
# Tooling checkpoint (component 1): evidence for build-object reuse.
#   check-build-reuse.sh OUTDIR
# Run from the repository root after `make nift`. Checks, each logged under
# OUTDIR and summarised as PASS/FAIL lines on stdout (exit 1 on any FAIL):
#   deps      touching a header rebuilds exactly the objects whose compiler
#             dependency file names it (and relinks the binaries), nothing else
#   equiv     clean and incremental builds give byte-identical objects and
#             binaries (default and one define configuration)
#   negative  a changed flag or define rebuilds the object (no stale reuse),
#             and restoring the flags restores the identical object
#   separate  a define configuration's object differs from the default one
#   parallel  three concurrent makes in one tree, sharing objects: every object
#             and binary identical to the serial build, no temporary left over
# Builds only; it runs no test binary (the coordinator's Linode run does).
set -u
out=${1:?usage: check-build-reuse.sh OUTDIR}
mkdir -p "$out"
fail=0
pass() { echo "PASS build-reuse $1"; }
bad() { echo "FAIL build-reuse $1"; fail=1; }
M() { make "$@" >>"$out/make.log" 2>&1; }

DEFAULT_BINS=".build/rt/value-unit .build/rt/atom-budget .build/rt/native-budget"
CFG=no-cycle-check
CFG_BIN=.build/rt/value-cycle-bench-off
objs_default() { make -s -f Makefile -f - print-objs <<'EOF' 2>/dev/null
print-objs: ; @echo $(RT_UNIT_OBJECTS)
EOF
}
objs_cfg() { make -s -f Makefile -f - print-objs <<EOF 2>/dev/null
print-objs: ; @echo \$(RT_OBJECTS_$CFG)
EOF
}
DOBJ=$(objs_default); COBJ=$(objs_cfg)
[ -n "$DOBJ" ] && [ -n "$COBJ" ] || { bad "setup: cannot list the object sets"; exit 1; }
manifest() { sha256sum $DOBJ $COBJ $DEFAULT_BINS $CFG_BIN 2>/dev/null | sort -k2; }

# --- clean build (only the configuration directory and the binaries; the
# default objects are `make nift`'s, rebuilt clean here too) ---
rm -f $DOBJ ${DOBJ//.o/.d} $DEFAULT_BINS $CFG_BIN
rm -rf ".build/rt-obj/$CFG"
start=$(date +%s)
M -j2 $DEFAULT_BINS $CFG_BIN || bad "clean build failed (make.log)"
echo "clean build seconds: $(( $(date +%s) - start ))" > "$out/timing-clean.txt"
manifest > "$out/manifest-clean.txt"

# --- deps: touch one header; exactly the objects naming it rebuild ---
hdr=src/rt/value_cycle.h
sleep 1; touch "$hdr"
expect=$(grep -l "$hdr" $(for o in $DOBJ $COBJ; do echo "${o%.o}.d"; done) | sed 's/\.d$/.o/' | sort)
before=$(stat -c '%n %Y' $DOBJ $COBJ | sort)
M -j2 $DEFAULT_BINS $CFG_BIN || bad "incremental build failed"
after=$(stat -c '%n %Y' $DOBJ $COBJ | sort)
rebuilt=$(diff <(echo "$before") <(echo "$after") | sed -n 's/^> \(\S*\) .*/\1/p' | sort)
printf '%s\n' "$expect" > "$out/deps-expected.txt"; printf '%s\n' "$rebuilt" > "$out/deps-rebuilt.txt"
if [ -n "$expect" ] && [ "$expect" = "$rebuilt" ]; then
  pass "deps: touching $hdr rebuilt exactly the $(echo "$expect" | wc -l) objects whose .d names it"
else bad "deps: rebuilt set differs from the dependency files (deps-*.txt)"; fi
# a source file: only its own object
sleep 1; touch src/rt/aggregate.cpp
before=$(stat -c '%n %Y' $DOBJ $COBJ | sort)
M -j2 $DEFAULT_BINS $CFG_BIN || bad "incremental build failed"
after=$(stat -c '%n %Y' $DOBJ $COBJ | sort)
rebuilt=$(diff <(echo "$before") <(echo "$after") | sed -n 's/^> \(\S*\) .*/\1/p' | sort | tr '\n' ' ')
if [ "$rebuilt" = "src/rt/aggregate.o .build/rt-obj/$CFG/src/rt/aggregate.o " ] || \
   [ "$rebuilt" = ".build/rt-obj/$CFG/src/rt/aggregate.o src/rt/aggregate.o " ]; then
  pass "deps: touching src/rt/aggregate.cpp rebuilt only its object in each configuration"
else bad "deps: touching aggregate.cpp rebuilt: $rebuilt"; fi
# nothing to do afterwards (make -q cannot answer this: the signature files
# are FORCE targets that always run; they rewrite nothing when unchanged)
before=$(stat -c '%n %Y' $DOBJ $COBJ $DEFAULT_BINS $CFG_BIN | sort)
M -j2 $DEFAULT_BINS $CFG_BIN || bad "no-op build failed"
after=$(stat -c '%n %Y' $DOBJ $COBJ $DEFAULT_BINS $CFG_BIN | sort)
[ "$before" = "$after" ] && pass "deps: a further make rebuilds and relinks nothing" || \
  bad "deps: a no-op make rebuilt: $(diff <(echo "$before") <(echo "$after") | grep '^>' | head -3)"

# --- equiv: incremental result == clean result ---
manifest > "$out/manifest-incremental.txt"
if cmp -s "$out/manifest-clean.txt" "$out/manifest-incremental.txt"; then
  pass "equiv: $(wc -l < "$out/manifest-clean.txt") objects and binaries byte-identical (clean vs incremental)"
else bad "equiv: clean and incremental differ (manifest-*.txt)"; fi

# --- negative control: a changed flag/define is never served by a stale object ---
# Rebuilt is shown by the object's mtime (a define the file never reads gives
# a byte-identical object, so a hash alone cannot show a rebuild); a flag that
# changes code generation (-O1) shows the stale object would differ.
mt() { stat -c %Y "$1"; }
o=src/rt/value.o; h0=$(sha256sum < $o); t0=$(mt $o); sleep 1
M "$o" CXXFLAGS="-std=c++17 -O1 -Wall -Wextra -pedantic -pthread" || bad "negative: build"
h1=$(sha256sum < $o); t1=$(mt $o)
if grep -q -- '-O1 ' .build/obj-config/default.flags && [ "$t1" != "$t0" ] && [ "$h1" != "$h0" ]; then
  pass "negative: CXXFLAGS -O2 -> -O1 rebuilds the default object (signature updated; the object differs)"
else bad "negative: a CXXFLAGS change did not rebuild $o"; fi
sleep 1
M "$o" CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread -DNIFT_TOOLING_NEGATIVE_CONTROL" || bad "negative: build"
t2=$(mt $o)
grep -q NIFT_TOOLING_NEGATIVE_CONTROL .build/obj-config/default.flags && [ "$t2" != "$t1" ] && \
  pass "negative: an added define rebuilds the default object (mtime; value.cpp does not read it)" || \
  bad "negative: an added define did not rebuild $o"
M "$o" || bad "negative: rebuild"
[ "$(sha256sum < $o)" = "$h0" ] && pass "negative: restoring the flags rebuilds the original object (byte-identical)" || \
  bad "negative: restored object differs"
# the configuration directory: vm_closure.cpp is the object NIFT_RT_NO_CYCLE_CHECK changes
co=.build/rt-obj/$CFG/src/rt/vm_closure.o; c0=$(sha256sum < $co); ct0=$(mt $co); sleep 1
M "$co" "RT_OBJFLAGS_$CFG=-std=c++17 -O2 -Wall -Wextra -pedantic -pthread" || bad "negative: cfg build"
c1=$(sha256sum < $co)
[ "$(mt $co)" != "$ct0" ] && [ "$c1" != "$c0" ] && [ "$c1" = "$(sha256sum < src/rt/vm_closure.o)" ] && \
  pass "negative: dropping -DNIFT_RT_NO_CYCLE_CHECK rebuilds the $CFG object (now equal to the default one)" || \
  bad "negative: $CFG define change did not rebuild"
M "$co" || bad "negative: cfg rebuild"
[ "$(sha256sum < $co)" = "$c0" ] && pass "negative: restored $CFG object byte-identical" || bad "negative: $CFG restore differs"
# the old Makefile (c1f43300) would have reused the stale object: shown for the record
if git show c1f43300:Makefile > "$out/old.mk" 2>/dev/null; then
  if make -q -f "$out/old.mk" "$o" CXXFLAGS="-std=c++17 -O1" 2>/dev/null; then
    echo "INFO build-reuse negative: c1f43300's Makefile considers $o up to date under different CXXFLAGS (the hazard the signature closes)"
  fi
fi
M -j2 $DEFAULT_BINS $CFG_BIN || bad "relink after the negative control"

# --- separate: configurations never mix ---
differ=$(for x in $COBJ; do y=${x#.build/rt-obj/$CFG/}; cmp -s "$x" "$y" || echo "$y"; done)
echo "$differ" > "$out/separate-differing.txt"
if [ -n "$differ" ] && ! cmp -s ".build/rt-obj/$CFG/src/rt/vm_closure.o" src/rt/vm_closure.o; then
  pass "separate: the $CFG objects are its own ($(echo "$differ" | wc -l) differ from the default set: $(echo $differ))"
else bad "separate: no $CFG object differs from the default set"; fi
manifest > "$out/manifest-after-negative.txt"
cmp -s "$out/manifest-clean.txt" "$out/manifest-after-negative.txt" && \
  pass "equiv: after the negative controls every object and binary is again byte-identical to the clean build" || \
  bad "equiv: tree differs after the negative controls"

# --- parallel: three concurrent makes sharing objects ---
for round in 1 2 3; do
  victims=$(echo $DOBJ | tr ' ' '\n' | awk -v r=$round 'NR % 9 == r')
  rm -f $victims $DEFAULT_BINS
  ( make .build/rt/value-unit >"$out/par-$round-a.log" 2>&1; echo $? > "$out/par-$round-a.rc" ) &
  pa=$!
  ( make .build/rt/atom-budget >"$out/par-$round-b.log" 2>&1; echo $? > "$out/par-$round-b.rc" ) &
  pb=$!
  ( make .build/rt/native-budget >"$out/par-$round-c.log" 2>&1; echo $? > "$out/par-$round-c.rc" ) &
  pc=$!
  wait $pa $pb $pc
  rcs="$(cat "$out/par-$round-a.rc" "$out/par-$round-b.rc" "$out/par-$round-c.rc" | tr '\n' ' ')"
  leftovers=$(find src markuppp minifypp .build/rt -name '*.tmp.*' 2>/dev/null | head -5)
  manifest > "$out/manifest-par-$round.txt"
  if [ "$rcs" = "0 0 0 " ] && [ -z "$leftovers" ] && cmp -s "$out/manifest-clean.txt" "$out/manifest-par-$round.txt"; then
    pass "parallel round $round: 3 concurrent makes, $(echo $victims | wc -w) shared objects rebuilt, all identical, no temporaries"
  else bad "parallel round $round: rc=$rcs leftovers=[$leftovers] (manifest-par-$round.txt)"; fi
done

exit $fail
