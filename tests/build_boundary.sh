#!/usr/bin/env bash
# Durable build-boundary gate (P7): plain `make` / `make nift` must build ONLY
# the reduced ordinary Nift CLI. It must not compile or link:
#   - src/embed/* (Engine, Context, c_abi implementation);
#   - libnift_c.a / libnift_c.so;
#   - any language binding.
# This test fails if a future source glob or target accidentally pulls
# embedding implementation into the CLI, so the boundary is enforced by CI
# rather than by the current object list.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

make clean >/dev/null 2>&1
# Prove `make nift` does not CREATE these, independent of what `make clean`
# removes (e.g. a prior `make embed`).
rm -f libnift_c.a libnift_c.so bindings/go/embed-harness
rm -rf bindings/node/build bindings/python/build
rm -f bindings/python/nift/_nift*.so
make nift >/dev/null 2>&1

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

echo "PASS: plain make builds only the reduced CLI (no src/embed objects, no libnift_c, no bindings)"