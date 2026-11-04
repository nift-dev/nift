#!/usr/bin/env bash
# Tooling checkpoint (component 1): evidence for build-object reuse.
#   check-build-reuse.sh OUTDIR
# Run from the repository root after `make nift`. Each check prints a PASS or
# FAIL line (exit 1 on any FAIL); logs and manifests go to OUTDIR. Builds only:
# no test binary is run (the full lanes run on Linode).
#   deps       touching a header rebuilds exactly the objects whose compiler
#              dependency file names it; a source only its own object; a
#              further make does nothing
#   equiv      clean and incremental builds are byte-identical
#   A->B->A    for each signature input (compiler identity, flags, a feature
#              define, a generated input, a header edit): building B rebuilds
#              every affected object (no A object reused), and building A again
#              reproduces the first A build byte for byte (objects, sidecars,
#              dependency files, binaries), reusing none of B's objects
#   mixing     an object of another configuration in a directory is refused at
#              link time (check-config.sh), never linked
#   parallel   concurrent makes of different configurations in one tree, and
#              of one configuration sharing objects: every object, sidecar and
#              dependency file identical to a clean build of its own
#              configuration, no temporary left
set -u
out=${1:?usage: check-build-reuse.sh OUTDIR}
mkdir -p "$out"
fail=0
pass() { echo "PASS build-reuse $1"; }
bad() { echo "FAIL build-reuse $1"; fail=1; }
M() { make "$@" >>"$out/make.log" 2>&1; }
var() { make -s -f Makefile -f - print-var <<EOF 2>/dev/null
print-var: ; @echo \$($1)
EOF
}

DOBJ=$(var RT_UNIT_OBJECTS)
GEN_OBJ=src/ParserExpression.o            # the one object that includes libffi's generated headers
DEFAULT_BINS=".build/rt/value-unit .build/rt/atom-budget .build/rt/native-budget"
C1=no-cycle-check; C1_BIN=.build/rt/value-cycle-bench-off
C2=saturated-slots; C2_BIN=.build/rt/heap-account-oom
C1OBJ=$(var RT_OBJECTS_$C1); C2OBJ=$(var RT_OBJECTS_$C2)
[ -n "$DOBJ" ] && [ -n "$C1OBJ" ] && [ -n "$C2OBJ" ] || { bad "setup: cannot list the object sets"; exit 1; }
side() { for o in "$@"; do echo "$o" "${o%.o}.cfg" "${o%.o}.d"; done; }
manifest() { sha256sum $(side "$@") 2>/dev/null | sort -k2; }
DSET="$DOBJ $GEN_OBJ"
dmanifest() { { manifest $DSET; sha256sum $DEFAULT_BINS; } | sort -k2; }
mtimes() { stat -c '%n %Y' "$@" | sort; }
changed() { diff <(echo "$1") <(echo "$2") | sed -n 's/^> \(\S*\) .*/\1/p' | sort; }
build_default() { M -j2 $DEFAULT_BINS $GEN_OBJ "$@"; }

# --- clean build of the default configuration ---
rm -f $(side $DSET) $DEFAULT_BINS
start=$(date +%s)
build_default || bad "clean build failed (make.log)"
echo "default clean build seconds: $(( $(date +%s) - start ))" > "$out/timing.txt"
dmanifest > "$out/A1.txt"

# --- deps ---
hdr=src/rt/value_cycle.h
sleep 1; touch "$hdr"
expect=$(grep -l "$hdr" $(for o in $DSET; do echo "${o%.o}.d"; done) | sed 's/\.d$/.o/' | sort)
b=$(mtimes $DSET); build_default || bad "incremental build failed"; a=$(mtimes $DSET)
got=$(changed "$b" "$a")
[ -n "$expect" ] && [ "$expect" = "$got" ] && \
  pass "deps: touching $hdr rebuilt exactly the $(echo "$expect" | wc -l) objects whose .d names it" || \
  bad "deps: rebuilt set differs from the dependency files"
sleep 1; touch src/rt/aggregate.cpp
b=$(mtimes $DSET); build_default || bad "incremental build failed"; a=$(mtimes $DSET)
[ "$(changed "$b" "$a" | tr '\n' ' ')" = "src/rt/aggregate.o " ] && \
  pass "deps: touching src/rt/aggregate.cpp rebuilt only its object" || bad "deps: aggregate.cpp rebuilt more"
