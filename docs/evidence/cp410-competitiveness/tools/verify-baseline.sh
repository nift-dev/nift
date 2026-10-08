#!/bin/bash
set -euo pipefail
b=.build/cp410/baseline
run(){ echo "START $1" >> "$b/progress.log"; name=$1; shift; "$@" > "$b/$name.log" 2>&1; echo "PASS $name" >> "$b/progress.log"; }
run native-bindings make -j4 test embed test-bindings
run warnings make test-warnings
run binding-warnings make test-binding-warnings
run nrs bash -c 'cd /home/nick/Repositories/nift/nift-regression-suite; NIFT_BIN=/home/nick/Repositories/nift/nift/nift NIFT_EXPECT_VERSION=4.10.0 NIFT_EMBED_PREFIX=/home/nick/Repositories/nift/nift/dist/embed-prefix bash run-contract.sh'
run prs bash -c 'cd /home/nick/Repositories/nift/packages-regression-suite; NIFT_BIN=/home/nick/Repositories/nift/nift/nift bash run.sh'
echo COMPLETE >> "$b/progress.log"
