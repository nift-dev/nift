#!/usr/bin/env bash
# Negative control: the inventory without the shared module-host-op check
# (NIFT_INVENTORY_HOST_MUTANT) must make vm_inventory_links.py fail on exactly
# the module-callable case, while the real build passes the same suite.
set -uo pipefail
NIFT=$1
MUTANT=$2
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
if [ ! -x "$MUTANT" ]; then
    echo "FAIL inventory host mutant: mutant binary missing (harness failure)" >&2
    exit 1
fi
if ! python3 "$HERE/vm_inventory_links.py" --nift "$NIFT" >"$TMP/real.log" 2>&1; then
    echo "FAIL inventory host mutant: the unmodified build fails the suite" >&2
    cat "$TMP/real.log" >&2
    exit 1
fi
if python3 "$HERE/vm_inventory_links.py" --nift "$MUTANT" >"$TMP/mutant.log" 2>&1; then
    echo "FAIL inventory host mutant: suite passed (mutant not caught)" >&2
    cat "$TMP/mutant.log" >&2
    exit 1
fi
if grep -qE '^FAIL [a-z0-9-]+: harness:' "$TMP/mutant.log"; then
    echo "FAIL inventory host mutant: harness failure in the mutant run (not a detection)" >&2
    cat "$TMP/mutant.log" >&2
    exit 1
fi
failing=$(grep -E '^FAIL [a-z0-9-]+:' "$TMP/mutant.log" | sed -E 's/^FAIL ([a-z0-9-]+):.*/\1/' | sort -u | tr '\n' ' ')
if [ "$failing" != "module-callable " ]; then
    echo "FAIL inventory host mutant: expected only module-callable to fail, got: ${failing:-none}" >&2
    cat "$TMP/mutant.log" >&2
    exit 1
fi
echo "PASS inventory host mutant: unmodified build passes; mutant fails only module-callable (runner/inventory first-reason mismatch)"
