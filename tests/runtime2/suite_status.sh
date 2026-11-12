#!/bin/bash
# C140c (D-083 #2, #3): run each test-rt-vm suite, keep its complete log, record its status, and summarise, so that a
# suite that ran and failed, one that never ran, and the known defect R1 are each visible and never look like a pass.
#
#   suite_status.sh reset STATUS LOGDIR             start a run: empty STATUS, recreate LOGDIR
#   suite_status.sh run STATUS LOGDIR NAME -- CMD   run CMD (output to stdout and LOGDIR/NAME.log), append
#                                                   "NAME RC" to STATUS, exit RC
#   suite_status.sh summary STATUS NAME...          one line per expected suite (PASSED, FAILED rc=N, NOT-RUN,
#                                                   or FAILED (known, outstanding) for async-memory exit 3), the
#                                                   counts, and the exit status: 0 every suite passed; 3 every
#                                                   suite passed except async-memory, which reported the known
#                                                   defect R1 (exit 3); 1 anything else (a failure or a NOT-RUN)
set -u -o pipefail
mode=$1; shift
case "$mode" in
reset)
    : > "$1"; rm -rf "$2"; mkdir -p "$2" ;;
run)
    status=$1 logdir=$2 name=$3; shift 4  # NAME --
    mkdir -p "$logdir"
    echo "SUITE-START $name"
    "$@" 2>&1 | tee "$logdir/$name.log"
    rc=${PIPESTATUS[0]}
    echo "$name $rc" >> "$status"
    echo "SUITE-END $name rc=$rc"
    exit "$rc" ;;
summary)
    status=$1; shift
    ran=0 passed=0 failed=0 notrun=0 known=0
    echo "test-rt-vm suites (expected ${#}):"
    for name in "$@"; do
        rc=$(awk -v n="$name" '$1 == n { r = $2 } END { if (r != "") print r }' "$status" 2>/dev/null)
        if [ -z "$rc" ]; then echo "  NOT-RUN $name"; notrun=$((notrun + 1)); continue; fi
        ran=$((ran + 1))
        if [ "$rc" = 0 ]; then echo "  PASSED $name"; passed=$((passed + 1))
        elif [ "$name" = async-memory ] && [ "$rc" = 3 ]; then
            echo "  FAILED (known, outstanding) $name: KNOWN DEFECT R1 (P1, D-082), exit 3"; known=1
        else echo "  FAILED $name rc=$rc"; echo "FAIL test-rt-vm suite $name: rc=$rc"; failed=$((failed + 1)); fi
    done
    for name in $(awk '{ print $1 }' "$status" 2>/dev/null | sort -u); do
        case " $* " in *" $name "*) ;; *) echo "FAIL test-rt-vm suite $name: ran but is not in the expected list"; failed=$((failed + 1)) ;; esac
    done
    echo "test-rt-vm suites: ${#} expected, $ran ran, $passed passed, $failed failed, $notrun not run$([ $known = 1 ] && echo ', known defect R1 outstanding (FAILED)')"
    [ $notrun -gt 0 ] && echo "FAIL test-rt-vm: $notrun suite(s) did not run"
    if [ $failed -gt 0 ] || [ $notrun -gt 0 ]; then exit 1; fi
    [ $known = 1 ] && exit 3
    exit 0 ;;
*) echo "suite_status.sh: unknown mode $mode" >&2; exit 2 ;;
esac
