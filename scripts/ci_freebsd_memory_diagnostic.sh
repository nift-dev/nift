#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C.UTF-8 LANG=C.UTF-8 NIFT_BIN="$PWD/nift"
out="$PWD/.build/freebsd-ci"
mkdir -p "$out/bin"
ln -sf "$(command -v gmake)" "$out/bin/make"
export PATH="$out/bin:$PATH"
{
    uname -a
    c++ --version
    sysctl kern.clockrate
    git show -s --format='commit %H tree %T'
} | tee "$out/summary.md"
memory=0
python3 -u tests/runtime2/freebsd_memory_diagnostic.py 2>&1 | tee "$out/memory.txt" || memory=$?
host=0
python3 -u tests/runtime2/host_deps.py 2>&1 | tee "$out/host-deps.txt" || host=$?
printf '%s %s\n' "$memory" "$host" > "$out/guest-status.txt"
