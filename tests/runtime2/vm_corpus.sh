#!/usr/bin/env bash
# Runtime 2.0 VM regression corpus: scripts with golden stdout, stderr and exit
# code recorded from the canonical (legacy textual) evaluator. Any new engine
# must reproduce them byte for byte unless semantics.md records a change.
# NIFT_VM_CORPUS_RECORD=1 rewrites the golden files (only for a recorded change).
set -u
NIFT="${NIFT_BIN:-$(pwd)/nift}"
ROOT="$(cd "$(dirname "$0")/vm-corpus" && pwd)"
fail=0; n=0
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
for f in "$ROOT"/*/*.f; do
  d="$(dirname "$f")"; b="$(basename "$f" .f)"; n=$((n+1))
  # Diagnostics name the script by absolute path; golden files store the
  # corpus root as <CORPUS> so the corpus runs from any checkout location.
  ( cd "$d" && timeout 20 "$NIFT" "$b.f" >"$W/out.raw" 2>"$W/err.raw"; echo $? >"$W/exit" )
  sed "s#$ROOT#<CORPUS>#g" "$W/out.raw" >"$W/out"; sed "s#$ROOT#<CORPUS>#g" "$W/err.raw" >"$W/err"
  if [ -n "${NIFT_VM_CORPUS_RECORD:-}" ]; then cp "$W/out" "$d/$b.stdout"; cp "$W/err" "$d/$b.stderr"; cp "$W/exit" "$d/$b.exit"; fi
  for k in out:stdout err:stderr exit:exit; do
    if ! cmp -s "$W/${k%%:*}" "$d/$b.${k##*:}"; then
      echo "FAIL ${f#$ROOT/} (${k##*:} differs)"; diff "$d/$b.${k##*:}" "$W/${k%%:*}" | head -6; fail=1
    fi
  done
done
[ "$fail" -eq 0 ] && echo "PASS vm corpus: $n scripts"
exit $fail
