#!/usr/bin/env bash
# B01 (combined-b3-b5.md section 43): negative controls for the VM's read-limit checks.
#
# Rebuilds the translation units that include the read natives
# (src/rt/natives/host_fs_stream.h, host_fs_internal.h, host_stream_handles.cpp) with
# -DNIFT_READ_LIMIT_MUTANT=n, links them with the tree's other objects, and runs
# tests/runtime2/read_limit_controls.py on the result:
#   0  control (no mutation): must pass;
#   1  read_whole/read_source as before B01 (the per-chunk check that wraps when one 64 KiB
#      chunk is larger than the limit, no size check, growth not bounded by the limit);
#   2  stream growth not bounded by the limit (read_line, read_all double past it);
#   3  no per-chunk limit check in read_whole;
#   4  no read_line limit check;
#   5  no size-against-limit check before a regular file's buffer is sized;
#   6  read_whole reads what is available now (readsome): a short read ends the read;
#   7  read_whole's byte count restarts at every read;
#   8  stream read(n) checks the requested count n, not the bytes read (C53, the pre-C53 rule).
# Each mutant must compile to objects that differ from the control's (provenance: no vacuous
# build) and must fail the controls. A build failure is a harness failure.
# Usage (from the repository root; the Makefile passes the objects):
#   READ_LIMIT_OBJECTS="<CLI objects> <libffi.a>" READ_LIMIT_CPPFLAGS="..." read_limit_mutants.sh
set -u -o pipefail
objects=${READ_LIMIT_OBJECTS:?the Makefile passes the CLI objects}
CPPFLAGS=${READ_LIMIT_CPPFLAGS:?the Makefile passes CPPFLAGS}
CXX=${CXX:-g++}
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
OUT=.build/read-limit-mutants
rm -rf "$OUT"; mkdir -p "$OUT"
export PYTHONDONTWRITEBYTECODE=1
TUS=(src/rt/natives/host_file_handles.cpp src/rt/natives/host_file_edits.cpp src/rt/natives/host_file_ops.cpp src/rt/natives/host_fs_nav.cpp
     src/rt/natives/host_fs_read.cpp src/rt/natives/host_fs_write.cpp src/rt/natives/host_stream_handles.cpp
     src/rt/natives/host_stream_extract.cpp)
# Every TU that includes the mutated code is rebuilt (a TU left out would keep production code).
for tu in $(grep -lE 'rt/natives/(host_fs_stream|host_fs_internal|host_file_handles_internal|host_fs_observe)\.h' \
              src/rt/*.cpp src/rt/natives/*.cpp) src/rt/natives/host_stream_handles.cpp; do
  printf '%s\n' "${TUS[@]}" | grep -qx "$tu" || { echo "FAIL read-limit mutants: $tu includes the read natives but is not rebuilt"; exit 1; }
done
fail() { echo "FAIL read-limit mutants: $*"; exit 1; }

build() {  # build N: the mutated objects and binary in $OUT/N
  local n=$1
  local dir=$OUT/$n
  mkdir -p "$dir"
  printf '%s\n' "${TUS[@]}" | xargs -P "${JOBS:-2}" -I{} sh -c \
    '"$1" $2 $3 -DNIFT_READ_LIMIT_MUTANT=$4 -c "$5" -o "$6/$(basename "$5" .cpp).o"' _ "$CXX" "$CPPFLAGS" "$CXXFLAGS" "$n" {} "$dir" \
    >"$dir/build.log" 2>&1 || { tail -20 "$dir/build.log"; fail "mutant $n: build failed (harness failure)"; }
  # Linked in the production order, each rebuilt object in its own object's place, so the control
  # binary is byte-identical to the tree's nift.
  local link=() libs=() obj
  for obj in $objects; do
    case $obj in
      *.a) libs+=("$obj") ;;
      *) if printf '%s\n' "${TUS[@]}" | grep -qx "${obj%.o}.cpp"; then link+=("$dir/$(basename "$obj")")
         else link+=("$obj"); fi ;;
    esac
  done
  $CXX $CXXFLAGS "${link[@]}" -ldl "${libs[@]}" -o "$dir/nift" >>"$dir/build.log" 2>&1 ||
    { tail -20 "$dir/build.log"; fail "mutant $n: link failed (harness failure)"; }
}

build 0
# Provenance: the control objects are the production ones (same flags, no mutation), so the
# mutants differ from production only by their hook.
for tu in "${TUS[@]}"; do
  cmp -s "$OUT/0/$(basename "$tu" .cpp).o" "${tu%.cpp}.o" ||
    fail "control object for $tu differs from the production object (flags differ: rebuild nift first)"
done
cmp -s "$OUT/0/nift" nift || fail "the control binary differs from ./nift (rebuild nift first)"
NIFT_BIN="$PWD/$OUT/0/nift" python3 -B tests/runtime2/read_limit_controls.py >"$OUT/0/test.log" 2>&1 ||
  { grep -v '^PASS' "$OUT/0/test.log" | tail -20; fail "control (NIFT_READ_LIMIT_MUTANT=0) failed the controls"; }
echo "PASS read-limit control: NIFT_READ_LIMIT_MUTANT=0 passes ($(grep -c '^PASS' "$OUT/0/test.log") checks)"

status=0
for n in 1 2 3 4 5 6 7 8; do
  build "$n"
  differs=0
  for tu in "${TUS[@]}"; do
    obj=$(basename "$tu" .cpp).o
    cmp -s "$OUT/$n/$obj" "$OUT/0/$obj" || differs=1
  done
  [ "$differs" = 1 ] || fail "mutant $n: every object equals the control's (no compiled mutation)"
  if NIFT_BIN="$PWD/$OUT/$n/nift" timeout 900 python3 -B tests/runtime2/read_limit_controls.py >"$OUT/$n/test.log" 2>&1; then
    echo "FAIL read-limit mutant $n: survived the controls"; status=1
  else
    echo "PASS read-limit mutant $n: rejected; $(grep -c '^FAIL' "$OUT/$n/test.log") failing check(s), first: $(grep -m1 '^FAIL' "$OUT/$n/test.log" | cut -c1-110)"
  fi
done
find tests/runtime2 -name __pycache__ -type d -exec rm -rf {} + 2>/dev/null
[ "$status" = 0 ] && echo "read-limit mutants: all 8 rejected, control passes"
exit "$status"
