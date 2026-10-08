#!/bin/bash
set -euo pipefail
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1
unset LD_PRELOAD
python3 .build/cp410/contracts.py .build/nift-sanitize-lifetime capture-sanitized
NIFT=.build/nift-sanitize-lifetime NIFT_BASELINE=.build/cp410/baseline/nift python3 tests/v49_selector_parity.py
bash tests/v44_root_path_reference_smoke.sh .build/nift-sanitize-lifetime
NIFT=.build/nift-sanitize-lifetime bash tests/v44_root_path_corruption_reproducers.sh
NIFT_BIN="$PWD/.build/nift-sanitize-lifetime" bash tests/v45_async.sh
