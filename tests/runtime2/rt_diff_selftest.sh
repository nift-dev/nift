#!/usr/bin/env bash
# CP08 harness self-tests (review findings 1-10; owner CP08 requirements).
# Proves that rt-diff (a) catches a seeded difference on every channel, (b) its
# normalisations hide only what they document, (c) every traced Host-op
# category is exercised by the corpus, (d) tracing changes nothing but the
# trace, (e) approved-difference policies suppress only their exact
# difference, (f) missing/empty corpora fail, (g) generated programs are
# reproducible and differences are minimised, (h) timeouts are differences and
# leave no process behind. Round-2 review reproducers live in
# tests/runtime2/rt-diff-selftest-corpus.
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
REPO="$(pwd)"
RTDIFF="$REPO/tools/rt-diff/rt-diff"
MUTANT="$REPO/tests/runtime2/rt_diff_mutant.py"
CORPUS=(--corpus "$REPO/tests/runtime2/rt-diff-corpus" --corpus "$REPO/tests/runtime2/semantics")
SELF="$REPO/tests/runtime2/rt-diff-selftest-corpus"
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
# Round-2 review: page-info is compared byte for byte (no JSON re-serialisation,
# no dropped keys, no fingerprint masking); escapes through `..` are snapshotted;
# whole trace objects are compared; single-threaded page order is compared.
check_caught x-info-reformat 'files'
check_caught x-info-dupkey 'files'
check_caught x-info-escape 'files'
check_caught x-info-number 'files'
check_caught x-racy-inject 'files'
check_caught x-tick-inject 'files'
check_caught x-listing-garbage 'files'
check_caught x-escape-write 'files'
check_caught x-trace-extra-field 'trace'
check_caught x-trace-relabel 'trace'
check_caught x-trace-page-reverse 'ctx-order'
check_caught_self() { # mode channel-regex corpus-subdir
  local rc; rc=$(run --b "$(mutant "$1")" --corpus "$SELF/$3")
  if [ "$rc" = 1 ] && grep -E "^DIFF \S+ ($2)" "$W/out" >/dev/null; then pass "caught: $1 in $3 ($2)"
  else bad "missed: $1 in $3 (rc=$rc)"; tail -3 "$W/out"; fi
}
check_caught_self x-time-user 'stdout' channels
check_caught_self interleave 'combined' channels
grep -q '^DIFF channels/il2/main.f combined' "$W/out" && pass "combined order compared although a comment mentions async" || bad "combined skipped by text match"

# (b) normalisations hide only what they document ---------------------------
check_hidden() { # mode
  local rc; rc=$(run --b "$(mutant "$1")" "${CORPUS[@]}")
  if [ "$rc" = 0 ]; then pass "hidden by design: $1"; else bad "normalisation too narrow for $1"; grep '^DIFF' "$W/out" | head -3; fi
}
check_hidden identity
check_hidden time-number
rc=$(run --b "$NIFT" --corpus "$SELF/channels")
[ "$rc" = 0 ] && grep -q 'combined order unstable (not compared) in 0' "$W/out" && pass "combined order measured stable for the async probes" || bad "combined order unstable or differing (rc=$rc)"

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
# A command that emits trace events (round-2 review: `status` emitted none, so
# the refusal check passed vacuously): with the explicit allow switch the
# trace does reach stdout; without it, stdout is unchanged.
rm -rf "$W/s3" "$W/s4" "$W/s5"; cp -r "$SITE" "$W/s3"; cp -r "$SITE" "$W/s4"; cp -r "$SITE" "$W/s5"
allowed="$(cd "$W/s5" && NIFT_TRACE_HOST=1 NIFT_TRACE_HOST_ALLOW_STDIO=1 "$NIFT" build --all 2>/dev/null)"
case "$allowed" in *'"op":'*) pass "trace events reach fd 1 when explicitly allowed (check is not vacuous)";; *) bad "build --all emitted no trace events";; esac
out1="$(cd "$W/s3" && NIFT_TRACE_HOST=1 "$NIFT" build --all 2>&1 | sed 's/time taken: .*//')"
out2="$(cd "$W/s4" && "$NIFT" build --all 2>&1 | sed 's/time taken: .*//')"
[ "$out1" = "$out2" ] && pass "NIFT_TRACE_HOST=1 refused (build stdout unchanged)" || bad "trace written to stdout"
envout="$(cd "$W" && printf 'print(getenv("NIFT_TRACE_HOST"))\n' > env.f && NIFT_TRACE_HOST="" "$NIFT" env.f)"
[ "$envout" = "null" ] && pass "empty NIFT_TRACE_HOST removed from the environment" || bad "empty NIFT_TRACE_HOST visible: $envout"

