#!/usr/bin/env bash
# N5 control: a failing CP21 fault check must fail the enclosing Make target.
set -euo pipefail
root=${1:?usage: cp21_make_failure_control.sh REPO_ROOT}
real_python=$(command -v python3)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0
for mode in --fault-delay --fault-cont --fault-handoff --fault-restore; do
    cat >"$tmp/python3" <<SHIM
#!/usr/bin/env bash
for arg in "\$@"; do
    if [ "\$arg" = "$mode" ]; then
        echo "REVIEW INJECTED $mode gate failure (exit 23)"
        exit 23
    fi
done
exec "$real_python" "\$@"
SHIM
    chmod +x "$tmp/python3"
    log="$tmp/${mode#--}.log"
    set +e
    PATH="$tmp:$PATH" make -C "$root" test-cp21-interactive-host >"$log" 2>&1
    rc=$?
    set -e
    if [ "$rc" -eq 0 ]; then
        echo "FAIL make control $mode: target exited 0"
        tail -20 "$log"
        fail=1
        continue
    fi
    if ! grep -q "REVIEW INJECTED $mode gate failure" "$log"; then
        echo "FAIL make control $mode: injected failure not observed"
        tail -20 "$log"
        fail=1
        continue
    fi
    if grep -q "PASS CP21 terminal lease" "$log"; then
        echo "FAIL make control $mode: target continued after failure"
        tail -20 "$log"
        fail=1
        continue
    fi
    echo "PASS make control $mode: target failed and stopped"
done
exit "$fail"
