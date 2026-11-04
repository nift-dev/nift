#!/usr/bin/env bash
# B2 (owner requirement B): a storage commit outside the mutation protocol is
# rejected by the compiler.
#   run.sh <cxx> <cppflags...>
# 1. absent.cpp MUST compile: static_asserts prove, by substitution failure and
#    independent of any diagnostic text, that the removed free commits do not
#    exist and that the Storage commits and the protocol's raw commit are not
#    accessible outside the protocol (with controls that the detectors fire).
# 2. Each bypass_*.cpp (a real call) must fail to compile, with an error that
#    names the bypassed function; the control must compile. Diagnostics are
#    produced under LC_ALL=C (g++ quotes names with typographic quotes in a
#    UTF-8 locale), and only the name is matched, not the wording.
set -u
export LC_ALL=C
cxx=$1; shift
dir=$(dirname "$0"); fail=0
flags=("$@")
if out=$("$cxx" "${flags[@]}" -fsyntax-only "$dir/absent.cpp" 2>&1); then
  echo "PASS compile-fail absent.cpp: no commit reachable outside the protocol (structural)"
else echo "FAIL compile-fail absent.cpp:"; printf '%s\n' "$out" | grep -m5 'error'; fail=1; fi
check() { local f=$1 name=$2
  if out=$("$cxx" "${flags[@]}" -fsyntax-only "$dir/$f" 2>&1); then
    echo "FAIL compile-fail $f: compiled (a bypass of the protocol is accepted)"; fail=1
  elif ! printf '%s\n' "$out" | grep 'error' | grep -q "$name"; then
    echo "FAIL compile-fail $f: rejected, but no error names $name:"; printf '%s\n' "$out" | head -5; fail=1
  else echo "PASS compile-fail $f: rejected ($(printf '%s\n' "$out" | grep -m1 'error' | sed 's/.*error: //'))"; fi
}
check bypass_storage.cpp array_set
check bypass_removed.cpp map_set
check bypass_commit.cpp commit_array
check bypass_sort.cpp sort_items
check bypass_sort_key_storage.cpp SortKey
check bypass_sort_key_items.cpp SortKey
check bypass_sort_key_commit_permutation.cpp SortKey
check bypass_sort_paste.cpp items
check bypass_sort_plain.cpp items
check bypass_sort_redefine.cpp sort_items
check bypass_sort_key_redefine.cpp SortKey
# Tooling checkpoint (component 4, RECHECK2 N3): byte-copy and copy-out of a forged key
check bypass_sort_key_bitcast.cpp 'not trivially copyable'
check bypass_sort_key_copy.cpp 'deleted function.*SortKey'
if out=$("$cxx" "${flags[@]}" -fsyntax-only "$dir/control.cpp" 2>&1); then echo "PASS compile-fail control compiles"
else echo "FAIL compile-fail control does not compile:"; printf '%s\n' "$out" | head -5; fail=1; fi
exit $fail
