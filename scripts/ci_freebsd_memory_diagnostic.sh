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
for suite in vm_subset vm_host_memory; do
    python3 -u "tests/runtime2/$suite.py" --nift "$NIFT_BIN" 2>&1 | tee "$out/$suite.txt" || memory=$?
done
host=0
python3 -u tests/runtime2/host_deps.py 2>&1 | tee "$out/host-deps.txt" || host=$?
for suite in file_copy_move_freedom build_write_ownership; do
    bash "tests/runtime2/$suite.sh" 2>&1 | tee "$out/$suite.txt" || host=$?
done
printf '%s %s\n' "$memory" "$host" > "$out/guest-status.txt"
