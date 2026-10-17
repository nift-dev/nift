#!/usr/bin/env bash
# CP08 harness self-tests (review findings 1-10; owner CP08 requirements).
# Proves that rt-diff (a) catches a seeded difference on every channel, (b) its
# normalisations hide only what they document, (c) every traced Host-op
# category is exercised by the corpus, (d) tracing changes nothing but the
# trace, (e) approved-difference policies suppress only their exact
# difference, (f) missing/empty corpora fail, (g) generated programs are
# reproducible and differences are minimised.
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
REPO="$(pwd)"
RTDIFF="$REPO/tools/rt-diff/rt-diff"
MUTANT="$REPO/tests/runtime2/rt_diff_mutant.py"
CORPUS=(--corpus "$REPO/tests/runtime2/rt-diff-corpus" --corpus "$REPO/tests/runtime2/semantics")
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
fail=0
pass() { echo "PASS $1"; }
bad() { echo "FAIL $1"; fail=1; }

mutant() { # mode -> path of a wrapper binary
  local f="$W/mutant-$(echo "$1" | tr -c 'A-Za-z0-9\n' '_')"
  printf '#!/bin/sh\nRT_MUTANT_R2_DIR=%q exec python3 %q %q %q "$@"\n' "${RT_MUTANT_R2_DIR:-}" "$MUTANT" "$1" "$NIFT" > "$f"; chmod +x "$f"; echo "$f"
}
run() { python3 "$RTDIFF" --a "$NIFT" --jobs "${RT_DIFF_JOBS:-8}" "$@" > "$W/out" 2>&1; echo $?; }

# (a) every channel catches its seeded difference ---------------------------
check_caught() { # mode channel-regex
  local rc; rc=$(run --b "$(mutant "$1")" "${CORPUS[@]}")
  if [ "$rc" = 1 ] && grep -E "^DIFF \S+ ($2)" "$W/out" >/dev/null; then pass "caught: $1 ($2)"
  else bad "missed: $1 (rc=$rc)"; tail -3 "$W/out"; fi
}
check_caught file-mode 'files'
check_caught empty-dir 'files'
check_caught nift-garble 'files'
check_caught home-write 'home'
check_caught tmp-write 'tmp'
check_caught home-tmp-swap 'stdout|combined'
check_caught stdout-only 'stdout'
check_caught stderr-only 'stderr'
check_caught exit-only 'exit'
check_caught stdin-only 'stdout'
check_caught eval-key-order 'stdout'
check_caught number-format 'stdout'
check_caught interleave 'combined'
check_caught trace-drop 'trace'
check_caught trace-reorder 'trace'
check_caught trace-async-reorder 'trace'
check_caught time-word 'stdout'

# (b) normalisations hide only what they document ---------------------------
check_hidden() { # mode
  local rc; rc=$(run --b "$(mutant "$1")" "${CORPUS[@]}")
  if [ "$rc" = 0 ]; then pass "hidden by design: $1"; else bad "normalisation too narrow for $1"; grep '^DIFF' "$W/out" | head -3; fi
}
check_hidden identity
check_hidden time-number
# Rule 4/5 (page-info timing keys, listing fingerprints) is checked on the
# normaliser itself: a mutant rewriting page-info mid-sequence would change
# Nift's own incremental decisions, which is a real behaviour difference.
if python3 - "$RTDIFF" <<'PY'
import importlib.machinery, importlib.util, json, sys
loader = importlib.machinery.SourceFileLoader("rtdiff", sys.argv[1])
spec = importlib.util.spec_from_loader("rtdiff", loader); m = importlib.util.module_from_spec(spec); loader.exec_module(m)
base = {"name": "/", "reqs": [], "listings": {"glob:a/*.txt": "1111"}, "tick-hashes": {"a": "1"}}
same = dict(base, **{"tick-hashes": {"b": "2"}, "racy-dependencies": ["x"], "listings": {"glob:a/*.txt": "2222"}})
other_key = dict(base, reqs=["y"])
other_listing = dict(base, listings={"glob:b/*.txt": "1111"})
n = lambda d: m.normalise_page_info(json.dumps(d).encode())
assert n(base) == n(same), "timing keys / fingerprint values must be hidden"
assert n(base) != n(other_key), "other keys must stay visible"
assert n(base) != n(other_listing), "listing keys must stay visible"
PY
then pass "page-info normalisation hides timing keys and fingerprint values only"; else bad "page-info normalisation"; fi

# (c) every traced op category is exercised (dropping it is caught) ----------
for op in stat list glob read open save touch remove mkdir copy-to move-to move-from read-source read-json getenv env-all spawn; do
  rc=$(run --b "$(mutant "trace-drop-op:$op")" "${CORPUS[@]}")
  if [ "$rc" = 1 ] && grep -q '^DIFF .* trace' "$W/out"; then pass "trace op exercised: $op"; else bad "trace op not exercised: $op"; fi
done

# (d) tracing changes nothing but the trace ---------------------------------
rc=$(run --b "$NIFT" --trace-neutrality "${CORPUS[@]}" --corpus "$REPO/tests/runtime2/vm-corpus")
[ "$rc" = 0 ] && pass "trace neutrality over the corpus" || { bad "tracing changed output"; grep '^DIFF' "$W/out" | head -5; }
SITE="$REPO/tests/runtime2/rt-diff-corpus/sites/multipage-threads"
for i in 1 2 3 4 5; do
  rm -rf "$W/s1" "$W/s2"; cp -r "$SITE" "$W/s1"; cp -r "$SITE" "$W/s2"
  (cd "$W/s1" && "$NIFT" build --all >/dev/null 2>&1)
  (cd "$W/s2" && NIFT_TRACE_HOST=3 "$NIFT" build --all >/dev/null 2>&1 3>&-)
  if ! diff -r "$W/s1/public" "$W/s2/public" >/dev/null; then bad "closed trace fd changed page output (run $i)"; break; fi
