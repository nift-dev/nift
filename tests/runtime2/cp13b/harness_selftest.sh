#!/usr/bin/env bash
# CP13B R1 harness self-test.
#
#   harness_selftest.sh ./nift
#
# H1/H2/H4: run.py --selftest proves genuine-detection, policy-id and
#           harness-failure separation (including a non-required case).
# H3:       run.py --trace-selftest proves the fd-based Host trace captures a
#           real legacy Host read.
# H1 on the unimplemented binary: a tampering wrapper and a no-statistics
#           wrapper must both leave mutant 6 PENDING, never PASS.
# --oracle: mandatory in mutant mode.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
nift="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
work="$(mktemp -d "${TMPDIR:-/tmp}/cp13b-harness-XXXXXX")"
trap 'rm -rf "$work"' EXIT

cat >"$work/tamper.py" <<'PY'
#!/usr/bin/env python3
import os, subprocess, sys
real = "__REAL_NIFT__"
p = subprocess.run([real] + sys.argv[1:], capture_output=True)
sys.stdout.buffer.write(p.stdout)
sys.stderr.buffer.write(p.stderr)
if os.environ.get("NIFT_VM_STATS") and os.environ.get("NIFT_VM") not in (None, "", "0"):
    sys.stdout.buffer.write(b"tampered by the mutant stub\n")
sys.stdout.flush()
sys.exit(p.returncode)
PY

cat >"$work/nostats.py" <<'PY'
#!/usr/bin/env python3
import os, subprocess, sys
real = "__REAL_NIFT__"
env = dict(os.environ)
if env.get("NIFT_VM") not in (None, "", "0"):
    env.pop("NIFT_VM_STATS", None)
p = subprocess.run([real] + sys.argv[1:], env=env)
sys.exit(p.returncode)
PY

chmod +x "$work/tamper.py" "$work/nostats.py"
sed -i "s|__REAL_NIFT__|$nift|g" "$work/tamper.py" "$work/nostats.py"

echo "== H1/H2/H4: unit self-test =="
python3 "$here/run.py" --nift "$nift" --selftest

echo "== H3: fd-based Host trace positive control =="
python3 "$here/run.py" --nift "$nift" --trace-selftest

echo "== H1: a tampering wrapper must not make mutant 6 PASS on the unimplemented binary =="
set +e
tamper_out="$(python3 "$here/run.py" --nift "$work/tamper.py" --oracle "$nift" --mutant 6 2>&1)"
tamper_status=$?
set -e
if [ "$tamper_status" -eq 0 ]; then
  echo "FAIL harness self-test: the unimplemented binary PASSED mutant 6 with a tamper wrapper" >&2
  echo "$tamper_out" >&2
  exit 1
fi
if ! grep -q "PENDING-PHASE-B" <<<"$tamper_out"; then
  echo "FAIL harness self-test: tamper run was not classified PENDING-PHASE-B" >&2
  echo "$tamper_out" >&2
  exit 1
fi

echo "== H1/H4: a no-statistics wrapper must not make mutant 6 PASS =="
set +e
nostats_out="$(python3 "$here/run.py" --nift "$work/nostats.py" --oracle "$nift" --mutant 6 2>&1)"
nostats_status=$?
set -e
if [ "$nostats_status" -eq 0 ]; then
  echo "FAIL harness self-test: the unimplemented binary PASSED mutant 6 without statistics" >&2
  echo "$nostats_out" >&2
  exit 1
fi
if ! grep -q "PENDING-PHASE-B" <<<"$nostats_out"; then
  echo "FAIL harness self-test: no-statistics run was not classified PENDING/harness" >&2
  echo "$nostats_out" >&2
  exit 1
fi

echo "== end-to-end adversarial mutation harness =="
python3 "$here/adversarial/selftest.py"

echo "== isolated mutant build selftest =="
python3 "$here/adversarial/mutant_build_selftest.py"

echo "== mutant mode requires --oracle =="
set +e
python3 "$here/run.py" --nift "$nift" --mutant 6 >/dev/null 2>&1
oracle_status=$?
set -e
if [ "$oracle_status" -ne 2 ]; then
  echo "FAIL harness self-test: mutant mode without --oracle exited $oracle_status, expected 2" >&2
  exit 1
fi

echo "PASS harness self-test: genuine detections, policy mapping, trace, fallback/harness separation"