b=$(mtimes $DSET $DEFAULT_BINS); build_default || bad "no-op build failed"; a=$(mtimes $DSET $DEFAULT_BINS)
[ "$b" = "$a" ] && pass "deps: a further make rebuilds and relinks nothing" || bad "deps: a no-op make rebuilt something"
dmanifest > "$out/A1-incremental.txt"
cmp -s "$out/A1.txt" "$out/A1-incremental.txt" && \
  pass "equiv: clean and incremental byte-identical ($(wc -l < "$out/A1.txt") files: objects, sidecars, .d, binaries)" || \
  bad "equiv: clean and incremental differ"

# --- A -> B -> A for each signature input ---
# $1 label, $2 the objects B must rebuild ("all" or a list), then B's make arguments
aba() {
  local label=$1 must=$2; shift 2
  local t0 t1 t2 rebuilt_b rebuilt_a want n
  sleep 1; t0=$(mtimes $DSET)
  build_default "$@" || { bad "$label: B build failed"; return; }
  t1=$(mtimes $DSET); rebuilt_b=$(changed "$t0" "$t1")
  manifest $DSET > "$out/B-$label.txt"
  if [ "$must" = all ]; then want=$(echo $DSET | tr ' ' '\n' | sort); else want=$(echo $must | tr ' ' '\n' | sort); fi
  n=$(comm -12 <(grep '\.o$' "$out/A1.txt" | sort) <(grep '\.o$' "$out/B-$label.txt" | sort) | wc -l)
  if [ "$rebuilt_b" = "$want" ]; then
    pass "$label: B rebuilt exactly the $(echo "$want" | wc -l) affected objects ($n of them happen to be the same bytes as A)"
  else bad "$label: B rebuilt $(echo "$rebuilt_b" | grep -c .) objects, expected $(echo "$want" | wc -l)"; fi
  sleep 1
  build_default || { bad "$label: A rebuild failed"; return; }
  t2=$(mtimes $DSET); rebuilt_a=$(changed "$t1" "$t2")
  dmanifest > "$out/A2-$label.txt"
  if [ "$rebuilt_a" = "$want" ] && cmp -s "$out/A1.txt" "$out/A2-$label.txt"; then
    pass "$label: A again rebuilt every object B built (none of B's reused) and is byte-identical to the first A"
  else bad "$label: second A differs from the first or reused B objects ($out/A2-$label.txt)"; fi
}
# compiler identity: clang++ when present; otherwise a wrapper of the same
# compiler under another path reporting another --version (a compiler-version
# change as the signature sees it)
if command -v clang++ >/dev/null 2>&1; then other_cxx=clang++
else
  other_cxx="$out/fake-cxx/c++-other"; mkdir -p "$out/fake-cxx"
  printf '#!/bin/sh\nif [ "$1" = --version ]; then echo "wrapped compiler (build-reuse check)"; fi\nexec %s "$@"\n' \
    "$(command -v g++)" > "$other_cxx"; chmod +x "$other_cxx"
  echo "INFO build-reuse: clang++ not found; the compiler row uses a g++ wrapper with another identity"
fi
aba compiler all CXX="$other_cxx"
aba flags all CXXFLAGS="-std=c++17 -O1 -Wall -Wextra -pedantic -pthread"
aba define all CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread -DNIFT_TOOLING_PROBE"
# generated input: libffi's installed headers, regenerated by its build when
# its signature (LIBFFI_CFLAGS) changes; only the objects including them rebuild
aba generated "$GEN_OBJ" LIBFFI_CFLAGS="-O2 -fPIC -Wno-deprecated-declarations -DNIFT_TOOLING_PROBE"
# a header dependency edit (content change, then the original content back)
hdr=src/rt/value_cycle.h; cp "$hdr" "$out/hdr.orig"
hdr_users=$(grep -l "$hdr" $(for o in $DSET; do echo "${o%.o}.d"; done) | sed 's/\.d$/.o/' | sort | tr '\n' ' ')
sleep 1; t0=$(mtimes $DSET)
printf '\n// tooling build-reuse probe\ninline int nift_tooling_header_probe() { return 1; }\n' >> "$hdr"
build_default || bad "header: B build failed"
t1=$(mtimes $DSET)
[ "$(changed "$t0" "$t1" | tr '\n' ' ')" = "$hdr_users" ] && \
  pass "header: editing $hdr rebuilt exactly its $(echo $hdr_users | wc -w) dependent objects" || bad "header: B rebuilt another set"
