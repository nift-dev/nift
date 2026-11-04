#!/usr/bin/env bash
# Tooling checkpoint (component 1): compile one object atomically.
#   compile-object.sh OUT -- COMPILER [FLAGS...] -c SOURCE
# The compiler writes a private temporary object and dependency file (-MMD -MP,
# target OUT); both are renamed into place only on success, dependency file
# first. Concurrent make processes in one tree (scripts/certify.py) may compile
# the same object at once: each rename is atomic, so a reader (another compile
# rule's link, or make's dependency scan) sees either the old complete file or
# the new complete file, never a partial one.
set -u
out=$1; shift
[ "${1:-}" = -- ] || { echo "compile-object.sh: usage: OUT -- COMPILER ARGS..." >&2; exit 2; }
shift
mkdir -p "$(dirname "$out")"
tmp="$out.tmp.$$"
dep="${out%.o}.d"
if ! "$@" -MMD -MP -MF "$tmp.d" -MT "$out" -o "$tmp"; then
  rm -f "$tmp" "$tmp.d"
  exit 1
fi
mv -f "$tmp.d" "$dep" && mv -f "$tmp" "$out"
