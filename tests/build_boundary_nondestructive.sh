#!/usr/bin/env bash
# Non-destructive proof for tests/build_boundary.sh (P7): the boundary gate must
# perform NO writes in the checkout under test. All scenarios run in DISPOSABLE
# clones (temporary directories), never in the original caller checkout.
#
# The ORIGINAL caller checkout is snapshotted before and after the whole proof
# (git tracked/untracked state plus a hash of every file, including ignored
# build artifacts) and must be byte-identical - so running
# `make test-build-boundary-nondestructive` is itself provably non-destructive.
#
# Scenarios:
#   A - .build/ absent before -> remains absent; full state unchanged.
#   B - a pre-existing arbitrary .build/boundary-sentinel plus representative
#       artifacts (libnift_c.a, the Node addon, an object file) remain
#       byte-identical after the gate.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
[ -e .git ] || { echo "FAIL: must run inside a Nift git checkout"; exit 1; }

# A certifier may write this target's own log inside the checkout
# (.build/certify/<target>.log) while the proof runs. Exclude exactly that one
# file, never a directory or pattern; every other file, ignored or not, counts.
OWN_LOG=""
if [ -n "${NIFT_CERTIFY_TARGET_LOG:-}" ] && [ -f "$NIFT_CERTIFY_TARGET_LOG" ]; then
  own_abs="$(cd "$(dirname "$NIFT_CERTIFY_TARGET_LOG")" && pwd -P)/$(basename "$NIFT_CERTIFY_TARGET_LOG")"
  case "$own_abs" in "$(pwd -P)"/*) OWN_LOG="./${own_abs#"$(pwd -P)"/}";; esac
fi

snapshot() {
  git status --porcelain | sort
  find . -path ./.git -prune -o -type f -print | sort | while read -r f; do
    [ -n "$OWN_LOG" ] && [ "$f" = "$OWN_LOG" ] && continue
    echo "$(md5sum "$f" | cut -d' ' -f1)  $f"
  done
}

# Exclusion self-check (negative control): exactly one file is excluded and a
# change to any other ignored file is still detected. It runs before every
# certified proof that uses the exclusion, and alone with --selftest.
exclusion_selftest() {
  b="$(snapshot)"; echo "own-log line" >> "$OWN_LOG"
  [ "$b" = "$(snapshot)" ] || { echo "FAIL selftest: own log change was not excluded"; exit 1; }
  other="$(dirname "$OWN_LOG")/selftest-other-$$.log"; : > "$other"
  [ "$b" != "$(snapshot)" ] || { rm -f "$other"; echo "FAIL selftest: a new sibling ignored file was not detected"; exit 1; }
  rm -f "$other"
  [ "$b" = "$(snapshot)" ] || { echo "FAIL selftest: snapshot not stable"; exit 1; }
  echo "PASS nondestructive snapshot selftest: own log excluded; sibling ignored file detected"
}
if [ "${1:-}" = "--selftest" ]; then
  [ -n "$OWN_LOG" ] || { echo "FAIL selftest: NIFT_CERTIFY_TARGET_LOG must name a file inside the checkout"; exit 1; }
  exclusion_selftest; exit 0
fi
[ -z "$OWN_LOG" ] || exclusion_selftest

CALLER_BEFORE="$(snapshot)"

WORK="$(mktemp -d /tmp/nift-bnd-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

# Case A: disposable checkout with .build absent.
git clone -q "$ROOT" "$WORK/caseA"
(
  cd "$WORK/caseA"
  [ ! -d .build ] || { echo "FAIL: clone unexpectedly has .build"; exit 1; }
  BEFORE_A="$(snapshot)"
  bash tests/build_boundary.sh >/dev/null 2>&1 || { echo "FAIL: boundary gate failed (case A)"; exit 1; }
  AFTER_A="$(snapshot)"
  [ "$BEFORE_A" = "$AFTER_A" ] || { echo "FAIL: case A state changed"; exit 1; }
  [ ! -d .build ] || { echo "FAIL: .build/ created by the gate"; exit 1; }
)

# Case B: separate disposable checkout with sentinel + representative artifacts.
git clone -q "$ROOT" "$WORK/caseB"
(
  cd "$WORK/caseB"
  mkdir -p .build
  printf 'arbitrary-sentinel-content-123' > .build/boundary-sentinel
  touch src/Parser.o
  make -s embed >/dev/null 2>&1 || { echo "FAIL: could not build representative embed artifact"; exit 1; }
  make -s node-binding >/dev/null 2>&1 || { echo "FAIL: could not build representative node artifact"; exit 1; }
  BEFORE_B="$(snapshot)"
  bash tests/build_boundary.sh >/dev/null 2>&1 || { echo "FAIL: boundary gate failed (case B)"; exit 1; }
  AFTER_B="$(snapshot)"
  [ "$BEFORE_B" = "$AFTER_B" ] || { echo "FAIL: case B state changed"; exit 1; }
  [ -f .build/boundary-sentinel ] || { echo "FAIL: sentinel deleted"; exit 1; }
  [ "$(cat .build/boundary-sentinel)" = "arbitrary-sentinel-content-123" ] || { echo "FAIL: sentinel altered"; exit 1; }
  [ -f src/Parser.o ] && [ -f libnift_c.a ] && [ -f bindings/node/build/nift_node.node ] \
    || { echo "FAIL: representative artifact missing after the gate"; exit 1; }
)

CALLER_AFTER="$(snapshot)"
[ "$CALLER_BEFORE" = "$CALLER_AFTER" ] || {
  echo "FAIL: the non-destructive proof modified the original caller checkout"; exit 1; }

echo "PASS: boundary gate is non-destructive; proof ran in disposable checkouts and the original checkout is byte-identical before/after (incl. ignored artifacts)"