#!/bin/bash
set -euo pipefail
b=../safety
mkdir -p "$b"
echo "RESUME with tracing permission" >> "$b/progress.log"
run(){ echo "START $1" >> "$b/progress.log"; name=$1; shift; "$@" > "$b/$name.log" 2>&1; echo "PASS $name" >> "$b/progress.log"; }
run native-bindings make -j4 test embed test-bindings
run warnings make test-warnings
run binding-warnings make test-binding-warnings
run lifetime make -j2 test-sanitize-lifetime
run sanitized-filesystem env NIFT="$PWD/.build/nift-sanitize-lifetime" python3 tests/v410_filesystem_recipe_parity.py
run sanitized-prefix env NIFT="$PWD/.build/nift-sanitize-lifetime" python3 tests/v410_glob_prefix_parity.py
run sanitized-glob env NIFT="$PWD/.build/nift-sanitize-lifetime" python3 tests/v410_glob_parity.py
run deep-sanitizer make -j2 test-sanitize
echo COMPLETE >> "$b/progress.log"
