#!/usr/bin/env bash
# B4 control: the syscall fixture/layout must not change its counts when the
# certifier supplies a deeper private TMPDIR. The helper uses a fixed-depth
# fixture root and reports it explicitly.
set -u -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NIFT="${NIFT_BIN:?set NIFT_BIN}"
T="$(mktemp -d /tmp/nift-cp01-depth.XXXXXX)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/a/b/c"
counts() { # tmpdir, out
  TMPDIR="$1" bash "$HERE/syscalls.sh" 200 10 depth="$NIFT" >"$2" 2>&1
}
counts /tmp "$T/shallow.log"
counts "$T/a/b/c" "$T/deep.log"
sline=$(grep ' noop total=' "$T/shallow.log" | head -1)
dline=$(grep ' noop total=' "$T/deep.log" | head -1)
[ -n "$sline" ] && [ -n "$dline" ] || { echo "FAIL depth control: missing no-op measurement"; exit 1; }
st=$(sed -n 's/.* total=\([0-9]*\).*/\1/p' <<<"$sline")
dt=$(sed -n 's/.* total=\([0-9]*\).*/\1/p' <<<"$dline")
sr=$(sed -n 's/.* readlink=\([0-9]*\).*/\1/p' <<<"$sline")
dr=$(sed -n 's/.* readlink=\([0-9]*\).*/\1/p' <<<"$dline")
[ "$st" = "$dt" ] && [ "$sr" = "$dr" ] || { echo "FAIL depth control: shallow ($st/$sr) != nested ($dt/$dr)"; exit 1; }
grep -q 'syscall fixture root: /tmp/nift-cp01-sys\.' "$T/shallow.log" || { echo "FAIL depth control: fixture location not reported"; exit 1; }
# The aggregate measurement path must also pass under a nested TMPDIR.
TMPDIR="$T/a/b/c" NIFT_BIN="$NIFT" bash "$HERE/../cp01_simplify.sh" --syscalls-only >"$T/aggregate.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then echo "FAIL depth control: nested aggregate rc=$rc"; tail -20 "$T/aggregate.log"; exit 1; fi
echo "PASS syscall fixture-depth control: total=$st readlink=$sr stable under nested TMPDIR"
