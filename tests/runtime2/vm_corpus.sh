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
# Each script runs in its own fresh copy of its directory (golden files are not
# copied), with a fixed, minimal environment (CP08 review: no inherited HOME,
# LANG or shared working copy).
# NIFT_VM_CORPUS_EXPECT=r2 compares against the intended Runtime 2.0 goldens
# (<name>.r2.stdout/.r2.stderr/.r2.exit, D-009) where they exist.
for dir in "$HERE"/vm-corpus/*/ "$HERE"/semantics/; do
  name="$(basename "$dir")"
  for f in "$dir"*.f; do
    b="$(basename "$f" .f)"; n=$((n+1))
    rm -rf "$W/$name" "$W/home" "$W/tmp"; mkdir -p "$W/$name" "$W/home" "$W/tmp"
    (cd "$dir" && find . -type f ! -name '*.stdout' ! -name '*.stderr' ! -name '*.exit' -exec cp --parents {} "$W/$name/" \;)
    real="$(cd "$W/$name" && pwd -P)"
    ( cd "$W/$name" && env -i PATH="$PATH" HOME="$W/home" TMPDIR="$W/tmp" LANG=C LC_ALL=C TZ=UTC \
        timeout 20 "$NIFT" "$b.f" >"$W/out" 2>"$W/err" </dev/null; echo $? >"$W/exit" )
    for s in out err; do sed -i "s#$real#\$ROOT#g; s#$W/$name#\$ROOT#g" "$W/$s"; done
    for k in out:stdout err:stderr exit:exit; do
      golden="$dir$b.${k##*:}"
      if [ "${NIFT_VM_CORPUS_EXPECT:-}" = r2 ] && [ -f "$dir$b.r2.${k##*:}" ]; then golden="$dir$b.r2.${k##*:}"; fi
      if [ "${NIFT_VM_CORPUS_REGENERATE:-0}" = 1 ]; then cp "$W/${k%%:*}" "$golden"; continue; fi
      if ! cmp -s "$W/${k%%:*}" "$golden"; then
        echo "FAIL $name/$b.f (${k##*:} differs)"; diff "$golden" "$W/${k%%:*}" | head -6; fail=1
      fi
    done
  done
done
[ "$fail" -eq 0 ] && echo "PASS vm corpus: $n scripts"
exit $fail