# (e) policies suppress only their exact difference --------------------------
PC_HASH="$(python3 -c 'import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$SELF/pc/one/main.f")"
policy() { # old new count [extra json fields]
  local ch rules=""
  for ch in stdout combined; do
    rules="$rules${rules:+,}{\"id\":\"self-$ch\",\"decision\":\"D-009#6\",\"entry\":\"pc/one/main.f\",\"source_sha256\":\"$PC_HASH\",\"channel\":\"$ch\",\"old\":\"$1\",\"new\":\"$2\",\"count\":$3,\"justification\":\"self-test\",\"test\":\"tests/runtime2/rt_diff_selftest.sh\"${4:-}}"
  done
  printf '[%s]\n' "$rules" > "$W/policy.json"
}
policy item ITEM 2
rc=$(run --b "$(mutant replace:item:ITEM)" --corpus "$SELF/pc" --policy "$W/policy.json")
[ "$rc" = 0 ] && grep -q '^APPROVED pc/one/main.f stdout self' "$W/out" && pass "policy approves its exact substitution" || bad "policy did not approve"
rc=$(run --b "$(mutant replace:item:ITEMS)" --corpus "$SELF/pc" --policy "$W/policy.json")
[ "$rc" = 1 ] && pass "policy rejects a different substitution" || bad "policy over-approved a different change"
rc=$(run --b "$(mutant x-replace-plus)" --corpus "$SELF/pc" --policy "$W/policy.json")
[ "$rc" = 1 ] && pass "policy rejects its substitution plus an unrelated change" || bad "policy approved an extra change"
policy item ITEM 1
rc=$(run --b "$(mutant replace:item:ITEM)" --corpus "$SELF/pc" --policy "$W/policy.json")
[ "$rc" = 1 ] && pass "policy rejects a wrong occurrence count" || bad "policy ignored its count"
policy item ITEM 2
rc=$(run --b "$(mutant replace:item:ITEM)" --corpus "$SELF/other/pc" --policy "$W/policy.json")
[ "$rc" = 1 ] && grep -q '^UNUSED-POLICY' "$W/out" && pass "policy bound to its source: same entry id, different program not approved" || bad "policy approved another program with the same id"
rc=$(run --b "$NIFT" --corpus "$SELF/pc" --policy "$W/policy.json")
[ "$rc" = 1 ] && grep -q '^UNUSED-POLICY self-stdout' "$W/out" && pass "unused policies are failures" || bad "unused policy not reported (rc=$rc)"
for bad_rule in 'old=' 'channel=trace' 'decision=D9' 'test=no/such/file' 'count=0'; do
  key="${bad_rule%%=*}"; val="${bad_rule#*=}"
  python3 - "$W/policy.json" "$key" "$val" "$W/bad.json" <<'PY'
import json, sys
rules = json.load(open(sys.argv[1])); key, val = sys.argv[2], sys.argv[3]
rules[0][key] = int(val) if key == "count" else val
json.dump(rules, open(sys.argv[4], "w"))
PY
  rc=$(run --b "$NIFT" --corpus "$SELF/pc" --policy "$W/bad.json")
  [ "$rc" = 2 ] && pass "invalid policy refused: $bad_rule" || bad "invalid policy accepted: $bad_rule (rc=$rc)"
done
printf '[{"id":"wild","decision":"D-009#6","entry":"pc/*","source_sha256":"%s","channel":"stdout","old":"a","new":"b","count":1,"justification":"x","test":"tests/runtime2/rt_diff_selftest.sh"}]\n' "$PC_HASH" > "$W/wild.json"
rc=$(run --b "$NIFT" --corpus "$SELF/pc" --policy "$W/wild.json")
[ "$rc" = 2 ] && pass "pattern policies are refused" || bad "pattern policy accepted (rc=$rc)"

