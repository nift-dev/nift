#!/usr/bin/env bash
# Review of C67-C71, S-1 (combined-b3-b5.md section 62): a permanent negative control that the
# binary a harness uses as "legacy" really is the legacy engine and the VM binary really is the
# VM. A probe whose output differs between the engines (B5: a callee's push is visible to the
# caller only on the VM) must give legacy's output on LEGACY and the VM's on VM; if both agree,
# a harness comparing them would compare one engine with itself.
#   legacy_engine_check.sh LEGACY_BINARY VM_BINARY
set -u -o pipefail
legacy=$1 vm=$2
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
printf 'fn(add9(xs)) { xs.push(99); return 0 }\na := [1, 2]\nadd9(a)\nprint(a.stringify())\n' > "$work/probe.f"
l=$(cd "$work" && env -i PATH="$PATH" HOME="$work" "$legacy" probe.f 2>&1); lrc=$?
v=$(cd "$work" && env -i PATH="$PATH" HOME="$work" NIFT_VM=1 "$vm" probe.f 2>&1); vrc=$?
if [ "$lrc" -ne 0 ] || [ "$vrc" -ne 0 ]; then
  echo "FAIL legacy/VM engine check: probe failed (legacy exit $lrc: $l; VM exit $vrc: $v)"; exit 1
fi
if [ "$l" = "$v" ]; then
  echo "FAIL legacy/VM engine check: both binaries print '$l'; the 'legacy' binary is not the legacy engine"; exit 1
fi
if [ "$l" != "[1,2]" ] || [ "$v" != "[1,2,99]" ]; then
  echo "FAIL legacy/VM engine check: expected legacy [1,2] and VM [1,2,99], got '$l' and '$v'"; exit 1
fi
echo "PASS legacy/VM engine check: $legacy is legacy ([1,2]), $vm is the VM ([1,2,99])"
