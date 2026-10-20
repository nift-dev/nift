#!/bin/sh
# FreeBSD CI: build Nift with GNU make and run the regression scripts that are
# portable to FreeBSD (those made portable in PR #1). Every test result is one
# of PASS, FAIL or NOT-VERIFIED (exit 77: the test could not observe what it
# guards on this host). NOT-VERIFIED is reported separately and is never
# counted as a pass; any FAIL, or a build failure, fails the job. Scripts in
# tests/freebsd_ci_excluded.txt are NOT RUN and listed with their reason.
#
# Runs as an unprivileged user: several suites deliberately refuse or behave
# differently as root (unreadable-file and permission checks), so a root run
# would not exercise them. The locale is UTF-8 because the regression scripts
# match UTF-8 text (a byte-wise C-locale bracket expression misreads it).
set -u
if [ "$(id -u)" = 0 ]; then echo "ci_freebsd.sh: run as an unprivileged user, not root"; exit 2; fi
LC_ALL=C.UTF-8; LANG=C.UTF-8; export LC_ALL LANG
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2
OUT="$ROOT/.build/freebsd-ci"; mkdir -p "$OUT"
SUMMARY="$OUT/summary.md"; : > "$SUMMARY"
JOBS=${NIFT_CI_JOBS:-2}

echo "### FreeBSD $(uname -r) $(uname -m)" >> "$SUMMARY"
c++ --version > "$OUT/compiler.txt" 2>&1 || true
echo "- compiler: $(sed -n 1p "$OUT/compiler.txt")" >> "$SUMMARY"
echo "- commit: $(git rev-parse HEAD 2>/dev/null || echo unknown)" >> "$SUMMARY"
echo "- user: $(id -un) (unprivileged), locale: $LC_ALL" >> "$SUMMARY"

start=$(date +%s)
if ! gmake -j"$JOBS" nift > "$OUT/build.log" 2>&1; then
  echo "- **build: FAIL**" >> "$SUMMARY"
  tail -40 "$OUT/build.log"
  exit 1
fi
echo "- build: PASS ($(( $(date +%s) - start ))s)" >> "$SUMMARY"
NIFT_BIN="$ROOT/nift"; export NIFT_BIN NIFT="$NIFT_BIN"

pass=0; fail=0; nv=0; failed=""; notverified=""
LIST="$ROOT/tests/freebsd_ci_scripts.txt"
[ -s "$LIST" ] || { echo "missing $LIST"; exit 2; }
while IFS= read -r t; do
  [ -n "$t" ] || continue
  [ "${t#\#}" = "$t" ] || continue
  log="$OUT/$(basename "$t").log"
  timeout 900 bash "$t" > "$log" 2>&1 < /dev/null; rc=$?
  case $rc in
    0) pass=$((pass + 1)) ;;
    77) nv=$((nv + 1)); notverified="$notverified $t" ;;
    *) fail=$((fail + 1)); failed="$failed $t(rc=$rc)"; echo "---- FAIL $t (rc=$rc)"; tail -20 "$log" ;;
  esac
done < "$LIST"
if timeout 900 gmake test-source-map > "$OUT/test-source-map.log" 2>&1; then pass=$((pass + 1))
else fail=$((fail + 1)); failed="$failed test-source-map"; tail -20 "$OUT/test-source-map.log"; fi

notrun=""; nr=0
while IFS= read -r line; do
  case $line in ''|'#'*) continue ;; esac
  nr=$((nr + 1)); notrun="$notrun
  - \`${line%%	*}\`: ${line#*	}"
done < "$ROOT/tests/freebsd_ci_excluded.txt"
{
  echo "- regression: PASS $pass, FAIL $fail, NOT-VERIFIED $nv"
  [ -n "$failed" ] && echo "- failed:$failed"
  [ -n "$notverified" ] && echo "- not verified on this host (exit 77, NOT a pass):$notverified"
  [ "$nr" -gt 0 ] && echo "- NOT RUN on FreeBSD ($nr, NOT a pass):$notrun"
  echo "- total time: $(( $(date +%s) - start ))s"
} >> "$SUMMARY"
cat "$SUMMARY"
[ "$fail" -eq 0 ]
