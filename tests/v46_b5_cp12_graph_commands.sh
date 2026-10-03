#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
NIFT=${NIFT_BIN:-${NIFT:-$ROOT/nift}}
case "$NIFT" in /*) ;; *) NIFT="$ROOT/$NIFT" ;; esac

# ---- 1. CP10/CP11 representation + resolver units + recovery ----
make .build/package-graph-lock-unit
.build/package-graph-lock-unit
make .build/package-graph-resolver-unit
.build/package-graph-resolver-unit
(cd "$ROOT" && python3 tests/v46_b5_cp10a_recovery_smoke.py >/dev/null 2>&1)

# ---- 2. real graph package commands E2E (local + git transitive, conflicts,
#        targeted/full update, orphan cleanup, v1->v2 migration, offline) ----
(cd "$ROOT" && python3 tests/v46_b5_cp12_graph_commands.py >/dev/null 2>&1)

# ---- 3. existing package/recovery walls stay green under the v2 lock format ----
(cd "$ROOT" && python3 tests/package_transaction_smoke.py >/dev/null 2>&1)
NIFT="$NIFT" bash tests/package_metadata_smoke.sh
NIFT="$NIFT" bash tests/package_refs_smoke.sh
NIFT="$NIFT" bash tests/package_hardening_smoke.sh
NIFT="$NIFT" bash tests/package_callable_closure_smoke.sh
NIFT="$NIFT" bash tests/v46_b4_cp5b_import_source_recoverable.sh
NIFT="$NIFT" bash tests/v44_relative_import_ownership_smoke.sh

echo 'v4.6 Batch 5 CP12 graph commands: PASS'