# D-009 policies: an engine emitting the intended (.r2) goldens is fully
# approved by tools/rt-diff/policies/d009.json and fully flagged without it.
export RT_MUTANT_R2_DIR="$REPO/tests/runtime2/semantics"
rc=$(run --b "$(mutant r2-goldens)" --corpus "$REPO/tests/runtime2/semantics" --policy "$REPO/tools/rt-diff/policies/d009.json")
n_ok=$(grep -c '^APPROVED' "$W/out")
[ "$rc" = 0 ] && [ "$n_ok" -ge 6 ] && ! grep -q '^UNUSED-POLICY' "$W/out" && pass "D-009 policies approve exactly the intended changes ($n_ok channels, none unused)" || { bad "D-009 policies (rc=$rc)"; grep '^DIFF' "$W/out" | head -5; }
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
rc=$(run --b "$NIFT" --corpus "$SELF/pc" --min-entries 5)
[ "$rc" = 2 ] && pass "minimum entry count enforced" || bad "minimum entry count ignored"
printf 'pc/* # no channels\n' > "$W/ign.txt"
rc=$(run --b "$NIFT" --corpus "$SELF/pc" --ignore "$W/ign.txt")
[ "$rc" = 2 ] && pass "exclusion without channels= refused" || bad "exclusion without channels accepted (rc=$rc)"
printf 'pc/one/main.f channels=stdout,combined # self-test\n' > "$W/ign.txt"
rc=$(run --b "$(mutant stdout-only)" --corpus "$SELF/pc" --ignore "$W/ign.txt")
[ "$rc" = 0 ] && pass "channel exclusion hides only its channels (stdout)" || bad "channel exclusion did not apply"
rc=$(run --b "$(mutant x-escape-write)" --corpus "$SELF/pc" --ignore "$W/ign.txt")
[ "$rc" = 1 ] && pass "channel exclusion leaves other channels compared (files)" || bad "channel exclusion hid other channels"

# (g) reproducibility ------------------------------------------------------------
python3 "$REPO/tools/rt-diff/gen.py" --count 30 --seed 7 --out "$W/g1" >/dev/null
python3 "$REPO/tools/rt-diff/gen.py" --count 30 --seed 7 --out "$W/g2" >/dev/null
diff -r "$W/g1" "$W/g2" >/dev/null && pass "generator deterministic by seed" || bad "generator not deterministic"
head -1 "$W/g1/g00003/main.f" | grep -q 'seed 7 index 3' && pass "programs record seed and index" || bad "program lacks seed/index"
# The minimiser copies the whole entry directory (round-2 review: a program
# importing a module minimised to an unreproducible file).
rc=$(run --b "$(mutant replace:2101:2102)" --corpus "$SELF/gen" --repro-dir "$W/repro" --no-interleave)
if [ "$rc" = 1 ] && grep -q '^REPRO gen/m1/main.f .*verified' "$W/out"; then
  d="$(ls -d "$W"/repro/*/ | head -1)"; n="$(wc -l < "$d/main.f")"
  replay="$(sh "$d/replay.sh" 2>&1)"
  case "$replay" in *2101*2102*) pass "module program minimised to $n lines with its module; replay reproduces";; *) bad "replay does not reproduce: $replay";; esac
else bad "no verified reproducer for a module program (rc=$rc)"; grep -E 'REPRO' "$W/out"; fi
rc=$(run --b "$(mutant replace:true:TRUE)" --gen 12 --seed 7 --repro-dir "$W/repro2" --repro-limit 1 --no-interleave)
[ "$rc" = 1 ] && grep -q '^REPRO .*verified' "$W/out" && pass "generated difference minimised and verified" || bad "generated difference not minimised (rc=$rc)"

# (h) timeouts ----------------------------------------------------------------
count_procs() { ps -eo args | grep -cxF -e "$NIFT main.f" -e "sleep 25" || true; }
before="$(count_procs)"
rc=$(run --b "$NIFT" --corpus "$SELF/timeout" --timeout 3)
[ "$rc" = 1 ] && [ "$(grep -c '^DIFF .* timeout' "$W/out")" = 2 ] && pass "timeouts on both sides are differences" || bad "timeouts not reported (rc=$rc)"
sleep 1
after="$(count_procs)"
[ "$after" -le "$before" ] && pass "no process outlives a timed-out entry" || bad "processes left behind: $before before, $after after"

[ "$fail" -eq 0 ] && echo "PASS rt-diff self-tests" || echo "FAIL rt-diff self-tests"
exit $fail
