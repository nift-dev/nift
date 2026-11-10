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
work=$(mktemp -d "${TMPDIR:-/tmp}/nift-isolation.XXXXXX"); trap 'restore_all; restore_objects; rm -rf "$work"' EXIT
fail=0; saved=()
# The objects the cases rebuild (re-check note): saved with their sidecars and
# dependency files, mtimes included, and put back at the end, so the tree is
# left exactly as it was (`make -q nift` still 0); sources are restored with
# their own mtimes (cp -p, no touch).
OBJS="src/rt/vm_ops.o src/Hooks.o markuppp/src/Markup.o minifypp/src/Minify.o src/rt/natives/object_methods.o
      src/rt/vm_closure.o src/rt/compiler_coro.o"
mkdir -p "$work/objs"
for o in $OBJS; do for f in "$o" "${o%.o}.cfg" "${o%.o}.d"; do cp -p "$f" "$work/objs/$(echo "$f" | tr / _)"; done; done
restore_objects() {
  for o in $OBJS; do for f in "$o" "${o%.o}.cfg" "${o%.o}.d"; do
    [ -f "$work/objs/$(echo "$f" | tr / _)" ] && cp -p "$work/objs/$(echo "$f" | tr / _)" "$f"; done; done
}
save() { cp -p "$1" "$work/$(echo "$1" | tr / _).orig"; saved+=("$1"); }
restore_all() {
  for f in "${saved[@]}"; do cp -p "$work/$(echo "$f" | tr / _).orig" "$f"; done; saved=()
  rm -rf src/rt/tests src/rt/natives/probe.hxx src/rt/natives/probe.def minifypp/include/probe_access.h \
         src/rt/natives/probe_undef.h src/rt/natives/probe_system.h src/rt/natives/probe_n.h \
         "src/rt/natives/probe dir" "src/rt/natives/probe'q.h" "src/rt/natives/m:"
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
# B14 (combined-b3-b5.md section 74): quote-agnostic. GCC 13 quotes the name "...", GCC 15 '...'
# (and a UTF-8 locale would give the 3-byte quotes); the name must still end there.
POISON='poisoned [^[:alnum:]_ ]{0,3}StorageTestAccess([^[:alnum:]_]|$)'
# The pattern itself, against recorded diagnostics of both compilers and two that must not match.
for probe in 'src/Hooks.cpp:118:29: error: attempt to use poisoned "StorageTestAccess"' \
             "src/Hooks.cpp:118:29: error: attempt to use poisoned 'StorageTestAccess'" \
             'src/Hooks.cpp:118:29: error: attempt to use poisoned \xe2\x80\x98StorageTestAccess\xe2\x80\x99'; do
  printf '%b\n' "$probe" | grep -Eq "$POISON" || { echo "FAIL isolation pattern: /$POISON/ misses: $probe"; fail=1; }
done
for probe in "error: attempt to use poisoned 'StorageTestAccessor'" "error: 'StorageTestAccess' was not declared"; do
  printf '%s\n' "$probe" | grep -Eq "$POISON" && { echo "FAIL isolation pattern: /$POISON/ matches: $probe"; fail=1; }
done

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
# ordering: a definition before rt/aggregate.h (a member template naming
# Storage only when instantiated, used after the include through a name that
# is not the poisoned one); the completeness assertion in aggregate.h fires
save src/rt/vm_ops.cpp
{ printf 'namespace nift::rt { class Value; struct StorageTestAccess { template <class S, class V> static void c(V& v) { S::array_mut(v).clear(); } };\n'
  printf 'template <class S, class V> void nift_probe_order(V& v) { StorageTestAccess::c<S>(v); } }\n'
  cat "$work/src_rt_vm_ops.cpp.orig"
  printf '\nnamespace nift::rt { void nift_probe_order_use(Value& v) { nift_probe_order<Storage>(v); } }\n'; } > src/rt/vm_ops.cpp
case_ order-definition-first src/rt/vm_ops.o 'defined in a production translation unit'

# Tooling review F4: the marker undefined before rt/aggregate.h (x/i1_undef.cpp,
# the re-checker's vm_ops.cpp plant): in the unit itself, in a header it
# includes (spelt with a comment and a backslash-blank-newline splice), and on
# the command line
save src/rt/vm_ops.cpp
{ printf '#undef NIFT_PRODUCTION_TU\n'; cat "$work/src_rt_vm_ops.cpp.orig"; printf '\n%s\n' "$DEF"; } > src/rt/vm_ops.cpp
case_ f4-undef-in-unit src/rt/vm_ops.o 'undefines the production marker'
save src/rt/vm_ops.cpp
printf '#pragma once\n# /* x */ undef NIFT_PRODUCTION_\\ \nTU\n' > src/rt/natives/probe_undef.h
{ printf '#include "rt/natives/probe_undef.h"\n'; cat "$work/src_rt_vm_ops.cpp.orig"; printf '\n%s\n' "$DEF"; } > src/rt/vm_ops.cpp
case_ f4-undef-in-header src/rt/vm_ops.o 'undefines the production marker'
if out=$(bash scripts/build/compile-object.sh --production "$work/cl.o" -- g++ -include src/build/production_tu.h \
         -UNIFT_PRODUCTION_TU -Isrc -Iinclude -std=c++17 -c src/rt/atom.cpp 2>&1); then
  echo "FAIL isolation f4-undef-command-line: built"; fail=1
elif printf '%s\n' "$out" | grep -q 'command line undefines NIFT_PRODUCTION_TU'; then
  echo "PASS isolation f4-undef-command-line: refused (-UNIFT_PRODUCTION_TU)"
else echo "FAIL isolation f4-undef-command-line: $out"; fail=1; fi
# Tooling review F6: a test header behind `#pragma GCC system_header` (x/sysh)
save src/rt/vm_ops.cpp
printf '#pragma once\n#pragma GCC system_header\n#include "../../../tests/rt/rt_test.h"\n' > src/rt/natives/probe_system.h
printf '\n#include "rt/natives/probe_system.h"\n' >> src/rt/vm_ops.cpp
case_ f6-system-header src/rt/vm_ops.o 'depends on the test tree'

# Re-review N3 (rr/e2-mutant-define.diff): a test-only switch defined in source
# would turn a test mutant (and its in-place key friendship) on in production
save src/rt/natives/object_methods.cpp
{ printf '#define NIFT_REFSEM_MUTANT 26\n'; cat "$work/src_rt_natives_object_methods.cpp.orig"; } > src/rt/natives/object_methods.cpp
case_ n3-mutant-define-in-unit src/rt/natives/object_methods.o 'defines the test-only switch NIFT_REFSEM_MUTANT'
save src/rt/vm_ops.cpp
printf '#pragma once\n#define NIFT_SORT_WORDING_TEST\n' > src/rt/natives/probe_n.h
printf '\n#include "rt/natives/probe_n.h"\n' >> src/rt/vm_ops.cpp
case_ n3-test-switch-in-header src/rt/vm_ops.o 'defines the test-only switch NIFT_SORT_WORDING_TEST'
if out=$(bash scripts/build/compile-object.sh --production "$work/cl2.o" -- g++ -include src/build/production_tu.h \
         -DNIFT_REFSEM_MUTANT=26 -Isrc -Iinclude -std=c++17 -c src/rt/atom.cpp 2>&1); then
  echo "FAIL isolation n3-switch-command-line: built"; fail=1
elif printf '%s\n' "$out" | grep -q 'command line sets the test-only switch NIFT_REFSEM_MUTANT'; then
  echo "PASS isolation n3-switch-command-line: refused (-DNIFT_REFSEM_MUTANT=26)"
else echo "FAIL isolation n3-switch-command-line: $out"; fail=1; fi
# Re-review N4, N6: the marker undefined in spellings the compiler reads as
# directives, and dependencies the scan cannot review
n4() { local name=$1 header=$2 expect=$3; shift 3
  save src/rt/vm_ops.cpp
  printf '\n#include "%s"\n' "$header" >> src/rt/vm_ops.cpp
  case_ "$name" src/rt/vm_ops.o "$expect"
}
printf 'int nift_probe_cr;\r#undef NIFT_PRODUCTION_TU\r' > src/rt/natives/probe_n.h
n4 n4-lone-cr rt/natives/probe_n.h 'undefines the production marker'
printf '#un\\\rdef NIFT_PRODUCTION_TU\n' > src/rt/natives/probe_n.h
n4 n4-backslash-cr-splice rt/natives/probe_n.h 'undefines the production marker'
printf '\xef\xbb\xbf#undef NIFT_PRODUCTION_TU\n' > src/rt/natives/probe_n.h
n4 n4-utf8-bom rt/natives/probe_n.h 'undefines the production marker'
mkdir -p "src/rt/natives/probe dir"; printf '#undef NIFT_PRODUCTION_TU\n' > "src/rt/natives/probe dir/sp.h"
n4 n4-space-in-path "rt/natives/probe dir/sp.h" 'undefines the production marker'
printf '#pragma once\n' > "$work/outside.h"; ln -sf "$work/outside.h" src/rt/natives/probe_n.h
n4 n4-symlink-outside rt/natives/probe_n.h 'outside the repository'
rm -f src/rt/natives/probe_n.h
n4 n4-absolute-outside "$work/outside.h" 'outside the repository'
printf '#pragma once\n#include "../../../tests/rt/rt_test.h"\n' > "src/rt/natives/probe'q.h"
n4 n6-quote-in-path "rt/natives/probe'q.h" 'depends on the test tree'
# check C1: a dependency whose name ends in ':' (ck/plants.diff, first hunk)
save src/rt/natives/object_methods.cpp
printf '#define NIFT_REFSEM_MUTANT 26\n' > "src/rt/natives/m:"
{ printf '#include "rt/natives/m:"\n'; cat "$work/src_rt_natives_object_methods.cpp.orig"; } > src/rt/natives/object_methods.cpp
case_ c1-colon-in-name src/rt/natives/object_methods.o 'defines the test-only switch NIFT_REFSEM_MUTANT in: src/rt/natives/m:'
# check C2: a configuration switch defined in source (ck/nocycle.diff)
save src/rt/vm_closure.cpp
{ printf '#define NIFT_RT_NO_CYCLE_CHECK\n'; cat "$work/src_rt_vm_closure.cpp.orig"; } > src/rt/vm_closure.cpp
case_ c2-config-switch-in-source src/rt/vm_closure.o 'defines the test-only switch NIFT_RT_NO_CYCLE_CHECK'
# check C3: a reviewed default with another value
save src/rt/compiler_coro.cpp
sed -i 's/^#define NIFT_CORO_MUTANT 0$/#define NIFT_CORO_MUTANT 18/' src/rt/compiler_coro.cpp
grep -q '^#define NIFT_CORO_MUTANT 18$' src/rt/compiler_coro.cpp || { echo "FAIL isolation c3: plant not applied"; fail=1; }
case_ c3-default-value-changed src/rt/compiler_coro.o 'defines the test-only switch NIFT_CORO_MUTANT'
# the refused set and the configuration allowances follow the Makefile
if out=$(python3 scripts/build/check-production-tu.py --self-test); then echo "$out"
else echo "$out"; fail=1; fi

# control: a harmless plant in the same file builds, so the cases above fail
# for their content, not for the harness
save src/rt/vm_ops.cpp
printf '\nnamespace nift::rt { int nift_isolation_control() { return 1; } }\n' >> src/rt/vm_ops.cpp
if make src/rt/vm_ops.o > "$work/control.log" 2>&1; then echo "PASS isolation control: a harmless plant builds"
else echo "FAIL isolation control: the control plant does not build"; tail -5 "$work/control.log"; fail=1; fi
restore_all
# leave the production objects as they were (the saved objects, sidecars and
# dependency files, mtimes included)
restore_objects
# shellcheck disable=SC2086
make -q $OBJS || { echo "FAIL isolation: the restored objects are not up to date"; fail=1; }
git diff --quiet -- src markuppp minifypp || { echo "FAIL isolation: the tree was not restored"; fail=1; }
echo "$( [ $fail = 0 ] && echo PASS || echo FAIL ) isolation: production cannot reach the storage-level test helpers"
exit $fail
