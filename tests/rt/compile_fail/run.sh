#!/usr/bin/env bash
# B2 (owner requirement B): a storage commit outside the mutation protocol is
# rejected by the compiler. Each bypass must fail to compile with an access /
# declaration error; the control must compile.
#   run.sh <cxx> <cppflags...>
set -u
cxx=$1; shift
dir=$(dirname "$0"); fail=0
flags=("$@")
check() { local f=$1 re=$2; shift 2
  if out=$("$cxx" "${flags[@]}" -fsyntax-only "$dir/$f" 2>&1); then
    echo "FAIL compile-fail $f: compiled (a bypass of the protocol is accepted)"; fail=1
  elif ! printf '%s' "$out" | grep -Eq "$re"; then
    echo "FAIL compile-fail $f: rejected for another reason:"; printf '%s\n' "$out" | head -5; fail=1
  else echo "PASS compile-fail $f: rejected ($(printf '%s' "$out" | grep -Eo "$re" | head -1))"; fi
}
check bypass_storage.cpp 'is private within this context'
check bypass_removed.cpp "'map_set' is not a member of 'nift::rt'|was not declared"
check bypass_commit.cpp 'is private within this context'
if out=$("$cxx" "${flags[@]}" -fsyntax-only "$dir/control.cpp" 2>&1); then echo "PASS compile-fail control compiles"
else echo "FAIL compile-fail control does not compile:"; printf '%s\n' "$out" | head -5; fail=1; fi
exit $fail
