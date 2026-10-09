#!/bin/bash
set -euo pipefail
b=../safety
mkdir -p "$b"
: > "$b/progress.log"
run(){ echo "START $1" >> "$b/progress.log"; name=$1; shift; "$@" > "$b/$name.log" 2>&1; echo "PASS $name" >> "$b/progress.log"; }
run focused make test-v410-glob-prefix-parity test-v410-glob-prefix-guard test-v410-glob-parity test-v410-glob-relative-guard test-v49-glob-order test-v49-glob-key-guard test-v410-callback-overlay-parity test-v410-callback-overlay-guard test-diagnostics
run native-bindings make -j4 test embed test-bindings
run warnings make test-warnings
run binding-warnings make test-binding-warnings
run lifetime make -j2 test-sanitize-lifetime
run sanitized-prefix env NIFT="$PWD/.build/nift-sanitize-lifetime" python3 tests/v410_glob_prefix_parity.py
run sanitized-glob env NIFT="$PWD/.build/nift-sanitize-lifetime" python3 tests/v410_glob_parity.py
run deep-sanitizer make -j2 test-sanitize
echo COMPLETE >> "$b/progress.log"
