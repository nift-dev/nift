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
# Tooling review F4: the production marker (src/build/production_tu.h) is a
# macro, so a unit could undefine it before rt/aggregate.h and lose the
# poison and the assertion. A production object fails when its command line
# undefines the marker (-U) or when any of its dependencies inside the
# repository contains an undef directive for it: the text read as the
# preprocessor reads it (line splices, including backslash-blank-newline,
# removed; %: read as #; comments allowed between the tokens).
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
  prev=
  for arg in "$@"; do
    if [ "$arg" = -UNIFT_PRODUCTION_TU ] || { [ "$prev" = -U ] && [ "$arg" = NIFT_PRODUCTION_TU ]; }; then
      echo "error: production object $out: the command line undefines NIFT_PRODUCTION_TU (tooling.md 3)" >&2
      exit 1
    fi
    prev=$arg
  done
fi
if ! "$@" $depflag -MP -MF "$tmp.d" -MT "$out" -o "$tmp"; then
  rm -f "$tmp" "$tmp.d"
  exit 1
fi
if [ "$production" = 1 ]; then
  root=$(pwd -P)
  deps=$(sed -e 's/\\$//' -e 's/^[^ ]*: *//' "$tmp.d" | tr ' ' '\n' | grep -v '^$' |
         xargs -r realpath -m --relative-to="$root" 2>/dev/null | sort -u)
  bad=$(printf '%s\n' "$deps" | grep '^tests/' | head -3)
  undef=$(printf '%s\n' "$deps" | grep -v -e '^\.\./' -e '^/' -e '^$' | xargs -r perl -0777 -ne '
    s/\\[ \t\f\v]*\r?\n//g; s/%:/#/g;
    my $c = qr{(?:[ \t\f\v]|/\*.*?\*/)};
    print "$ARGV\n" if /^$c*#$c*undef$c+NIFT_PRODUCTION_TU\b/ms;' | head -3)
  if [ -n "$undef" ]; then
    echo "error: production object $out undefines the production marker NIFT_PRODUCTION_TU in: $(echo $undef)" \
         "(tooling.md 3)" >&2
    rm -f "$tmp" "$tmp.d"
    exit 1
  fi
  if [ -n "$bad" ]; then
    echo "error: production object $out depends on the test tree: $(echo $bad)" \
         "(test helpers are unreachable from production: tooling.md 3)" >&2
    rm -f "$tmp" "$tmp.d"
    exit 1
  fi
fi
# Tooling review F7: the sidecar also holds the sha256 of the object it was
# written with, so a sidecar and an object from two compiles (the renames below
# are atomic one by one, not as a pair) never pass check-config.sh as a pair.
printf '%s %s\n' "$id" "$(sha256sum < "$tmp" | cut -d' ' -f1)" > "$tmp.cfg" &&
  mv -f "$tmp.cfg" "$base.cfg" && mv -f "$tmp.d" "$base.d" && mv -f "$tmp" "$out"
