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

# ---- 1. graph query unit (path enumeration, determinism, validation) ----
make .build/package-graph-query-unit
.build/package-graph-query-unit

# ---- 2. determinism / reproducibility / no-op stability / failure immutability ----
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/v46_b5_cp13_determinism.py >/dev/null 2>&1)
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/v46_b5_cp13a_certification.py >/dev/null 2>&1)

# ---- 3. compose CP10/CP11/CP12 gates (representation, resolver, commands) ----
make .build/package-graph-lock-unit
.build/package-graph-lock-unit
make .build/package-graph-resolver-unit
.build/package-graph-resolver-unit
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/v46_b5_cp10a_recovery_smoke.py >/dev/null 2>&1)
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/v46_b5_cp12_graph_commands.py >/dev/null 2>&1)
(cd "$ROOT" && "${PYTHON_CMD[@]}" tests/package_transaction_smoke.py >/dev/null 2>&1)

# ---- 4. query command sanity: works from manifest + lock without the store ----
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/b/src" "$TMP/a/src" "$TMP/site"
printf '{"name":"b","version":"0.1.0","entry":"src/main.f"}\n' > "$TMP/b/manifest.json"
printf 'v_b := "bb"\nexport(v_b)\n' > "$TMP/b/src/main.f"
printf '{"name":"a","version":"0.1.0","entry":"src/main.f","dependencies":{"b":{"source":"%s","ref":"local"}}}\n' "$TMP/b" > "$TMP/a/manifest.json"
printf 'v_a := "aa"\nexport(v_a)\n' > "$TMP/a/src/main.f"
printf '{"dependencies":{}}\n' > "$TMP/site/manifest.json"
(cd "$TMP/site" && "$NIFT" add "$TMP/a" >/dev/null 2>&1)
(cd "$TMP/site" && "$NIFT" packages 2>&1 | grep -q 'resolved source') || {
  echo 'CP13 FAIL: packages query failed' >&2; exit 1
}
(cd "$TMP/site" && rm -rf .nift/packages && "$NIFT" packages b 2>&1 | grep -q 'required by') || {
  echo 'CP13 FAIL: packages query must work without the installed store' >&2; exit 1
}

echo 'v4.6 Batch 5 CP13 determinism/graph-query: PASS'
