#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
NIFT=${NIFT_BIN:-${NIFT:-$ROOT/nift}}
case "$NIFT" in /*) ;; *) NIFT="$ROOT/$NIFT" ;; esac

# ---- 1. Batch 5 aggregate: CP10..CP13 gates (composed) ----
make test-v46-b5-cp13

# ---- 2. important package/runtime walls not guaranteed transitively ----
NIFT="$NIFT" bash tests/package_metadata_smoke.sh
NIFT="$NIFT" bash tests/package_refs_smoke.sh
NIFT="$NIFT" bash tests/package_hardening_smoke.sh
NIFT="$NIFT" bash tests/package_callable_closure_smoke.sh
NIFT="$NIFT" bash tests/package_module_export_smoke.sh
NIFT="$NIFT" bash tests/v44_relative_import_ownership_smoke.sh
NIFT="$NIFT" bash tests/v46_import_worker_ownership_smoke.sh
NIFT="$NIFT" bash tests/v46_b4_cp6_import_module_projection.sh
NIFT="$NIFT" bash tests/v46_b4_cp5b_import_source_recoverable.sh
NIFT="$NIFT" bash tests/v44_package_language_smoke.sh
(cd "$ROOT" && python3 tests/package_transaction_smoke.py >/dev/null 2>&1)
(cd "$ROOT" && python3 tests/v46_b5_cp10a_recovery_smoke.py >/dev/null 2>&1)

# ---- 3. final adversarial smoke (highest-risk scenarios, reused fixtures) ----
(cd "$ROOT" && python3 tests/v46_b5_cp12_graph_commands.py >/dev/null 2>&1)
(cd "$ROOT" && python3 tests/v46_b5_cp13_determinism.py >/dev/null 2>&1)
(cd "$ROOT" && python3 tests/v46_b5_cp13a_certification.py >/dev/null 2>&1)

# ---- 4. final reproducibility repeat (CP13 fixture) ----
(cd "$ROOT" && python3 tests/v46_b5_cp13_determinism.py >/dev/null 2>&1)

# ---- 5. recovery-epoch resource wall ----
timeout 900 make test-recovery-epoch >/dev/null 2>&1

echo 'v4.6 Batch 5 CP14 final certification: PASS'