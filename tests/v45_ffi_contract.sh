#!/usr/bin/env bash
set -euo pipefail
mkdir -p .build
cc -std=c99 -Wall -Wextra -Werror -fPIC -shared tests/ffi/fixture.c -o .build/libnift_ffi_fixture.so
nm -D .build/libnift_ffi_fixture.so | grep -q nift_ffi_add_i64
nm -D .build/libnift_ffi_fixture.so | grep -q nift_ffi_call_cb
echo 'v4.5 ffi contract fixture: PASS'
