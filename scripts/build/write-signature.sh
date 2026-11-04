#!/usr/bin/env bash
# Tooling checkpoint (component 1): the configuration signature of an object
# set.  write-signature.sh FILE COMPILER [ARGS...] [--- COMPILER [ARGS...]]... [@@@ FILE ...]
# FILE holds each compile template's identity (config-id.sh: the compiler's
# resolved path, a hash of its full --version output, the exact flags). It is
# rewritten only when its content changes, so an unchanged configuration never
# invalidates the objects, while any change of compiler, compiler version,
# flags or defines makes FILE newer than every object (each object depends on
# it) and forces a rebuild. Each process writes its own temporary file.
set -u
# Several signatures in one call (one $(shell) in the Makefile at parse time,
# Linode item 1): FILE ARGS... [@@@ FILE ARGS...]...
. "$(dirname "$0")/config-id.sh"
write_one() {
  local file=$1; shift
  local t
  if [ -n "${NIFT_SIGNATURE_CHECK_ONLY:-}" ]; then
    t=$(mktemp) || return 1
  else
    mkdir -p "$(dirname "$file")" || return 1
    t="$file.tmp.$$"
    : > "$t" || return 1
  fi
  while [ $# -gt 0 ]; do
    local part=()
    while [ $# -gt 0 ] && [ "$1" != --- ]; do part+=("$1"); shift; done
    [ $# -gt 0 ] && shift
    if [ ${#part[@]} -gt 0 ]; then config_text "${part[@]}" >> "$t" || { rm -f "$t"; return 1; }; fi
  done
  if cmp -s "$file" "$t"; then rm -f "$t"
  elif [ -n "${NIFT_SIGNATURE_CHECK_ONLY:-}" ]; then rm -f "$t"; printf '%s\n' "$file"   # would change
  else mv -f "$t" "$file" || return 1; fi
}
# Every group is written (or, with NIFT_SIGNATURE_CHECK_ONLY, checked: the
# files whose content would change are printed, nothing is written: make -n and
# make -q); the exit status is non-zero if any group fails (re-check N3).
rc=0
while [ $# -gt 0 ]; do
  group=()
  while [ $# -gt 0 ] && [ "$1" != @@@ ]; do group+=("$1"); shift; done
  [ $# -gt 0 ] && shift
  if [ ${#group[@]} -gt 0 ]; then write_one "${group[@]}" || rc=1; fi
done
exit $rc
