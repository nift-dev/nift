#!/usr/bin/env bash
# CP03 simplification gates: unrestricted default paths, retained --fs-root
# restriction and the deterministic no-op syscall guard.
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
HERE="$(cd "$(dirname "$0")" && pwd)"
fail=0
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

NIFT_BIN="$NIFT" bash "$HERE/fs_freedom.sh" >"$T/fs.log" 2>&1 || { cat "$T/fs.log"; fail=1; }
NIFT_BIN="$NIFT" bash "$HERE/file_copy_move_freedom.sh" >"$T/copy.log" 2>&1 || { cat "$T/copy.log"; fail=1; }
bash "$HERE/../v44_cp27_restricted_smoke.sh" "$NIFT" >"$T/restricted.log" 2>&1 || { cat "$T/restricted.log"; fail=1; }
python3 "$HERE/../v46_resource_paths.py" "$NIFT" >"$T/resources.log" 2>&1 || { cat "$T/resources.log"; fail=1; }
NIFT_BIN="$NIFT" bash "$HERE/../path_safety_smoke.sh" >"$T/path-safety.log" 2>&1 || { cat "$T/path-safety.log"; fail=1; }
python3 "$HERE/cp03_restricted.py" "$NIFT" >"$T/restricted-probes.log" 2>&1 || { cat "$T/restricted-probes.log"; fail=1; }

unmeasured=0
if ! command -v strace >/dev/null 2>&1; then
  unmeasured=1
else
  bash "$HERE/cp01/syscall_harness_negative.sh" >"$T/syscall-controls.log" 2>&1; rc=$?
  if [ "$rc" -eq 77 ]; then unmeasured=1; elif [ "$rc" -ne 0 ]; then cat "$T/syscall-controls.log"; fail=1; fi
  if [ "$unmeasured" -eq 0 ]; then
    bash "$HERE/cp01/syscalls.sh" 200 10 cp03="$NIFT" >"$T/sys200.log" 2>&1; rc=$?
    if [ "$rc" -eq 77 ]; then unmeasured=1; elif [ "$rc" -ne 0 ]; then fail=1; fi
    bash "$HERE/cp01/syscalls.sh" 20 20 cp03="$NIFT" >"$T/sys20.log" 2>&1; rc=$?
    if [ "$rc" -eq 77 ]; then unmeasured=1; elif [ "$rc" -ne 0 ]; then fail=1; fi
  fi
  if [ "$unmeasured" -eq 0 ]; then
    for spec in "sys200.log 5944" "sys20.log 919"; do
      set -- $spec; file=$1; limit=$2
      line=$(grep ' noop total=' "$T/$file" | head -1)
      [ -n "$line" ] || { echo "FAIL CP03 syscalls: no no-op line in $file"; fail=1; continue; }
      total=$(sed -n 's/.* total=\([0-9]*\).*/\1/p' <<<"$line")
      [ "$total" -le "$limit" ] || { echo "FAIL CP03 syscalls: $file total=$total > $limit"; fail=1; }
      grep -q 'readlink=0' <<<"$line" || { echo "FAIL CP03 syscalls: $file readlink is not zero"; fail=1; }
      grep -q 'path.clock=0' <<<"$line" || { echo "FAIL CP03 syscalls: $file touched .nift/clock"; fail=1; }
    done
  fi
fi
if [ "$fail" -ne 0 ]; then exit 1; fi
if [ "$unmeasured" -ne 0 ]; then
  echo "SKIP CP03 syscall measurements: tracer unavailable; performance unmeasured"
  exit 77
fi
echo "PASS CP03 simplification"
