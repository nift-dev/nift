#!/usr/bin/env bash
# Durable build-boundary gate (P7): plain `make` / `make nift` must build ONLY
# the reduced ordinary Nift CLI. It must not compile or link:
#   - src/embed/* (Engine, Context, c_abi implementation);
#   - libnift_c.a / libnift_c.so;
#   - any language binding.
#
# NON-DESTRUCTIVE: all work happens in a temporary CLEAN source tree exported
# from the committed HEAD via `git archive`. The caller's working tree is never
# modified (no `make clean`, no artifact removal, no embed rebuild). A sentinel
# is planted in the caller's tree before the run and verified unchanged after,
# proving the caller checkout is untouched.
#
# This test fails if a future source glob or target accidentally pulls
# embedding implementation into the CLI.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

[ -e .git ] || { echo "FAIL: must run inside a Nift git checkout"; exit 1; }

# Sentinel proving the caller's tree is untouched.
mkdir -p "$ROOT/.build"
SENTINEL="$ROOT/.build/boundary-sentinel"
printf 'boundary-sentinel' > "$SENTINEL"

TMP="$(mktemp -d /tmp/nift-boundary-XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# Export a CLEAN source tree (no ignored build artifacts) from committed HEAD.
git archive HEAD | tar -x -C "$TMP"
cd "$TMP"

make nift >/dev/null 2>&1 || { echo "FAIL: make nift did not succeed in the clean tree"; exit 1; }
[ -x nift ] || { echo "FAIL: make nift did not produce the CLI binary"; exit 1; }

# 1. No src/embed/ object files compiled.
if ls src/embed/*.o >/dev/null 2>&1; then
  echo "FAIL: src/embed/*.o present after plain make (embedding compiled into CLI build)"; exit 1
fi

# 2. No native embedding library built.
[ ! -f libnift_c.a ] || { echo "FAIL: libnift_c.a built by plain make"; exit 1; }
[ ! -f libnift_c.so ] || { echo "FAIL: libnift_c.so built by plain make"; exit 1; }

# 3. No binding artifacts built.
for p in bindings/go/embed-harness bindings/node/build/nift_node.node bindings/python/nift/_nift*.so; do
  if ls $p >/dev/null 2>&1; then echo "FAIL: $p built by plain make"; exit 1; fi
done

# 4. The CLI binary must not contain embedding entry points / engine symbols.
if nm nift 2>/dev/null | grep -qE "nift_engine_|nift_context_|_ZN5nift6Engine|_ZN5nift7Context"; then
  echo "FAIL: CLI binary contains embedding symbols"; exit 1
fi

# 5. The CLI still runs and reports a version.
./nift --version >/dev/null 2>&1 || { echo "FAIL: reduced CLI does not run"; exit 1; }

cd "$ROOT"
rm -rf "$TMP"
trap - EXIT

# 6. Prove the caller's checkout is untouched.
if [ ! -f "$SENTINEL" ] || [ "$(cat "$SENTINEL")" != "boundary-sentinel" ]; then
  echo "FAIL: build-boundary gate modified the caller's checkout"; exit 1
fi
rm -f "$SENTINEL"

echo "PASS: plain make builds only the reduced CLI (no src/embed objects, no libnift_c, no bindings); caller checkout untouched"