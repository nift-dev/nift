#!/bin/bash
# C140c (D-083 #2, #3): run each test-rt-vm suite, keep its complete log, record its status, and summarise, so that a
# suite that ran and failed and one that never ran are each visible and never look like a pass. C141 (D-084 L2): every
# run has an identity (HEAD, tree, nift sha256, start time, nonce); results are accepted only from a completed run of
# the current identity, so a status file or log left by an earlier run is never evidence (`verify`).
#
#   suite_status.sh invalidate STATUS LOGDIR        delete STATUS and LOGDIR (test-rt-vm's first prerequisite: a
#                                                   later prerequisite that fails leaves no earlier evidence behind)
#   suite_status.sh reset STATUS LOGDIR NIFT        start a run: STATUS holds only the new identity line, LOGDIR is
#                                                   recreated (old logs and SUMMARY are deleted)
#   suite_status.sh run STATUS LOGDIR NAME -- CMD   run CMD (output to stdout and LOGDIR/NAME.log), append
#                                                   "NAME RC" to STATUS, exit RC
#   suite_status.sh summary STATUS LOGDIR NAME...   one line per expected suite (PASSED, FAILED rc=N, NOT-RUN), the
#                                                   counts, and the exit status: 0 every suite passed, 1 anything else
#                                                   (a failure, a NOT-RUN, or a STATUS of another identity); then
#                                                   writes LOGDIR/SUMMARY (the identity and that status), last
#   suite_status.sh verify STATUS LOGDIR            exit 0 only for a completed (SUMMARY), passed run of the current
#                                                   identity; 1 for a failed, incomplete, stale or foreign one
set -u -o pipefail
identity() {  # NIFT: the identity of the checkout and binary now (the start time and nonce are added at reset)
    local head tree dirty sha
    head=$(git rev-parse HEAD 2>/dev/null || echo none); tree=$(git rev-parse 'HEAD^{tree}' 2>/dev/null || echo none)
    dirty=$(git status --porcelain 2>/dev/null | wc -l); sha=$(sha256sum "$1" 2>/dev/null | cut -c1-64)
    echo "head=$head tree=$tree dirty=$dirty nift=$1 sha256=${sha:-missing}"
}
current() {  # STATUS: true when its identity line names the current checkout and binary
    local line recorded nift
    line=$(head -n 1 "$1" 2>/dev/null)
    case "$line" in "#identity "*" time="*) ;; *) return 1 ;; esac
    recorded=${line#\#identity }; recorded=${recorded% time=*}
    nift=$(sed -n 's/.* nift=\([^ ]*\) .*/\1/p' <<<"$recorded")
    [ "$(identity "$nift")" = "$recorded" ]
}
mode=$1; shift
case "$mode" in
invalidate)
    rm -f "$1"; rm -rf "$2" ;;
reset)
    rm -rf "$2"; mkdir -p "$2"
    echo "#identity $(identity "$3") time=$(date -u +%Y-%m-%dT%H:%M:%SZ) nonce=$$-$RANDOM" > "$1"
    sed 's/^#//' "$1" ;;
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
    status=$1 logdir=$2; shift 2
    ran=0 passed=0 failed=0 notrun=0
    echo "test-rt-vm suites (expected ${#}):"
    for name in "$@"; do
        rc=$(awk -v n="$name" '$1 == n { r = $2 } END { if (r != "") print r }' "$status" 2>/dev/null)
        if [ -z "$rc" ]; then echo "  NOT-RUN $name"; notrun=$((notrun + 1)); continue; fi
        ran=$((ran + 1))
        if [ "$rc" = 0 ]; then echo "  PASSED $name"; passed=$((passed + 1))
        else echo "  FAILED $name rc=$rc"; echo "FAIL test-rt-vm suite $name: rc=$rc"; failed=$((failed + 1)); fi
    done
    for name in $(awk '!/^#/ { print $1 }' "$status" 2>/dev/null | sort -u); do
        case " $* " in *" $name "*) ;; *) echo "FAIL test-rt-vm suite $name: ran but is not in the expected list"; failed=$((failed + 1)) ;; esac
    done
    echo "test-rt-vm suites: ${#} expected, $ran ran, $passed passed, $failed failed, $notrun not run"
    [ $notrun -gt 0 ] && echo "FAIL test-rt-vm: $notrun suite(s) did not run"
    rc=0
    if [ $failed -gt 0 ] || [ $notrun -gt 0 ]; then rc=1; fi
    if ! current "$status"; then echo "FAIL test-rt-vm: $status is not this run's (identity missing or changed)"; rc=1; fi
    echo "test-rt-vm run: $(head -n 1 "$status" 2>/dev/null | sed 's/^#//')"
    { head -n 1 "$status"; echo "summary rc=$rc"; } > "$logdir/SUMMARY"
    exit $rc ;;
verify)
    status=$1 logdir=$2
    if ! current "$status"; then echo "FAIL test-rt-vm evidence: $status is stale or foreign (identity missing or not the current checkout and binary)"; exit 1; fi
    if [ ! -f "$logdir/SUMMARY" ] || [ "$(head -n 1 "$logdir/SUMMARY")" != "$(head -n 1 "$status")" ]; then
        echo "FAIL test-rt-vm evidence: the run $(head -n 1 "$status" | sed 's/.* time=//') did not complete (no SUMMARY of it)"; exit 1
    fi
    if [ "$(sed -n 2p "$logdir/SUMMARY")" != "summary rc=0" ]; then echo "FAIL test-rt-vm evidence: the run failed ($(sed -n 2p "$logdir/SUMMARY"))"; exit 1; fi
    echo "VERIFIED test-rt-vm evidence: $(head -n 1 "$status" | sed 's/^#//')" ;;
*) echo "suite_status.sh: unknown mode $mode" >&2; exit 2 ;;
esac
