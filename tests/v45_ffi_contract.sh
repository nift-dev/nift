#!/usr/bin/env bash
set -euo pipefail
mkdir -p .build
FIXTURE="$(bash tests/ffi/build_fixture.sh .build)"
nm -g "$FIXTURE" | grep -q nift_ffi_add_i64
nm -g "$FIXTURE" | grep -q nift_ffi_call_cb
echo 'v4.5 ffi contract fixture: PASS'
