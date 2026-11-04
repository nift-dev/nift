#!/usr/bin/env bash
# Tooling checkpoint (component 3): a production translation unit cannot reach
# the storage-level test helpers. Each case plants the RECHECK2 N1/N2 (or an R3
# spelling) reproducer into a real production source, builds that object with
# its real production rule (`make OBJECT`, the same command as `make nift`),
# requires the build to FAIL with the expected diagnostic, and restores the
# tree byte for byte. A control plant (harmless code) must build.
#   run.sh   (from the repository root, after `make nift`)
set -u
export LC_ALL=C
repo=$(pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/nift-isolation.XXXXXX"); trap 'restore_all; rm -rf "$work"' EXIT
fail=0; saved=()
save() { cp -p "$1" "$work/$(echo "$1" | tr / _).orig"; saved+=("$1"); }
restore_all() {
  for f in "${saved[@]}"; do cp -p "$work/$(echo "$f" | tr / _).orig" "$f"; touch "$f"; done; saved=()
  rm -rf src/rt/tests src/rt/natives/probe.hxx src/rt/natives/probe.def minifypp/include/probe_access.h
}
# case NAME OBJECT EXPECT   (the plant has been applied; EXPECT: a regex of the diagnostic)
case_() {
  local name=$1 obj=$2 expect=$3 log="$work/$1.log"
  if make "$obj" > "$log" 2>&1; then
    echo "FAIL isolation $name: $obj built"; fail=1
  elif grep -Eq "$expect" "$log"; then
    echo "PASS isolation $name: $obj does not build ($(grep -Em1 "$expect" "$log" | sed 's/.*error: //' | cut -c1-100))"
  else echo "FAIL isolation $name: failed, but not with /$expect/:"; grep -m3 -E 'error|FAIL' "$log"; fail=1; fi
  restore_all
}
DEF='namespace nift::rt { struct StorageTestAccess { static void clear(Value& v) { Storage::array_mut(v).clear(); } }; }'
POISON='poisoned "StorageTestAccess"'

# N1: the test header included from production by a relative path, its free
# functions called (rc2/m-include)
save src/rt/vm_ops.cpp
sed -i 's|#include "rt/aggregate.h"|#include "rt/aggregate.h"\n#include "../../tests/rt/storage_access.h"|' src/rt/vm_ops.cpp
sed -i 's|^bool push_value(Value& array, const Value& value, Fault& fault, const Note\* note, const CycleGraph\* graph,$|void nift_probe_n1(Value\& a, const Value\& v) { array_append(a, v); }\n&|' src/rt/vm_ops.cpp
case_ n1-include-storage-access src/rt/vm_ops.o 'test-only header|poisoned'
# N1': any other test header (not storage_access.h) is a test dependency
save src/Hooks.cpp
printf '\n#include "../tests/rt/rt_test.h"\n' >> src/Hooks.cpp
case_ n1-any-test-header src/Hooks.o 'depends on the test tree'
# N2a: a definition with the old annotation, outside src/rt (rc2/m-outside probe C)
save src/Hooks.cpp
printf '\n#include "rt/aggregate.h"\n%s  // rt-lint: test-access-decl\n' "$DEF" >> src/Hooks.cpp
case_ n2a-annotated-definition src/Hooks.o "$POISON"
# N2b: a definition in markuppp/ (rc2/m-outside probe B) and a header under minifypp/include
save markuppp/src/Markup.cpp
printf '\n#include "rt/aggregate.h"\n%s\n' "$DEF" >> markuppp/src/Markup.cpp
case_ n2b-markuppp markuppp/src/Markup.o "$POISON"
save minifypp/src/Minify.cpp
printf '#pragma once\n#include "rt/aggregate.h"\n%s\n' "$DEF" > minifypp/include/probe_access.h
printf '\n#include "probe_access.h"\n' >> minifypp/src/Minify.cpp
case_ n2b-minifypp-header minifypp/src/Minify.o "$POISON"
# N2c: a nested tests directory under src (not the repository's tests/)
save src/rt/vm_ops.cpp
mkdir -p src/rt/tests; printf '#pragma once\n#include "rt/aggregate.h"\n%s\n' "$DEF" > src/rt/tests/helper.h
printf '\n#include "rt/tests/helper.h"\n' >> src/rt/vm_ops.cpp
case_ n2c-nested-tests-dir src/rt/vm_ops.o "$POISON"
# N2d: unscanned suffixes
for sfx in hxx def; do
  save src/rt/vm_ops.cpp
  printf '%s\n' "$DEF" > "src/rt/natives/probe.$sfx"
  printf '\n#include "rt/natives/probe.%s"\n' "$sfx" >> src/rt/vm_ops.cpp
  case_ "n2d-suffix-$sfx" src/rt/vm_ops.o "$POISON"
done
# R3 spellings: token pasting, the %:%: digraph, a line splice inside the name
save src/rt/vm_ops.cpp
printf '\n#define NIFT_PROBE_CAT(a, b) a##b\nnamespace nift::rt { struct NIFT_PROBE_CAT(StorageTest, Access) { static void c(Value& v) { Storage::array_mut(v).clear(); } }; }\n' >> src/rt/vm_ops.cpp
case_ r3-token-paste src/rt/vm_ops.o "$POISON"
save src/rt/vm_ops.cpp
printf '\n%%:define NIFT_PROBE_CAT(a, b) a%%:%%:b\nnamespace nift::rt { struct NIFT_PROBE_CAT(StorageTest, Access) { static void c(Value& v) { Storage::array_mut(v).clear(); } }; }\n' >> src/rt/vm_ops.cpp
case_ r3-digraph-paste src/rt/vm_ops.o "$POISON"
save src/rt/vm_ops.cpp
printf '\nnamespace nift::rt { struct StorageTest\\\nAccess { static void c(Value& v) { Storage::array_mut(v).clear(); } }; }\n' >> src/rt/vm_ops.cpp
case_ r3-line-splice src/rt/vm_ops.o "$POISON"
# ordering: a definition before the file's own first include
save src/Hooks.cpp
{ printf 'namespace nift::rt { struct Value; struct StorageTestAccess { template <class S> static void c(Value& v) { S::array_mut(v).clear(); } }; }\n'; cat "$work/src_Hooks.cpp.orig"; } > src/Hooks.cpp
case_ order-definition-first src/Hooks.o "$POISON"

# control: a harmless plant in the same file builds, so the cases above fail
# for their content, not for the harness
save src/rt/vm_ops.cpp
printf '\nnamespace nift::rt { int nift_isolation_control() { return 1; } }\n' >> src/rt/vm_ops.cpp
if make src/rt/vm_ops.o > "$work/control.log" 2>&1; then echo "PASS isolation control: a harmless plant builds"
else echo "FAIL isolation control: the control plant does not build"; tail -5 "$work/control.log"; fail=1; fi
restore_all
# leave the production objects as they were
make src/rt/vm_ops.o src/Hooks.o markuppp/src/Markup.o minifypp/src/Minify.o > "$work/rebuild.log" 2>&1 || { echo "FAIL isolation: rebuild after restore"; fail=1; }
git diff --quiet -- src markuppp minifypp || { echo "FAIL isolation: the tree was not restored"; fail=1; }
echo "$( [ $fail = 0 ] && echo PASS || echo FAIL ) isolation: production cannot reach the storage-level test helpers"
exit $fail
