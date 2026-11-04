#!/usr/bin/env bash
# Tooling checkpoint (component 1): compile one object atomically.
#   compile-object.sh [--production] OUT -- COMPILER [FLAGS...] -c SOURCE
# The compiler writes a private temporary object and dependency file (-MMD -MP,
# target OUT); on success the configuration sidecar OUT.cfg (config-id.sh: the
# compiler identity and these exact flags), the dependency file and the object
# are renamed into place, object last. Concurrent make processes in one tree
# (scripts/certify.py) may compile the same object at once: each rename is
# atomic, so a reader sees either the old complete file or the new one, never
# a partial one; the sidecar lets a link refuse objects of another
# configuration (check-config.sh).
# --production (component 3): a production translation unit must not depend on
# anything in the repository's top-level tests/ tree. After the compile, every
# dependency the compiler recorded (the complete include closure, resolved by
# the compiler, so no spelling, macro or relative path hides one) is resolved
# against the repository root; one under tests/ fails the build.
# Tooling review F6: production dependencies are recorded with -MD (every
# header), not -MMD, so a header marked `#pragma GCC system_header` (and what
# it includes) is in the list and in make's dependencies.
# Tooling review F4, re-review N3, N4, N6: after the compile,
# check-production-tu.py parses the dependency list with make's escapes,
# refuses a dependency in tests/ or outside the repository and the compiler's
# system include directories, and reads every repository dependency as the
# preprocessor does (BOM, splices with LF, CR LF or lone CR, %:): an #undef of
# the production marker NIFT_PRODUCTION_TU, or a #define/#undef of a test-only
# switch (NIFT_*MUTANT*, *TEST*, *PROBE*, *HOOK*, *BYPASS*) other than the
# reviewed defaults, fails the object. Before it, the command line may neither
# undefine the marker nor set a test-only switch other than the -D names of its
# own rt-obj configuration (check-production-tu.py documents both sets).
set -u
production=0
if [ "${1:-}" = --production ]; then production=1; shift; fi
out=$1; shift
[ "${1:-}" = -- ] || { echo "compile-object.sh: usage: OUT -- COMPILER ARGS..." >&2; exit 2; }
shift
. "$(dirname "$0")/config-id.sh"
mkdir -p "$(dirname "$out")"
tmp="$out.tmp.$$"
base="${out%.o}"
id=$(config_id "$@")
depflag=-MMD
[ "$production" = 1 ] && depflag=-MD
if [ "$production" = 1 ]; then
  # the command line: no undefinition of the marker, no test-only switch other
  # than its own rt-obj configuration's (check-production-tu.py --args)
  python3 "$(dirname "$0")/check-production-tu.py" --args "$out" "$@" || exit 1
fi
if ! "$@" $depflag -MP -MF "$tmp.d" -MT "$out" -o "$tmp"; then
  rm -f "$tmp" "$tmp.d"
  exit 1
fi
if [ "$production" = 1 ]; then
  # re-review N4, N6: the dependency list is parsed with make's escapes and
  # each file read as the compiler reads it (check-production-tu.py)
  src=; prev=
  for arg in "$@"; do [ "$prev" = -c ] && src=$arg; prev=$arg; done
  lang=c++; case "$src" in *.c) lang=c ;; esac
  if ! python3 "$(dirname "$0")/check-production-tu.py" "$out" "$tmp.d" "$1" "$lang"; then
    rm -f "$tmp" "$tmp.d"
    exit 1
  fi
fi
# Tooling review F7: the sidecar also holds the sha256 of the object it was
# written with, so a sidecar and an object from two compiles (the renames below
# are atomic one by one, not as a pair) never pass check-config.sh as a pair.
printf '%s %s\n' "$id" "$(sha256sum < "$tmp" | cut -d' ' -f1)" > "$tmp.cfg" &&
  mv -f "$tmp.cfg" "$base.cfg" && mv -f "$tmp.d" "$base.d" && mv -f "$tmp" "$out"
