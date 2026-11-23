#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C.UTF-8 LANG=C.UTF-8 PYTHONDONTWRITEBYTECODE=1
export NIFT_CERTIFICATION_REQUIRE_HISTORY=1
out="$PWD/.build/freebsd-ci"
mkdir -p "$out/bin"
for pair in make:gmake timeout:gtimeout; do
    ln -sf "$(command -v "${pair#*:}")" "$out/bin/${pair%%:*}"
done
export PATH="$out/bin:$PATH"
result=0
gtimeout --kill-after=30s 18m gmake -k test-runtime2-certification-guard \
    2>&1 | tee "$out/guard.log" || result=$?
printf '%s\n' "$result" > "$out/guest-status.txt"
