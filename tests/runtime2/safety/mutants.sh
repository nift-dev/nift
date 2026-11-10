#!/usr/bin/env bash
# B0 pre-release safety mutation controls (docs/runtime-2/checkpoints/b0-safety.md):
# each mutant rebuilds the one translation unit holding its deliberate defect
# (-DNIFT_SAFETY_MUTANT=n), links it with the normal objects, and the suite
# (safety.py) must FAIL on it.
#   mutants.sh <nift> [n ...]      (default: every mutant)
set -u -o pipefail
NIFT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
OUT=.build/safety-mutants
mkdir -p "$OUT"
# mutant -> source file : the internal stop it restores
declare -A FILE=(
  [1]=src/rt/natives/format_methods.cpp  # S1: a handle or future presented (stringify/prettify/highlight) is Status::Internal
  [2]=src/rt/vm_array_mutation.cpp       # S2: a non-array receiver of an array mutator is an internal failure
  [3]=src/rt/natives/json_methods.cpp    # S3: a function, handle, future, collection or instance given to validate() is Status::Internal
  [4]=src/CLI.cpp                        # S4: a session statement value with no shown form stops the session with exit 70
  [5]=src/rt/vm_session_render.cpp       # S5: a handle or earlier collection as a session statement tail is an internal failure
  [6]=src/rt/natives/format_methods.cpp  # S6: to_string of a kind the interpolation lowering set aside is Status::Internal (review F1)
  [7]=src/rt/vm_ops.cpp                  # S7: appending such a value to an interpolation's string is an internal failure
)
# B4 (b4-dispatch.md 5): the lowering no longer sets kinds aside (scalar_only is
# gone; mixed parts dispatch at run time), so S6/S7's paths are reachable only
# through a narrowing defect. They are built together with B4 mutant A9 (the
# narrowing restored): the B0 paths must still be what keeps that defect from
# exit 70, and S6/S7 must still be caught.
declare -A WITH_A9=([6]=1 [7]=1)
list=("$@"); [ ${#list[@]} -eq 0 ] && list=(1 2 3 4 5 6 7)
CXX=${CXX:-g++}
CPPFLAGS="-Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark -I.build/libffi/$(${CC:-cc} -dumpmachine 2>/dev/null || uname -m)-$(basename "${CC:-cc}")/install/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
# The normal CLI objects (the Makefile passes them; otherwise ask make).
objects=${SAFETY_OBJECTS:-$(make -s -p -q nift 2>/dev/null | sed -n 's/^CLI_OBJECTS := //p' | head -1) \
  $(make -s -p -q nift 2>/dev/null | sed -n 's/^LIBFFI_A := //p' | head -1)}
# The real binary passes first (otherwise a "caught" mutant proves nothing).
if ! python3 tests/runtime2/safety/safety.py --nift "$NIFT" > "$OUT/base.log" 2>&1; then
  tail -5 "$OUT/base.log"; echo "FAIL safety mutants: the unmutated binary fails the suite"; exit 1
fi
echo "base ($NIFT): $(tail -1 "$OUT/base.log")"
fail=0
for n in "${list[@]}"; do
  src=${FILE[$n]}; obj=${src%.cpp}.o; mobj=$OUT/s$n-$(basename "$obj")
  $CXX $CPPFLAGS $CXXFLAGS -DNIFT_SAFETY_MUTANT=$n -c "$src" -o "$mobj" || { echo "mutant S$n: build failed"; exit 2; }
  # C80-L2 (combined-b3-b5.md section 79): the mutant object must differ from production.
  if cmp -s "$mobj" "$obj"; then echo "mutant S$n: compiles to the production $obj (a compiled-out hook)"; exit 2; fi
  extra=""; skip="$obj"
  if [ -n "${WITH_A9[$n]:-}" ]; then
    extra=$OUT/a9-compiler_interp.o; skip="$obj|src/rt/compiler_interp.o"
    $CXX -include src/build/production_tu.h $CPPFLAGS $CXXFLAGS -DNIFT_B4_MUTANT=9 -c src/rt/compiler_interp.cpp -o "$extra" \
      || { echo "mutant S$n: A9 build failed"; exit 2; }
  fi
  link=$(printf '%s\n' $objects | grep -vxE "$skip" | grep -v '\.a$' | tr '\n' ' ')
  libs=$(printf '%s\n' $objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CXXFLAGS $link "$mobj" $extra -ldl $libs -o "$OUT/nift-s$n" || { echo "mutant S$n: link failed"; exit 2; }
  if python3 tests/runtime2/safety/safety.py --nift "$OUT/nift-s$n" > "$OUT/s$n.log" 2>&1; then
    echo "mutant S$n ($src): SURVIVED"; fail=1
  else
    echo "mutant S$n ($src): caught ($(grep -c ': exit 70$' "$OUT/s$n.log") cases exit 70," \
         "$(grep -c '^FAIL ' "$OUT/s$n.log") failing checks)"
  fi
  rm -f "$OUT/nift-s$n" "$mobj"
done
[ $fail -eq 0 ] && echo "PASS safety mutants: ${#list[@]} caught" || echo "FAIL safety mutants"
exit $fail