sleep 1; cp "$out/hdr.orig" "$hdr"; touch "$hdr"
build_default || bad "header: A build failed"
t2=$(mtimes $DSET); dmanifest > "$out/A2-header.txt"
[ "$(changed "$t1" "$t2" | tr '\n' ' ')" = "$hdr_users" ] && cmp -s "$out/A1.txt" "$out/A2-header.txt" && \
  pass "header: restoring it rebuilt the same objects, byte-identical to the first A" || bad "header: A differs"
git diff --quiet -- "$hdr" || bad "header: $hdr not restored"
# the old Makefile (c1f43300) would have reused stale objects: for the record
if git show c1f43300:Makefile > "$out/old.mk" 2>/dev/null && \
   make -q -f "$out/old.mk" src/rt/value.o CXXFLAGS="-std=c++17 -O1" 2>/dev/null; then
  echo "INFO build-reuse: c1f43300's Makefile considers src/rt/value.o up to date under other CXXFLAGS (the hazard closed here)"
fi

# --- mixing: an object of another configuration is refused at link ---
M -j2 $C1_BIN || bad "mixing: $C1 build failed"
o=src/rt/vm_closure.o; cp "$o" "$out/keep.o"; cp "${o%.o}.cfg" "$out/keep.cfg"
cp ".build/rt-obj/$C1/$o" "$o"; cp ".build/rt-obj/$C1/${o%.o}.cfg" "${o%.o}.cfg"; touch "$o"
rm -f .build/rt/value-unit
if make .build/rt/value-unit > "$out/mixing.log" 2>&1; then bad "mixing: a $C1 object was linked into a default binary"
elif grep -q 'check-config: 1 of' "$out/mixing.log"; then
  pass "mixing: a $C1 object placed in the default directory is refused at link ($(grep -o 'check-config: [^;]*' "$out/mixing.log"))"
else bad "mixing: the link failed for another reason (mixing.log)"; fi
cp "$out/keep.o" "$o"; cp "$out/keep.cfg" "${o%.o}.cfg"; touch "$o"
build_default || bad "mixing: restore failed"
dmanifest > "$out/A2-mixing.txt"; cmp -s "$out/A1.txt" "$out/A2-mixing.txt" || bad "mixing: restore differs"

# --- parallel ---
# references: a clean serial build of each configuration
rm -rf ".build/rt-obj/$C1" ".build/rt-obj/$C2"
M -j2 $C1_BIN && manifest $C1OBJ > "$out/ref-$C1.txt" || bad "parallel: $C1 reference build failed"
M -j2 $C2_BIN && manifest $C2OBJ > "$out/ref-$C2.txt" || bad "parallel: $C2 reference build failed"
for round in 1 2; do
  victims=$(echo $DOBJ | tr ' ' '\n' | awk -v r=$round 'NR % 9 == r')
  rm -rf ".build/rt-obj/$C1" ".build/rt-obj/$C2"; rm -f $(side $victims) $DEFAULT_BINS $C1_BIN $C2_BIN
  pids=""
  for t in $C1_BIN $C2_BIN .build/rt/value-unit .build/rt/atom-budget .build/rt/native-budget; do
    ( make "$t" > "$out/par-$round-$(basename "$t").log" 2>&1; echo $? > "$out/par-$round-$(basename "$t").rc" ) &
    pids="$pids $!"
  done
  wait $pids
  rcs=$(cat "$out"/par-$round-*.rc | tr -d '\n')
  leftovers=$(find src markuppp minifypp .build/rt .build/rt-obj -name '*.tmp.*' 2>/dev/null | head -3)
  manifest $C1OBJ > "$out/par-$round-$C1.txt"; manifest $C2OBJ > "$out/par-$round-$C2.txt"
  dmanifest > "$out/par-$round-default.txt"
  if [ "$rcs" = 00000 ] && [ -z "$leftovers" ] && cmp -s "$out/ref-$C1.txt" "$out/par-$round-$C1.txt" && \
     cmp -s "$out/ref-$C2.txt" "$out/par-$round-$C2.txt" && cmp -s "$out/A1.txt" "$out/par-$round-default.txt"; then
    pass "parallel round $round: 5 concurrent makes over 3 configurations ($C1, $C2 from clean, default with $(echo $victims | wc -w) shared objects deleted): every object, sidecar and .d identical to its own configuration's clean build, no temporaries"
  else bad "parallel round $round: rc=$rcs leftovers=[$leftovers]"; fi
done

exit $fail
