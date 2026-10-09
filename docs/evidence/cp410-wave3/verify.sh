#!/bin/bash
set -euo pipefail
b=.build/cp410-wave3/safety
mkdir -p "$b"
: > "$b/progress.log"
run(){ echo "START $1" >> "$b/progress.log"; name=$1; shift; "$@" > "$b/$name.log" 2>&1; echo "PASS $name" >> "$b/progress.log"; }
run focused make test-v410-string-replace-parity test-v410-native-dispatch-parity test-v410-glob-parity test-v410-glob-relative-guard test-v49-glob-order test-v49-glob-key-guard test-diagnostics test-v410-sort-factory-parity test-v410-object-member-parity test-v410-object-member-unit test-v410-object-member-guard test-v48-callable-parity test-v48-callback-parity test-v410-allocation-guard
run native-bindings make -j4 test embed test-bindings
run warnings make test-warnings
run binding-warnings make test-binding-warnings
run nrs bash -c 'cd /home/nick/Repositories/nift/nift-regression-suite; NIFT_BIN=/home/nick/Repositories/nift/nift/nift NIFT_EXPECT_VERSION=4.10.0 NIFT_EMBED_PREFIX=/home/nick/Repositories/nift/nift/dist/embed-prefix bash run-contract.sh'
run prs bash -c 'cd /home/nick/Repositories/nift/packages-regression-suite; NIFT_BIN=/home/nick/Repositories/nift/nift/nift bash run.sh'
run lifetime make -j2 test-sanitize-lifetime
run extra-safety bash .build/cp410/extra-capture-safety.sh
run sanitized-object python3 tests/v410_object_member_parity.py --nift .build/nift-sanitize-lifetime
run sanitized-sort python3 tests/v410_sort_factory_parity.py --nift .build/nift-sanitize-lifetime
run core-memory env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 python3 scripts/checkpoint3_core_memory.py --nift "$PWD/.build/nift-sanitize-lifetime" --rounds 4 --output "$b/core-memory.json"
run deep-sanitizer make -j2 test-sanitize
run parser-fuzz make checkpoint-9-parser-fuzz
cp .build/checkpoint-9/parser-fuzz.json "$b/parser-fuzz.json"
echo COMPLETE >> "$b/progress.log"
