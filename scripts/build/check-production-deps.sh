#!/usr/bin/env bash
# Tooling checkpoint (component 3): no production translation unit depends on
# a test path.  check-production-deps.sh OUTDIR [MAKE-TARGET...]
# Takes every production compile command from `make -n -B` of the production
# artefacts (nift, libnift_c.so, the sanitizer and TSan nift builds), runs each
# compile command with -M (the compiler's own complete dependency list, system
# headers included) and fails if any dependency resolves into the repository's
# tests/ tree. The compiler resolves includes, so no spelling, macro or
# relative path can hide one.
set -u -o pipefail
out=${1:?usage: check-production-deps.sh OUTDIR [TARGET...]}; shift
targets=${*:-nift libnift_c.so .build/nift-sanitize .build/nift-sanitize-lifetime .build/nift-tsan}
mkdir -p "$out"
root=$(pwd -P)
make -n -B $targets 2>/dev/null | grep '^bash scripts/build/compile-object.sh ' > "$out/commands.txt"
n=$(wc -l < "$out/commands.txt")
[ "$n" -gt 0 ] || { echo "FAIL production-deps: no production compile command found"; exit 1; }
# one command per line: everything after " -- " is the compiler invocation
run_one() {
  local line=$1 cmd
  cmd=${line#* -- }
  # shellcheck disable=SC2086
  if ! deps=$(eval "$cmd -M -MF /dev/stdout" 2>/dev/null); then echo "ERROR $cmd"; return; fi
  printf '%s\n' "$deps" | sed -e 's/\\$//' -e 's/^[^ ]*: *//' | tr ' ' '\n' | grep -v '^$' | while read -r f; do
    r=$(realpath -m --relative-to="$root" "$f")
    case "$r" in tests/*) echo "TESTDEP ${cmd##* -c } -> $r";; esac
  done
}
export -f run_one; export root
tr '\n' '\0' < "$out/commands.txt" | xargs -0 -P "$(nproc)" -I{} bash -c 'run_one "$1"' _ {} > "$out/result.txt"
errors=$(grep -c '^ERROR' "$out/result.txt"); bad=$(grep -c '^TESTDEP' "$out/result.txt")
if [ "$errors" -ne 0 ]; then echo "FAIL production-deps: $errors compile commands failed under -M ($out/result.txt)"; exit 1; fi
if [ "$bad" -ne 0 ]; then
  echo "FAIL production-deps: $bad dependencies of production translation units are test paths:"; grep '^TESTDEP' "$out/result.txt" | head; exit 1
fi
echo "PASS production-deps: $n production compile commands, no dependency in tests/ (-M, system headers included)"
