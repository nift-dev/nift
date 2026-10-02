#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
NIFT=${NIFT_BIN:-${NIFT:-$ROOT/nift}}
case "$NIFT" in /*) ;; *) NIFT="$ROOT/$NIFT" ;; esac

# ---- 1. graph lock representation unit test ----
make .build/package-graph-lock-unit
.build/package-graph-lock-unit

# ---- 2. existing package commands still emit v1 (no lockfileVersion, no packages) ----
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/pkg/src" "$TMP/site"
printf '{"name":"demo","version":"0.1.0","entry":"src/main.f"}\n' > "$TMP/pkg/manifest.json"
printf 'fn(demo_hello()) { return 42 }\nexport(demo_hello)\n' > "$TMP/pkg/src/main.f"
printf '{"dependencies":{}}\n' > "$TMP/site/manifest.json"
(cd "$TMP/site" && "$NIFT" add "$TMP/pkg" >/dev/null)
if grep -q '"lockfileVersion"' "$TMP/site/.nift/packages.lock.json"; then
    echo 'CP10 FAIL: add emitted a graph-shaped lock' >&2; exit 1
fi
grep -q '"commit": "local"' "$TMP/site/.nift/packages.lock.json" || {
    echo 'CP10 FAIL: add did not emit the v1 lock shape' >&2; exit 1
}

# ---- 3. read-only v1 does not rewrite ----
before=$(sha256sum "$TMP/site/.nift/packages.lock.json" | awk '{print $1}')
(cd "$TMP/site" && printf 'import("demo")\nprint(demo_hello())\n' > main.f && "$NIFT" main.f >/dev/null)
after=$(sha256sum "$TMP/site/.nift/packages.lock.json" | awk '{print $1}')
if [ "$before" != "$after" ]; then
    echo 'CP10 FAIL: a read-only operation rewrote the v1 lock' >&2; exit 1
fi

# ---- 4. existing v1 package transaction recovery remains green ----
(cd "$ROOT" && python3 tests/package_transaction_smoke.py >/dev/null 2>&1) || {
    echo 'CP10 FAIL: v1 package transaction recovery wall failed' >&2; exit 1
}

echo 'v4.6 Batch 5 CP10 lock graph: PASS'