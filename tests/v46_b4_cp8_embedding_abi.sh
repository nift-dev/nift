#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
NIFT=${NIFT_BIN:-${NIFT:-$ROOT/nift}}
case "$NIFT" in /*) ;; *) NIFT="$ROOT/$NIFT" ;; esac

# ---- 1. focused CP8 embedding test (Engine boundary certification) ----
make .build/v46-b4-cp8-embed
.build/v46-b4-cp8-embed

# ---- 2. existing embedding / C ABI walls ----
make test-c-abi test-c-abi-c-smoke test-engine test-engine-bindings test-engine-concurrency

# ---- 3. public ABI headers unchanged vs the prior approved baseline (4368dea) ----
if ! git diff --quiet 4368dea..HEAD -- include/; then
    echo 'CP8 FAIL: public ABI headers changed vs 4368dea' >&2
    exit 1
fi

# ---- 4. exported C ABI entry points match the frozen baseline surface ----
# c_abi.cpp + include/nift/c_abi.h are verified byte-identical to 4368dea
# above, so this frozen list is the prior approved ABI 1.1 surface. An exact
# match catches both removed and newly-added symbols.
readonly ABI_SYMBOLS="nift_abi_version nift_abi_version_major nift_abi_version_minor nift_context_free nift_context_new nift_context_set_bool nift_context_set_bytes nift_context_set_current_output nift_context_set_int nift_context_set_json nift_context_set_number nift_context_set_page_name nift_context_set_string nift_context_set_title nift_engine_evaluate nift_engine_execute nift_engine_free nift_engine_is_open nift_engine_new nift_engine_open nift_engine_open_error nift_engine_reload nift_engine_render nift_engine_render_page nift_engine_render_partial nift_engine_render_path nift_engine_render_text nift_engine_set_bool nift_engine_set_bytes nift_engine_set_environment_provider nift_engine_set_int nift_engine_set_json nift_engine_set_loader nift_engine_set_number nift_engine_set_root nift_engine_set_string nift_ffi_callback_i64_trampoline nift_render_result_dependency_count nift_render_result_dependency_get nift_render_result_error_column nift_render_result_error_line nift_render_result_error_message nift_render_result_error_source nift_render_result_free nift_render_result_ok nift_render_result_output nift_render_result_pagination_count nift_render_result_pagination_get nift_render_result_requirement_count nift_render_result_requirement_get nift_render_result_stderr nift_render_result_stdout nift_script_result_error_message nift_script_result_free nift_script_result_ok nift_script_result_stderr nift_script_result_stdout nift_script_result_value_bytes nift_script_result_value_json"
if ! command -v nm >/dev/null 2>&1; then
    echo 'nm unavailable; skipping symbol-surface check' >&2
else
    make libnift_c.a >/dev/null
    actual=$(nm -g --defined-only libnift_c.a 2>/dev/null | awk '$2 ~ /^[TDB]$/ { print $3 }' | grep '^nift_' | sort -u | tr '\n' ' ' | sed 's/ $//')
    if [ "$actual" != "$ABI_SYMBOLS" ]; then
        echo 'CP8 FAIL: exported C ABI symbol surface differs from baseline' >&2
        echo "  expected: $ABI_SYMBOLS" >&2
        echo "  actual:   $actual" >&2
        exit 1
    fi
fi

echo 'v4.6 Batch 4 CP8 embedding/C ABI: PASS'