done
[ "$i" = 5 ] && pass "closed trace descriptor never reaches page outputs (5 x 25 pages, 4 threads)"
out1="$(cd "$W/s1" && NIFT_TRACE_HOST=1 "$NIFT" status 2>&1)"; out2="$(cd "$W/s1" && "$NIFT" status 2>&1)"
[ "$out1" = "$out2" ] && pass "NIFT_TRACE_HOST=1 refused (stdout unchanged)" || bad "trace written to stdout"

# (e) policies suppress only their exact difference --------------------------
mkdir -p "$W/pc/one"; printf 'print("item a")\nprint("item b")\n' > "$W/pc/one/main.f"
policy() { # old new count
  local ch rules=""
  for ch in stdout combined; do
    rules="$rules${rules:+,}{\"id\":\"self-$ch\",\"decision\":\"D-009#6\",\"entry\":\"pc/one/main.f\",\"channel\":\"$ch\",\"old\":\"$1\",\"new\":\"$2\",\"count\":$3,\"justification\":\"self-test\",\"test\":\"tests/runtime2/rt_diff_selftest.sh\"}"
  done
  printf '[%s]\n' "$rules" > "$W/policy.json"
}
policy item ITEM 2
rc=$(run --b "$(mutant replace:item:ITEM)" --corpus "$W/pc" --policy "$W/policy.json")
[ "$rc" = 0 ] && grep -q '^APPROVED pc/one/main.f stdout self' "$W/out" && pass "policy approves its exact substitution" || bad "policy did not approve"
rc=$(run --b "$(mutant replace:item:ITEMS)" --corpus "$W/pc" --policy "$W/policy.json")
[ "$rc" = 1 ] && pass "policy rejects a different substitution" || bad "policy over-approved a different change"
policy item ITEM 1
rc=$(run --b "$(mutant replace:item:ITEM)" --corpus "$W/pc" --policy "$W/policy.json")
[ "$rc" = 1 ] && pass "policy rejects a wrong occurrence count" || bad "policy ignored its count"
printf '[{"id":"wild","decision":"D-009#6","entry":"pc/*","channel":"stdout","old":"a","new":"b","count":1,"justification":"x","test":"x"}]\n' > "$W/wild.json"
rc=$(run --b "$NIFT" --corpus "$W/pc" --policy "$W/wild.json")
[ "$rc" != 0 ] && pass "pattern policies are refused" || bad "pattern policy accepted"

# D-009 policies: an engine emitting the intended (.r2) goldens is fully
# approved by tools/rt-diff/policies/d009.json and fully flagged without it.
export RT_MUTANT_R2_DIR="$REPO/tests/runtime2/semantics"
rc=$(run --b "$(mutant r2-goldens)" --corpus "$REPO/tests/runtime2/semantics" --policy "$REPO/tools/rt-diff/policies/d009.json")
n_ok=$(grep -c '^APPROVED' "$W/out")
[ "$rc" = 0 ] && [ "$n_ok" -ge 6 ] && pass "D-009 policies approve exactly the intended changes ($n_ok channels)" || { bad "D-009 policies (rc=$rc)"; grep '^DIFF' "$W/out" | head -5; }
rc=$(run --b "$(mutant r2-goldens)" --corpus "$REPO/tests/runtime2/semantics")
n_diff=$(grep -c '^DIFF' "$W/out")
[ "$rc" = 1 ] && pass "without policies the same changes are regressions ($n_diff channel diffs)" || bad "intended changes not flagged without policy"
unset RT_MUTANT_R2_DIR

# (f) corpus robustness --------------------------------------------------------
rc=$(run --b "$NIFT" --corpus "$W/does-not-exist")
[ "$rc" = 2 ] && pass "missing corpus directory fails" || bad "missing corpus accepted (rc=$rc)"
mkdir -p "$W/empty"
rc=$(run --b "$NIFT" --corpus "$W/empty")
[ "$rc" = 2 ] && pass "empty corpus directory fails" || bad "empty corpus accepted (rc=$rc)"
rc=$(run --b "$NIFT" --corpus "$W/pc" --min-entries 5)
[ "$rc" = 2 ] && pass "minimum entry count enforced" || bad "minimum entry count ignored"

# (g) reproducibility ------------------------------------------------------------
python3 "$REPO/tools/rt-diff/gen.py" --count 30 --seed 7 --out "$W/g1" >/dev/null
python3 "$REPO/tools/rt-diff/gen.py" --count 30 --seed 7 --out "$W/g2" >/dev/null
diff -r "$W/g1" "$W/g2" >/dev/null && pass "generator deterministic by seed" || bad "generator not deterministic"
head -1 "$W/g1/g00003/main.f" | grep -q 'seed 7 index 3' && pass "programs record seed and index" || bad "program lacks seed/index"
rc=$(run --b "$(mutant replace:true:TRUE)" --gen 12 --seed 7 --repro-dir "$W/repro" --repro-limit 1 --no-interleave)
if [ "$rc" = 1 ] && ls "$W"/repro/*.min.f >/dev/null 2>&1 && ls "$W"/repro/*.replay.sh >/dev/null 2>&1; then
  m="$(ls "$W"/repro/*.min.f | head -1)"; n="$(wc -l < "$m")"
  pass "differences minimised to a reproducer ($n lines) with a replay script"
else bad "no minimised reproducer (rc=$rc)"; fi

[ "$fail" -eq 0 ] && echo "PASS rt-diff self-tests" || echo "FAIL rt-diff self-tests"
exit $fail
