#!/usr/bin/env bash
# Runtime 2.0 VM regression corpus: scripts with golden stdout, stderr and exit
# code recorded from the canonical (legacy textual) evaluator
# (tests/runtime2/vm-corpus/*/ and tests/runtime2/semantics/). Any new engine
# must reproduce them byte for byte unless semantics.md records a change.
# Each corpus directory is copied to a temporary directory and run there; the
# copy's path is replaced by $ROOT so goldens do not depend on the checkout.
# NIFT_VM_CORPUS_REGENERATE=1 rewrites the goldens (only when semantics.md
# records an intentional change).
set -u
NIFT="${NIFT_BIN:-$(pwd)/nift}"
HERE="$(cd "$(dirname "$0")" && pwd)"
fail=0; n=0
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
# Corpus directories: every vm-corpus/<set>/ plus the semantics probes.
for dir in "$HERE"/vm-corpus/*/ "$HERE"/semantics/; do
  name="$(basename "$dir")"
  rm -rf "$W/$name"; cp -R "$dir" "$W/$name"
  real="$(cd "$W/$name" && pwd -P)"
  for f in "$dir"*.f; do
    b="$(basename "$f" .f)"; n=$((n+1))
    ( cd "$W/$name" && timeout 20 "$NIFT" "$b.f" >"$W/out" 2>"$W/err"; echo $? >"$W/exit" )
    for s in out err; do sed -i "s#$real#\$ROOT#g; s#$W/$name#\$ROOT#g" "$W/$s"; done
    for k in out:stdout err:stderr exit:exit; do
      if [ "${NIFT_VM_CORPUS_REGENERATE:-0}" = 1 ]; then cp "$W/${k%%:*}" "$dir$b.${k##*:}"; continue; fi
      if ! cmp -s "$W/${k%%:*}" "$dir$b.${k##*:}"; then
        echo "FAIL $name/$b.f (${k##*:} differs)"; diff "$dir$b.${k##*:}" "$W/${k%%:*}" | head -6; fail=1
      fi
    done
  done
done
[ "$fail" -eq 0 ] && echo "PASS vm corpus: $n scripts"
exit $fail
