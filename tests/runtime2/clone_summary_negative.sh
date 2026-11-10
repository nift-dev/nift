#!/usr/bin/env bash
# Negative control for the clones' cycle summary (review of C72-C78, T-1; combined-b3-b5.md section
# 68). A clone must carry its source's "may hold identity" summary (copy_summary): without it the
# cycle check skips the clone and a store can close a real reference cycle. For each clone site a
# temporary nift is linked from a copy of the source with that one copy_summary call removed
# (test-side, never in production), and tests/runtime2/cycle_refusal_routes.py must FAIL on it.
# The unmodified binary must pass first.
#   clone_summary_negative.sh NIFT   (run via make: needs CXX, CPPFLAGS, CXXFLAGS, LDLIBS, CLI_OBJECTS)
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
NIFT=$1
: "${CLI_OBJECTS:?run via 'make test-runtime2-clone-summary-negative'}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"

python3 -B tests/runtime2/cycle_refusal_routes.py --nift "$NIFT" > "$TMP/control.log" ||
  { echo "FAIL: the unmodified binary fails the cycle routes" >&2; cat "$TMP/control.log" >&2; exit 1; }
echo "control: PASS ($(tail -n 1 "$TMP/control.log"))"

# site: source file, function whose copy_summary call is removed
SITES="object_shallow_clone:src/rt/aggregate.cpp array_shallow_clone:src/rt/aggregate.cpp collection_shallow_clone:src/rt/aggregate_collection.cpp"
caught=0 total=0 missed_required="" escaped=""
for site in $SITES; do
  fn=${site%%:*} src=${site#*:}
  obj=${src%.cpp}.o
  total=$((total + 1))
  mkdir -p "$TMP/$fn"
  python3 - "$src" "$TMP/$fn/$(basename "$src")" "$fn" <<'PY'
import re, sys
src, dst, fn = sys.argv[1:]
text = open(src).read()
start = text.index("Value %s(" % fn)
end = text.index("\n}\n", start)
body = text[start:end]
if body.count("copy_summary(") != 1:
    raise SystemExit("mutant pattern not found in %s; update the negative control" % fn)
mutated = re.sub(r"\n\s*copy_summary\([^;]*\);", "\n    // MUTANT: copy_summary removed", body, count=1)
open(dst, "w").write(text[:start] + mutated + text[end:])
PY
  if ! $CXX ${PRODUCTION_GUARD:-} $CPPFLAGS $CXXFLAGS -c "$TMP/$fn/$(basename "$src")" -o "$TMP/$fn/mutant.o" ||
     ! $CXX $CXXFLAGS $(for o in $CLI_OBJECTS; do [ "$o" = "$obj" ] || printf '%s ' "$o"; done) "$TMP/$fn/mutant.o" \
         $LDLIBS -o "$TMP/$fn/nift"; then
    echo "FAIL: could not build the $fn mutant (harness error, not a detection)" >&2; exit 1
  fi
  # C80-L2: the mutant object must differ from production (a compiled-out removal is a harness error).
  if cmp -s "$TMP/$fn/mutant.o" "$obj"; then
    echo "FAIL: the $fn mutant compiles to the production $obj (harness error, not a detection)" >&2; exit 1
  fi
  if python3 -B tests/runtime2/cycle_refusal_routes.py --nift "$TMP/$fn/nift" > "$TMP/$fn/log"; then
    echo "SURVIVED $fn without copy_summary: every cycle route still refused"
    [ "$fn" = array_shallow_clone ] || missed_required="$missed_required $fn"
  else
    caught=$((caught + 1))
    # C79-L1: the array clone is an internal snapshot, never stored, so its summary is never read and
    # its mutant must survive. A route that catches it means a script can now reach (store) a snapshot.
    [ "$fn" = array_shallow_clone ] && escaped="$escaped $fn"
    echo "caught $fn without copy_summary: $(grep '^FAIL ' "$TMP/$fn/log" | grep -vc 'cycle refusal routes:') route(s) fail ($(grep '^FAIL ' "$TMP/$fn/log" | grep -v 'cycle refusal routes:' | head -3 | awk '{print $2}' | tr '\n' ' '))"
  fi
  rm -f "$TMP/$fn/nift" "$TMP/$fn/mutant.o"
done
# array_shallow_clone is only an internal snapshot (no script can store its result): its mutant must
# survive (combined-b3-b5.md sections 68 and 79, C79-L1); the other two must be caught.
echo "clone-summary mutants: $caught of $total caught"
if [ -n "$missed_required" ]; then
  echo "FAIL: a clone that escapes to scripts lost its cycle summary undetected:$missed_required"; exit 1
fi
if [ -n "$escaped" ]; then
  echo "FAIL: the internal array snapshot reached a stored value (its copy_summary mutant was caught):$escaped"; exit 1
fi
echo "negative-control: PASS"
