#!/usr/bin/env bash
# Tooling checkpoint (component 1): the configuration signature of an object
# set.  write-signature.sh FILE COMPILER [ARGS...] [--- COMPILER [ARGS...]]...
# FILE holds each compile template's identity (config-id.sh: the compiler's
# resolved path, a hash of its full --version output, the exact flags). It is
# rewritten only when its content changes, so an unchanged configuration never
# invalidates the objects, while any change of compiler, compiler version,
# flags or defines makes FILE newer than every object (each object depends on
# it) and forces a rebuild. Each process writes its own temporary file.
set -u
file=$1; shift
. "$(dirname "$0")/config-id.sh"
mkdir -p "$(dirname "$file")"
t="$file.tmp.$$"
: > "$t"
while [ $# -gt 0 ]; do
  part=()
  while [ $# -gt 0 ] && [ "$1" != --- ]; do part+=("$1"); shift; done
  [ $# -gt 0 ] && shift
  [ ${#part[@]} -gt 0 ] && config_text "${part[@]}" >> "$t"
done
if cmp -s "$file" "$t"; then rm -f "$t"; else mv -f "$t" "$file"; fi
