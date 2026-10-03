#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

# ---- 1. resolver unit test (compute-only, fake provider) ----
make .build/package-graph-resolver-unit
.build/package-graph-resolver-unit

# ---- 2. CP10 representation + recovery compatibility stays green ----
make .build/package-graph-lock-unit
.build/package-graph-lock-unit
(cd "$ROOT" && python3 tests/v46_b5_cp10a_recovery_smoke.py >/dev/null 2>&1)

# ---- 3. no command wiring: a real add still emits a v1 lock ----
NIFT=${NIFT_BIN:-${NIFT:-$ROOT/nift}}
case "$NIFT" in /*) ;; *) NIFT="$ROOT/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/pkg/src" "$TMP/site"
printf '{"name":"demo","version":"0.1.0","entry":"src/main.f"}\n' > "$TMP/pkg/manifest.json"
printf 'fn(demo_hello()) { return 42 }\nexport(demo_hello)\n' > "$TMP/pkg/src/main.f"
printf '{"dependencies":{}}\n' > "$TMP/site/manifest.json"
(cd "$TMP/site" && "$NIFT" add "$TMP/pkg" >/dev/null)
grep -q '"lockfileVersion": 2' "$TMP/site/.nift/packages.lock.json" || {
    echo 'CP11 FAIL: a command did not emit a v2 graph lock' >&2; exit 1
}

echo 'v4.6 Batch 5 CP11 resolver: PASS'