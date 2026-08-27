#!/usr/bin/env bash
# External non-destructive proof for tests/build_boundary.sh: the gate must
# perform NO writes in the caller's checkout. Uses a before/after filesystem
# state comparison rather than a sentinel planted by the gate itself.
#
# Covers:
#   - when .build/ is initially ABSENT, it remains absent;
#   - a pre-existing .build/boundary-sentinel with arbitrary contents remains
#     byte-identical;
#   - representative existing artifacts (libnift_c.a, the Node addon, an object
#     file) remain byte-identical;
#   - tracked and untracked caller state is unchanged before and after the gate.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

[ -e .git ] || { echo "FAIL: must run inside a Nift git checkout"; exit 1; }

snapshot() {
  # Git tracked/untracked state plus a hash of every file under the repo
  # (excluding .git). Sorted for stable comparison.
  git status --porcelain | sort
  find . -path ./.git -prune -o -type f -print | sort | while read -r f; do
    echo "$(md5sum "$f" | cut -d' ' -f1)  $f"
  done
}

# Case A: .build/ absent before -> absent after.
rm -rf .build
BEFORE_A="$(snapshot)"
bash tests/build_boundary.sh >/dev/null 2>&1 || { echo "FAIL: boundary gate failed (case A)"; exit 1; }
AFTER_A="$(snapshot)"
[ "$BEFORE_A" = "$AFTER_A" ] || { echo "FAIL: caller state changed when .build was absent"; exit 1; }
[ ! -d .build ] || { echo "FAIL: .build/ was created by the gate"; exit 1; }

# Case B: pre-existing sentinel + representative artifacts remain byte-identical.
mkdir -p .build
printf 'arbitrary-sentinel-content-123' > .build/boundary-sentinel
touch src/Parser.o                       # representative object artifact
make -s embed >/dev/null 2>&1            # representative native library
make -s node-binding >/dev/null 2>&1     # representative Node addon
BEFORE_B="$(snapshot)"
bash tests/build_boundary.sh >/dev/null 2>&1 || { echo "FAIL: boundary gate failed (case B)"; exit 1; }
AFTER_B="$(snapshot)"
[ "$BEFORE_B" = "$AFTER_B" ] || { echo "FAIL: caller state changed with artifacts present"; exit 1; }
[ -f .build/boundary-sentinel ] || { echo "FAIL: sentinel deleted"; exit 1; }
[ "$(cat .build/boundary-sentinel)" = "arbitrary-sentinel-content-123" ] || { echo "FAIL: sentinel altered"; exit 1; }
[ -f src/Parser.o ] || { echo "FAIL: object file deleted"; exit 1; }
[ -f libnift_c.a ] || { echo "FAIL: libnift_c.a deleted"; exit 1; }
[ -f bindings/node/build/nift_node.node ] || { echo "FAIL: Node addon deleted"; exit 1; }

# Clean up the test's own scenario artifacts.
rm -rf .build src/Parser.o

echo "PASS: build-boundary gate is non-destructive (caller state byte-identical before/after; absent .build stays absent)"