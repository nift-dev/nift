#!/usr/bin/env bash
# Tooling checkpoint (component 1): refuse to link objects of another
# configuration.  check-config.sh COMPILER [ARGS...] -- OBJECT...
# Every OBJECT's sidecar (OBJECT minus .o, plus .cfg, written by
# compile-object.sh) must hold the identity of COMPILER ARGS (config-id.sh).
# A missing sidecar or another identity means the object was built by another
# configuration (for example a concurrent make with other flags in the same
# object directory), so the link stops instead of mixing configurations.
set -u
. "$(dirname "$0")/config-id.sh"
tpl=()
while [ $# -gt 0 ] && [ "$1" != -- ]; do tpl+=("$1"); shift; done
[ "${1:-}" = -- ] || { echo "check-config.sh: usage: COMPILER ARGS... -- OBJECTS..." >&2; exit 2; }
shift
want=$(config_id "${tpl[@]}")
bad=0; first=""
for o in "$@"; do
  c="${o%.o}.cfg"
  if [ ! -f "$c" ] || [ "$(cat "$c")" != "$want" ]; then
    bad=$((bad + 1)); [ -n "$first" ] || first=$o
  fi
done
if [ "$bad" -ne 0 ]; then
  echo "check-config: $bad of $# objects were not built by this configuration (first: $first);" \
       "another configuration wrote them (rebuild, and do not run makes with different flags" \
       "on one object directory at once)" >&2
  exit 1
fi
