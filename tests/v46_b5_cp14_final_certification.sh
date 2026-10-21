#!/usr/bin/env bash
set -euo pipefail

if [ -n "${PYTHON:-}" ]; then
  read -r -a PYTHON_CMD <<< "$PYTHON"
elif command -v python3 >/dev/null 2>&1; then
  PYTHON_CMD=(python3)
elif command -v python >/dev/null 2>&1; then
  PYTHON_CMD=(python)
elif command -v py >/dev/null 2>&1; then
  PYTHON_CMD=(py -3)
else
  PYTHON_CMD=()
  for candidate in /usr/local/bin/python3.[0-9]*; do
    if [ -x "$candidate" ] && [ -f "$candidate" ]; then
      PYTHON_CMD=("$candidate")
      break
    fi
  done
  if [ "${#PYTHON_CMD[@]}" -eq 0 ]; then
    printf '%s\n' "error: Python 3 is required to run this script" >&2
    exit 1
  fi
fi

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
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/package_transaction_smoke.py >/dev/null 2>&1)
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/v46_b5_cp10a_recovery_smoke.py >/dev/null 2>&1)

# ---- 3. final adversarial smoke (highest-risk scenarios, reused fixtures) ----
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/v46_b5_cp12_graph_commands.py >/dev/null 2>&1)
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/v46_b5_cp13_determinism.py >/dev/null 2>&1)
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/v46_b5_cp13a_certification.py >/dev/null 2>&1)

# ---- 4. final reproducibility repeat (CP13 fixture) ----
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/v46_b5_cp13_determinism.py >/dev/null 2>&1)

# ---- 5. recovery-epoch resource wall ----
timeout 900 make test-recovery-epoch >/dev/null 2>&1

echo 'v4.6 Batch 5 CP14 final certification: PASS'
