#!/bin/sh
# Verifies the CP16 §12 syntax examples against the legacy binary, in two passes:
#  legacy: the example as written (run/cmd are today's built-ins). With PATH
#          unset so nothing is launched. Shows what the example does TODAY
#          (compatibility): the options object stops at `run: arguments must be
#          scalar`; unknown cmd() methods at `unknown value or malformed expression`.
#  mock:   mock.f + the example with run( -> mrun( and cmd( -> mcmd(, so every
#          line is executed by the legacy parser/evaluator: the code is real Nift
#          syntax; only the built-ins' new behaviour is hypothetical.
# Usage: syntax_check.sh NIFT [--write]   (expected output: syntax/expected.txt)
set -u
NIFT=${1:?usage: syntax_check.sh NIFT [--write]}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cp16-syntax-XXXXXX")
out="$WORK/result.txt"; : > "$out"
for f in "$HERE"/[ABC]_*.f; do
  case "$f" in *.mock) continue;; esac
  id=$(basename "$f" .f)
  { printf 'url := "https://example.invalid"\n'; cat "$f"; } > "$WORK/legacy.f"
  src="$f"; [ -f "$HERE/$id.mock" ] && src="$HERE/$id.mock"
  { cat "$HERE/mock.f"; sed -E 's/(^|[^.A-Za-z_])run\(/\1mrun(/g; s/(^|[^.A-Za-z_])cmd\(/\1mcmd(/g' "$src"; } > "$WORK/mock.f"
  printf '== %s legacy: ' "$id" >> "$out"
  # No pipeline (POSIX sh has no pipefail): the legacy run is expected to
  # fail (PATH without curl); its first output line is the recorded result.
  (cd "$WORK" && env PATH=/nonexistent "$NIFT" legacy.f > "$WORK/legacy.out" 2>&1)
  sed -n "1{s#$WORK/##;p;}" "$WORK/legacy.out" >> "$out"
  printf '== %s mock (exit %s):\n' "$id" "$(cd "$WORK" && "$NIFT" mock.f > mock.out 2>&1; echo $?)" >> "$out"
  sed "s#$WORK/##" "$WORK/mock.out" >> "$out"
done
if [ "${2:-}" = --write ]; then cp "$out" "$HERE/expected.txt"; fi
cat "$out"
rm -rf "$WORK"
