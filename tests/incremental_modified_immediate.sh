#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"; T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T"; "$NIFT_BIN" init >/dev/null

# Regression: modified-mode staleness must never report success while a source
# edit goes unnoticed. The old strictly-greater mtime predicate treated a
# content file whose mtime equals the page-info mtime as "unchanged". On
# filesystems with coarse timestamp resolution (content and page-info writes
# land in the same quantum), an immediate edit produces exactly that equality,
# so the build returned exit 0 with stale output. Equal timestamps must be
# treated as potentially stale so the rebuild is always attempted.

# 1. Deterministic equal-mtime boundary: set the content mtime to exactly the
# page-info mtime (what a coarse filesystem produces for an immediate edit).
# valid -> changed valid: the rebuild must run and render the new source.
printf 'AAA\n' > content/index.html
"$NIFT_BIN" build >/dev/null
printf 'BBB\n' > content/index.html
touch -r .nift/public/index.info.json content/index.html
"$NIFT_BIN" build >/dev/null
grep -q 'BBB' public/index.html

# valid -> invalid at the equal-mtime boundary: must attempt the rebuild and
# FAIL, never return success with stale output.
printf 'CCC\n' > content/index.html
"$NIFT_BIN" build >/dev/null
printf 'BROKEN\n$[x := 1]\n$[x = "boom"]\n' > content/index.html
touch -r .nift/public/index.info.json content/index.html
if "$NIFT_BIN" build >/dev/null 2>&1; then
  echo "FAIL: equal-mtime invalid content was accepted (stale output kept)" >&2
  exit 1
fi

# 2. Filesystem-resolution boundary stress: repeated immediate edits with no
# sleeps, so a coarse quantum (content == info mtime) is exercised naturally.
# valid -> different valid: rebuild renders the new source.
for i in $(seq 1 30); do
  printf 'MARKER-V-%s\n$[%s + %s]\n' "$i" "$i" "$i" > content/index.html
  "$NIFT_BIN" build >/dev/null
  grep -q "MARKER-V-$i" public/index.html
  grep -q "$((i + i))" public/index.html
done

# valid -> invalid stress: each rebuild must fail, never succeed with stale
# generated output.
for i in $(seq 1 30); do
  printf 'MARKER-I-%s\n$[x := 1]\n$[x = "boom"]\n' "$i" > content/index.html
  if "$NIFT_BIN" build >/dev/null 2>&1; then
    echo "FAIL: invalid content at iteration $i was accepted (stale output kept)" >&2
    exit 1
  fi
done

# After the failing edits, a valid source must rebuild cleanly again.
printf 'RECOVERED\n' > content/index.html
"$NIFT_BIN" build >/dev/null
grep -q 'RECOVERED' public/index.html