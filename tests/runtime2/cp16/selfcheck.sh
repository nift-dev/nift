#!/bin/sh
# CP16 fixture self-check: the comparison must accept the oracle binary and
# reject each seeded defect (mutant.py). Usage: selfcheck.sh NIFT
# Not wired into the Makefile (design fixtures); CP16 implementation moves the
# same matrix into the rt-diff self-test.
set -u
NIFT=${1:?usage: selfcheck.sh NIFT}
HERE=$(cd "$(dirname "$0")" && pwd)
fail=0
TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT
check() { # mode probe expect(OK|DIFF)
    # No pipeline (POSIX sh has no pipefail): capture.py's exit status is kept
    # and must agree with its verdict (0 = OK, 1 = DIFF); a crash is a failure.
    if [ "$1" = none ]; then python3 "$HERE/capture.py" --nift "$NIFT" --check --runs 1 --only "$2" > "$TMP/out" 2>&1
    else python3 "$HERE/capture.py" --nift "$NIFT" --check --runs 1 --only "$2" --mutant "$1" > "$TMP/out" 2>&1; fi
    rc=$?
    out=$(sed -n '$p' "$TMP/out")
    want_rc=0; [ "$3" = DIFF ] && want_rc=1
    case "$out" in
        "$3"*) if [ "$rc" = "$want_rc" ]; then echo "pass  $1 $2 -> $out"; else echo "FAIL  $1 $2 verdict $out but exit $rc"; fail=1; fi ;;
        *) echo "FAIL  $1 $2 expected $3 (exit $want_rc), got exit $rc: $out"; fail=1 ;;
    esac
}
for p in fs_read fs_meta fs_write fs_errors proc_run site_deps site_observe; do check none $p OK; done
check trace-path fs_read DIFF          # wrong path
check trace-path site_deps DIFF
check trace-res fs_meta DIFF           # wrong result class
check trace-drop:read fs_read DIFF     # missing read effect
check trace-drop:read site_deps DIFF
check trace-drop:spawn proc_run DIFF   # missing spawn
check trace-drop:save fs_write DIFF    # missing write effect
check trace-swap fs_write DIFF         # reordered effects
check code fs_errors DIFF              # wrong error code
check dep-drop site_deps DIFF          # missing dependency
check absent-drop site_observe DIFF    # missing absence observation
exit $fail
