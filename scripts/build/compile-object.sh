#!/usr/bin/env bash
# Tooling checkpoint (component 1): compile one object atomically.
#   compile-object.sh OUT -- COMPILER [FLAGS...] -c SOURCE
# The compiler writes a private temporary object and dependency file (-MMD -MP,
# target OUT); on success the configuration sidecar OUT.cfg (config-id.sh: the
# compiler identity and these exact flags), the dependency file and the object
# are renamed into place, object last. Concurrent make processes in one tree
# (scripts/certify.py) may compile the same object at once: each rename is
# atomic, so a reader sees either the old complete file or the new one, never
# a partial one; the sidecar lets a link refuse objects of another
# configuration (check-config.sh).
set -u
out=$1; shift
[ "${1:-}" = -- ] || { echo "compile-object.sh: usage: OUT -- COMPILER ARGS..." >&2; exit 2; }
shift
. "$(dirname "$0")/config-id.sh"
mkdir -p "$(dirname "$out")"
tmp="$out.tmp.$$"
base="${out%.o}"
id=$(config_id "$@")
if ! "$@" -MMD -MP -MF "$tmp.d" -MT "$out" -o "$tmp"; then
  rm -f "$tmp" "$tmp.d"
  exit 1
fi
printf '%s\n' "$id" > "$tmp.cfg" && mv -f "$tmp.cfg" "$base.cfg" && mv -f "$tmp.d" "$base.d" && mv -f "$tmp" "$out"
